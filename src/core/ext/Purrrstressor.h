// Purrrstressor DSP core v0.3. Plain C++17, no JUCE, no allocation after prepare().
// Original design: feed-forward compressor, dB-domain attack/release smoothing,
// per-ratio knee, detector filters, audio filter and two harmonic stages.
// Not based on any vendor code. Behaviour targets come from the public manuals
// (UA EL8 Distressor manual page; Empirical Labs EL8X manual).
// Anything marked OURS is our own choice, not a measurement of hardware.
// Detector tap: default is feed-forward (taps after the input amplifier, always stable).
// The UA diagram shows the detector tapping AFTER the distortion stage, which is a feedback
// loop. Params::detFeedback switches to a feedback detector with the gain computer inverted
// so the labelled ratios stay correct; the loop is stabilised by limiting the smoothing
// speed to 1/ratio per sample (so very fast attacks at 20:1 and Nuke are slower in this mode).
// Manual points modelled: nonlinear input and output amplifiers (scaled by Headroom, amounts
// OURS), THD and Redline indicators (Redline also means output clipping), attack faster than
// 50 us on 2:1, 3:1 and 4:1 (factor OURS). Not modelled (no published numbers): the mono
// "dead patch" Link behaviour and the per-ratio amount of threshold shift from Input.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>

namespace purr {

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)  { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

// Biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    // Magnitude response (linear) at f Hz, for the editor's filter display.
    double mag (double f, double fs) const
    {
        const std::complex<double> z1i = std::polar (1.0, -6.283185307179586 * f / fs), z2i = z1i * z1i;
        return std::abs (((double) b0 + (double) b1 * z1i + (double) b2 * z2i) / (1.0 + (double) a1 * z1i + (double) a2 * z2i));
    }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    // RBJ peaking EQ.
    void setPeak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 6.283185307179586 * f / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + al / A;
        b0 = (float) ((1.0 + al * A) / a0);
        b1 = (float) (-2.0 * c / a0);
        b2 = (float) ((1.0 - al * A) / a0);
        a1 = b1;
        a2 = (float) ((1.0 - al / A) / a0);
    }
    // Analog s^2 / (s^2 + b*wc*s + c*wc^2) through the bilinear transform, corner prewarped to fc.
    void setHighPass2 (double fs, double fc, double b, double c)
    {
        const double wc = 6.283185307179586 * fc, K = wc / std::tan (wc / (2.0 * fs));
        const double a0 = K * K + b * wc * K + c * wc * wc;
        b0 = (float) (K * K / a0);
        b1 = (float) (-2.0 * K * K / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * K * K + 2.0 * c * wc * wc) / a0);
        a2 = (float) ((K * K - b * wc * K + c * wc * wc) / a0);
    }
};

// First order high-pass: analog s / (s + a*wc), bilinear, corner prewarped.
struct OnePoleHP
{
    float b0 = 1, b1 = -1, a1 = 0, x1 = 0, y1 = 0;
    void reset() { x1 = y1 = 0; }
    double mag (double f, double fs) const
    {
        const std::complex<double> z = std::polar (1.0, -6.283185307179586 * f / fs);
        return std::abs (((double) b0 + (double) b1 * z) / (1.0 + (double) a1 * z));
    }
    float process (float x)
    {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = y;
        return y;
    }
    void set (double fs, double fc, double a = 1.0)
    {
        const double wc = 6.283185307179586 * fc, K = wc / std::tan (wc / (2.0 * fs));
        const double a0 = K + a * wc;
        b0 = (float) (K / a0);
        b1 = -b0;
        a1 = (float) ((a * wc - K) / a0);
    }
};

