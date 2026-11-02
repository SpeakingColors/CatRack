// Apurrr2500 DSP core v0.1. Plain C++17, no JUCE, no allocation after construction.
//
// A stereo bus compressor with a threshold-relative gain computer, a 3 ms RMS detector, a "tilt" filter in the
// detector path, selectable Old (detector after the gain stage, feedback) or New (detector before it, feed forward)
// topology, partial left/right linking with a link-shape smoother, a manual or automatic make-up gain, a
// blend control (100 percent, cross-fade, parallel) and an optional external side-chain.
//
// This is our own implementation, ported from the approved interactive mockup (docs/mockup/Apurrr2500.html), which
// documents the intended behaviour. Calibration facts used (taken from the mockup, not measured on hardware):
//   * Threshold knob in dBu, range +10 to -20; 0 dBu is -22 dBFS (ALIGN).
//   * The needle meter reads 0 VU at -18 dBFS by default (editor setting only, never changes the sound).
//   * Ratios 1.5, 2, 3, 4, 6, 10, Inf; attacks 0.03 to 30 ms; releases 50 ms to 2 s plus a Var knob 50 ms to 3 s.
//   * Knee widths 12 / 6 / 0.5 dB (Soft / Med / Hard).
//   * Tilt filter: 12 first-order sections, 2.5 dB each, from 20 Hz in quarter decades (3 dB per octave).
// Anything else (smoothing times, the IN ramp, the denormal guards) is our own choice.
//
// Not modelled: component tolerances, VCA distortion, transformer colour. The gain element is an ideal multiplier.
// Aliasing: the signal path contains no waveshaper. The gain signal is smoothed by one pole filters, so the product
// of gain and signal is band limited well below Nyquist; tests/test_core.cpp measures the residue.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace apurr {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSections = 12;
constexpr double kAlignDb = 22.0;   // 0 dBu = -22 dBFS

inline double dbToLin (double db) { return std::exp (db * 0.11512925464970229); }
inline double linToDb (double x)  { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }
inline float  flush (float x) { return std::fabs (x) < 1.0e-30f ? 0.0f : x; }

inline const double* ratioTable()   { static const double t[7] = { 1.5, 2, 3, 4, 6, 10, 1000 }; return t; }
inline const double* attackTable()  { static const double t[7] = { 0.03, 0.1, 0.3, 1, 3, 10, 30 }; return t; }
inline const double* releaseTable() { static const double t[6] = { 50, 100, 200, 500, 1000, 2000 }; return t; }
inline const double* linkTable()    { static const double t[7] = { 0, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 }; return t; }
inline const double* kneeTable()    { static const double t[3] = { 12, 6, 0.5 }; return t; }

// Everything the user can set. Plain numbers, indices as in the plugin's choice parameters.
struct Params
{
    double thrDbu = 4;        // +10 .. -20
    int ratio = 1;            // 0..6
    double makeupDb = 0;      // manual make-up, 0..24
    bool manualMakeup = false;
    int attack = 5;           // 0..6
    int release = 2;          // 0..6, 6 = Var
    double relVarMs = 150;    // 50..3000
    int knee = 1;             // 0 Soft, 1 Med, 2 Hard
    int tilt = 0;             // 0 Norm, 1 Med, 2 Loud
    bool oldType = false;     // true: detector after the gain stage (feedback)
    bool extSc = false;       // external side-chain (only in the New type)
    int link = 6;             // 0 IND, 1..6 = 50 .. 100 percent
    int shape = 0;            // 0 Fast, 1 Slow, 2 Both
    int blend = 0;            // 0 100 percent, 1 cross-fade, 2 parallel
    double mix = 1.0;         // 0..1
    bool inSw = true;         // compression on
    bool levelMatch = false;
};

// ---- static curve maths, shared by the engine and the editor displays -----------------------------------------------
// Gain reduction in dB for a level L (dB) over a threshold, feed forward, quadratic knee of width w.
inline double gcomp (double L, double thr, double R, double w)
{
    const double over = L - thr;
    if (2.0 * over < -w) return 0.0;
    double out;
    if (2.0 * std::fabs (over) <= w) { const double k = over + w * 0.5; out = L + (1.0 / R - 1.0) * k * k / (2.0 * w); }
    else out = thr + over / R;
    return std::max (0.0, L - out);
}

