#pragma once
// The rack: up to 12 units in rows. A row holds one unit, or two side by side (a parallel pair with lane levels and a
// dry/wet). Latency is the sum over rows of the largest unit latency in the row; every path is delayed to line up, so
// the reported latency never changes when a unit is switched off, soloed or muted. Pure C++17, no JUCE.
#include "Units.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace catr
{
constexpr int kMaxUnits = 12;
constexpr int kChunk = 256;          // the rack works in blocks of at most this many samples
constexpr int kMaxPd = 6;

// ---- control value mapping (the mockup's own rules) ----------------------------------------------------------------
inline float stepOf (const ParamDef& d) { return d.nNames > 0 ? 1.0f : (d.dec > 0 ? std::pow (10.0f, (float) -d.dec) : 1.0f); }
inline float toNorm (const ParamDef& d, float v)
{
    v = clampf (v, d.mn, d.mx);
    const float n = d.logScale ? std::log (v / d.mn) / std::log (d.mx / d.mn) : (v - d.mn) / (d.mx - d.mn);
    return clampf (n, 0.0f, 1.0f);
}
inline float fromNorm (const ParamDef& d, float n)
{
    n = clampf (n, 0.0f, 1.0f);
    float v = d.logScale ? d.mn * std::pow (d.mx / d.mn, n) : d.mn + n * (d.mx - d.mn);
    const float s = stepOf (d);
    v = std::round (v / s) * s;
    return clampf (v, d.mn, d.mx);
}

// ---- structure ------------------------------------------------------------------------------------------------------
struct Stage { int n = 0; int slot[2] { -1, -1 }; };
struct Structure
{
    int nStages = 0;
    Stage st[kMaxUnits];
    int slotType[kMaxUnits];
    int slotLat[kMaxUnits] {};
    int stageLat[kMaxUnits] {};
    int totalLat = 0;
    Structure() { for (auto& t : slotType) t = -1; }
    int unitCount() const { int c = 0; for (int i = 0; i < nStages; ++i) c += st[i].n; return c; }
    int stageOf (int slot) const { for (int i = 0; i < nStages; ++i) for (int k = 0; k < st[i].n; ++k) if (st[i].slot[k] == slot) return i; return -1; }
    int freeSlot() const { for (int i = 0; i < kMaxUnits; ++i) if (slotType[i] < 0) return i; return -1; }

    // "CR1:3.0|1.5+2.0" means row 0 holds slot 3 (unit type 0), row 1 holds slot 1 (type 5) beside slot 2 (type 0).
    std::string toText() const
    {
        std::string s = "CR1:";
        for (int i = 0; i < nStages; ++i)
        {
            if (i) s += '|';
            for (int k = 0; k < st[i].n; ++k)
            {
                if (k) s += '+';
                s += std::to_string (st[i].slot[k]) + "." + std::to_string (slotType[st[i].slot[k]]);
            }
        }
        return s;
    }
    static bool fromText (const std::string& text, Structure& out)
    {
        Structure r;
        if (text.compare (0, 4, "CR1:") != 0) return false;
        size_t i = 4; const size_t n = text.size();
        if (i == n) { out = r; return true; }
        int used = 0;
        auto readInt = [&] (int& v) { if (i >= n || text[i] < '0' || text[i] > '9') return false; int x = 0, d = 0; while (i < n && text[i] >= '0' && text[i] <= '9' && d < 4) { x = x * 10 + (text[i] - '0'); ++i; ++d; } v = x; return true; };
        while (true)
        {
            if (r.nStages >= kMaxUnits) return false;
            Stage& g = r.st[r.nStages];
            while (true)
            {
                int slot, type;
                if (! readInt (slot) || i >= n || text[i] != '.') return false;
                ++i;
                if (! readInt (type)) return false;
                if (slot < 0 || slot >= kMaxUnits || type < 0 || type >= kBuiltUnits || r.slotType[slot] >= 0 || g.n >= 2) return false;
                r.slotType[slot] = type; g.slot[g.n++] = slot; ++used;
                if (i < n && text[i] == '+') { ++i; continue; }
                break;
            }
            ++r.nStages;
            if (i < n && text[i] == '|') { ++i; continue; }
            break;
        }
        if (i != n || used > kMaxUnits) return false;
        out = r;
        return true;
    }
};

// ---- per block controls, read from the parameters by the processor ------------------------------------------------------
struct SlotCtl { bool on = true, solo = false, mute = false; float lane = -6.0f; float p[kMaxPd] {}; };
struct Ctl
{
    SlotCtl slot[kMaxUnits];
    float stageMix[kMaxUnits];
    float inDb = 0, outDb = 0;
    bool bypass = false;
    Ctl() { for (auto& m : stageMix) m = 100.0f; }
};

class Rack
{
public:
    Rack()
    {
        for (int i = 0; i < kMaxUnits; ++i) { unit[i].store (nullptr); slotGr[i] = 0; slotPk[i] = 0; stagePk[i] = 0; }
        inPk[0] = inPk[1] = outPk[0] = outPk[1] = 0.0f;
    }
    ~Rack()
    {
        for (auto& u : unit) delete u.exchange (nullptr);
        for (auto& r : retired) delete r.first;
    }

    // ---- message thread ----
    void prepare (double sampleRate, int)
    {
        fs = sampleRate;
        for (auto& u : unit) { auto* o = u.exchange (nullptr); delete o; }
        for (auto& r : retired) delete r.first;
        retired.clear();
        for (int i = 0; i < kMaxUnits; ++i)
        {
            procAlign[i].init (4096); bypAlign[i].init (4096); dryAlign[i].init (4096);
            actW[i] = 0.0f; muteG[i] = 1.0f; laneG[i].setup (sampleRate, 10.0); laneG[i].snap (0.5f); mixG[i].setup (sampleRate, 10.0); mixG[i].snap (1.0f);
        }
        totalAlign.init (16384);
        inG.setup (sampleRate, 10.0); inG.snap (1.0f); outG.setup (sampleRate, 10.0); outG.snap (1.0f); byW = 0.0f;
        rampStep = (float) (1.0 / (0.010 * sampleRate));
        Structure s = published;
        published = Structure();
        prepared = true;
        applyStructure (s);
    }

    // Create or replace units so they match the structure, publish it, and return the structure with latencies filled.
    Structure applyStructure (Structure s)
    {
        collectGarbage();
        if (prepared)
        {
            for (int i = 0; i < kMaxUnits; ++i)
            {
                IUnit* cur = unit[i].load();
                if (s.slotType[i] < 0)
                {
                    if (cur) { unit[i].store (nullptr); retire (cur); }
                    continue;
                }
                if (cur && cur->type == s.slotType[i]) continue;
                auto nu = makeUnit (s.slotType[i]);
                if (! nu) { s.slotType[i] = -1; continue; }
                nu->prepare (fs, kChunk);
                const auto& d = unitTable()[s.slotType[i]];
                float v[kMaxPd] {};
                for (int k = 0; k < kMaxPd; ++k) v[k] = k < d.nParams ? d.p[k].def : 0.0f;
                nu->setParams (v);
                IUnit* old = unit[i].exchange (nu.release());
                if (old) retire (old);
            }
            for (int i = 0; i < kMaxUnits; ++i)
            {
                IUnit* u = unit[i].load();
                s.slotLat[i] = (u && s.slotType[i] >= 0) ? std::min (4000, u->latency()) : 0;
            }
            int tot = 0;
            for (int g = 0; g < s.nStages; ++g)
            {
                int m = 0;
                for (int k = 0; k < s.st[g].n; ++k) m = std::max (m, s.slotLat[s.st[g].slot[k]]);
                s.stageLat[g] = m; tot += m;
            }
            s.totalLat = std::min (tot, 16000);
        }
        {
            std::lock_guard<std::mutex> lk (mtx);
            published = s;
            version.fetch_add (1);
        }
        return s;
    }
    Structure current() const { std::lock_guard<std::mutex> lk (mtx); return published; }
    void collectGarbage()
    {
        const auto now = std::chrono::steady_clock::now();
        for (size_t i = retired.size(); i-- > 0;)
            if (now - retired[i].second > std::chrono::seconds (3)) { delete retired[i].first; retired.erase (retired.begin() + (long) i); }
    }

    // ---- audio thread ----
    void process (float* l, float* r, int n, const Ctl& c)
    {
        for (int off = 0; off < n; off += kChunk) chunk (l + off, r + off, std::min (kChunk, n - off), c);
    }

    // meters, written by the audio thread; the editor reads and clears the peaks
    std::atomic<float> slotGr[kMaxUnits], slotPk[kMaxUnits], stagePk[kMaxUnits], inPk[2], outPk[2];
    static void raiseTo (std::atomic<float>& a, float v) { float cur = a.load(); while (v > cur && ! a.compare_exchange_weak (cur, v)) {} }

private:
    struct Local { Structure s; int version = -1; } loc;

    void retire (IUnit* u) { retired.emplace_back (u, std::chrono::steady_clock::now()); }

    void syncStructure()
    {
        const int v = version.load();
        if (v == loc.version) return;
        if (mtx.try_lock())
        {
            loc.s = published; loc.version = v;
            mtx.unlock();
            for (int g = 0; g < loc.s.nStages; ++g)
            {
                const int L = loc.s.stageLat[g];
                dryAlign[g].setDelay (L); dryAlign[g].reset();
                for (int k = 0; k < loc.s.st[g].n; ++k)
                {
                    const int sl = loc.s.st[g].slot[k];
                    procAlign[sl].setDelay (L - loc.s.slotLat[sl]); procAlign[sl].reset();
                    bypAlign[sl].setDelay (L); bypAlign[sl].reset();
                }
            }
            totalAlign.setDelay (loc.s.totalLat); totalAlign.reset();
        }
    }

    // Run one slot on its own copy of the signal (in place), with the delays that line it up with its row.
    void runSlot (int slot, float* l, float* r, int n, const Ctl& c, bool anySolo)
    {
        const SlotCtl& sc = c.slot[slot];
        IUnit* u = unit[slot].load();
        const bool ok = u != nullptr && u->type == loc.s.slotType[slot];
        const bool active = ok && sc.on && (! anySolo || sc.solo);
        float* byp[2] = { tmpL[slot % 2], tmpR[slot % 2] };
        float& w = actW[slot];
        if (! ok) w = 0.0f;
        if (! active && w <= 0.0f)
        {
            bypAlign[slot].process (l, r, n);
            slotGr[slot].store (0.0f);
        }
        else
        {
            if (active && w <= 0.0f) u->reset();            // coming on: start from a clean state
            std::copy (l, l + n, byp[0]); std::copy (r, r + n, byp[1]);
            bypAlign[slot].process (byp[0], byp[1], n);
            const auto& d = unitTable()[u->type];
            float v[kMaxPd];
            for (int k = 0; k < kMaxPd; ++k) v[k] = k < d.nParams ? sc.p[k] : 0.0f;
            u->setParams (v);
            u->process (l, r, n);
            procAlign[slot].process (l, r, n);
            const float target = active ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                if (w < target) w = std::min (target, w + rampStep); else if (w > target) w = std::max (target, w - rampStep);
                if (w >= 1.0f) continue;
                l[i] = byp[0][i] + w * (l[i] - byp[0][i]); r[i] = byp[1][i] + w * (r[i] - byp[1][i]);
            }
            slotGr[slot].store (active ? u->grDb() : 0.0f);
        }
        // mute: this unit goes quiet (ramped)
        const float mt = sc.mute ? 0.0f : 1.0f;
        float& g = muteG[slot];
        if (g != mt || mt == 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                if (g < mt) g = std::min (mt, g + rampStep); else if (g > mt) g = std::max (mt, g - rampStep);
                l[i] *= g; r[i] *= g;
            }
        }
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (l[i]), std::fabs (r[i])));
        raiseTo (slotPk[slot], pk);
    }

    void chunk (float* l, float* r, int n, const Ctl& c)
    {
        syncStructure();
        // bypass path: the input, delayed by the total latency, untouched
        std::copy (l, l + n, rawL); std::copy (r, r + n, rawR);
        totalAlign.process (rawL, rawR, n);
        inG.set (dbToLin (c.inDb)); outG.set (dbToLin (c.outDb)); const float byT = c.bypass ? 1.0f : 0.0f;
        float ip0 = 0, ip1 = 0;
        for (int i = 0; i < n; ++i) { const float g = inG.next(); l[i] *= g; r[i] *= g; ip0 = std::max (ip0, std::fabs (l[i])); ip1 = std::max (ip1, std::fabs (r[i])); }
        raiseTo (inPk[0], ip0); raiseTo (inPk[1], ip1);
        bool anySolo = false;
        for (int g = 0; g < loc.s.nStages; ++g)
            for (int k = 0; k < loc.s.st[g].n; ++k) { const int sl = loc.s.st[g].slot[k]; if (c.slot[sl].solo && c.slot[sl].on) anySolo = true; }
        for (int g = 0; g < loc.s.nStages; ++g)
        {
            const Stage& st = loc.s.st[g];
            if (st.n == 1) runSlot (st.slot[0], l, r, n, c, anySolo);
            else if (st.n == 2)
            {
                std::copy (l, l + n, dryL); std::copy (r, r + n, dryR);
                dryAlign[g].process (dryL, dryR, n);
                std::copy (l, l + n, bL); std::copy (r, r + n, bR);
                runSlot (st.slot[0], l, r, n, c, anySolo);
                runSlot (st.slot[1], bL, bR, n, c, anySolo);
                if (actW[st.slot[0]] <= 0.0f && actW[st.slot[1]] <= 0.0f && ! c.slot[st.slot[0]].mute && ! c.slot[st.slot[1]].mute)
                { std::copy (dryL, dryL + n, l); std::copy (dryR, dryR + n, r); }
                else {
                laneG[st.slot[0]].set (dbToLin (c.slot[st.slot[0]].lane)); laneG[st.slot[1]].set (dbToLin (c.slot[st.slot[1]].lane));
                mixG[g].set (clampf (c.stageMix[g], 0.0f, 100.0f) / 100.0f);
                for (int i = 0; i < n; ++i)
                {
                    const float ga = laneG[st.slot[0]].next(), gb = laneG[st.slot[1]].next(), m = mixG[g].next();
                    l[i] = dryL[i] * (1.0f - m) + (l[i] * ga + bL[i] * gb) * m;
                    r[i] = dryR[i] * (1.0f - m) + (r[i] * ga + bR[i] * gb) * m;
                }
                }
            }
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (l[i]), std::fabs (r[i])));
            raiseTo (stagePk[g], pk);
        }
        float op0 = 0, op1 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float g = outG.next();
            l[i] = finitef (l[i] * g); r[i] = finitef (r[i] * g);
            if (byW < byT) byW = std::min (byT, byW + rampStep); else if (byW > byT) byW = std::max (byT, byW - rampStep);
            const float w = byW;
            if (w >= 1.0f) { l[i] = rawL[i]; r[i] = rawR[i]; }
            else if (w > 0.0f) { l[i] += w * (rawL[i] - l[i]); r[i] += w * (rawR[i] - r[i]); }
            op0 = std::max (op0, std::fabs (l[i])); op1 = std::max (op1, std::fabs (r[i]));
        }
        raiseTo (outPk[0], op0); raiseTo (outPk[1], op1);
    }

    double fs = 48000; bool prepared = false; float rampStep = 0.01f;
    std::atomic<IUnit*> unit[kMaxUnits];
    std::vector<std::pair<IUnit*, std::chrono::steady_clock::time_point>> retired;
    mutable std::mutex mtx; Structure published; std::atomic<int> version { 0 };
    AlignDelay procAlign[kMaxUnits], bypAlign[kMaxUnits], dryAlign[kMaxUnits], totalAlign;
    float actW[kMaxUnits] {}, muteG[kMaxUnits] {};
    Smooth laneG[kMaxUnits], mixG[kMaxUnits], inG, outG; float byW = 0.0f;
    float rawL[kChunk], rawR[kChunk], dryL[kChunk], dryR[kChunk], bL[kChunk], bR[kChunk], tmpL[2][kChunk], tmpR[2][kChunk];
};
} // namespace catr