// 3rd order Bessel high-pass (18 dB/oct), -3 dB at fc. Prototype poles are the
// standard delay-normalised 3rd order Bessel poles; scaled here to -3 dB at 1 rad/s.
struct BesselHP3
{
    OnePoleHP one;
    Biquad two;
    void reset() { one.reset(); two.reset(); }
    double mag (double f, double fs) const { return one.mag (f, fs) * two.mag (f, fs); }
    float process (float x) { return two.process (one.process (x)); }
    void set (double fs, double fc)
    {
        using C = std::complex<double>;
        const C p1 (-2.3221854, 0.0), p2 (-1.8389073, 1.7543810), p3 = std::conj (p2);
        const C num = p1 * p2 * p3;
        double lo = 0.1, hi = 10.0;
        for (int i = 0; i < 60; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const C s (0.0, mid);
            (std::abs (num / ((s - p1) * (s - p2) * (s - p3))) > 0.70710678 ? lo : hi) = mid;
        }
        const double w3 = 0.5 * (lo + hi);
        const C r1 = 1.0 / (p1 / w3), r2 = 1.0 / (p2 / w3); // high-pass poles at fc = 1 rad/s
        one.set (fs, fc, -r1.real());
        two.setHighPass2 (fs, fc, -2.0 * r2.real(), std::norm (r2));
    }
};

// Ratio steps of the Ratio button. Index 5 is the 10:1 "Opto" curve (separate detector
// behaviour in the hardware), index 7 is Nuke.
inline constexpr float kRatios[8]   = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 100.0f };
// Knee width per ratio. Manuals: 2:1 and 3:1 are long parabolic knees (2:1 up to 30 dB),
// 4:1 and 6:1 steeper, 10:1 shorter, 20:1 and Nuke hard. Exact widths are OURS.
inline constexpr float kKneeDb[8]   = { 0.0f, 30.0f, 20.0f, 12.0f, 8.0f, 4.0f, 0.5f, 0.5f };
// Threshold per ratio. Manual: each ratio sets threshold and ratio together; values are
// not published, so all equal for now (OURS, to tune by ear).
inline constexpr float kThrDb[8]    = { -18.0f, -18.0f, -18.0f, -18.0f, -18.0f, -18.0f, -18.0f, -18.0f };
inline constexpr int   kOptoIndex = 5;

enum class DistMode { Off = 0, Dist2 = 1, Dist3 = 2 };

// First order antiderivative anti-aliasing (ADAA) for the memoryless waveshapers.
// Only the nonlinear part r(x) = g(x) - x goes through ADAA: y = x + (R(x[n]) - R(x[n-1])) / dx,
// R = antiderivative of r. (Running the whole shaper through ADAA would add a two point average
// that darkens the linear signal by 12 dB at 20 kHz.) R is always evaluated for the previous
// input with the CURRENT amounts, so a slowly moving amount cannot cause spikes. Zero latency;
// the added harmonics sit about half a sample later than the fundamental, which is inaudible.
struct AdaaQuad // g(x) = x + a x^2
{
    float xp = 0;
    void reset() { xp = 0; }
    float process (float x, float a)
    {
        const double dx = (double) x - xp;
        double r;
        if (std::fabs (dx) < 1.0e-4) { const double m = 0.5 * ((double) x + xp); r = a * m * m; }
        else r = a * ((double) x * x * x - (double) xp * xp * xp) / (3.0 * dx);
        xp = x;
        return x + (float) r;
    }
};

struct AdaaCubic // g(x) = c - b c^3 with c = clamp(x, -lim, lim), lim = 0.9 / sqrt(3 b) (monotonic)
{
    float xp = 0;
    void reset() { xp = 0; }
    static double Rn (double x, double b, double lim) // r(x) = g(x) - x
    {
        const double c = std::clamp (x, -lim, lim);
        return c - b * c * c * c - x;
    }
    static double FR (double x, double b, double lim) // antiderivative of r, even function
    {
        const double ax = std::fabs (x);
        if (ax <= lim) return -0.25 * b * ax * ax * ax * ax;
        const double glim = lim - b * lim * lim * lim;
        return -0.25 * b * lim * lim * lim * lim + glim * (ax - lim) - 0.5 * (ax * ax - lim * lim);
    }
    float process (float x, float b)
    {
        if (b < 1.0e-9f) { xp = x; return x; }
        const double lim = 0.9 / std::sqrt (3.0 * (double) b);
        const double dx = (double) x - xp;
        double r;
        if (std::fabs (dx) < 1.0e-4) r = Rn (0.5 * ((double) x + xp), b, lim);
        else r = (FR (x, b, lim) - FR (xp, b, lim)) / dx;
        xp = x;
        return x + (float) r;
    }
};

