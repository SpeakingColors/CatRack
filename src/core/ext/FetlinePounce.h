// FetlinePounce DSP core v0.1. Plain C++17, no JUCE, no allocation after prepare().
// Original design of a fast FET style limiting amplifier (1176 class): a feed-forward gain
// computer with four ratio buttons and an "all buttons" mode, a fixed threshold per ratio that the
// Input knob pushes the signal into, soft knee, attack 20 to 800 us, release 50 ms to 1.1 s that
// slows down after long loud passages, an optional detector high-pass, a soft saturating gain
// stage whose colour grows with the gain reduction, an Output knob and a dry/wet Mix.
// Everything runs at 2x oversampling below 80 kHz (linear phase half-band filters), so the
// gain stage and the saturator do not alias. The dry path is delayed by the exact latency.
// The ratio values, thresholds, knee, time maps and colour amounts are OUR choices from the
// approved mockup, not measurements of a hardware unit. Not based on any vendor code.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fp {

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)  { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- ranges and maps shared with the editor (knob units 0..10 as printed on the dials) ----------
constexpr int   kNumRatios = 4;
constexpr float kRatioVal[kNumRatios] = { 4.0f, 8.0f, 12.0f, 20.0f };
constexpr float kThrDb[kNumRatios]    = { -10.0f, -8.0f, -6.0f, -4.0f };   // fixed threshold per ratio button
constexpr float kAllRatio = 16.0f, kAllThrDb = -5.0f, kKneeDb = 4.0f;       // all buttons in
constexpr float kInputMaxDb = 30.0f, kOutputMinDb = -20.0f, kOutputMaxDb = 20.0f;
constexpr float kAttackMinUs = 20.0f, kAttackMaxUs = 800.0f, kReleaseMinMs = 50.0f, kReleaseMaxMs = 1100.0f;

inline float ratioOf (int ri, bool all) { return all ? kAllRatio : kRatioVal[std::min (std::max (ri, 0), kNumRatios - 1)]; }
inline float thrOf (int ri, bool all)   { return all ? kAllThrDb : kThrDb[std::min (std::max (ri, 0), kNumRatios - 1)]; }

// Gain reduction in dB (positive) for a detector level in dB: soft knee, then a straight line.
inline float grStatic (float levelDb, int ri, bool all)
{
    const float R = ratioOf (ri, all), T = thrOf (ri, all), k = 1.0f - 1.0f / R, over = levelDb - T;
    if (over <= -kKneeDb * 0.5f) return 0.0f;
    if (over < kKneeDb * 0.5f) { const float u = over + kKneeDb * 0.5f; return k * u * u / (2.0f * kKneeDb); }
    return std::min (40.0f, k * over);
}

// Knob 0..10 to time. Clockwise (10) is the fastest, like the hardware dials.
inline float attackUsFromKnob (float k)  { return kAttackMaxUs * std::pow (kAttackMinUs / kAttackMaxUs, k * 0.1f); }
inline float releaseMsFromKnob (float k) { return kReleaseMaxMs * std::pow (kReleaseMinMs / kReleaseMaxMs, k * 0.1f); }
inline float knobFromAttackUs (float us)  { return 10.0f * std::log (us / kAttackMaxUs) / std::log (kAttackMinUs / kAttackMaxUs); }
inline float knobFromReleaseMs (float ms) { return 10.0f * std::log (ms / kReleaseMaxMs) / std::log (kReleaseMinMs / kReleaseMaxMs); }
// Effective times: higher ratio buttons attack a little slower, all buttons faster; release follows
// how long and how hard the limiter has been working (sus01 0..1).
inline float attackEffUs (float us, int ri, bool all)      { return us * (1.0f + 0.12f * (all ? 3 : ri)) * (all ? 0.7f : 1.0f); }
inline float releaseEffMs (float ms, float sus01, bool all) { return ms * (0.5f + 1.5f * sus01) * (all ? 1.3f : 1.0f); }

struct Params
{
    float inputDb = 4.5f, outputDb = 1.2f;       // 0..30, -20..+20
    float attackUs = 87.0f, releaseMs = 319.0f;  // 20..800, 50..1100
    int   ratioIndex = 0;                        // 0..3 = 4, 8, 12, 20
    bool  all = false, hp = false, bypass = false;
    float mix = 1.0f;                            // 0 dry .. 1 limited
};

// Linear-phase half-band FIR (61 taps, Kaiser). Used as 2x up and down sampler.
struct HalfbandTaps
{
    static constexpr int N = 61, C = 30;         // C is even, so non-zero taps sit at C and at odd indices
    float h[N] {};
    HalfbandTaps()
    {
        const double pi = 3.14159265358979323846, beta = 8.0;
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        double sum = 0;
        double hd[N];
        for (int i = 0; i < N; ++i)
        {
            const double m = i - C;
            double v;
            if (i == C) v = 0.5;
            else if (((i - C) & 1) == 0) v = 0.0;
            else v = 0.5 * std::sin (pi * m * 0.5) / (pi * m * 0.5);
            const double r = m / (double) C;
            v *= i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            hd[i] = v; sum += v;
        }
        for (int i = 0; i < N; ++i) h[i] = (float) (hd[i] / sum);   // unity gain at DC
    }
};
inline const HalfbandTaps& hbTaps() { static const HalfbandTaps t; return t; }

struct Upsampler2
{
    float x[64] {}; int w = 0;
    void reset() { for (auto& v : x) v = 0; w = 0; }
    void process (float in, float& o0, float& o1)
    {
        const auto& t = hbTaps();
        x[w & 63] = in;
        o0 = 2.0f * t.h[HalfbandTaps::C] * x[(w - HalfbandTaps::C / 2) & 63];   // even phase: only the centre tap
        float s = 0;
        for (int i = 1; i < HalfbandTaps::N; i += 2) s += t.h[i] * x[(w - (i - 1) / 2) & 63];
        o1 = 2.0f * s;
        ++w;
    }
};

struct Downsampler2
{
    float v[128] {}; int w = 0;
    void reset() { for (auto& a : v) a = 0; w = 0; }
    float process (float a, float b)
    {
        const auto& t = hbTaps();
        v[w & 127] = a; v[(w + 1) & 127] = b; w += 2;
        const int n = w - 2;   // newest even (decimated) sample index
        float s = t.h[HalfbandTaps::C] * v[(n - HalfbandTaps::C) & 127];
        for (int i = 1; i < HalfbandTaps::N; i += 2) s += t.h[i] * v[(n - i) & 127];
        return s;
    }
};

struct HighPass2
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (double fs, double fc)
    {
        const double w0 = 6.283185307179586 * fc / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.70710678);
        const double a0 = 1.0 + al;
        b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (! std::isfinite (y)) { reset(); return 0.0f; }
        return y;
    }
};

