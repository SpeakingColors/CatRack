#pragma once
// The units CatRack hosts. Seven wrap the pure C++ cores of the other Studio 87 plugins (src/core/ext, our own
// code); thirteen are written for the rack. All take the REAL control values of the mockup's tables (UnitData.h),
// in table order, and process a stereo pair in place. Nothing here allocates after prepare().
#include "Dsp.h"
#include "UnitData.h"
#include "ext/Apurrr2500.h"
#include "ext/FetlinePounce.h"
#include "ext/Furchild.h"
#include "ext/HairballCore.h"
#include "ext/LapCat2A.h"
#include "ext/Pawltec.h"
#include "ext/PunchPaw.h"
#include "ext/RackPorts.h"
#include "ext/Purrrstressor.h"
#include "ext/ScfxEngine.h"
#include "ext/ShimmerReverb.h"
#include <memory>

namespace catr
{
enum UnitType { U_PURR = 0, U_FUR, U_LAP, U_PAW, U_FET, U_PUNCH, U_APU, U_CLW, U_TMS, U_PEQ, U_PDR, U_PTR, U_PFZ,
                U_ECH, U_CHO, U_PHA, U_FLN, U_AIR, U_MS, U_GAN,
                U_MXZ, U_CRC, U_FAT, U_SPC, U_BAS, U_SHM, U_SDL, U_HBL, U_SCR, U_STC, U_CPY };

struct IUnit
{
    int type = -1;
    virtual ~IUnit() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void setParams (const float* v) = 0;                 // real values, table order
    virtual void process (float* l, float* r, int n) = 0;
    virtual int latency() const { return 0; }                     // valid after prepare
    virtual float grDb() const { return 0.0f; }                   // positive dB, for the meters
};

namespace detail
{
inline int nearestLog (float x, const float* t, int n)
{
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < n; ++i)
    {
        const float d = std::fabs (std::log (std::max (x, 1.0e-6f) / t[i]));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// Gentle tube-like warmth used by the units whose mockup has a Warmth control. Off is bit exact.
struct Warm
{
    AdaaTanh ad[2];
    Smooth amt;
    void prepare (double fs) { amt.setup (fs, 10.0); amt.snap (0.0f); ad[0].reset(); ad[1].reset(); }
    void reset() { ad[0].reset(); ad[1].reset(); amt.snap (amt.t); }
    void set (float pct) { amt.set (clampf (pct / 100.0f, 0.0f, 1.0f)); }
    void process (float* l, float* r, int n)
    {
        if (amt.t <= 0.0f && amt.settled() && amt.v <= 0.0f) return;
        for (int i = 0; i < n; ++i)
        {
            const float w = amt.next();
            const float yl = ad[0].process (l[i], 1.5, 0.0) / 1.5f, yr = ad[1].process (r[i], 1.5, 0.0) / 1.5f;
            l[i] += w * (yl - l[i]); r[i] += w * (yr - r[i]);
        }
    }
};

// Input trim below zero for units whose core only has 0 to +N dB of input.
struct Trim
{
    Smooth g;
    void prepare (double fs) { g.setup (fs, 10.0); g.snap (1.0f); }
    void set (float db) { g.set (dbToLin (db)); }
    void process (float* l, float* r, int n)
    {
        if (g.t == 1.0f && g.settled()) return;
        for (int i = 0; i < n; ++i) { const float k = g.next(); l[i] *= k; r[i] *= k; }
    }
};
} // namespace detail

// ---- hosted cores --------------------------------------------------------------------------------------------------
struct PurrUnit : IUnit
{
    purr::Compressor c; purr::Params p; detail::Trim trim;
    void prepare (double fs, int) override { c.prepare (fs); trim.prepare (fs); c.setParams (p); }
    void reset() override { c.reset(); }
    void setParams (const float* v) override
    {
        static const float kR[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 100.0f };
        trim.set (std::min (0.0f, v[0]));
        p.inputDb = std::max (0.0f, v[0]); p.ratioIndex = detail::nearestLog (v[1], kR, 8); p.outputDb = v[2];
        p.attackMs = v[3]; p.releaseMs = v[4]; p.mix = v[5] / 100.0f; c.setParams (p);
    }
    void process (float* l, float* r, int n) override { trim.process (l, r, n); c.process (l, r, n); }
    float grDb() const override { return std::fabs (c.grDb()); }
};

struct FurUnit : IUnit
{
    fur::Compressor c; fur::Params p; detail::Trim trim; detail::Warm warm;
    void prepare (double fs, int) override { c.prepare (fs, true); trim.prepare (fs); warm.prepare (fs); c.setParams (p); }
    void reset() override { c.reset(); warm.reset(); }
    void setParams (const float* v) override
    {
        trim.set (std::min (0.0f, v[0]));
        p.inDb[0] = p.inDb[1] = std::max (0.0f, v[0]);
        p.thKnob[0] = p.thKnob[1] = clampf (-v[1] / 3.2f, 0.0f, 10.0f);
        p.tc[0] = p.tc[1] = std::max (0, std::min (5, (int) std::lround (v[3]) - 1));
        p.link = true; p.outDb = v[2]; p.mix = v[5] / 100.0f; warm.set (v[4]); c.setParams (p);
    }
    void process (float* l, float* r, int n) override { trim.process (l, r, n); c.process (l, r, n); warm.process (l, r, n); }
    int latency() const override { return c.latencySamples(); }
    float grDb() const override { return (float) std::fabs (std::max (c.grNow (0), c.grNow (1))); }
};

struct LapUnit : IUnit
{
    lap::Core c; lap::Params p; detail::Warm warm;
    void prepare (double fs, int) override { c.prepare (fs); warm.prepare (fs); c.setParams (p); }
    void reset() override { c.reset(); warm.reset(); }
    void setParams (const float* v) override
    {
        p.peak = v[0]; p.gain = v[1] * 2.5f; p.mix = v[2]; p.limit = v[3] > 0.5f ? 1.0f : 0.0f; p.emph = v[4];
        p.noise = false; p.bypass = false; warm.set (v[5]); c.setParams (p);
    }
    void process (float* l, float* r, int n) override { c.process (l, r, n); warm.process (l, r, n); }
    int latency() const override { return c.latency(); }
    float grDb() const override { return (float) std::fabs (c.grNow()); }
};

struct PawUnit : IUnit
{
    pawl::Engine c; pawl::Params p;
    void prepare (double fs, int) override { c.prepare (fs); c.setParams (p); }
    void reset() override { c.reset(); }
    void setParams (const float* v) override
    {
        p.lowB = v[0] / 12.0f * 10.0f; p.hiB = v[1] / 12.0f * 10.0f; p.hiA = v[2] / 12.0f * 10.0f; p.lowA = v[3] / 12.0f * 10.0f;
        p.bw = v[4] / 10.0f; p.lowF = std::max (0, std::min (3, (int) std::lround (v[5]))); p.hiF = 3; p.attF = 1;
        p.tube = 0.0f; p.out = 5.0f; p.bypass = false; c.setParams (p);
    }
    void process (float* l, float* r, int n) override { c.process (l, r, n); }
    int latency() const override { return c.latency(); }
};

struct FetUnit : IUnit
{
    fp::Pounce c; fp::Params p;
    void prepare (double fs, int) override { c.prepare (fs); c.setParams (p); }
    void reset() override { c.reset(); }
    void setParams (const float* v) override
    {
        static const float kR[4] = { 4.0f, 8.0f, 12.0f, 20.0f };
        p.inputDb = v[0]; p.outputDb = v[1]; p.ratioIndex = detail::nearestLog (v[2], kR, 4); p.attackUs = v[3]; p.releaseMs = v[4];
        p.mix = v[5] / 100.0f; p.all = false; p.hp = false; p.bypass = false; c.setParams (p);
    }
    void process (float* l, float* r, int n) override { c.process (l, r, n); }
    int latency() const override { return c.latencySamples(); }
    float grDb() const override { return std::fabs (c.grDb()); }
};

struct PunchUnit : IUnit
{
    pp::Compressor c; pp::Params p;
    void prepare (double fs, int) override { c.prepare (fs); c.setParams (p); }
    void reset() override { c.reset(); }
    // punch, ceiling, release, lookahead (the detector listens this many ms ahead of the audio), soft clip, mix
    void setParams (const float* v) override
    {
        p.thrDb = clampf (v[1] - v[0] * 0.12f - v[4] * 0.05f, -40.0f, 0.0f); p.slope = 0.05f; p.outDb = 0.0f; p.hpHz = 0.0f;
        p.mix = v[5] / 100.0f; p.ceilDb = v[1]; p.soft = v[4] > 0.5f; p.manual = true; p.atkMs = 1.0f; p.relMs = v[2]; p.lookMs = v[3]; c.setParams (p);
    }
    void process (float* l, float* r, int n) override { c.process (l, r, n, false); }
    int latency() const override { return c.latencySamples(); }
    float grDb() const override { return std::fabs (c.grNow()); }
};

struct ApuUnit : IUnit
{
    apurr::Compressor c; apurr::Params p;
    void prepare (double fs, int) override { c.prepare (fs); c.setParams (p, true); }
    void reset() override { c.reset(); }
    void setParams (const float* v) override
    {
        static const float kR[6] = { 1.5f, 2, 3, 4, 6, 10 }, kA[7] = { 0.03f, 0.1f, 0.3f, 1, 3, 10, 30 };
        p.thrDbu = clampf (v[0], -20.0f, 10.0f); p.ratio = detail::nearestLog (v[1], kR, 6); p.makeupDb = v[2]; p.manualMakeup = true;
        p.attack = detail::nearestLog (v[3], kA, 7); p.release = 6; p.relVarMs = clampf (v[4], 50.0f, 3000.0f); p.mix = v[5] / 100.0;
        p.inSw = true; p.levelMatch = false; c.setParams (p);
    }
    void process (float* l, float* r, int n) override { c.process (l, r, n); }
    float grDb() const override { return (float) std::fabs (c.grPeak()); }
};

// ---- written for the rack ------------------------------------------------------------------------------------------
// Everyday compressor: peak detector (linked), soft knee, attack and release, make-up, dry/wet.
struct ClwUnit : IUnit
{
    CompCore cc; Smooth thr, ratio, mk, mix; double fs = 48000; float grNow = 0;
    void prepare (double sr, int) override { fs = sr; cc.prepare (sr); for (auto* s : { &thr, &ratio, &mk, &mix }) s->setup (sr, 10.0); thr.snap (-18); ratio.snap (4); mk.snap (4); mix.snap (1); }
    void reset() override { cc.reset(); grNow = 0; }
    void setParams (const float* v) override
    {
        thr.set (v[0]); ratio.set (v[1]); mk.set (v[2]); mix.set (v[5] / 100.0f);
        cc.knee = 6; cc.setTimes (v[3] / 1000.0f, v[4] / 1000.0f);
    }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            cc.thr = thr.next(); cc.ratio = std::max (1.0f, ratio.next());
            const float lv = linToDb (std::max (std::fabs (l[i]), std::fabs (r[i])));
            grNow = cc.step (lv);
            const float g = dbToLin (-grNow + mk.next()), m = mix.next();
            l[i] += m * (l[i] * g - l[i]); r[i] += m * (r[i] * g - r[i]);
        }
    }
    float grDb() const override { return grNow; }
};