struct Params
{
    float inputDb   = 0.0f;    // 0 to +30, drives detector and audio (hardware: input gain and threshold)
    float outputDb  = 0.0f;    // -24 to +24
    float attackMs  = 10.0f;   // 0.05 to 30 (manual: 50 us to 30 ms)
    float releaseMs = 200.0f;  // 50 to 3500 (manual: 0.05 s to 3.5 s; Opto up to 20 s)
    int   ratioIndex = 3;      // 0..7 into kRatios
    bool  detHP     = false;   // detector high-pass, 100 Hz, 6 dB/oct
    bool  detBand   = false;   // detector band emphasis, boost around 6 kHz
    bool  link      = true;    // stereo link
    bool  audioHP   = false;   // audio high-pass, 18 dB/oct Bessel
    DistMode dist   = DistMode::Off;
    float headroomDb = 16.0f;  // plug-in only HR control, 4 to 28 in 4 dB steps, default 16
    float mix       = 1.0f;    // 0 dry .. 1 compressed (parallel, plug-in only)
    bool  detFeedback = false; // detector taps after the distortion (feedback), stabilised
    bool  levelMatch = false;  // slowly match processed loudness to the dry input (compare aid)
    bool  autoTrim   = false;  // ratchet the output down whenever it reaches 0 dBFS
};