class Pounce
{
public:
    static constexpr int kLatencyOs2 = (HalfbandTaps::N - 1) / 2;   // 30 samples at the host rate

    void setParams (const Params& np) { p = np; recompute(); }
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        os = fs < 80000.0 ? 2 : 1;
        fsHi = fs * os;
        latency = os == 2 ? kLatencyOs2 : 0;
        sc = 1.0f - std::exp (-1.0f / (0.005f * (float) fs));   // 5 ms smoothing of Input, Output and Mix
        bypassStep = 1.0f / (0.010f * (float) fs);
        dec = std::exp (-1.0f / (0.001f * (float) fsHi));
        aSus = 1.0f - std::exp (-1.0f / (0.8f * (float) fsHi));
        aRms = 1.0f - std::exp (-1.0f / (0.3f * (float) fs));
        for (auto& f : hpf) f.set (fsHi, 100.0);
        recompute();
        inG = tIn; outG = tOut; mixS = p.mix; byp = p.bypass ? 1.0f : 0.0f;
        reset();
    }
    void reset()
    {
        for (auto& u : up) u.reset();
        for (auto& d : down) d.reset();
        for (auto& f : hpf) f.reset();
        for (auto& b : dryBuf) for (auto& v : b) v = 0.0f;
        wp = 0; env = 0; gr = 0; sus = 0; ms = 0; grHold = 0;
    }
    int latencySamples() const { return latency; }
    float grDb() const { return gr; }
    float grPeakDb() const { return grHold; }
    void clearGrPeak() { grHold = gr; }
    float outRmsDb() const { return linToDb (std::sqrt (std::max (ms, 0.0f))); }

    // r may be null (mono). In place.
    void process (float* l, float* r, int n)
    {
        const int nc = r != nullptr ? 2 : 1;
        float* chn[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { 0, 0 }, dry[2] = { 0, 0 };
            bool bad = false;
            for (int c = 0; c < nc; ++c)
            {
                float v = chn[c][i];
                if (! std::isfinite (v)) { v = 0.0f; bad = true; }
                x[c] = v;
                dryBuf[c][wp & 63] = v;
                dry[c] = dryBuf[c][(wp - latency) & 63];
            }
            ++wp;
            if (bad) { env = 0; gr = 0; }
            inG += (tIn - inG) * sc; outG += (tOut - outG) * sc; mixS += (p.mix - mixS) * sc;
            const float bt = p.bypass ? 1.0f : 0.0f;
            if (byp < bt) byp = std::min (bt, byp + bypassStep); else if (byp > bt) byp = std::max (bt, byp - bypassStep);
            if (std::fabs (mixS - p.mix) < 1.0e-6f) mixS = p.mix;

            float hi[2][2] = { { 0, 0 }, { 0, 0 } }, wet[2][2] = { { 0, 0 }, { 0, 0 } };
            for (int c = 0; c < nc; ++c)
            {
                if (os == 2) up[c].process (x[c] * inG, hi[c][0], hi[c][1]);
                else hi[c][0] = x[c] * inG;
            }
            for (int s = 0; s < os; ++s)
            {
                float a0 = hi[0][s], a1 = nc > 1 ? hi[1][s] : a0;
                if (p.hp) { a0 = hpf[0].process (a0); a1 = nc > 1 ? hpf[1].process (a1) : a0; }
                const float lvl = std::max (std::fabs (a0), std::fabs (a1));
                env = std::max (lvl, env * dec);
                if (env < 1.0e-20f) env = 0.0f;
                const float tg = grStatic (linToDb (env), p.ratioIndex, p.all);
                if (tg > gr) gr += (tg - gr) * aAtk;
                else
                {
                    const float aRel = 1.0f - std::exp (-1.0f / (releaseEffMs (p.releaseMs, clampf (sus * (1.0f / 12.0f), 0.0f, 1.0f), p.all) * 0.001f * (float) fsHi));
                    gr += (tg - gr) * aRel;
                }
                if (gr < 1.0e-6f) gr = 0.0f;
                sus += (gr - sus) * aSus;
                const float g = dbToLin (-gr);
                const float sa = 0.25f + 0.5f * std::min (gr * 0.05f, 1.0f) + (p.all ? 0.7f : 0.0f);
                for (int c = 0; c < nc; ++c) wet[c][s] = std::tanh (sa * hi[c][s] * g) / sa;
                grHold = std::max (grHold, gr);
            }
            float msq = 0;
            for (int c = 0; c < nc; ++c)
            {
                float y = os == 2 ? down[c].process (wet[c][0], wet[c][1]) : wet[c][0];
                y *= outG;
                float o = dry[c] * (1.0f - mixS) + y * mixS;
                if (byp >= 1.0f) o = dry[c];
                else if (byp > 0.0f) o = o * (1.0f - byp) + dry[c] * byp;
                if (! std::isfinite (o)) { o = 0.0f; env = 0; gr = 0; reset(); }
                chn[c][i] = o;
                msq += o * o;
            }
            ms += (msq / (float) nc - ms) * aRms;
        }
    }

private:
    void recompute()
    {
        tIn = dbToLin (clampf (p.inputDb, 0.0f, kInputMaxDb));
        tOut = dbToLin (clampf (p.outputDb, kOutputMinDb, kOutputMaxDb));
        p.mix = clampf (p.mix, 0.0f, 1.0f);
        aAtk = 1.0f - std::exp (-1.0f / (attackEffUs (clampf (p.attackUs, kAttackMinUs, kAttackMaxUs), p.ratioIndex, p.all) * 1.0e-6f * (float) fsHi));
    }
    Params p;
    double fs = 48000.0, fsHi = 96000.0;
    int os = 2, latency = kLatencyOs2;
    float sc = 0.002f, bypassStep = 0.002f, dec = 0.99f, aSus = 0.0f, aRms = 0.0f, aAtk = 0.5f;
    float tIn = 1, tOut = 1, inG = 1, outG = 1, mixS = 1, byp = 0;
    float env = 0, gr = 0, sus = 0, ms = 0, grHold = 0;
    Upsampler2 up[2]; Downsampler2 down[2]; HighPass2 hpf[2];
    float dryBuf[2][64] {};
    unsigned wp = 0;
};

} // namespace fp
