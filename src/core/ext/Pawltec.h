#pragma once
// Pawltec core: a passive-style tube program equalizer. Pure C++17, no JUCE, no allocation while processing.
//
// Signal flow (one channel, the other is identical):
//   input -> four biquad sections (low boost, low atten, high boost, high atten) -> tube stage (4x oversampled) -> output gain
// Bypass and "tube 0" are bit-exact (the signal is only delayed by latency()).
//
// The filter shapes are our own model of the documented behaviour of this kind of equalizer (low shelf boost and
// cut at the same switch frequency interact, high boost is a bell with adjustable bandwidth, high atten is a
// shelf). Frequencies, maximum gains and shapes are the numbers the approved mockup uses; they are our own
// choices, not measurements of any hardware.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pawl
{
// ---- constants (the same numbers as the mockup) ------------------------------------------------------
inline constexpr double kLowF[4]  = { 20.0, 30.0, 60.0, 100.0 };
inline constexpr double kHiF[7]   = { 3000.0, 4000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0 };
inline constexpr double kAttF[3]  = { 5000.0, 10000.0, 20000.0 };
inline constexpr double kMaxLowBoostDb = 13.5, kMaxLowAttenDb = 17.5, kMaxHiBoostDb = 18.0, kMaxHiAttenDb = 16.0;
inline constexpr double kLowBoostRatio = 2.2, kLowBoostS = 1.3;   // boost shelf corner = switch frequency x 2.2
inline constexpr double kLowAttenRatio = 3.0, kLowAttenS = 0.4;   // atten shelf corner = switch frequency x 3
inline constexpr double kHiAttenS = 0.5;
inline constexpr double kOutDbPerStep = 2.4;                       // Output knob 0..10, 5 = 0 dB, so -12 to +12 dB
inline constexpr double kPi = 3.14159265358979323846;

// Knob values are the numbers printed on the faceplate (0..10), frequency switches are indices.
struct Params
{
    int lowF = 2;          // 0..3   20, 30, 60, 100 Hz
    float lowB = 0.0f;     // 0..10
    float lowA = 0.0f;     // 0..10
    int hiF = 4;           // 0..6   3, 4, 5, 8, 10, 12, 16 kHz
    float hiB = 0.0f;      // 0..10
    float bw = 5.0f;       // 0..10  bandwidth, 0 narrow, 10 wide
    int attF = 2;          // 0..2   5, 10, 20 kHz
    float hiA = 0.0f;      // 0..10
    float tube = 0.0f;     // 0..10  0 = clean and bit exact
    float out = 5.0f;      // 0..10  5 = 0 dB
    bool bypass = false;

    bool operator== (const Params& o) const
    {
        return lowF == o.lowF && lowB == o.lowB && lowA == o.lowA && hiF == o.hiF && hiB == o.hiB && bw == o.bw
            && attF == o.attF && hiA == o.hiA && tube == o.tube && out == o.out && bypass == o.bypass;
    }
};

inline double qOfWidth (double bw) { return 2.2 * std::pow (0.3 / 2.2, bw / 10.0); }
inline double outGainDb (double out) { return (out - 5.0) * kOutDbPerStep; }

// ---- biquads (RBJ cookbook shapes) --------------------------------------------------------------------
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   // a0 normalised to 1
    bool isIdentity() const { return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0; }
};

inline double clampFreq (double f, double fs) { return std::min (f, 0.49 * fs); }