class Compressor
{
public:
    static constexpr float kDetHPHz   = 100.0f; // manual
    static constexpr float kBandHz    = 6000.0f; // manual: "around 6 kHz"
    static constexpr float kBandGainDb = 9.0f;   // OURS, amount not published
    static constexpr float kAudioHPHz = 60.0f;   // -3 dB point from the EL8X manual (about -3 dB at 60 Hz, -12 dB at 30 Hz); UA page says 80 Hz cutoff
    static constexpr float kOptoReleaseGrow = 4.7f; // OURS: 3.5 s * (1 + 4.7) = about 20 s
    static constexpr float kFastAttackFactor = 0.5f; // OURS: 2:1, 3:1, 4:1 reach faster attacks than 50 us (manual)
    static constexpr float kTrimMarginDb = 0.1f;   // Auto Trim drops this much past each overshoot
    static constexpr float kIoAmpK = 0.01f;         // OURS: input amp cubic amount at HR 16 (output amp uses half)

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        smoothCoef = 1.0f - std::exp (-1.0f / (0.010f * (float) fs)); // about 10 ms
        dcR = 1.0f - 6.283185f * 10.0f / (float) fs;
        lmEnergyK = 1.0f - std::exp (-1.0f / (1.0f * (float) fs));  // 1 s level averaging
        lmGainK   = 1.0f - std::exp (-1.0f / (2.0f * (float) fs));  // 2 s gain smoothing
        pkDecay   = std::exp (-1.0f / (0.1f * (float) fs));         // 100 ms peak hold decay for THD and clip
        for (int c = 0; c < 2; ++c)
        {
            detHP[c].set (fs, kDetHPHz);
            detBand[c].setPeak (fs, kBandHz, 0.7, kBandGainDb);
            audHP[c].set (fs, kAudioHPHz);
        }
        setParams (p);
        curIn = tgtIn; curOut = tgtOut; curMix = p.mix;
        reset();
    }

    void reset()
    {
        gr[0] = gr[1] = 0; grMeter = 0;
        for (int c = 0; c < 2; ++c)
        {
            detHP[c].reset(); detBand[c].reset(); audHP[c].reset(); dcX[c] = dcY[c] = 0;
            aaIn[c].reset(); aaOut[c].reset(); aaQ[c].reset(); aaC[c].reset(); yPrev[c] = 0;
        }
        resetLevelMatch(); resetTrim();
        inPk = outPk = thdEnv = 0;
    }
    void resetTrim() { trimLin = 1.0f; }
    void resetLevelMatch() { lmGain = 1.0f; eDry = eWet = 0.0f; }
    void setAntiAlias (bool on) { antiAlias = on; } // on by default; off only for A/B tests

    void setParams (const Params& np)
    {
        p = np;
        p.ratioIndex = std::clamp (p.ratioIndex, 0, 7);
        p.attackMs   = std::clamp (p.attackMs, 0.05f, 30.0f);
        p.releaseMs  = std::clamp (p.releaseMs, 50.0f, 3500.0f);
        p.mix        = std::clamp (p.mix, 0.0f, 1.0f);
        p.headroomDb = std::clamp (p.headroomDb, 4.0f, 28.0f);
        tgtIn  = dbToLin (std::clamp (p.inputDb, 0.0f, 30.0f));
        tgtOut = dbToLin (std::clamp (p.outputDb, -24.0f, 24.0f));
        float atk = p.attackMs;
        if (p.ratioIndex >= 1 && p.ratioIndex <= 3) atk *= kFastAttackFactor;
        aAtk   = std::exp (-1.0f / (atk * 0.001f * (float) fs));
        if (! p.levelMatch) resetLevelMatch();
        if (! p.autoTrim) resetTrim();
    }

    // r may be nullptr for mono.
    void process (float* l, float* r, int n)
    {
        const bool stereo = (r != nullptr);
        const int  ri = p.ratioIndex;
        const float ratio = kRatios[ri];
        const bool compress = ratio > 1.0f;
        const int chans = stereo ? 2 : 1;
        const float hrLin = dbToLin (p.headroomDb - 16.0f);
        const float kIn = kIoAmpK * hrLin * hrLin, kOutAmp = 0.5f * kIn; // cubic amounts of the I/O amplifiers

        for (int i = 0; i < n; ++i)
        {
            curIn  = approach (curIn,  tgtIn);
            curOut = approach (curOut, tgtOut);
            curMix = approach (curMix, p.mix);

            float dry[2] = { l[i], stereo ? r[i] : 0.0f };
            float x[2];
            float pkNow = 0.0f;
            for (int c = 0; c < chans; ++c)
            {
                pkNow = std::max (pkNow, std::fabs (dry[c]));
                // Input amplifier (UA diagram: Input, then Input Level), then the Input Level gain.
                const float amp = antiAlias ? aaIn[c].process (dry[c], kIn) : cubicNaive (dry[c], kIn);
                x[c] = amp * curIn;
            }
            if (! stereo) x[1] = 0.0f;

            float lv[2] = { -180.0f, -180.0f };
            for (int c = 0; c < chans; ++c)
            {
                float d = p.detFeedback ? yPrev[c] : x[c];
                if (p.detHP)   d = detHP[c].process (d);
                if (p.detBand) d = detBand[c].process (d);
                lv[c] = linToDb (std::fabs (d));
            }
            if (! stereo) lv[1] = lv[0];

            if (compress)
            {
                if (p.link || ! stereo)
                {
                    gr[0] = smooth (gr[0], target (std::max (lv[0], lv[1]), ri), ri);
                    gr[1] = gr[0];
                }
                else
                {
                    gr[0] = smooth (gr[0], target (lv[0], ri), ri);
                    gr[1] = smooth (gr[1], target (lv[1], ri), ri);
                }
            }
            else gr[0] = gr[1] = 0.0f;

            grMeter = std::max (gr[0], gr[1]);

            float wet[2], mixed[2];
            float thd2 = 0.0f;
            for (int c = 0; c < chans; ++c)
            {
                // Order from the UA signal flow diagram: Input, VCA, Distortion, Audio HP, Output.
                float y = x[c] * dbToLin (-gr[c]);
                if (p.dist != DistMode::Off) y = distort (y, gr[c], c, thd2);
                yPrev[c] = y;
                if (p.audioHP) y = audHP[c].process (y);
                const float yo = y * curOut;
                wet[c] = antiAlias ? aaOut[c].process (yo, kOutAmp) : cubicNaive (yo, kOutAmp); // output amplifier
            }

            // Level Match: scale the processed signal to the loudness of the dry input.
            if (p.levelMatch)
            {
                float ed = 0.0f, ew = 0.0f;
                for (int c = 0; c < chans; ++c) { ed += dry[c] * dry[c]; ew += wet[c] * wet[c]; }
                eDry += (ed / (float) chans - eDry) * lmEnergyK;
                eWet += (ew / (float) chans - eWet) * lmEnergyK;
                if (eDry > 1.0e-9f && eWet > 1.0e-9f)
                {
                    const float t = std::clamp (std::sqrt (eDry / eWet), 0.0625f, 16.0f); // +-24 dB
                    lmGain += (t - lmGain) * lmGainK;
                }
                for (int c = 0; c < chans; ++c) wet[c] *= lmGain;
            }

            float peak = 0.0f;
            for (int c = 0; c < chans; ++c)
            {
                float o;
                if (curMix >= 1.0f) o = wet[c];
                else if (curMix <= 0.0f) o = dry[c];
                else o = dry[c] * (1.0f - curMix) + wet[c] * curMix;
                if (p.autoTrim) o *= trimLin;
                if (! std::isfinite (o)) { reset(); o = 0.0f; }
                mixed[c] = o;
                peak = std::max (peak, std::fabs (o));
            }
            // Auto Trim: no lookahead, so the overshoot itself passes; later samples are lower.
            if (p.autoTrim && peak >= 1.0f) trimLin *= dbToLin (-kTrimMarginDb) / peak;

            for (int c = 0; c < chans; ++c) (c == 0 ? l : r)[i] = mixed[c];

            // Indicators: THD estimate from the I/O amplifiers and distortion stage, and output peak.
            inPk  = std::max (pkNow, inPk * pkDecay);
            outPk = std::max (peak, outPk * pkDecay);
            const float h3in = kIn * inPk * inPk * 0.25f;
            thdEnv = std::max (std::sqrt (thd2 + h3in * h3in), thdEnv * pkDecay);
        }
    }

    // For the editor displays: the static transfer curve and the filter responses (dB).
    static float staticGrDb (float levelDb, int ri) { return grTarget (levelDb, ri); }
    static float detectorResponseDb (bool hp, bool band, double fs, double f)
    {
        double m = 1.0;
        if (hp)   { OnePoleHP h; h.set (fs, kDetHPHz); m *= h.mag (f, fs); }
        if (band) { Biquad b; b.setPeak (fs, kBandHz, 0.7, kBandGainDb); m *= b.mag (f, fs); }
        return (float) (20.0 * std::log10 (std::max (m, 1.0e-9)));
    }
    static float audioHpResponseDb (double fs, double f)
    {
        BesselHP3 h; h.set (fs, kAudioHPHz);
        return (float) (20.0 * std::log10 (std::max (h.mag (f, fs), 1.0e-9)));
    }
    float grDb() const { return grMeter; }           // positive dB, for the GR meter
    float levelMatchDb() const { return linToDb (lmGain); }
    float trimDb() const { return linToDb (trimLin); }  // 0 or negative
    float thd() const { return thdEnv; }              // linear ratio, 0.01 = 1 %
    bool  thd1Percent() const { return thdEnv >= 0.01f; }
    bool  redline() const { return thdEnv >= 0.03f || outPk >= 1.0f; } // manual: about 3 % THD, or output clipping