// Four band compressor and limiter. One Depth knob does most of the work.
struct TmsUnit : IUnit
{
    static constexpr double kF[3] = { 150.0, 1500.0, 6000.0 };
    // The allpass compensation is built from explicit filters so every one keeps its own state.
    struct Chan2
    {
        LR4 lp1, hp1, lp2, hp2, lp3, hp3;
        LR4 a2L, a2H, a3L_l, a3L_h, a3M_l, a3M_h;   // lo through AP(f2) then AP(f3); mid1 through AP(f3)
        void design (double fs)
        {
            lp1.low (fs, kF[0]); hp1.high (fs, kF[0]); lp2.low (fs, kF[1]); hp2.high (fs, kF[1]); lp3.low (fs, kF[2]); hp3.high (fs, kF[2]);
            a2L.low (fs, kF[1]); a2H.high (fs, kF[1]); a3L_l.low (fs, kF[2]); a3L_h.high (fs, kF[2]); a3M_l.low (fs, kF[2]); a3M_h.high (fs, kF[2]);
        }
        void reset() { for (auto* f : { &lp1, &hp1, &lp2, &hp2, &lp3, &hp3, &a2L, &a2H, &a3L_l, &a3L_h, &a3M_l, &a3M_h }) f->reset(); }
        void split (float x, float* b)
        {
            const float lo = lp1.process (x), r1 = hp1.process (x);
            const float m1 = lp2.process (r1), r2 = hp2.process (r1);
            const float m2 = lp3.process (r2), hi = hp3.process (r2);
            const float lo2 = a2L.process (lo) + a2H.process (lo);
            b[0] = a3L_l.process (lo2) + a3L_h.process (lo2);
            b[1] = a3M_l.process (m1) + a3M_h.process (m1);
            b[2] = m2; b[3] = hi;
        }
    };
    Chan2 ch[2]; CompCore cc[4]; LookaheadLimiter lim; AlignDelay dry; Smooth depth, tim, lo, hi, mix; double fs = 48000; float grMax = 0;
    float ceilDb = -0.3f;
    void prepare (double sr, int maxBlock) override
    {
        fs = sr;
        scratchL.assign ((size_t) std::max (64, maxBlock), 0.0f); scratchR = scratchL;
        for (auto& c : ch) { c.design (sr); c.reset(); }
        for (auto& c : cc) { c.prepare (sr); c.knee = 12; }
        lim.prepare (sr); dry.init (lim.latency() + 8); dry.setDelay (lim.latency());
        for (auto* s : { &depth, &tim, &lo, &hi, &mix }) s->setup (sr, 10.0);
        depth.snap (60); tim.snap (40); lo.snap (0); hi.snap (0); mix.snap (1);
    }
    void reset() override { for (auto& c : ch) c.reset(); for (auto& c : cc) c.reset(); lim.reset(); dry.reset(); }
    void setParams (const float* v) override
    {
        depth.set (v[0]); tim.set (v[1]); lo.set (v[2]); hi.set (v[3]); ceilDb = v[4]; mix.set (v[5] / 100.0f); lim.setCeilingDb (v[4]);
    }
    int latency() const override { return lim.latency(); }
    void process (float* l, float* r, int n) override
    {
        std::vector<float>& dl = scratchL; std::vector<float>& dr = scratchR;
        n = std::min (n, (int) dl.size());   // the rack calls with blocks no larger than maxBlock
        std::copy (l, l + n, dl.begin()); std::copy (r, r + n, dr.begin());
        dry.process (dl.data(), dr.data(), n);
        float g = 0;
        for (int i = 0; i < n; ++i)
        {
            const float d = depth.next(), t = tim.next(), gl = dbToLin (lo.next()), gh = dbToLin (hi.next());
            float bl[4], br[4];
            ch[0].split (l[i], bl); ch[1].split (r[i], br);
            const float thr = -10.0f - d * 0.2f, ratio = 2.0f + d * 0.08f, mk = d * 0.05f;
            float sl = 0, sr = 0;
            for (int b = 0; b < 4; ++b)
            {
                cc[b].thr = thr; cc[b].ratio = ratio; cc[b].setTimes (0.004f, 0.04f + t * 0.004f);
                const float gr = cc[b].step (linToDb (std::max (std::fabs (bl[b]), std::fabs (br[b]))));
                g = std::max (g, gr);
                float k = dbToLin (-gr + mk);
                if (b == 0) k *= gl;
                if (b == 3) k *= gh;
                sl += bl[b] * k; sr += br[b] * k;
            }
            l[i] = sl; r[i] = sr;
        }
        lim.process (l, r, n);
        for (int i = 0; i < n; ++i) { const float m = mix.next(); l[i] = dl[(size_t) i] + m * (l[i] - dl[(size_t) i]); r[i] = dr[(size_t) i] + m * (r[i] - dr[(size_t) i]); }
        grMax = g;
    }
    float grDb() const override { return grMax; }
    std::vector<float> scratchL, scratchR;
};

