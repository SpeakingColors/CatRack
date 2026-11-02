#pragma once
// Small DSP building blocks for the CatRack units that are written for this plugin (pure C++17, no JUCE,
// no allocation after prepare). Everything is smoothed (about 10 ms), denormal safe and NaN safe.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace catr
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
inline float flushf (float x) { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }
inline float finitef (float x) { return std::isfinite (x) ? x : 0.0f; }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// One pole parameter smoother. advance (n) moves it n samples in one step.
struct Smooth
{
    float v = 0.0f, t = 0.0f, a = 1.0f;
    double ms = 10.0, fs = 48000.0;
    void setup (double sampleRate, double timeMs = 10.0)
    {
        fs = sampleRate; ms = timeMs;
        a = (float) (1.0 - std::exp (-1.0 / (0.001 * ms * fs)));
    }
    void set (float x) { t = x; }
    void snap (float x) { v = t = x; }
    float next() { v += (t - v) * a; return v; }
    float advance (int n)
    {
        const float k = 1.0f - std::pow (1.0f - a, (float) n);
        v += (t - v) * k;
        return v;
    }
    bool settled() const { return std::fabs (t - v) < 1.0e-6f; }
};

// RBJ biquad, transposed direct form II, double state.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (std::fabs (z1) < 1.0e-30) z1 = 0;
        if (std::fabs (z2) < 1.0e-30) z2 = 0;
        return (float) y;
    }
    void setRaw (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    static double clampFreq (double f, double fs) { return std::max (10.0, std::min (f, fs * 0.45)); }
    void lowpass (double fs, double f, double q = 0.70710678)
    {
        f = clampFreq (f, fs);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        setRaw ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double fs, double f, double q = 0.70710678)
    {
        f = clampFreq (f, fs);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        setRaw ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fs, double f, double q, double gainDb)
    {
        f = clampFreq (f, fs);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        setRaw (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double fs, double f, double gainDb)
    {
        f = clampFreq (f, fs);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        setRaw (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double fs, double f, double gainDb)
    {
        f = clampFreq (f, fs);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        setRaw (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

// Fourth order Linkwitz-Riley low or high pass (two Butterworth biquads).
struct LR4
{
    Biquad a, b;
    void reset() { a.reset(); b.reset(); }
    void low (double fs, double f) { a.lowpass (fs, f); b.lowpass (fs, f); }
    void high (double fs, double f) { a.highpass (fs, f); b.highpass (fs, f); }
    float process (float x) { return b.process (a.process (x)); }
};

// First order antiderivative anti-aliasing for f(x) = tanh (g x + bias) - tanh (bias).
// F(x) = log cosh (g x + bias) / g - tanh (bias) x.
struct AdaaTanh
{
    double x1 = 0, F1 = 0;
    static double logcosh (double u) { const double a = std::fabs (u); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }
    void reset() { x1 = 0; F1 = 0; }
    float process (float xin, double g, double bias)
    {
        const double x = (double) xin, tb = std::tanh (bias);
        const double F = logcosh (g * x + bias) / g - tb * x;
        double y;
        const double dx = x - x1;
        if (std::fabs (dx) < 1.0e-6) y = std::tanh (g * 0.5 * (x + x1) + bias) - tb;
        else y = (F - F1) / dx;
        x1 = x; F1 = F;
        return (float) y;
    }
};

// Delay line with linear interpolation. Allocates in init only.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init (int minSamples)
    {
        int n = 16;
        while (n < minSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (float d) const   // d samples back, d >= 1
    {
        const float p = (float) w - d;
        int i = (int) std::floor (p);
        const float f = p - (float) i;
        const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)];
        return a + (b - a) * f;
    }
    float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
};

// Fixed integer delay for a stereo pair, used to line up paths with different latency.
struct AlignDelay
{
    std::vector<float> l, r;
    int mask = 0, w = 0, d = 0;
    void init (int maxDelay)
    {
        int n = 16;
        while (n < maxDelay + 2) n <<= 1;
        l.assign ((size_t) n, 0.0f); r.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; d = 0;
    }
    void setDelay (int samples) { d = std::max (0, std::min (samples, mask)); }
    void reset() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); w = 0; }
    void process (float* L, float* R, int n)
    {
        if (d == 0) return;
        for (int i = 0; i < n; ++i)
        {
            l[(size_t) w] = L[i]; r[(size_t) w] = R[i];
            const int rp = (w - d) & mask;
            L[i] = l[(size_t) rp]; R[i] = r[(size_t) rp];
            w = (w + 1) & mask;
        }
    }
};

// Feed-forward compressor in the dB domain: soft knee, attack and release on the gain reduction.
struct CompCore
{
    float thr = -18, ratio = 4, knee = 6, atkS = 0.01f, relS = 0.2f, gr = 0, a = 0, r = 0;
    double fs = 48000;
    void prepare (double sr) { fs = sr; reset(); setTimes (atkS, relS); }
    void reset() { gr = 0; }
    void setTimes (float atk, float rel)
    {
        atkS = std::max (atk, 1.0e-5f); relS = std::max (rel, 1.0e-3f);
        a = (float) std::exp (-1.0 / (atkS * fs)); r = (float) std::exp (-1.0 / (relS * fs));
    }
    // levelDb: detector level. Returns the gain reduction in dB (positive) after ballistics.
    float step (float levelDb)
    {
        const float over = levelDb - thr;
        float tg = 0;
        if (knee > 0.01f && std::fabs (over) <= knee * 0.5f)
        {
            const float o = over + knee * 0.5f;
            tg = (1.0f - 1.0f / ratio) * o * o / (2.0f * knee);
        }
        else if (over > 0) tg = over * (1.0f - 1.0f / ratio);
        const float c = tg > gr ? a : r;
        gr = tg + (gr - tg) * c;
        if (gr < 1.0e-6f) gr = 0;
        return gr;
    }
};

// Look-ahead brick wall limiter. Latency is exactly latency() samples. The gain is the moving average of the
// window minimum of the required gain, so the ceiling holds (sample peak).
struct LookaheadLimiter
{
    int M = 24, L = 48;
    std::vector<float> dl, dr, req, wm;
    int mask = 0, pos = 0;
    float rg = 1.0f, relC = 0.999f, ceil = 1.0f;
    double fs = 48000;
    void prepare (double sr)
    {
        fs = sr;
        M = std::max (2, (int) std::lround (0.0005 * sr)); L = 2 * M;
        int n = 16;
        while (n < L + 8) n <<= 1;
        dl.assign ((size_t) n, 0.0f); dr = dl; req.assign ((size_t) n, 1.0f); wm = req; mask = n - 1; pos = 0; rg = 1.0f;
        relC = (float) std::exp (-1.0 / (0.08 * sr));
    }
    void reset() { std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f); std::fill (req.begin(), req.end(), 1.0f); std::fill (wm.begin(), wm.end(), 1.0f); rg = 1.0f; pos = 0; }
    int latency() const { return L; }
    void setCeilingDb (float db) { ceil = dbToLin (db); }
    void process (float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            dl[(size_t) pos] = l[i]; dr[(size_t) pos] = r[i];
            const float pk = std::max (std::fabs (l[i]), std::fabs (r[i]));
            const float need = pk > ceil ? ceil / pk : 1.0f;
            rg = need < rg ? need : 1.0f + (rg - 1.0f) * relC + (need - 1.0f) * (1.0f - relC);   // fast attack, slow release
            if (rg > 1.0f) rg = 1.0f;
            req[(size_t) pos] = rg;
            float m = 1.0f;
            for (int k = 0; k <= L; ++k) m = std::min (m, req[(size_t) ((pos - k) & mask)]);
            wm[(size_t) pos] = m;
            float s = 0;
            for (int k = 0; k <= M; ++k) s += wm[(size_t) ((pos - k) & mask)];
            const float g = s / (float) (M + 1);
            const int rp = (pos - L) & mask;
            l[i] = clampf (dl[(size_t) rp] * g, -ceil, ceil);
            r[i] = clampf (dr[(size_t) rp] * g, -ceil, ceil);
            pos = (pos + 1) & mask;
        }
    }
};

} // namespace catr