private:
    static float cubicNaive (float x, float b)
    {
        if (b < 1.0e-9f) return x;
        const float lim = 0.9f / std::sqrt (3.0f * b), c = std::clamp (x, -lim, lim);
        return c - b * c * c * c;
    }

    float approach (float cur, float tgt) const
    {
        const float d = tgt - cur;
        if (std::fabs (d) < 1.0e-7f) return tgt;
        return cur + d * smoothCoef;
    }

    // Static gain computer, returns gain reduction in dB (>= 0).
    static float grTarget (float levelDb, int ri)
    {
        const float ratio = kRatios[ri], thr = kThrDb[ri], w = kKneeDb[ri];
        const float over = levelDb - thr;
        if (2.0f * over < -w) return 0.0f;
        float out;
        if (2.0f * std::fabs (over) <= w)
        {
            const float k = over + w * 0.5f;
            out = levelDb + (1.0f / ratio - 1.0f) * k * k / (2.0f * w);
        }
        else out = thr + over / ratio;
        return std::max (0.0f, levelDb - out);
    }

    // Feedback version: the level seen is the OUTPUT of the gain stage, so invert the static
    // curve (output level to input level). Steady state matches grTarget on the input level.
    static float grTargetFeedback (float outDb, int ri)
    {
        const float ratio = kRatios[ri], thr = kThrDb[ri], w = kKneeDb[ri];
        const float start = thr - 0.5f * w;
        if (outDb <= start) return 0.0f;
        const float d = outDb - start;
        const float a = (1.0f / ratio - 1.0f) / (2.0f * w); // negative
        const float dEnd = w * (0.5f + 0.5f / ratio);        // output offset at the end of the knee
        float inDb;
        if (d >= dEnd) inDb = thr + (outDb - thr) * ratio;
        else
        {
            const float disc = std::max (0.0f, 1.0f + 4.0f * a * d);
            const float u = (std::fabs (a) < 1.0e-9f) ? d : (-1.0f + std::sqrt (disc)) / (2.0f * a);
            inDb = start + u;
        }
        return std::clamp (inDb - outDb, 0.0f, 60.0f); // cap: the inverse curve explodes for a loud first sample
    }

    float target (float levelDb, int ri) const
    {
        return p.detFeedback ? grTargetFeedback (levelDb, ri) : grTarget (levelDb, ri);
    }

    // One-pole smoothing in the dB domain; attack when GR grows, release when it falls.
    // Opto (10:1): release grows with depth, up to about 20 s at the longest setting (OURS).
    // Feedback detector: coefficients are limited to 1/ratio, which keeps the loop stable
    // (loop gain is at most ratio - 1).
    float smooth (float cur, float target, int ri) const
    {
        const float kMax = p.detFeedback ? 1.0f / kRatios[ri] : 1.0f;
        if (target > cur)
        {
            const float k = std::min (1.0f - aAtk, kMax);
            return cur + (target - cur) * k;
        }
        float relMs = p.releaseMs;
        if (ri == kOptoIndex) relMs *= 1.0f + kOptoReleaseGrow * std::min (cur / 20.0f, 1.0f);
        const float aRel = std::exp (-1.0f / (relMs * 0.001f * (float) fs));
        const float k = std::min (1.0f - aRel, kMax);
        return cur + (target - cur) * k;
    }

    // Dist 2: mostly 2nd harmonic. Dist 3: 3rd dominant with some 2nd (manuals).
    // Amount rises with gain reduction and with Headroom (HR, default 16 dB). Constants OURS.
    // Both stages use ADAA. thd2 accumulates the squared harmonic estimate from the running peak.
    float distort (float x, float grDbNow, int c, float& thd2)
    {
        const float lf = 0.4f + 0.6f * std::min (grDbNow, 20.0f) / 20.0f;
        const float d  = dbToLin (p.headroomDb - 16.0f);
        const float a2 = (p.dist == DistMode::Dist2 ? 0.15f : 0.05f) * lf * d;
        const float A = std::min (std::fabs (x), 1.5f); // instantaneous level; thdEnv holds the peak
        float y = antiAlias ? aaQ[c].process (x, a2) : x + a2 * x * x;
        float h2 = a2 * 0.5f * A, h3 = 0.0f;
        if (p.dist == DistMode::Dist3)
        {
            const float b = 0.3f * lf * d * d;
            y = antiAlias ? aaC[c].process (y, b) : cubicNaive (y, b);
            h3 = b * 0.25f * A * A;
        }
        thd2 = std::max (thd2, h2 * h2 + h3 * h3);
        const float o = y - dcX[c] + dcR * dcY[c]; // DC blocker (x^2 makes DC)
        dcX[c] = y; dcY[c] = o;
        return o;
    }

    Params p;
    double fs = 48000.0;
    float smoothCoef = 0.01f, dcR = 0.999f, aAtk = 0.9f, lmEnergyK = 1.0e-5f, lmGainK = 1.0e-5f, pkDecay = 0.9999f;
    float tgtIn = 1.0f, tgtOut = 1.0f, curIn = 1.0f, curOut = 1.0f, curMix = 1.0f;
    float gr[2] = { 0, 0 }, grMeter = 0;
    float yPrev[2] = { 0, 0 };
    float trimLin = 1.0f, lmGain = 1.0f, eDry = 0.0f, eWet = 0.0f, inPk = 0.0f, outPk = 0.0f, thdEnv = 0.0f;
    bool antiAlias = true;
    OnePoleHP detHP[2];
    Biquad detBand[2];
    BesselHP3 audHP[2];
    AdaaCubic aaIn[2], aaOut[2], aaC[2];
    AdaaQuad aaQ[2];
    float dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 };
};

} // namespace purr