// Three band EQ: low shelf 120 Hz, a movable bell, high shelf 6 kHz, and an output level.
struct PeqUnit : IUnit
{
    Biquad ls[2], pk[2], hs[2]; Smooth lg, mg, hg, mf, q, out; double fs = 48000; bool idle = true;
    void prepare (double sr, int) override { fs = sr; for (auto* s : { &lg, &mg, &hg, &mf, &q, &out }) s->setup (sr, 10.0); lg.snap (0); mg.snap (0); hg.snap (0); mf.snap (1000); q.snap (1); out.snap (0); reset(); design(); }
    void reset() override { for (int c = 0; c < 2; ++c) { ls[c].reset(); pk[c].reset(); hs[c].reset(); } }
    void setParams (const float* v) override { lg.set (v[0]); mg.set (v[1]); hg.set (v[2]); mf.set (v[3]); q.set (v[4]); out.set (v[5]); }
    void design()
    {
        for (int c = 0; c < 2; ++c) { ls[c].lowShelf (fs, 120.0, lg.v); pk[c].peak (fs, mf.v, q.v, mg.v); hs[c].highShelf (fs, 6000.0, hg.v); }
    }
    void process (float* l, float* r, int n) override
    {
        const bool flat = std::fabs (lg.t) < 1.0e-4f && std::fabs (mg.t) < 1.0e-4f && std::fabs (hg.t) < 1.0e-4f && std::fabs (out.t) < 1.0e-4f
                          && std::fabs (lg.v) < 1.0e-3f && std::fabs (mg.v) < 1.0e-3f && std::fabs (hg.v) < 1.0e-3f && std::fabs (out.v) < 1.0e-3f;
        if (flat) { idle = true; return; }
        idle = false;
        for (int i = 0; i < n; i += 32)
        {
            const int m = std::min (32, n - i);
            for (auto* s : { &lg, &mg, &hg, &mf, &q }) s->advance (m);
            design();
            for (int k = 0; k < m; ++k)
            {
                const float o = dbToLin (out.next());
                l[i + k] = finitef (hs[0].process (pk[0].process (ls[0].process (l[i + k])))) * o;
                r[i + k] = finitef (hs[1].process (pk[1].process (ls[1].process (r[i + k])))) * o;
            }
        }
    }
};