inline Biquad shelf (double f0, double gDb, double S, bool high, double fs)
{
    if (gDb == 0.0) return {};
    f0 = clampFreq (f0, fs);
    const double A = std::pow (10.0, gDb / 40.0), w = 2.0 * kPi * f0 / fs, c = std::cos (w), s = std::sin (w);
    double d = (A + 1.0 / A) * (1.0 / S - 1.0) + 2.0;
    if (d < 1.0e-6) d = 1.0e-6;
    const double al = s / 2.0 * std::sqrt (d), sq = 2.0 * std::sqrt (A) * al;
    double b0, b1, b2, a0, a1, a2;
    if (! high)
    {
        b0 = A * ((A + 1) - (A - 1) * c + sq);  b1 = 2 * A * ((A - 1) - (A + 1) * c);  b2 = A * ((A + 1) - (A - 1) * c - sq);
        a0 = (A + 1) + (A - 1) * c + sq;        a1 = -2 * ((A - 1) + (A + 1) * c);     a2 = (A + 1) + (A - 1) * c - sq;
    }
    else
    {
        b0 = A * ((A + 1) + (A - 1) * c + sq);  b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
        a0 = (A + 1) - (A - 1) * c + sq;        a1 = 2 * ((A - 1) - (A + 1) * c);      a2 = (A + 1) - (A - 1) * c - sq;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline Biquad peak (double f0, double Q, double gDb, double fs)
{
    if (gDb == 0.0) return {};
    f0 = clampFreq (f0, fs);
    const double A = std::pow (10.0, gDb / 40.0), w = 2.0 * kPi * f0 / fs, c = std::cos (w), al = std::sin (w) / (2.0 * Q), a0 = 1.0 + al / A;
    return { (1.0 + al * A) / a0, -2.0 * c / a0, (1.0 - al * A) / a0, -2.0 * c / a0, (1.0 - al / A) / a0 };
}

// Magnitude in dB of one biquad at frequency f.
inline double magDb (const Biquad& k, double f, double fs)
{
    const double w = 2.0 * kPi * f / fs, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = k.b0 + k.b1 * c1 + k.b2 * c2, ni = -(k.b1 * s1 + k.b2 * s2);
    const double dr = 1.0 + k.a1 * c1 + k.a2 * c2, di = -(k.a1 * s1 + k.a2 * s2);
    return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di) + 1.0e-30);
}

enum Section { LowBoost = 0, LowAtten = 1, HiBoost = 2, HiAtten = 3, kNumSections = 4 };

inline int clampIdx (int v, int n) { return std::max (0, std::min (n - 1, v)); }
inline float clampKnob (float v) { return std::isfinite (v) ? std::max (0.0f, std::min (10.0f, v)) : 0.0f; }

inline Biquad sectionBiquad (Section s, const Params& p, double fs)
{
    switch (s)
    {
        case LowBoost: return shelf (kLowF[clampIdx (p.lowF, 4)] * kLowBoostRatio,  kMaxLowBoostDb * clampKnob (p.lowB) / 10.0, kLowBoostS, false, fs);
        case LowAtten: return shelf (kLowF[clampIdx (p.lowF, 4)] * kLowAttenRatio, -kMaxLowAttenDb * clampKnob (p.lowA) / 10.0, kLowAttenS, false, fs);
        case HiBoost:  return peak (kHiF[clampIdx (p.hiF, 7)], qOfWidth (clampKnob (p.bw)), kMaxHiBoostDb * clampKnob (p.hiB) / 10.0, fs);
        default:       return shelf (kAttF[clampIdx (p.attF, 3)], -kMaxHiAttenDb * clampKnob (p.hiA) / 10.0, kHiAttenS, true, fs);
    }
}

// Frequency of the handle that stands for each section in the response display.
inline double sectionMarkerHz (Section s, const Params& p)
{
    return s <= LowAtten ? kLowF[clampIdx (p.lowF, 4)] : s == HiBoost ? kHiF[clampIdx (p.hiF, 7)] : kAttF[clampIdx (p.attF, 3)];
}

// Total response of the four sections (what the display draws as the thick line).
inline double totalDb (const Params& p, double f, double fs)
{
    double t = 0.0;
    for (int s = 0; s < kNumSections; ++s) t += magDb (sectionBiquad ((Section) s, p, fs), f, fs);
    return t;
}

// ---- oversampling by 4 (two linear phase halfband stages) ---------------------------------------------
namespace detail
{
inline double bessel0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1.0e-18 * sum) break; }
    return sum;
}

// Kaiser windowed halfband lowpass (cutoff at a quarter of the sample rate), taps sum to 1.
template <int L>
std::array<double, L> halfband (double beta)
{
    std::array<double, L> h {};
    const int M = (L - 1) / 2;
    double sum = 0.0;
    for (int n = 0; n < L; ++n)
    {
        const double t = n - M, x = 0.5 * t;
        const double sinc = t == 0.0 ? 1.0 : std::sin (kPi * x) / (kPi * x);
        const double r = (double) (n - M) / M;
        const double win = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        h[(size_t) n] = 0.5 * sinc * win;
        sum += h[(size_t) n];
    }
    for (auto& v : h) v /= sum;
    return h;
}

constexpr int kLenA = 71, kLenB = 21;   // taps of the 1x to 2x and the 2x to 4x stage
constexpr int kDelayA = (kLenA - 1) / 2, kDelayB = (kLenB - 1) / 2;   // in samples at each stage's own high rate