// Same curve solved for the feedback (Old) detector: input is the level measured at the OUTPUT of the gain stage.
inline double gcompFb (double outDb, double thr, double R, double w)
{
    w = std::max (w, 0.5);
    const double start = thr - 0.5 * w;
    if (outDb <= start) return 0.0;
    const double d = outDb - start, a = (1.0 / R - 1.0) / (2.0 * w), dEnd = w * (0.5 + 0.5 / R);
    double inDb;
    if (d >= dEnd) inDb = thr + (outDb - thr) * R;
    else
    {
        const double disc = std::max (0.0, 1.0 + 4.0 * a * d);
        const double u = std::fabs (a) < 1.0e-9 ? d : (-1.0 + std::sqrt (disc)) / (2.0 * a);
        inDb = start + u;
    }
    return std::min (40.0, std::max (0.0, inDb - outDb));
}

struct Derived
{
    double thr, R, w, atkMs, relMs, muDb, link, mix;
    bool fb, ext, inSw;
};

inline double autoMakeupDb (const Params& p)
{
    const double thr = p.thrDbu - kAlignDb, R = ratioTable()[p.ratio], w = kneeTable()[p.knee];
    return std::min (24.0, std::max (0.0, gcomp (-18.0, thr, R, w)));
}

inline Derived derive (const Params& p)
{
    Derived d;
    d.thr = p.thrDbu - kAlignDb;
    d.R = ratioTable()[std::clamp (p.ratio, 0, 6)];
    d.w = kneeTable()[std::clamp (p.knee, 0, 2)];
    d.atkMs = attackTable()[std::clamp (p.attack, 0, 6)];
    d.relMs = p.release < 6 ? releaseTable()[std::clamp (p.release, 0, 5)] : std::clamp (p.relVarMs, 50.0, 3000.0);
    d.muDb = p.manualMakeup ? std::clamp (p.makeupDb, 0.0, 24.0) : autoMakeupDb (p);
    d.link = linkTable()[std::clamp (p.link, 0, 6)];
    d.mix = std::clamp (p.mix, 0.0, 1.0);
    d.fb = p.oldType;
    d.ext = ! p.oldType && p.extSc;
    d.inSw = p.inSw;
    return d;
}

// Static transfer curve for the editor: output level (dB) for an input level (dB) with the compressor engaged.
inline double staticOutDb (double inDb, const Params& p)
{
    const Derived d = derive (p);
    if (! p.inSw) return inDb;
    const double mu = d.muDb;
    if (! d.fb) return inDb - gcomp (inDb, d.thr, d.R, d.w) + mu;
    double lo = inDb + mu - 40.0, hi = inDb + mu;
    for (int i = 0; i < 30; ++i)
    {
        const double m = 0.5 * (lo + hi);
        if (m + gcompFb (m, d.thr, d.R, d.w) - (inDb + mu) > 0.0) hi = m; else lo = m;
    }
    return 0.5 * (lo + hi);
}

// First order section, unity gain at DC, rises by `rise` dB between fz and fp (bilinear transform).
struct Section
{
    double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
    void reset() { x1 = y1 = 0; }
    void set (double fs, double fz, double fp)
    {
        double fm = std::sqrt (fz * fp);
        const double lim = 0.45 * fs;
        if (fm > lim) { fz *= lim / fm; fp *= lim / fm; fm = lim; }
        const double K = 2.0 * kPi * fm / std::tan (kPi * fm / fs), a = K / (2.0 * kPi * fz), b = K / (2.0 * kPi * fp);
        b0 = (1 + a) / (1 + b); b1 = (1 - a) / (1 + b); a1 = (1 - b) / (1 + b);
    }
    double process (double x)
    {
        double y = b0 * x + b1 * x1 - a1 * y1;
        if (std::fabs (y) < 1.0e-25) y = 0.0;
        x1 = x; y1 = y;
        return y;
    }
    double mag (double f, double fs) const
    {
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double nr = b0 + b1 * c, ni = -b1 * s, dr = 1 + a1 * c, di = -a1 * s;
        return std::hypot (nr, ni) / std::hypot (dr, di);
    }
};

struct TiltDef { double rises[kSections]; double g0Db; };
inline TiltDef tiltDef (int mode)
{
    TiltDef t {};
    for (int i = 0; i < kSections; ++i)
    {
        const bool on = mode == 2 || (mode == 1 && (i <= 3 || i >= 9));
        t.rises[i] = on ? 2.5 : 0.0;
    }
    t.g0Db = mode == 2 ? -15.0 : mode == 1 ? -10.0 : 0.0;
    return t;
}

class Compressor
{
public:
    Compressor() { prepare (48000.0); }