// Purr colour stages: Drive (symmetric), Trans (lopsided, even harmonics, low weight), Fuzz (thick).
// First order ADAA tanh, level matched at a reference level so more drive means more colour, not more volume.
struct DriveUnit : IUnit
{
    int kind = 0;
    static constexpr int kTaps = 41, kOsLat = (kTaps - 1) / 2;   // 2x oversampling: up and down each delay 20 samples at 2x = 20 at the host rate in total
    pawl::detail::Up2<kTaps> up[2]; pawl::detail::Down2<kTaps> dn[2]; DelayLine dryD[2];
    AdaaTanh ad[2]; Biquad tone[2]; Biquad dc[2]; Smooth drv, aux, outG, mix; double fs = 48000;
    float inMs = 0, outMs = 0, mkS = 1.0f, rmsC = 0.9999f, mkC = 0.999f, mkNow = 1.0f;
    explicit DriveUnit (int k) : kind (k) {}
    void prepare (double sr, int) override
    {
        fs = sr; for (auto* s : { &drv, &aux, &outG, &mix }) s->setup (sr, 10.0); drv.snap (0); aux.snap (50); outG.snap (0); mix.snap (1);
        rmsC = (float) std::exp (-1.0 / (0.25 * sr)); mkC = (float) std::exp (-1.0 / (0.1 * sr));
        const auto taps = pawl::detail::halfband<kTaps> (9.0);
        for (int c = 0; c < 2; ++c) { up[c].init (taps); dn[c].init (taps); dryD[c].init (kOsLat + 8); }
        reset(); for (auto& d : dc) d.highpass (sr, 15.0);
    }
    int latency() const override { return kOsLat; }
    void reset() override { for (int c = 0; c < 2; ++c) { ad[c].reset(); tone[c].reset(); dc[c].reset(); up[c].reset(); dn[c].reset(); dryD[c].reset(); } inMs = outMs = 0; mkS = 1.0f; mkNow = 1.0f; }
    void setParams (const float* v) override { drv.set (v[0]); aux.set (v[1]); outG.set (v[2]); mix.set (v[3] / 100.0f); }
    void process (float* l, float* r, int n) override
    {
        const bool off = drv.t <= 0.0f && drv.v <= 1.0e-4f;
        for (int i = 0; i < n; ++i)
        {
            const float a = drv.next(), au = aux.next(), og = dbToLin (outG.next()), m = mix.next();
            if (off && a <= 1.0e-4f) { dryD[0].push (l[i]); dryD[1].push (r[i]); l[i] = dryD[0].readInt (kOsLat + 1) * og; r[i] = dryD[1].readInt (kOsLat + 1) * og; up[0].reset(); up[1].reset(); dn[0].reset(); dn[1].reset(); continue; }
            const float a01 = clampf (a / 100.0f, 0.0f, 1.0f);
            double g, bias = 0.0; float cut = 20000.0f;
            if (kind == 0) { g = 1.0 + 9.0 * a01; cut = 800.0f * std::pow (2.0f, au / 100.0f * 4.6f); }
            else if (kind == 1) { g = 1.0 + 6.0 * a01; bias = (0.1 + 0.5 * au / 100.0) * a01; }
            else { g = 1.0 + 30.0 * a01; bias = 0.15 * a01; cut = 800.0f * std::pow (2.0f, au / 100.0f * 4.6f); }
            if ((i & 31) == 0)
            {
                // static level match at a reference sine (RMS of the shaped sine against the plain sine), then the closed loop below trims the rest
                double s2 = 0; const double tb = std::tanh (bias);
                for (int k = 0; k < 16; ++k) { const double y = std::tanh (g * 0.25 * std::sin (kPi * (k + 0.5) / 16.0) + bias) - tb; s2 += y * y; }
                const double rms = std::sqrt (s2 / 16.0), ref = 0.25 * 0.70710678;
                mkS = rms > 1.0e-4 ? (float) (ref / rms) : 1.0f;
                for (int c = 0; c < 2; ++c) tone[c].lowpass (fs, cut);
            }
            const float w = std::min (1.0f, a01 * 4.0f);
            float* io[2] = { &l[i], &r[i] };
            float fo[2], xi[2];
            for (int c = 0; c < 2; ++c)
            {
                const float xraw = *io[c];
                dryD[c].push (xraw);
                xi[c] = dryD[c].readInt (kOsLat + 1);              // dry path, delayed like the oversampled path
                double h0, h1;
                up[c].push ((double) xraw, h0, h1);
                const double u0 = ad[c].process ((float) h0, g, bias), u1 = ad[c].process ((float) h1, g, bias);
                float f = (float) dn[c].push (u0, u1) * mkS;
                if (cut < 19000.0f) f = tone[c].process (f);
                if (bias > 0.0) f = dc[c].process (f);
                fo[c] = finitef (f);
            }
            inMs = rmsC * inMs + (1.0f - rmsC) * 0.5f * (xi[0] * xi[0] + xi[1] * xi[1]);
            outMs = rmsC * outMs + (1.0f - rmsC) * 0.5f * (fo[0] * fo[0] + fo[1] * fo[1]);
            const float tgt = outMs > 1.0e-10f ? clampf (std::sqrt (inMs / outMs), 0.4f, 2.5f) : 1.0f;
            mkNow = mkC * mkNow + (1.0f - mkC) * (inMs > 1.0e-10f ? tgt : 1.0f);
            for (int c = 0; c < 2; ++c)
            {
                const float y = xi[c] + w * (fo[c] * mkNow - xi[c]);
                *io[c] = finitef (xi[c] + m * (y - xi[c])) * og;
            }
        }
    }
};

