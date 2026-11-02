// PunchPaw core: a punch compressor for drums and bass. Pure C++17, no JUCE, no allocation after prepare().
//
// Behaviour follows the approved mockup (docs/mockup/PunchPaw.html), which is written from the public
// manuals of a classic 1970s hard-knee compressor: true RMS detector, hard knee, three controls
// (threshold, ratio, output), program dependent attack and a steady release in dB per second.
// Our additions: soft knee, sidechain high-pass, mix, a soft ceiling and a manual attack and release.
//
// Signal path:  in -> (sidechain high-pass, detector only) -> RMS -> gain computer -> attack/release
//               -> gain * output -> mix with dry -> ceiling (4x oversampled soft clipper) -> out
//
// The ceiling is the only non-linear stage after the gain. It runs 4x oversampled through a linear
// phase FIR and only the correction (clipped minus unclipped) is filtered, so the linear signal is never
// low-passed and, when nothing is clipped, the output is the input delayed by kLatency samples exactly.
// The same delay is applied to the dry path so Bypass and Mix stay aligned. Latency is a constant
// kLatency samples at every sample rate and setting.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pp
{
constexpr int kScopeN = 400;               // columns of the punch shape display (3 ms each)

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
inline float clampf (float v, float a, float b) { return std::min (b, std::max (a, v)); }

// ---- Knob positions (0..10 on the faceplate) to real values ---------------------------------------
inline float slopeOf (float v)             // 1 / ratio; past knob 8 the slope goes negative (ratio below infinity)
{
    static const float RS[9] = { 1.0f, 0.6667f, 0.5f, 0.3333f, 0.25f, 0.1667f, 0.1f, 0.05f, 0.0f };
    v = clampf (v, 0.0f, 10.0f);
    if (v <= 8.0f)
    {
        const int i = std::min (7, (int) std::floor (v));
        const float f = v - (float) i;
        return RS[i] + (RS[i + 1] - RS[i]) * f;
    }
    return -(v - 8.0f) / 2.0f;
}
inline std::string ratioText (float v)
{
    const float s = slopeOf (v);
    if (std::fabs (s) < 0.004f) return "inf:1";
    const float r = 1.0f / s, a = std::fabs (r);
    char b[32];
    std::snprintf (b, sizeof b, a >= 10.0f ? "%s%.0f:1" : "%s%.1f:1", r < 0 ? "-" : "", a);
    return b;
}
inline float thrDbOf (float v) { return -40.0f + 4.0f * v; }
inline float outDbOf (float v) { return (v - 5.0f) * 4.0f; }
inline float atkMsOf (float v) { return 0.1f * std::pow (1000.0f, v / 10.0f); }
inline float relMsOf (float v) { return 10.0f * std::pow (200.0f, v / 10.0f); }
inline float atkKnob (float ms) { return 10.0f * std::log (ms / 0.1f) / std::log (1000.0f); }
inline float relKnob (float ms) { return 10.0f * std::log (ms / 10.0f) / std::log (200.0f); }
inline float hpHzOf (float v)
{
    static const float HPF[11] = { 20, 20, 30, 40, 60, 80, 100, 130, 170, 220, 300 };
    v = clampf (v, 0.0f, 10.0f);
    if (v < 0.5f) return 0.0f;
    if (v < 1.0f) return 20.0f;
    const int i = std::min (9, (int) std::floor (v));
    const float f = v - (float) i;
    return std::exp (std::log (HPF[i]) + (std::log (HPF[i + 1]) - std::log (HPF[i])) * f);
}
// Ceiling in dBFS, or +100 for Off (knob 9.95 and above).
inline float ceilDbOf (float v)
{
    static const float CD[11] = { -12, -10, -9, -8, -7, -6, -5, -4, -3, -1, 0 };
    v = clampf (v, 0.0f, 10.0f);
    if (v >= 9.95f) return 100.0f;
    const int i = std::min (9, (int) std::floor (v));
    const float f = v - (float) i;
    return CD[i] + (CD[i + 1] - CD[i]) * f;
}

// ---- Gain computer and timing (the classic unit's public numbers) ----------------------------------
// over = detector level minus threshold in dB. Returns dB of gain reduction.
inline float grOf (float over, float slope, bool soft)
{
    const float k = 1.0f - slope;
    if (soft)
    {
        const float w = 8.0f;                                   // our soft knee, 8 dB wide
        if (2.0f * over < -w) return 0.0f;
        if (2.0f * std::fabs (over) <= w) { const float u = over + w * 0.5f; return k * u * u / (2.0f * w); }
    }
    return over > 0.0f ? over * k : 0.0f;
}
// Auto attack: about 15 ms at 10 dB, 5 ms at 20 dB, 3 ms at 30 dB of gain reduction (10 to 90 percent).
inline float autoAttackMs (float g)
{
    if (g <= 10.0f) return 15.0f;
    if (g <= 20.0f) return 15.0f - (g - 10.0f);
    if (g <= 30.0f) return 5.0f - (g - 20.0f) * 0.2f;
    return 3.0f;
}
constexpr float kAutoReleaseDbPerSec = 125.0f;

// The ceiling's soft clipper: identity up to 0.6 c, then a tanh knee that never passes c.
inline float softClip (float x, float c)
{
    const float a = 0.6f * c, ax = std::fabs (x);
    if (ax <= a) return x;
    const float y = a + (c - a) * std::tanh ((ax - a) / (c - a));
    return x < 0 ? -y : y;
}

struct Params
{
    float thrDb = -20.0f;          // -40..0
    float slope = 0.25f;           // 1/ratio, negative past infinity
    float outDb = 0.0f;            // -20..+20
    float hpHz = 0.0f;             // sidechain high-pass, 0 = off
    float mix = 1.0f;              // 0 dry .. 1 processed
    float ceilDb = 100.0f;         // soft ceiling in dBFS, >= 50 means off
    bool soft = false;             // soft knee
    bool manual = false;           // manual attack and release instead of auto
    float atkMs = 10.0f;           // 0.1..100
    float relMs = 150.0f;          // 10..2000
};

class Compressor
{
public:
    static constexpr int kM = 32;                 // latency in samples
    static constexpr int kL = 4 * kM + 1;         // FIR length at 4x
    static constexpr int kLatency = kM;
    static constexpr int kRing = 64;              // history ring for dry and wet (power of two, > kM + taps)
    static constexpr int kCRing = 256;            // oversampled correction ring (power of two, > kL + 4)

    Compressor() { designFir(); reset(); }

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        kr = 1.0f - std::exp (-1.0f / (0.008f * fs));       // 8 ms RMS window
        kvu = 1.0f - std::exp (-1.0f / (0.065f * fs));      // meter ballistics
        ks = 1.0f - std::exp (-1.0f / (0.005f * fs));       // 5 ms parameter smoothing
        kbyp = 1.0f / (0.010f * fs);                        // 10 ms bypass ramp
        colN = std::max (1, (int) std::lround (fs * 0.003));
        hangMax = (int) (0.08f * fs) + 2 * kL;
        reset();
        snapParams();
    }

    void reset()
    {
        ms = 0.0; cur = 0.0f; hx[0] = hx[1] = hy[0] = hy[1] = 0.0f;
        for (auto& c : ring) c.fill (0.0f);
        for (auto& c : dry) c.fill (0.0f);
        for (auto& c : cring) c.fill (0.0f);
        wr = 0; cw = 0; hang = 0;
        vuI = vuO = 0.0f; grNow_ = 0.0f; atkNow = 15.0f;
        accI = accO = accG = 0.0f; accN = 0;
        scopeIdx.store (0);
        for (int i = 0; i < kScopeN; ++i) { scIn[i].store (0.0f); scOut[i].store (0.0f); scGr[i].store (0.0f); }
        bypassMix = bypassTarget;
    }

    void setParams (const Params& p) { tgt = p; }
    // Jump all smoothed values to the target (used on prepare and in tests).
    void snapParams() { cu = tgt; curOutG = dbToLin (cu.outDb); ceilS = tgt.ceilDb >= 50.0f ? 100.0f : dbToLin (tgt.ceilDb); updateHp(); }

    // In place. r may be null (mono). bypass crossfades to the delayed dry signal over about 10 ms.
    void process (float* l, float* r, int n, bool bypass)
    {
        bypassTarget = bypass ? 1.0f : 0.0f;
        const int nch = r ? 2 : 1;
        float* ch[2] = { l, r };
        float ip = 0.0f, op = 0.0f, gm = 0.0f;
        const bool ceilWanted = tgt.ceilDb < 50.0f;
        const float ceilTarget = ceilWanted ? dbToLin (tgt.ceilDb) : 100.0f;
        const float outTarget = dbToLin (tgt.outDb);
        for (int i = 0; i < n; ++i)
        {
            // parameter smoothing (5 ms one-pole)
            cu.thrDb += (tgt.thrDb - cu.thrDb) * ks;
            cu.slope += (tgt.slope - cu.slope) * ks;
            cu.outDb += (tgt.outDb - cu.outDb) * ks;
            cu.mix += (tgt.mix - cu.mix) * ks;
            cu.hpHz += (tgt.hpHz - cu.hpHz) * ks;
            ceilS += (ceilTarget - ceilS) * ks;
            curOutG += (outTarget - curOutG) * ks;
            if (cu.hpHz != lastHp) updateHp();
            if (ceilWanted) hang = hangMax; else if (hang > 0) --hang;
            if (bypassMix < bypassTarget) bypassMix = std::min (bypassTarget, bypassMix + kbyp);
            else if (bypassMix > bypassTarget) bypassMix = std::max (bypassTarget, bypassMix - kbyp);

            float x[2] = { 0, 0 };
            bool finite = true;
            for (int c = 0; c < nch; ++c) { x[c] = ch[c][i]; if (! std::isfinite (x[c])) { finite = false; x[c] = 0.0f; } }
            if (! finite) softReset();
            if (nch == 1) x[1] = x[0];
            ip = std::max (ip, std::max (std::fabs (x[0]), nch == 2 ? std::fabs (x[1]) : 0.0f));

            // detector: sidechain high-pass, then true RMS of both channels together
            float s0 = x[0], s1 = x[1];
            if (hpA > 0.0f)
            {
                float y = hpA * (hy[0] + x[0] - hx[0]); hx[0] = x[0]; hy[0] = y; s0 = y;
                y = hpA * (hy[1] + x[1] - hx[1]); hx[1] = x[1]; hy[1] = y; s1 = y;
            }
            ms += ((double) (s0 * s0 + s1 * s1) * 0.5 - ms) * (double) kr;
            if (ms < 1.0e-20) ms = 0.0;
            lvl = 10.0f * std::log10 ((float) ms + 1.0e-12f);
            over = lvl - cu.thrDb;
            const float want = std::min (60.0f, grOf (over, cu.slope, tgt.soft));
            if (want > cur)
            {
                float am = tgt.manual ? std::max (0.05f, tgt.atkMs) : autoAttackMs (want);
                atkNow = am;
                cur += (want - cur) * (1.0f - std::exp (-1.0f / (am / 2.2f * 0.001f * fs)));
            }
            else if (tgt.manual)
            {
                const float rk = 1.0f - std::exp (-1.0f / (std::max (1.0f, tgt.relMs) / 2.2f * 0.001f * fs));
                cur = std::max (want, cur - (cur - want) * rk);
            }
            else cur = std::max (want, cur - kAutoReleaseDbPerSec / fs);
            if (! std::isfinite (cur)) cur = 0.0f;
            gm = std::max (gm, cur);
            const float g = dbToLin (-cur) * curOutG;

            const int wi = wr & (kRing - 1);
            const int di = (wr - kM) & (kRing - 1);
            const bool active = ceilWanted || hang > 0;
            float o[2] = { 0, 0 };
            for (int c = 0; c < nch; ++c)
            {
                const float d = x[c];
                const float w = d * g;
                float y = cu.mix >= 0.9999f ? w : d + (w - d) * cu.mix;
                if (! std::isfinite (y)) { softReset(); y = 0.0f; }
                ring[c][wi] = y;
                dry[c][wi] = d;
                const float yDel = ring[c][di];
                float out = yDel;
                if (active) out += ceilingCorrection (c);
                const float dryDel = dry[c][di];
                o[c] = bypassMix >= 1.0f ? dryDel : dryDel + (1.0f - bypassMix) * (out - dryDel);
                if (nch == 1) o[1] = o[0];
                ch[c][i] = o[c];
            }
            ++wr;
            if (active) cw += 4;

            op = std::max (op, std::max (std::fabs (o[0]), std::fabs (o[1])));
            vuI += ((x[0] * x[0] + x[1] * x[1]) * 0.5f - vuI) * kvu;
            vuO += ((o[0] * o[0] + o[1] * o[1]) * 0.5f - vuO) * kvu;
            accI = std::max (accI, std::max (std::fabs (x[0]), std::fabs (x[1])));
            accO = std::max (accO, std::max (std::fabs (o[0]), std::fabs (o[1])));
            accG = std::max (accG, cur);
            if (++accN >= colN)
            {
                const int k = scopeIdx.load (std::memory_order_relaxed);
                scIn[k].store (accI, std::memory_order_relaxed);
                scOut[k].store (accO, std::memory_order_relaxed);
                scGr[k].store (accG, std::memory_order_relaxed);
                scopeIdx.store ((k + 1) % kScopeN, std::memory_order_release);
                accI = accO = accG = 0.0f; accN = 0;
            }
        }
        grPkBlock = gm;
        grNow_ = cur;
        ipBlock = ip;
        opBlock = op;
    }

    // ---- read-outs for the editor (audio thread writes, the editor timer reads) --------------------
    float grNow() const { return grNow_; }
    float grPeakBlock() const { return grPkBlock; }
    float inPeakBlock() const { return ipBlock; }
    float outPeakBlock() const { return opBlock; }
    float vuIn() const { return vuI; }                 // mean square, 65 ms
    float vuOut() const { return vuO; }
    float detectorDb() const { return lvl; }
    float overDb() const { return over; }
    float autoAttackNow() const { return atkNow; }
    std::array<std::atomic<float>, kScopeN> scIn, scOut, scGr;
    std::atomic<int> scopeIdx { 0 };

private:
    void designFir()
    {
        // Kaiser windowed sinc, 4x rate, cutoff 0.47 of the base rate, DC gain 1.
        const double pi = 3.14159265358979323846, beta = 8.0, fc = 0.47 / 4.0;
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        double sum = 0.0;
        std::array<double, kL> t {};
        for (int k = 0; k < kL; ++k)
        {
            const double m = k - 2.0 * kM, a = 2.0 * k / (double) (kL - 1) - 1.0;
            const double sinc = m == 0.0 ? 2.0 * fc : std::sin (2.0 * pi * fc * m) / (pi * m);
            t[(size_t) k] = sinc * i0 (beta * std::sqrt (std::max (0.0, 1.0 - a * a))) / i0 (beta);
            sum += t[(size_t) k];
        }
        for (int k = 0; k < kL; ++k) h[(size_t) k] = (float) (t[(size_t) k] / sum);
    }

    static float corrOf (float u, float c)
    {
        if (std::fabs (u) <= 0.6f * c) return 0.0f;
        return softClip (u, c) - u;
    }

    // Called after ring[c][wi] was written for this sample. Returns the filtered correction aligned
    // with the delayed wet signal.
    float ceilingCorrection (int c)
    {
        const int wi = wr & (kRing - 1);
        const float cl = ceilS;
        for (int p = 0; p < 4; ++p)
        {
            float acc = 0.0f;
            int k = 0;
            for (int j = p; j < kL; j += 4, ++k) acc += h[(size_t) j] * ring[c][(wi - k) & (kRing - 1)];
            cring[c][(cw + p) & (kCRing - 1)] = corrOf (4.0f * acc, cl);
        }
        const int base = cw + 0;                 // index of c[4n]
        float dd = 0.0f;
        for (int j = 0; j < kL; ++j) dd += h[(size_t) j] * cring[c][(base - j) & (kCRing - 1)];
        return dd;
    }

    void softReset()
    {
        ms = 0.0; cur = 0.0f;
        for (auto& c : ring) c.fill (0.0f);
        for (auto& c : cring) c.fill (0.0f);
    }
    void updateHp()
    {
        lastHp = cu.hpHz;
        hpA = cu.hpHz >= 10.0f ? std::exp (-2.0f * 3.14159265f * cu.hpHz / fs) : 0.0f;
    }

    float fs = 48000.0f, kr = 0.0f, kvu = 0.0f, ks = 0.0f, kbyp = 0.0f;
    int colN = 144, hangMax = 4096, hang = 0;
    Params tgt, cu;
    float curOutG = 1.0f, ceilS = 100.0f, hpA = 0.0f, lastHp = -1.0f;
    double ms = 0.0;
    float cur = 0.0f, lvl = -90.0f, over = -99.0f, atkNow = 15.0f;
    float hx[2] {}, hy[2] {};
    float vuI = 0, vuO = 0, grNow_ = 0, grPkBlock = 0, ipBlock = 0, opBlock = 0;
    float accI = 0, accO = 0, accG = 0; int accN = 0;
    float bypassMix = 0.0f, bypassTarget = 0.0f;
    std::array<float, kL> h {};
    std::array<std::array<float, kRing>, 2> ring {}, dry {};
    std::array<std::array<float, kCRing>, 2> cring {};
    int wr = 0, cw = 0;
};
} // namespace pp