    void prepare (double sampleRate)
    {
        fs = std::max (8000.0, sampleRate);
        sc = 1.0 - std::exp (-1.0 / (0.010 * fs));       // 10 ms parameter smoothing
        kRms = 1.0 - std::exp (-1.0 / (0.003 * fs));     // 3 ms detector
        kIn = 1.0 - std::exp (-1.0 / (0.008 * fs));      // 8 ms IN switch ramp
        kLmD = 1.0 - std::exp (-1.0 / (1.0 * fs));
        kLmG = 1.0 - std::exp (-1.0 / (2.0 * fs));
        kShapeTab[0] = 1.0;
        kShapeTab[1] = 1.0 - std::exp (-1.0 / (0.040 * fs));
        kShapeTab[2] = 1.0 - std::exp (-1.0 / (0.150 * fs));
        tiltApplied = -1;
        setAvgMs (avgMs);
        setParams (p, true);
        reset();
        snap();
    }

    void setAvgMs (double ms) { avgMs = ms; kAvg = 1.0 - std::exp (-1.0 / (ms * 0.001 * fs)); }

    void setParams (const Params& np, bool force = false)
    {
        p = np;
        d = derive (np);
        aAtk = std::exp (-1.0 / (std::max (0.02, d.atkMs) * 0.001 * fs));
        aRel = std::exp (-1.0 / (d.relMs * 0.001 * fs));
        kShape = kShapeTab[std::clamp (np.shape, 0, 2)];
        tMu = dbToLin (d.inSw ? d.muDb : 0.0);
        if (force || np.tilt != tiltApplied)
        {
            const TiltDef t = tiltDef (std::clamp (np.tilt, 0, 2));
            g0lin = dbToLin (t.g0Db);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kSections; ++i)
                {
                    const double fz = 20.0 * std::pow (10.0, 0.25 * i), fp = fz * dbToLin (std::max (0.001, t.rises[i]));
                    sec[c][i].set (fs, fz, fp);
                }
            tiltApplied = np.tilt;
            tiltOn = np.tilt != 0;
        }
    }

    // Jump the smoothed values to their targets (used after prepare and in tests).
    void snap()
    {
        thrS = d.thr; invRS = 1.0 / d.R; wS = d.w; mixS = d.mix; curMu = tMu; inRamp = d.inSw ? 1.0 : 0.0;
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto& s : sec[c]) s.reset();
            pw[c] = 1e-12; g[c] = 0; yPrev[c] = 0; lkS[c] = 0; aIn[c] = aOut[c] = 1e-9;
        }
        eD = eW = 0; lm = 1.0;
    }
    void resetLevelMatch() { lm = 1.0; eD = eW = 0; }

    // L and R processed in place. sc may be null (then the signal itself is the detector source in the New type).
    // R may be null for a mono track.
    void process (float* L, float* R, int n, const float* scL = nullptr, const float* scR = nullptr)
    {
        float tmp[512];
        if (R == nullptr)
        {
            for (int off = 0; off < n; off += 512)
            {
                const int m = std::min (512, n - off);
                std::copy (L + off, L + off + m, tmp);
                processStereo (L + off, tmp, m, scL ? scL + off : nullptr, nullptr);
            }
            return;
        }
        processStereo (L, R, n, scL, scR ? scR : scL);
    }

    // ---- read-outs (valid after process) ----
    double grLeft() const { return effL; }
    double grRight() const { return effR; }
    double grPeak() const { return grPk; }           // largest of the two channels in the last block
    double grLeftPeak() const { return blkGrL; }     // largest reduction per channel in the last block
    double grRightPeak() const { return blkGrR; }
    double lastBlockInPeak() const { return inPk; }
    double lastBlockOutPeak() const { return outPk; }
    double inMeanSquare (int c) const { return aIn[c]; }
    double outMeanSquare (int c) const { return aOut[c]; }
    double levelMatchDb() const { return linToDb (lm); }
    double tiltMag (double f) const
    {
        if (! tiltOn) return 1.0;
        double m = g0lin;
        for (int i = 0; i < kSections; ++i) m *= sec[0][i].mag (f, fs);
        return m;
    }
    double sampleRate() const { return fs; }
    const Params& params() const { return p; }