// Echo: stereo delay, feedback with tone and low cut, spread bounces the repeats left and right.
struct EchUnit : IUnit
{
    DelayLine d[2]; Biquad lp[2], hp[2]; Smooth time, fb, tone, sp, lc, wet; double fs = 48000; float fbL = 0, fbR = 0;
    void prepare (double sr, int) override
    {
        fs = sr; d[0].init ((int) (sr * 1.05)); d[1].init ((int) (sr * 1.05));
        time.setup (sr, 40.0); for (auto* s : { &fb, &tone, &sp, &lc, &wet }) s->setup (sr, 10.0);
        time.snap (375); fb.snap (35); tone.snap (50); sp.snap (50); lc.snap (100); wet.snap (30); reset();
    }
    void reset() override { d[0].reset(); d[1].reset(); for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); } fbL = fbR = 0; }
    void setParams (const float* v) override { time.set (v[0]); fb.set (v[1]); tone.set (v[2]); sp.set (v[3]); lc.set (v[4]); wet.set (v[5]); }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            if ((i & 31) == 0)
            {
                const double tc = 500.0 * std::pow (2.0, tone.v / 100.0 * 5.0);
                for (int c = 0; c < 2; ++c) { lp[c].lowpass (fs, tc); hp[c].highpass (fs, lc.v); }
            }
            const float dsm = clampf ((float) (time.next() * 0.001 * fs), 2.0f, (float) (d[0].mask - 4));
            const float f = fb.next() / 100.0f * 0.95f, s = sp.next() / 100.0f, w = wet.next() / 100.0f; tone.next(); lc.next();
            const float eL = d[0].read (dsm), eR = d[1].read (dsm);
            // spread 0: each side repeats itself; 100: the repeats swap sides (ping pong)
            const float inL = flushf (l[i]), inR = flushf (r[i]);
            const float cL = eL * (1.0f - s) + eR * s, cR = eR * (1.0f - s) + eL * s;
            d[0].push (inL + hp[0].process (lp[0].process (cL)) * f);
            d[1].push (inR + hp[1].process (lp[1].process (cR)) * f);
            l[i] = finitef (inL + w * eL); r[i] = finitef (inR + w * eR);
        }
    }
};

// Chorus: two detuned, slowly moving copies per side.
struct ChoUnit : IUnit
{
    DelayLine d[2]; double ph = 0, fs = 48000; Smooth rate, depth, del, wid, wet;
    void prepare (double sr, int) override { fs = sr; d[0].init ((int) (sr * 0.06)); d[1].init ((int) (sr * 0.06)); for (auto* s : { &rate, &depth, &del, &wid, &wet }) s->setup (sr, 10.0); rate.snap (0.8f); depth.snap (50); del.snap (12); wid.snap (80); wet.snap (30); reset(); }
    void reset() override { d[0].reset(); d[1].reset(); ph = 0; }
    void setParams (const float* v) override { rate.set (v[0]); depth.set (v[1]); del.set (v[2]); wid.set (v[3]); wet.set (v[4]); }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            ph += rate.next() / fs; if (ph >= 1.0) ph -= 1.0;
            const float dp = depth.next() / 100.0f, dl = del.next(), wd = wid.next() / 100.0f, w = wet.next() / 100.0f;
            const float s1 = (float) std::sin (2 * kPi * ph), s2 = (float) std::sin (2 * kPi * (ph + 0.5)), s3 = (float) std::sin (2 * kPi * (ph + 0.25 * wd)), s4 = (float) std::sin (2 * kPi * (ph + 0.5 + 0.25 * wd));
            const float base = dl * 0.001f * (float) fs, mod = base * 0.45f * dp;
            d[0].push (l[i]); d[1].push (r[i]);
            const float a = 0.5f * (d[0].read (std::max (2.0f, base + mod * s1)) + d[0].read (std::max (2.0f, base + mod * s2)));
            const float b = 0.5f * (d[1].read (std::max (2.0f, base + mod * s3)) + d[1].read (std::max (2.0f, base + mod * s4)));
            l[i] = finitef (l[i] * (1.0f - 0.5f * w) + 0.5f * w * a); r[i] = finitef (r[i] * (1.0f - 0.5f * w) + 0.5f * w * b);
        }
    }
};

// Phaser: six first order allpass stages swept by a slow sine.
struct PhaUnit : IUnit
{
    float z[2][6] {}; float fbs[2] {}; double ph = 0, fs = 48000; Smooth rate, depth, fb, cen, wet;
    void prepare (double sr, int) override { fs = sr; for (auto* s : { &rate, &depth, &fb, &cen, &wet }) s->setup (sr, 10.0); rate.snap (0.5f); depth.snap (60); fb.snap (40); cen.snap (800); wet.snap (30); reset(); }
    void reset() override { for (auto& a : z) for (auto& v : a) v = 0; fbs[0] = fbs[1] = 0; ph = 0; }
    void setParams (const float* v) override { rate.set (v[0]); depth.set (v[1]); fb.set (v[2]); cen.set (v[3]); wet.set (v[4]); }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            ph += rate.next() / fs; if (ph >= 1.0) ph -= 1.0;
            const float s = (float) std::sin (2 * kPi * ph), dp = depth.next() / 100.0f, f = fb.next() / 100.0f * 0.9f, w = wet.next() / 100.0f;
            const double fr = clampf (cen.next() * std::pow (2.0f, dp * 1.5f * s), 40.0f, (float) (fs * 0.45)), t = std::tan (kPi * fr / fs);
            const float a = (float) ((t - 1.0) / (t + 1.0));
            float* io[2] = { &l[i], &r[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float x = *io[c];
                float v = x + fbs[c] * f;
                for (int k = 0; k < 6; ++k) { const float y = a * v + z[c][k]; z[c][k] = flushf (v - a * y); v = y; }
                fbs[c] = flushf (v);
                *io[c] = finitef (x * (1.0f - 0.5f * w) + 0.5f * w * v);
            }
        }
    }
};

