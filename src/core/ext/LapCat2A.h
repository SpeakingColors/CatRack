// LapCat2A DSP core v1.0. Plain C++17, no JUCE, no allocation after construction.
//
// A behaviour model of an optical leveling amplifier (LA-2A class), built from published
// manuals and measurements. It is NOT a circuit simulation and shares no code with any vendor.
// Signal flow: input, T4 cell (feedback side-chain, two-stage cell with memory, cubic cell
// distortion), Gain, tube stage (4x oversampled, small even-order bias that grows with drive),
// output transformer low-frequency saturation, optional noise floor and hum, Mix, output.
//
// Verified from sources (see the Guide in the plugin): Peak Reduction is side-chain drive (threshold
// 0 to -40 dB); Gain is up to 40 dB make-up after the cell and does not change the compression;
// the side-chain listens after the cell (feedback); attack about 10 ms; release about 60 ms to
// half, then 0.5 to 5 s and slower after long or deep compression; Compress about 3:1, Limit much
// higher. Everything else (knee shapes, two-stage time constants, distortion amounts, HF Emphasis
// corner, oversampler) is OUR OWN TUNING and is said so in the Guide and the manual.
//
// Latency: the 4x oversampler is a linear phase FIR, so the wet path is exactly kLatency samples
// at every sample rate. The dry path (Mix, Bypass) is delayed by the same number of samples.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lap
{
inline double dbToLin (double db) { return std::pow (10.0, db * 0.05); }
inline double linToDb (double x)  { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }

constexpr double kPi = 3.14159265358979323846;
constexpr double kEmphFc = 500.0;
constexpr int    kOsL = 4, kOsTaps = 121, kOsMid = 60; // 4x oversampler, linear phase Kaiser FIR
constexpr double kTubeA = 0.3, kCellK = 0.8, kXfmrK = 0.5;
constexpr int    kLatency = 2 * kOsMid / kOsL; // 30 samples, exact (wet path), see the header comment

struct Params
{
    float peak = 35.0f;   // Peak Reduction, 0..100
    float gain = 20.0f;   // Gain, 0..100 (0..40 dB)
    float limit = 0.0f;   // 0 Compress, 1 Limit
    float emph = 0.0f;    // HF Emphasis, 0..100
    float mix = 100.0f;   // 0..100 percent wet
    bool  bypass = false;
    bool  noise = false;  // hardware noise floor and mains hum (off by default)
};

// Static curve of the feedback side-chain, shared by the audio code and the transfer curve display.
struct Curve
{
    // demanded gain reduction (dB) per dB over the threshold; limit 0..1 blends Compress and Limit
    static double cOf (double ov, double limit)
    {
        const double o = std::min (std::max (ov, 0.0) / 20.0, 1.0), o2 = std::min (std::max (ov, 0.0) / 10.0, 1.0);
        const double comp = 0.8 + 1.4 * o, lim = 6.0 + 54.0 * o2;
        return comp + (lim - comp) * limit;
    }
    static double drive (double ov, double limit)
    {
        const double w = 6.0 + (3.0 - 6.0) * limit;
        const double sp = w * std::log (1.0 + std::exp (std::min (ov / w, 40.0)));
        return std::min (40.0, cOf (ov, limit) * sp);
    }
    static double threshold (double peak) { return -0.4 * peak; }
    // Steady state gain reduction (dB) for an input level Lin (dB) at the given settings.
    static double target (double peak, double limit, double Lin)
    {
        if (peak < 0.5) return 0.0;
        const double T = threshold (peak);
        double lo = 0.0, hi = 45.0;
        for (int i = 0; i < 32; ++i)
        {
            const double m = 0.5 * (lo + hi);
            (m - drive (Lin - m - T, limit) > 0.0) ? hi = m : lo = m;
        }
        return 0.5 * (lo + hi);
    }
};

class Core
{
public:
    Core() { designFir(); prepare (48000.0); }

    void prepare (double sampleRate)
    {
        fs = sampleRate > 1000.0 ? sampleRate : 48000.0;
        const auto c = [this] (double t) { return 1.0 - std::exp (-1.0 / (t * fs)); };
        aLp = 1.0 - std::exp (-2.0 * kPi * kEmphFc / fs);
        cDetA = c (0.005); cDetR = c (0.010); cRelA = c (0.042 / std::log (2.0)); cAttB = c (0.06);
        cMemUp = c (3.0); cMemDn = c (15.0); cEnv = c (0.03); cVu = c (0.012);
        aT = 1.0 - std::exp (-2.0 * kPi * 70.0 / fs);
        humW = 2.0 * kPi * 60.0 / fs;
        aDc = 1.0 - std::exp (-2.0 * kPi * 3.0 / fs);
        rampLen = std::max (1, (int) std::lround (0.010 * fs)); // 10 ms parameter smoothing
        reset();
        snap();
    }

    void reset()
    {
        lp[0] = lp[1] = 0.0; det = detIn = a = b = mem = gr = 0.0;
        grNowV = grAvgV = grPkV = 0.0; vuEV = 0.0; lvInV = -90.0; scV = -90.0;
        std::memset (xu, 0, sizeof xu); std::memset (zp, 0, sizeof zp);
        tf[0] = tf[1] = 0.0; dc[0] = dc[1] = 0.0; env = 0.0; hum = 0.0;
        std::memset (dly, 0, sizeof dly); di = 0;
        bypassMix = tBypass ? 1.0 : 0.0;
    }

    void setParams (const Params& q)
    {
        pr.set (q.peak, rampLen); gn.set (q.gain, rampLen); md.set (q.limit, rampLen);
        em.set (q.emph, rampLen); mx.set (q.mix, rampLen);
        tBypass = q.bypass; noiseOn = q.noise;
    }
    // Jump every smoother to its target (prepare and tests).
    void snap() { pr.snap(); gn.snap(); md.snap(); em.snap(); mx.snap(); bypassMix = tBypass ? 1.0 : 0.0; derive (true); }

    void setOversampling (bool on) { os = on; }
    // Output coupling: a 3 Hz one pole high-pass after the transformer stage removes the DC the tube bias creates.
    void setDcBlock (bool on) { dcBlock = on; }
    int latency() const { return kLatency; }

    // L and R are processed in place. R may be null for a mono bus.
    void process (float* L, float* R, int n)
    {
        const bool st = R != nullptr;
        double ip = 0.0, gs = 0.0, gm = 0.0;
        const double bpStep = 1.0 / (double) rampLen;
        for (int i = 0; i < n; ++i)
        {
            double x0 = L[i], x1 = st ? R[i] : x0;
            if (! std::isfinite (x0)) x0 = 0.0;
            if (! std::isfinite (x1)) x1 = 0.0;
            if (pr.step() | gn.step() | md.step() | em.step() | mx.step()) derive (false);
            const bool bpTarget = tBypass;
            if (bypassMix < (bpTarget ? 1.0 : 0.0)) bypassMix = std::min (1.0, bypassMix + bpStep);
            else if (bypassMix > (bpTarget ? 1.0 : 0.0)) bypassMix = std::max (0.0, bypassMix - bpStep);

            const double ax = std::max (std::fabs (x0), std::fabs (x1));
            ip = std::max (ip, ax);
            dly[0][di] = x0; dly[1][di] = x1;
            const int dj = (di - kLatency + 64) & 63;
            const double w0 = dly[0][dj], w1 = dly[1][dj];
            di = (di + 1) & 63;

            lp[0] += aLp * (x0 - lp[0]); lp[1] += aLp * (x1 - lp[1]);
            detIn += (ax > detIn ? cDetA : cDetR) * (ax - detIn);
            // the side-chain is fed from the signal after the cell (feedback), as in the manual
            const double g = dbToLin (-gr);
            const double s = std::max (std::fabs (x0 - kEmph * lp[0]), std::fabs (x1 - kEmph * lp[1])) * g;
            det += (s > det ? cDetA : cDetR) * (s - det);
            const double lv = linToDb (det);
            scV = lv; lvInV = linToDb (detIn);
            const double D = on ? Curve::drive (lv - thr, limitV) : 0.0;
            // T4 cell: fast stage and a slow stage whose release depends on the history
            const double tauA = std::min (0.010, std::max (0.006, 0.010 / (1.0 + D / 30.0)));
            a += (D > a ? 1.0 - std::exp (-1.0 / (tauA * fs)) : cRelA) * (D - a);
            const double tb = 0.35 + 3.65 * mem;
            b += (D > b ? cAttB : 1.0 - std::exp (-1.0 / (tb * fs))) * (D - b);
            const double e = std::min (1.0, D / 18.0);
            mem += (e > mem ? cMemUp : cMemDn) * (e - mem);
            const double wb = 0.12 + 0.28 * mem;
            gr = (1.0 - wb) * a + wb * b;
            if (! std::isfinite (gr)) { a = b = mem = gr = det = detIn = 0.0; }
            gs += gr; gm = std::max (gm, gr); grNowV = gr;

            // audio through the cell, with its cubic (3rd harmonic) distortion that grows with gain reduction
            const double g2 = dbToLin (-gr), eps = kCellK * gr;
            double y0 = sat3 (x0 * g2, eps) * gl, y1 = sat3 (x1 * g2, eps) * gl;

            // tube stage: tanh with an even-order bias that scales with drive; 2x oversampled
            env += cEnv * (std::max (std::fabs (y0), std::fabs (y1)) - env);
            const double B = 0.025 * std::min (1.2, env / 0.3), tB = std::tanh (B), nrm = 1.0 / (kTubeA * (1.0 - tB * tB));
            if (os)
            {
                y0 = tubeOs (0, y0, B, tB, nrm);
                if (st) y1 = tubeOs (1, y1, B, tB, nrm); else y1 = y0;
            }
            else { y0 = tube (y0, B, tB, nrm); y1 = st ? tube (y1, B, tB, nrm) : y0; }

            // output transformer: low-frequency core saturation
            tf[0] += aT * (y0 - tf[0]); tf[1] += aT * (y1 - tf[1]);
            y0 -= tf[0] - sat3 (tf[0], kXfmrK); y1 -= tf[1] - sat3 (tf[1], kXfmrK);

            if (dcBlock) { dc[0] += aDc * (y0 - dc[0]); dc[1] += aDc * (y1 - dc[1]); y0 -= dc[0]; y1 -= dc[1]; }
            if (noiseOn)
            {
                hum += humW; if (hum > 2.0 * kPi * 1000.0) hum -= 2.0 * kPi * 1000.0;
                const double h = 5.6e-5 * std::sin (hum) + 2.5e-5 * std::sin (2.0 * hum);
                y0 += rnd() * 2.2e-4 + h; y1 += rnd() * 2.2e-4 + h;
            }
            y0 = y0 * mixV + w0 * (1.0 - mixV); y1 = y1 * mixV + w1 * (1.0 - mixV);
            // Bypass is the dry signal delayed by the reported latency: exact once the 10 ms crossfade is done.
            if (bypassMix >= 1.0) { y0 = w0; y1 = w1; }
            else if (bypassMix > 0.0) { y0 += (w0 - y0) * bypassMix; y1 += (w1 - y1) * bypassMix; }
            if (! std::isfinite (y0)) y0 = 0.0;
            if (! std::isfinite (y1)) y1 = 0.0;
            L[i] = (float) y0; if (st) R[i] = (float) y1;
            vuEV += cVu * ((y0 * y0 + y1 * y1) * 0.5 - vuEV);
        }
        grPkV = gm; grAvgV = n > 0 ? gs / n : 0.0; inPkV = ip;
    }

    // Meters, read after process().
    double grNow() const { return grNowV; }      // dB, positive
    double grAvg() const { return grAvgV; }      // average over the last block
    double grPeak() const { return grPkV; }      // largest in the last block
    double vuEnergy() const { return vuEV; }     // slow mean square of the output
    double detectorIn() const { return lvInV; }  // dB, level that feeds the transfer curve dot
    double inputPeak() const { return inPkV; }
    double threshold() const { return thr; }

private:
    // linear ramp
    struct Ramp
    {
        double cur = 0, tgt = 0, inc = 0; int left = 0;
        void set (double t, int len) { if (t == tgt) return; tgt = t; left = len; inc = (tgt - cur) / (double) len; }
        void snap() { cur = tgt; left = 0; }
        bool step() { if (left <= 0) return false; cur += inc; if (--left == 0) cur = tgt; return true; }
    };

    static double sat3 (double x, double e) { return x - e * x * x * x / (1.0 + 4.0 * e * x * x); }
    static double tube (double u, double B, double tB, double nrm) { return (std::tanh (kTubeA * u + B) - tB) * nrm; }

    void derive (bool)
    {
        thr = Curve::threshold (pr.cur);
        on = pr.cur >= 0.5;
        gl = dbToLin (gn.cur * 0.4);
        limitV = md.cur;
        kEmph = 0.85 * em.cur / 100.0;
        mixV = mx.cur / 100.0;
    }

    // 4x polyphase oversampling around the tube stage. Up: four phases of the FIR on the last 31 inputs.
    // Down: the FIR on the 4x stream, decimated at phase 0, so the total delay is exactly 120 samples at 4x = 30 base samples.
    double tubeOs (int c, double y, double B, double tB, double nrm)
    {
        double* x = xu[c];
        double* z = zp[c];
        std::memmove (x + 1, x, (kXuLen - 1) * sizeof (double));
        x[0] = y;
        double zz[kOsL];
        for (int p = 0; p < kOsL; ++p)
        {
            double acc = 0.0;
            for (int q = 0; 4 * q + p < kOsTaps; ++q) acc += fir[4 * q + p] * x[q];
            zz[p] = tube ((double) kOsL * acc, B, tB, nrm);
        }
        double o = fir[0] * zz[0];
        for (int k = 1; k < kOsTaps; ++k) o += fir[k] * z[k - 1];
        std::memmove (z + kOsL, z, (kOsTaps - 1 - kOsL) * sizeof (double));
        z[0] = zz[3]; z[1] = zz[2]; z[2] = zz[1]; z[3] = zz[0];
        return o;
    }

    // Kaiser windowed sinc low-pass for the 4x oversampler: 121 taps, beta 7.9, cutoff 23.9 kHz at 48 kHz.
    // Flat within 0.3 dB at 20 kHz per pass (48 kHz base), stopband from about 28 kHz. Scales with the sample rate.
    static double besselI0 (double x)
    {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 80; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
        return s;
    }
    void designFir()
    {
        double s = 0.0;
        const double fc = 0.1245, beta = 7.9; // cycles per sample at the 4x rate
        for (int n = 0; n < kOsTaps; ++n)
        {
            const int k = n - kOsMid;
            const double sn = k == 0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * k) / (kPi * k);
            const double r = (double) k / (double) kOsMid;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            fir[n] = sn * w; s += fir[n];
        }
        for (int n = 0; n < kOsTaps; ++n) fir[n] /= s;
    }

    double rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (double) rng / 2147483648.0 - 1.0; }

    double fs = 48000.0;
    double aDc = 0, aLp = 0, cDetA = 0, cDetR = 0, cRelA = 0, cAttB = 0, cMemUp = 0, cMemDn = 0, cEnv = 0, cVu = 0, aT = 0, humW = 0;
    int rampLen = 480;
    Ramp pr, gn, md, em, mx;
    bool tBypass = false, noiseOn = false, os = true, on = true, dcBlock = true;
    double bypassMix = 0.0;
    double thr = -14.0, gl = 1.0, limitV = 0.0, kEmph = 0.0, mixV = 1.0;
    double dc[2] {}, lp[2] {}, det = 0, detIn = 0, a = 0, b = 0, mem = 0, gr = 0, env = 0, hum = 0, tf[2] {};
    double grNowV = 0, grAvgV = 0, grPkV = 0, vuEV = 0, lvInV = -90, scV = -90, inPkV = 0;
    double fir[kOsTaps] {};
    static constexpr int kXuLen = kOsTaps / kOsL + 1; // 31
    double xu[2][kXuLen] {}, zp[2][kOsTaps] {};
    double dly[2][64] {};
    int di = 0;
    uint32_t rng = 2463534242u;
};
} // namespace lap