// Interpolate by 2: every input sample gives two output samples.
template <int L>
struct Up2
{
    static constexpr int N = 128;
    std::array<double, L> h {};
    std::array<double, N> hist {};
    int pos = 0;
    void init (const std::array<double, L>& taps) { h = taps; reset(); }
    void reset() { hist.fill (0.0); pos = 0; }
    void push (double x, double& y0, double& y1)
    {
        pos = (pos + 1) & (N - 1);
        hist[(size_t) pos] = x;
        double a = 0.0, b = 0.0;
        for (int j = 0; 2 * j < L; ++j) a += h[(size_t) (2 * j)] * hist[(size_t) ((unsigned) (pos - j) & (N - 1))];
        for (int j = 0; 2 * j + 1 < L; ++j) b += h[(size_t) (2 * j + 1)] * hist[(size_t) ((unsigned) (pos - j) & (N - 1))];
        y0 = 2.0 * a; y1 = 2.0 * b;
    }
};

// Decimate by 2: two input samples give one output sample.
template <int L>
struct Down2
{
    static constexpr int N = 128;
    std::array<double, L> h {};
    std::array<double, N> hist {};
    int pos = 0;
    void init (const std::array<double, L>& taps) { h = taps; reset(); }
    void reset() { hist.fill (0.0); pos = 0; }
    double push (double u0, double u1)
    {
        pos = (pos + 1) & (N - 1);
        hist[(size_t) pos] = u0;
        double a = 0.0;
        for (int k = 0; k < L; ++k) a += h[(size_t) k] * hist[(size_t) ((unsigned) (pos - k) & (N - 1))];
        pos = (pos + 1) & (N - 1);
        hist[(size_t) pos] = u1;
        return a;
    }
};
} // namespace detail

// Total delay of the 4x oversampler in samples at the host rate (exact, an integer).
inline constexpr int kLatencySamples = detail::kDelayA + detail::kDelayB / 2;
static_assert (detail::kDelayB % 2 == 0, "stage B delay must be even so the total latency is a whole number of samples");

// The tube shaper: unity gain for small signals, soft rounding of peaks and a little asymmetry (even harmonics).
struct TubeShaper
{
    double g = 1.0, bias = 0.0, tb = 0.0, norm = 1.0;
    void set (double tube)   // tube 0..10
    {
        g = 1.0 + 0.5 * tube;
        bias = 0.08 * tube / 10.0;
        tb = std::tanh (bias);
        norm = g * (1.0 - tb * tb);
    }
    double operator() (double x) const { return (std::tanh (g * x + bias) - tb) / norm; }
};

// A delay of exactly kLatencySamples samples.
struct FixedDelay
{
    static constexpr int N = 64;   // power of two above kLatencySamples
    std::array<float, N> buf {};
    int pos = 0;
    void reset() { buf.fill (0.0f); pos = 0; }
    float process (float x)
    {
        buf[(size_t) pos] = x;
        const float y = buf[(size_t) ((unsigned) (pos - kLatencySamples) & (N - 1))];
        pos = (pos + 1) & (N - 1);
        return y;
    }
};
static_assert (FixedDelay::N > kLatencySamples && (FixedDelay::N & (FixedDelay::N - 1)) == 0, "delay size");

// ---- the engine ---------------------------------------------------------------------------------------
class Engine
{
public:
    Engine() { buildFilters(); }

    void prepare (double sampleRate)
    {
        fs = sampleRate > 1000.0 ? sampleRate : 44100.0;
        kCoef = 1.0 - std::exp (-16.0 / (0.005 * fs));      // 5 ms time constant, applied every 16 samples
        kGain = 1.0 - std::exp (-1.0 / (0.005 * fs));
        kRamp = 1.0 / (0.010 * fs);
        kMs   = 1.0 - std::exp (-1.0 / (0.25 * fs));        // 250 ms power average for the level match
        kGlide = 1.0 - std::exp (-1.0 / (0.10 * fs));
        dcR = 1.0 - 2.0 * kPi * 2.0 / fs;                   // 2 Hz DC blocker after the tube stage
        reset();
        snap = true;
        updateTargets();
        snapToTargets();
    }

    void reset()
    {
        for (auto& c : sec) for (auto& z : c) z = { 0.0, 0.0 };
        for (auto& d : delayX) d.reset();
        for (auto& d : delayE) d.reset();
        resetOs();
        mixOs = 0.0; osRun = false; osWarm = 0; byp = cur.bypass ? 1.0 : 0.0;
        subCount = 0;
    }