// Flanger: a very short delay moved by a sine, with feedback (negative values sound hollow).
struct FlnUnit : IUnit
{
    DelayLine d[2]; double ph = 0, fs = 48000; float fbv[2] {}; Smooth rate, depth, fb, del, wet;
    void prepare (double sr, int) override { fs = sr; d[0].init ((int) (sr * 0.02)); d[1].init ((int) (sr * 0.02)); for (auto* s : { &rate, &depth, &fb, &del, &wet }) s->setup (sr, 10.0); rate.snap (0.3f); depth.snap (60); fb.snap (30); del.snap (2); wet.snap (30); reset(); }
    void reset() override { d[0].reset(); d[1].reset(); ph = 0; fbv[0] = fbv[1] = 0; }
    void setParams (const float* v) override { rate.set (v[0]); depth.set (v[1]); fb.set (v[2]); del.set (v[3]); wet.set (v[4]); }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            ph += rate.next() / fs; if (ph >= 1.0) ph -= 1.0;
            const float s = (float) std::sin (2 * kPi * ph), dp = depth.next() / 100.0f, f = clampf (fb.next() / 100.0f, -0.95f, 0.95f), w = wet.next() / 100.0f;
            const float base = del.next() * 0.001f * (float) fs, dd = std::max (1.5f, base * (1.0f + 0.9f * dp * s));
            float* io[2] = { &l[i], &r[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float x = *io[c];
                const float y = d[c].read (dd);
                d[c].push (flushf (x + fbv[c] * f));
                fbv[c] = y;
                *io[c] = finitef (x * (1.0f - 0.5f * w) + 0.5f * w * y);
            }
        }
    }
};

// Air exciter: a smooth shelf of new harmonics above the From frequency (ADAA tanh), added to the signal.
struct AirUnit : IUnit
{
    Biquad hp[2], hp2[2], lp[2]; AdaaTanh ad[2]; Smooth air, freq, sm, out, mix; double fs = 48000;
    void prepare (double sr, int) override { fs = sr; for (auto* s : { &air, &freq, &sm, &out, &mix }) s->setup (sr, 10.0); air.snap (0); freq.snap (6000); sm.snap (50); out.snap (0); mix.snap (1); reset(); }
    void reset() override { for (int c = 0; c < 2; ++c) { hp[c].reset(); hp2[c].reset(); lp[c].reset(); ad[c].reset(); } }
    void setParams (const float* v) override { air.set (v[0]); freq.set (v[1]); sm.set (v[2]); out.set (v[3]); mix.set (v[4] / 100.0f); }
    void process (float* l, float* r, int n) override
    {
        const bool off = air.t <= 0.0f && air.v <= 1.0e-4f && std::fabs (out.t) < 1.0e-4f && std::fabs (out.v) < 1.0e-4f;
        if (off) return;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 31) == 0)
            {
                const double smc = 20000.0 - sm.v * 90.0;
                for (int c = 0; c < 2; ++c) { hp[c].highpass (fs, freq.v); hp2[c].highpass (fs, freq.v * 1.4); lp[c].lowpass (fs, smc); }
            }
            const float a = air.next() / 100.0f, og = dbToLin (out.next()), m = mix.next(); freq.next(); sm.next();
            float* io[2] = { &l[i], &r[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float x = *io[c];
                const float s = hp[c].process (x);
                const float e = lp[c].process (hp2[c].process (ad[c].process (s * 2.0f, 2.0, 0.0)));
                const float y = x + a * 0.5f * e;
                *io[c] = finitef (x + m * (y - x)) * og;
            }
        }
    }
};

struct MsUnit : IUnit
{
    Smooth wid, mid, side;
    void prepare (double sr, int) override { for (auto* s : { &wid, &mid, &side }) s->setup (sr, 10.0); wid.snap (100); mid.snap (0); side.snap (0); }
    void reset() override {}
    void setParams (const float* v) override { wid.set (v[0]); mid.set (v[1]); side.set (v[2]); }
    void process (float* l, float* r, int n) override
    {
        if (wid.t == 100.0f && wid.settled() && std::fabs (mid.t) < 1.0e-4f && std::fabs (side.t) < 1.0e-4f && std::fabs (mid.v) < 1.0e-3f && std::fabs (side.v) < 1.0e-3f) return;
        for (int i = 0; i < n; ++i)
        {
            const float gm = dbToLin (mid.next()), gs = dbToLin (side.next()) * wid.next() / 100.0f;
            const float m = 0.5f * (l[i] + r[i]) * gm, s = 0.5f * (l[i] - r[i]) * gs;
            l[i] = m + s; r[i] = m - s;
        }
    }
};

struct GanUnit : IUnit
{
    Smooth gain, pan; bool mono = false;
    void prepare (double sr, int) override { gain.setup (sr, 10.0); pan.setup (sr, 10.0); gain.snap (0); pan.snap (0); }
    void reset() override {}
    void setParams (const float* v) override { gain.set (v[0]); pan.set (v[1]); mono = v[2] > 0.5f; }
    void process (float* l, float* r, int n) override
    {
        if (!mono && std::fabs (gain.t) < 1.0e-4f && std::fabs (gain.v) < 1.0e-3f && pan.t == 0.0f && std::fabs (pan.v) < 1.0e-3f) return;
        for (int i = 0; i < n; ++i)
        {
            const float g = dbToLin (gain.next()), p = pan.next() / 100.0f;
            const float gl = p > 0 ? std::cos (p * (float) kPi * 0.5f) : 1.0f, gr = p < 0 ? std::cos (-p * (float) kPi * 0.5f) : 1.0f;
            float a = l[i], b = r[i];
            if (mono) a = b = 0.5f * (a + b);
            l[i] = a * g * gl; r[i] = b * g * gr;
        }
    }
};