private:
    double smoothGain (double cur, double t, double R) const
    {
        const double kMax = d.fb ? std::max (1.0 / std::min (R, 20.0), 0.05) : 1.0;
        const double k = t > cur ? 1.0 - aAtk : 1.0 - aRel;
        return cur + (t - cur) * std::min (k, kMax);
    }

    double detTilt (int c, double x)
    {
        if (! tiltOn) return x;
        double y = x;
        for (int i = 0; i < kSections; ++i) y = sec[c][i].process (y);
        return y * g0lin;
    }

    void processStereo (float* L, float* R, int n, const float* sL, const float* sR)
    {
        float* ch[2] = { L, R };
        const float* sidec[2] = { sL, sR ? sR : sL };
        const bool useExt = d.ext && sL != nullptr;
        double ip = 0, op = 0, gl = 0, gr = 0;
        const double tInRamp = d.inSw ? 1.0 : 0.0;
        const double tThr = d.thr, tInvR = 1.0 / d.R, tW = d.w, tMix = d.mix;
        const double link2 = d.link * 0.5;
        for (int i = 0; i < n; ++i)
        {
            curMu += (tMu - curMu) * sc;
            thrS += (tThr - thrS) * sc;
            invRS += (tInvR - invRS) * sc;
            wS += (tW - wS) * sc;
            mixS += (tMix - mixS) * sc;
            inRamp += (tInRamp - inRamp) * kIn;
            const double Rs = 1.0 / std::max (invRS, 1.0e-4);
            const double dry[2] = { (double) ch[0][i], (double) ch[1][i] };
            ip = std::max (ip, std::max (std::fabs (dry[0]), std::fabs (dry[1])));
            double lv[2];
            for (int c = 0; c < 2; ++c)
            {
                const double s = d.fb ? yPrev[c] : (useExt ? (double) sidec[c][i] : dry[c]);
                const double x = detTilt (c, s);
                pw[c] += (x * x - pw[c]) * kRms;
                lv[c] = 10.0 * std::log10 (std::max (pw[c], 1.0e-12));
            }
            for (int c = 0; c < 2; ++c)
            {
                const double t = d.fb ? gcompFb (lv[c], thrS, Rs, wS) : gcomp (lv[c], thrS, Rs, wS);
                g[c] = smoothGain (g[c], t, Rs);
            }
            lkS[0] += (g[1] - lkS[0]) * kShape;
            lkS[1] += (g[0] - lkS[1]) * kShape;
            double eff[2];
            for (int c = 0; c < 2; ++c) eff[c] = (g[c] * (1.0 - link2) + lkS[c] * link2) * inRamp;
            gl = std::max (gl, eff[0]); gr = std::max (gr, eff[1]);
            for (int c = 0; c < 2; ++c)
            {
                const double y = dry[c] * dbToLin (-eff[c]);
                double w = y * curMu;
                yPrev[c] = w;
                if (p.levelMatch)
                {
                    eD += (dry[c] * dry[c] - eD) * kLmD;
                    eW += (w * w - eW) * kLmD;
                    if (eD > 1.0e-9 && eW > 1.0e-9)
                    {
                        const double t = std::min (16.0, std::max (0.0625, std::sqrt (eD / eW)));
                        lm += (t - lm) * kLmG;
                    }
                    w *= lm;
                }
                double o;
                if (p.blend == 0) o = w;
                else if (p.blend == 1) o = dry[c] * (1.0 - mixS) + w * mixS;
                else o = dry[c] + mixS * w;
                if (! std::isfinite (o)) { reset(); o = 0.0; yPrev[c] = 0.0; }
                ch[c][i] = flush ((float) o);
                op = std::max (op, std::fabs (o));
                aIn[c] += (dry[c] * dry[c] - aIn[c]) * kAvg;
                aOut[c] += (o * o - aOut[c]) * kAvg;
            }
        }
        effL = lastEff (0); effR = lastEff (1);
        grPk = std::max (gl, gr); blkGrL = gl; blkGrR = gr;
        inPk = ip; outPk = op;
    }

    double lastEff (int c) const
    {
        const double link2 = d.link * 0.5;
        return (g[c] * (1.0 - link2) + lkS[c] * link2) * inRamp;
    }

    Params p;
    Derived d {};
    double fs = 48000.0, sc = 0, kRms = 0, kIn = 0, kLmD = 0, kLmG = 0, kAvg = 0, avgMs = 300, kShape = 1, kShapeTab[3] {};
    double aAtk = 0, aRel = 0, tMu = 1, g0lin = 1;
    double thrS = 0, invRS = 0.5, wS = 6, mixS = 1, curMu = 1, inRamp = 1;
    double pw[2] {}, g[2] {}, lkS[2] {}, yPrev[2] {}, aIn[2] {}, aOut[2] {};
    double eD = 0, eW = 0, lm = 1;
    double effL = 0, effR = 0, grPk = 0, blkGrL = 0, blkGrR = 0, inPk = 0, outPk = 0;
    int tiltApplied = -1;
    bool tiltOn = false;
    Section sec[2][kSections];
};

} // namespace apurr