    void setParams (const Params& np)
    {
        Params p = np;
        p.lowF = clampIdx (p.lowF, 4); p.hiF = clampIdx (p.hiF, 7); p.attF = clampIdx (p.attF, 3);
        p.lowB = clampKnob (p.lowB); p.lowA = clampKnob (p.lowA); p.hiB = clampKnob (p.hiB); p.bw = clampKnob (p.bw);
        p.hiA = clampKnob (p.hiA); p.tube = clampKnob (p.tube); p.out = clampKnob (p.out);
        if (p == cur && ! snap) return;
        cur = p;
        updateTargets();
    }

    int latency() const { return kLatencySamples; }

    // Process in place. r may be null for a mono signal.
    void process (float* l, float* r, int n)
    {
        const int nch = r != nullptr ? 2 : 1;
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            if (subCount == 0) controlTick();
            subCount = (subCount + 1) & 15;

            // one-pole output gain and bypass ramp
            gOut += (gTgt - gOut) * kGain;
            if (std::fabs (gOut - gTgt) < 1.0e-7) gOut = gTgt;
            const double bt = cur.bypass ? 1.0 : 0.0;
            if (byp < bt) byp = std::min (bt, byp + kRamp * 2.0); else if (byp > bt) byp = std::max (bt, byp - kRamp * 2.0);

            float e[2] = { 0.0f, 0.0f }, xd[2] = { 0.0f, 0.0f }, ed[2] = { 0.0f, 0.0f };
            for (int c = 0; c < nch; ++c)
            {
                const float v = ch[c][i];
                const bool bad = ! std::isfinite (v);
                xd[c] = delayX[c].process (bad ? 0.0f : v);   // a NaN or infinity never reaches the output
                double y = bad ? 0.0 : (double) v;
                for (int s = 0; s < kNumSections; ++s)
                {
                    if (! active[s]) continue;
                    const Biquad& k = coef[s];
                    auto& z = sec[c][s];
                    const double o = k.b0 * y + z[0];
                    z[0] = k.b1 * y - k.a1 * o + z[1];
                    z[1] = k.b2 * y - k.a2 * o;
                    y = o;
                }
                if (! std::isfinite (y) || bad)
                {
                    for (auto& zz : sec[c]) zz = { 0.0, 0.0 };
                    y = 0.0;
                }
                e[c] = (float) y;
                ed[c] = delayE[c].process (e[c]);
            }

            // tube path (4x oversampled), only while needed
            float tp[2] = { ed[0], ed[1] };
            const bool wantTube = cur.tube > 0.0f;
            if (wantTube && ! osRun) { osRun = true; osWarm = 0; resetOs(); }
            if (osRun)
            {
                double inMs = 0.0, outMs = 0.0;
                for (int c = 0; c < nch; ++c)
                {
                    double a0, a1;
                    upA[c].push (e[c], a0, a1);
                    double b0, b1, b2, b3;
                    upB[c].push (a0, b0, b1);
                    upB[c].push (a1, b2, b3);
                    // note: upB state advances once per 2x sample, so two pushes per input sample
                    const double s0 = shaper (b0), s1 = shaper (b1), s2 = shaper (b2), s3 = shaper (b3);
                    const double d0 = downB[c].push (s0, s1), d1 = downB[c].push (s2, s3);
                    double t = downA[c].push (d0, d1);
                    // DC blocker
                    const double dcy = t - dcX[c] + dcR * dcY[c];
                    dcX[c] = t; dcY[c] = dcy; t = dcy;
                    inMs += (double) e[c] * e[c]; outMs += t * t;
                    tpRaw[c] = t;
                }
                inMs /= nch; outMs /= nch;
                msIn += (inMs - msIn) * kMs; msOut += (outMs - msOut) * kMs;
                double target = 1.0;
                if (msIn > 1.0e-10 && msOut > 1.0e-12) target = std::sqrt (msIn / msOut);
                target = std::max (1.0, std::min (3.2, target));
                makeup += (target - makeup) * kGlide;
                for (int c = 0; c < nch; ++c) tp[c] = (float) (tpRaw[c] * makeup);
                ++osWarm;
            }
            // mix between the delayed EQ signal and the tube path
            const bool warm = osRun && osWarm > kLatencySamples + 8;
            const double mt = (wantTube && warm) ? 1.0 : 0.0;
            if (mixOs < mt) mixOs = std::min (mt, mixOs + kRamp); else if (mixOs > mt) mixOs = std::max (mt, mixOs - kRamp);
            if (osRun && ! wantTube && mixOs == 0.0) { osRun = false; }

            for (int c = 0; c < nch; ++c)
            {
                float y = ed[c];
                if (mixOs > 0.0) y = (float) (ed[c] + mixOs * ((double) tp[c] - ed[c]));
                if (gOut != 1.0) y = (float) (y * gOut);
                if (byp > 0.0)
                {
                    if (byp >= 1.0) y = xd[c];
                    else y = (float) (y + byp * ((double) xd[c] - y));
                }
                ch[c][i] = y;
            }
        }
    }