// ---- Hairball (grain freeze, FX mode) --------------------------------------------------------------------------------
struct HbUnit : IUnit
{
    hb::Engine eng; hb::Params P;
    void prepare (double sr, int) override { eng.prepare (sr); P.setDefaults(); P[hb::pMode] = 1.0f; eng.setParams (P); }
    void reset() override { eng.reset(); }
    void setParams (const float* v) override
    {
        P[hb::pMode] = 1.0f;
        P[hb::pFreeze] = v[0]; P[hb::pHold] = v[1]; P[hb::pCaptureOffset] = v[2]; P[hb::pLiveLen] = v[3]; P[hb::pGrainLen] = v[4];
        P[hb::pSpacing] = v[5]; P[hb::pWaveSpacing] = v[6]; P[hb::pPitch] = v[7]; P[hb::pDance] = v[8]; P[hb::pRiseOn] = v[9];
        P[hb::pRiseTime] = v[10]; P[hb::pRisePitch] = v[11]; P[hb::pWidth] = v[12]; P[hb::pDrive] = v[13]; P[hb::pTransistor] = v[14];
        P[hb::pFuzz] = v[15]; P[hb::pMix] = v[16]; P[hb::pOut] = v[17];
        eng.setParams (P);
    }
    void process (float* l, float* r, int n) override { eng.process (l, r, l, r, n); }
};

// ---- Shimmer Reverb (five reverbs) ------------------------------------------------------------------------------------
struct ShimRevUnit : IUnit
{
    wm::engine::ReverbUnit rev; wm::engine::ReverbSettings rs; Smooth mix;
    void prepare (double sr, int) override { rev.prepare ((float) sr); mix.setup (sr, 15.0); mix.snap (0.35f); }
    void reset() override { rev.reset(); mix.snap (mix.t); }
    void setParams (const float* v) override
    {
        using wm::engine::ReverbType;
        static const ReverbType map[5] = { ReverbType::Plate, ReverbType::Hall, ReverbType::Plate, ReverbType::Hall, ReverbType::Velvet };
        const int ty = clampi ((int) (v[3] + 0.5f), 0, 4);
        rs.type = map[ty]; rs.shimmer = ty == 2 || ty == 3; rs.lexBlend = 0.0f; rs.level = 1.0f;
        rs.p.decay = v[0] * 0.01f; rs.shimmerAmount = v[1] * 0.01f; mix.set (v[2] * 0.01f);
        rs.p.size = v[4]; rs.p.preDelayMs = v[5]; rs.p.dampHz = v[6]; rs.shimmerSemis = v[7]; rs.p.modDepth = v[8] * 0.01f;
        rs.p.color = v[9] * 0.01f; rs.shimmer2Semis = v[10]; rs.shimmer2Amount = v[11] * 0.01f; rs.wetHpHz = v[12]; rs.wetLpHz = v[13];
    }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            rs.mix = mix.next();
            float ol, orr; rev.process (l[i], r[i], ol, orr, rs);
            if (! std::isfinite (ol) || ! std::isfinite (orr) || std::fabs (ol) > 50.0f || std::fabs (orr) > 50.0f) { rev.reset(); ol = orr = 0.0f; }
            l[i] = ol; r[i] = orr;
        }
    }
};

// ---- Shimmer Delay -----------------------------------------------------------------------------------------------------
struct ShimDelUnit : IUnit
{
    wm::engine::ShimmerDelay del; wm::engine::ShimmerDelayParams dp; Smooth time, mix, fb;
    void prepare (double sr, int) override { del.prepare ((float) sr); time.setup (sr, 80.0); mix.setup (sr, 15.0); fb.setup (sr, 15.0); time.snap (450.0f); mix.snap (0.4f); fb.snap (0.55f); }
    void reset() override { del.reset(); time.snap (time.t); mix.snap (mix.t); fb.snap (fb.t); }
    void setParams (const float* v) override
    {
        time.set (v[0]); fb.set (v[1] * 0.01f); mix.set (v[2] * 0.01f);
        dp.shiftSemis = v[3]; dp.shimmerAmount = v[4] * 0.01f; dp.dampHz = v[5];
    }
    void process (float* l, float* r, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            dp.timeMs = time.next(); dp.feedback = fb.next(); dp.mix = mix.next();
            float ol, orr; del.process (l[i], r[i], ol, orr, dp);
            if (! std::isfinite (ol) || ! std::isfinite (orr) || std::fabs (ol) > 50.0f || std::fabs (orr) > 50.0f) { del.reset(); ol = orr = 0.0f; }
            l[i] = ol; r[i] = orr;
        }
    }
};

// ---- Scratch Cat FX (scratch deck on the live input, EQ, colour, one effect) --------------------------------------------
struct ScrUnit : IUnit
{
    scfx::Engine eng; scfx::FxParams fp; bool lastBrake = false, lastBack = false;
    void prepare (double sr, int) override
    {
        eng.prepare (sr);
        float z[1024] {}, o[1024] {};
        eng.process (fp, 1024, z, z, o, o);          // sizes the engine's work buffers now, not on the audio thread
        eng.prepare (sr);
        fp.fxOn[0] = true; fp.chainMix = 1.0f;
    }
    void reset() override { eng.deck.goLive(); }
    void setParams (const float* v) override
    {
        const bool touch = v[3] > 0.5f, brake = v[1] > 0.5f, back = v[2] > 0.5f;
        if (brake && ! lastBrake) eng.brake();
        if (back && ! lastBack) eng.backspin();
        if (! brake && ! back && ! touch && lastBrake) eng.goLive();
        lastBrake = brake; lastBack = back;
        eng.touch (touch, (double) v[0]);
        fp.eqLo = v[4]; fp.eqMid = v[5]; fp.eqHi = v[6]; fp.colour = v[7] * 0.01f; fp.trimDb = v[8];
        fp.fxType[0] = scfx::FxFilter + clampi ((int) (v[9] + 0.5f), 0, 4); fp.fxOn[0] = true;
        fp.fxP1[0] = v[10] * 0.01f; fp.fxP2[0] = v[11] * 0.01f; fp.chainMix = 1.0f; fp.masterDb = v[12];
    }
    void process (float* l, float* r, int n) override { eng.process (fp, n, l, r, l, r); }
};

// ---- Meowximizer ---------------------------------------------------------------------------------------------------------
struct MxzUnit : IUnit
{
    mx::Core core; mx::Params P; int lat = 0;
    void prepare (double sr, int) override { core.prepare (sr); lat = core.latency(); }
    void reset() override { core.reset(); }
    int latency() const override { return lat; }
    float grDb() const override { return std::max (0.0f, -(core.bandGainDb[0] + core.bandGainDb[1] + core.bandGainDb[2]) / 3.0f) + core.optoGrDb; }
    void setParams (const float* v) override
    {
        P.squeeze = v[0]; P.air = v[1]; P.ceil = v[2]; P.contour = v[3]; P.bass = v[4]; P.process = v[5]; P.mode = (int) (v[6] + 0.5f);
        P.warm = v[7]; P.clip = v[8]; P.push = v[9]; P.opto = v[10]; P.mix = v[11]; P.out = v[12]; P.lcmode = (int) (v[13] + 0.5f);
        P.airchar = (int) (v[14] + 0.5f); P.wchar = (int) (v[15] + 0.5f); P.clipPos = (int) (v[16] + 0.5f); P.nasty = false;
    }
    void process (float* l, float* r, int n) override { float* io[2] = { l, r }; core.process (io, 2, n, P); }
};

// ---- Meowbass --------------------------------------------------------------------------------------------------------------
struct BasUnit : IUnit
{
    rp::MeowBassCore core; rp::BassParams P;
    void prepare (double sr, int) override { core.prepare (sr); }
    void reset() override { core.reset(); }
    void setParams (const float* v) override
    {
        P.freq = v[0]; P.harm = v[1]; P.dyn = v[2]; P.orig = v[3]; P.in = v[4]; P.out = v[5]; P.resp = v[6]; P.odd = v[7]; P.hdrive = v[8];
        P.hfilt = (int) (v[9] + 0.5f); P.hq = v[10] > 0.5f;
    }
    void process (float* l, float* r, int n) override { core.process (l, r, n, P); }
};

// ---- Fat Cat ---------------------------------------------------------------------------------------------------------------
struct FatUnit : IUnit
{
    rp::FatCatCore core; rp::FatParams P;
    void prepare (double sr, int) override { core.prepare (sr); }
    void reset() override { core.reset(); }
    void setParams (const float* v) override
    {
        P.drive = v[0]; P.color = v[1]; P.out = v[2]; P.xs = v[3]; P.grit = v[4]; P.fuzz = v[5]; P.turbo = v[6]; P.mix = v[7]; P.purr = v[8] > 0.5f;
    }
    void process (float* l, float* r, int n) override { core.process (l, r, n, P); }
};

// ---- Cat Crusher -----------------------------------------------------------------------------------------------------------
struct CrcUnit : IUnit
{
    rp::CatCrusherCore core; rp::CrushParams P;
    void prepare (double sr, int) override { core.prepare (sr); }
    void reset() override { core.reset(); }
    float grDb() const override { return core.grDb(); }
    void setParams (const float* v) override
    {
        P.tube = v[0]; P.comp = v[1]; P.vol = v[2]; P.mech = v[3]; P.cut = v[4]; P.res = v[5]; P.type = clampi ((int) (v[6] + 0.5f), 0, 9); P.mix = v[7];
        P.phat = v[8] > 0.5f; P.don = v[9] > 0.5f; P.ftype = clampi ((int) (v[10] + 0.5f), 0, 2); P.fon = v[11] > 0.5f; P.con = v[12] > 0.5f;
    }
    void process (float* l, float* r, int n) override { core.process (l, r, n, P); }
};

inline std::unique_ptr<IUnit> makeUnit (int t)
{
    std::unique_ptr<IUnit> u;
    switch (t)
    {
        case U_PURR: u = std::make_unique<PurrUnit>(); break;
        case U_FUR: u = std::make_unique<FurUnit>(); break;
        case U_LAP: u = std::make_unique<LapUnit>(); break;
        case U_PAW: u = std::make_unique<PawUnit>(); break;
        case U_FET: u = std::make_unique<FetUnit>(); break;
        case U_PUNCH: u = std::make_unique<PunchUnit>(); break;
        case U_APU: u = std::make_unique<ApuUnit>(); break;
        case U_CLW: u = std::make_unique<ClwUnit>(); break;
        case U_TMS: u = std::make_unique<TmsUnit>(); break;
        case U_PEQ: u = std::make_unique<PeqUnit>(); break;
        case U_PDR: u = std::make_unique<DriveUnit> (0); break;
        case U_PTR: u = std::make_unique<DriveUnit> (1); break;
        case U_PFZ: u = std::make_unique<DriveUnit> (2); break;
        case U_ECH: u = std::make_unique<EchUnit>(); break;
        case U_CHO: u = std::make_unique<ChoUnit>(); break;
        case U_PHA: u = std::make_unique<PhaUnit>(); break;
        case U_FLN: u = std::make_unique<FlnUnit>(); break;
        case U_AIR: u = std::make_unique<AirUnit>(); break;
        case U_MS: u = std::make_unique<MsUnit>(); break;
        case U_GAN: u = std::make_unique<GanUnit>(); break;
        case U_SHM: u = std::make_unique<ShimRevUnit>(); break;
        case U_SDL: u = std::make_unique<ShimDelUnit>(); break;
        case U_HBL: u = std::make_unique<HbUnit>(); break;
        case U_SCR: u = std::make_unique<ScrUnit>(); break;
        case U_MXZ: u = std::make_unique<MxzUnit>(); break;
        case U_BAS: u = std::make_unique<BasUnit>(); break;
        case U_FAT: u = std::make_unique<FatUnit>(); break;
        case U_CRC: u = std::make_unique<CrcUnit>(); break;
        default: return nullptr;
    }
    u->type = t;
    return u;
}
} // namespace catr