private:
    void buildFilters()
    {
        const auto a = detail::halfband<detail::kLenA> (8.6);
        const auto b = detail::halfband<detail::kLenB> (7.5);
        for (int c = 0; c < 2; ++c) { upA[c].init (a); downA[c].init (a); upB[c].init (b); downB[c].init (b); }
    }
    void resetOs()
    {
        for (int c = 0; c < 2; ++c) { upA[c].reset(); upB[c].reset(); downA[c].reset(); downB[c].reset(); dcX[c] = dcY[c] = 0.0; tpRaw[c] = 0.0; }
        msIn = msOut = 0.0; makeup = 1.0;
    }
    void updateTargets()
    {
        for (int s = 0; s < kNumSections; ++s) tgt[s] = sectionBiquad ((Section) s, cur, fs);
        tubeTgt = cur.tube;
        gTgt = cur.out == 5.0f ? 1.0 : std::pow (10.0, outGainDb (cur.out) / 20.0);
    }
    void snapToTargets()
    {
        for (int s = 0; s < kNumSections; ++s) { coef[s] = tgt[s]; active[s] = ! tgt[s].isIdentity(); }
        tubeCur = tubeTgt; shaper.set (tubeCur);
        gOut = gTgt;
        byp = cur.bypass ? 1.0 : 0.0;
        snap = false;
    }
    // every 16 samples: move the coefficients and the tube amount toward their targets
    void controlTick()
    {
        if (snap) snapToTargets();
        for (int s = 0; s < kNumSections; ++s)
        {
            Biquad& c = coef[s];
            const Biquad& t = tgt[s];
            if (c.b0 == t.b0 && c.b1 == t.b1 && c.b2 == t.b2 && c.a1 == t.a1 && c.a2 == t.a2) continue;
            c.b0 += (t.b0 - c.b0) * kCoef; c.b1 += (t.b1 - c.b1) * kCoef; c.b2 += (t.b2 - c.b2) * kCoef;
            c.a1 += (t.a1 - c.a1) * kCoef; c.a2 += (t.a2 - c.a2) * kCoef;
            const double dist = std::fabs (c.b0 - t.b0) + std::fabs (c.b1 - t.b1) + std::fabs (c.b2 - t.b2) + std::fabs (c.a1 - t.a1) + std::fabs (c.a2 - t.a2);
            if (dist < 1.0e-9) c = t;
            if (c.isIdentity()) { active[s] = false; for (int ch = 0; ch < 2; ++ch) sec[ch][s] = { 0.0, 0.0 }; }
            else active[s] = true;
        }
        if (tubeCur != tubeTgt)
        {
            tubeCur += (tubeTgt - tubeCur) * kCoef;
            if (std::fabs (tubeCur - tubeTgt) < 1.0e-4) tubeCur = tubeTgt;
            shaper.set (tubeCur);
        }
    }

    double fs = 44100.0, kCoef = 0.1, kGain = 0.001, kRamp = 0.002, kMs = 1.0e-4, kGlide = 2.0e-4, dcR = 0.9997;
    Params cur;
    bool snap = true;
    Biquad coef[kNumSections], tgt[kNumSections];
    bool active[kNumSections] = { false, false, false, false };
    std::array<std::array<std::array<double, 2>, kNumSections>, 2> sec {};
    double tubeCur = 0.0, tubeTgt = 0.0, gOut = 1.0, gTgt = 1.0, byp = 0.0, mixOs = 0.0;
    int subCount = 0;
    TubeShaper shaper;
    FixedDelay delayX[2], delayE[2];
    detail::Up2<detail::kLenA> upA[2];
    detail::Up2<detail::kLenB> upB[2];
    detail::Down2<detail::kLenB> downB[2];
    detail::Down2<detail::kLenA> downA[2];
    double dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 }, tpRaw[2] = { 0, 0 }, msIn = 0.0, msOut = 0.0, makeup = 1.0;
    bool osRun = false;
    int osWarm = 0;
};
} // namespace pawl
