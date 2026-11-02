// Furchild DSP core. Plain C++17, no JUCE, no allocation after prepare().
// A stereo variable-mu tube limiting amplifier, our own implementation.
// Behaviour comes from the approved mockup (docs/mockup/Furchild.html) and from public descriptions of
// this kind of unit; anything marked OURS is our own choice, not a measurement of a hardware unit.
//
// Signal path per channel (Left/Right or Lat/Vert):
//   input trim (0 to +24 dB, 1 dB steps) -> gain stage (variable-mu gain reduction) -> tube colour -> DC block
//   -> output (-12 to +12 dB) -> optional Level Match -> soft ceiling -> Mix with the dry signal.
// The detector is a peak follower on the input trim, with an optional side-chain high-pass (60, 120, 240 Hz).
// Gain reduction law (OURS, "variable mu"): the compression ratio rises with level, r(x) = 1 + 19 x / (x + 24)
// with x the level above threshold in dB (about 1:1 at the threshold, 10:1 at 24 dB over, towards 20:1),
// through a 5 dB wide soft knee. Time constants: six pairs of attack and release, two of them
// program dependent (the release stretches the longer the unit has been working hard).
// Anti-aliasing: the two nonlinear stages (tube colour, soft ceiling) run 2x oversampled as a residual path:
// only the nonlinear part goes through the half-band filters, the main signal path stays untouched and flat.
// Latency: 16 samples at 44.1 and 48 kHz (both stages together), 0 at 88.2 kHz and above where it is not needed.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fur {

inline double dbToLin (double db) { return std::exp (db * 0.11512925464970229); }
inline double linToDb (double x)  { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }
inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite (double v) { return std::isfinite (v); }

// ---- Gain reduction law ----------------------------------------------------------------------------
// GR (dB) as a function of the level over threshold (dB). Integral of (1 - 1/r(x)), softplus knee 5 dB.
namespace law
{
constexpr int kN = 361;
constexpr double kLo = -30.0, kStep = 0.25, kHi = 60.0;
inline const std::array<double, kN>& table()
{
    static const std::array<double, kN> t = []
    {
        std::array<double, kN> a {};
        for (int i = 0; i < kN; ++i)
        {
            const double x = kLo + i * kStep, s = 5.0 * std::log (1.0 + std::exp (x / 5.0));
            a[(size_t) i] = std::min (50.0, 0.95 * (s - 1.2 * std::log ((s + 1.2) / 1.2)));
        }
        return a;
    }();
    return t;
}
inline double grDb (double over)
{
    const auto& t = table();
    if (over <= kLo) return 0.0;
    if (over >= kHi) return t[kN - 1];
    const double f = (over - kLo) / kStep;
    const int i = (int) f;
    return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * (f - i);
}
// Local compression ratio at a level over threshold (for the manual and the tests): d(in) / d(out).
inline double ratioAt (double over) { const double x = std::max (0.0, over); return 1.0 + 19.0 * x / (x + 24.0); }
} // namespace law

// ---- Time constants ------------------------------------------------------------------------------------
struct TimeConst { double attMs, relS; int autoKind; };  // autoKind 0 fixed, 5 = TC 5, 6 = TC 6
inline const TimeConst& timeConst (int i)
{
    static const TimeConst tcs[6] = {
        { 0.2, 0.3, 0 }, { 0.2, 0.8, 0 }, { 0.4, 2.0, 0 }, { 0.8, 5.0, 0 }, { 0.4, 2.0, 5 }, { 0.2, 0.3, 6 } };
    return tcs[std::max (0, std::min (5, i))];
}

inline double thresholdDb (double knob) { return -knob * 3.2; } // Threshold knob 0..10 -> 0 .. -32 dB

// ---- Parameters ------------------------------------------------------------------------------------------
struct Params
{
    double inDb[2] { 0, 0 };      // 0 .. 24
    double thKnob[2] { 2, 2 };    // 0 .. 10
    int tc[2] { 2, 2 };           // 0 .. 5 (TC 1 .. 6)
    int mode = 0;                 // 0 Left/Right, 1 Lat/Vert
    bool link = true;
    double outDb = 2.0;           // -12 .. 12
    double mix = 1.0;             // 0 .. 1
    int hp = 0;                   // side-chain high-pass: 0 off, 1 60 Hz, 2 120 Hz, 3 240 Hz
    bool levelMatch = false;
    bool bypass = false;          // crossfades (10 ms) to the dry signal, delayed like the processed path; the unit keeps running
};

constexpr double kTube = 0.04;    // OURS: size of the odd-order tube term, v + 0.04 v |v|
constexpr double kCeiling = 0.9;  // OURS: soft ceiling knee on the processed signal

// ---- Nonlinear stages ----------------------------------------------------------------------------------------
inline double tubeNl (double u) { return kTube * u * std::fabs (u); }   // the tube colour term (odd order)

// Soft ceiling: identity up to kCeiling, then kCeiling + (1 - kCeiling) tanh((|x| - kCeiling) / (1 - kCeiling)).
inline double ceilingNl (double x)   // returns only the excess (the nonlinear part)
{
    const double a = std::fabs (x), w = 1.0 - kCeiling;
    if (a <= kCeiling) return 0.0;
    const double e = kCeiling + w * std::tanh ((a - kCeiling) / w) - a;
    return x < 0 ? -e : e;
}

// 2x oversampled residual of a memoryless nonlinearity. Output = input delayed by kDelay samples plus the nonlinear
// residual computed at twice the sample rate with a linear phase half-band filter pair (17 taps, Kaiser window).
// Only the residual passes through the filters, so the main signal keeps its exact frequency response.
struct ResidualOS
{
    static constexpr int M = 8;                 // filter length 2M+1; delay M samples (M/2 up, M/2 down)
    static constexpr int kDelay = M;
    static const std::array<double, M>& taps()  // odd taps c[2j+1], normalised to sum 0.5 (unity DC gain with the 0.5 centre tap)
    {
        static const std::array<double, M> t = []
        {
            const double beta = 6.0;
            auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
            std::array<double, M> c {}; double sum = 0;
            for (int j = 0; j < M; ++j)
            {
                const int k = 2 * j + 1 - M;                       // offset from the centre
                const double sinc = std::sin (3.14159265358979323846 * k / 2.0) / (3.14159265358979323846 * k / 2.0);
                const double win = i0 (beta * std::sqrt (std::max (0.0, 1.0 - (double) k * k / ((double) M * M)))) / i0 (beta);
                c[(size_t) j] = 0.5 * sinc * win; sum += c[(size_t) j];
            }
            for (auto& v : c) v *= 0.5 / sum;
            return c;
        }();
        return t;
    }
    void reset() { for (auto& v : xh) v = 0; for (auto& v : ro) v = 0; for (auto& v : re) v = 0; }
    // f returns the nonlinear residual N(x), the part of the function that is not the identity
    template <class F> double process (double s, F&& f, bool oversample)
    {
        if (! oversample) return s + f (s);
        const auto& c = taps();
        for (int j = M; j > 0; --j) xh[j] = xh[j - 1];
        xh[0] = s;
        const double even = xh[M / 2];                       // the even phase 2x sample is the input delayed by M/2
        double odd = 0; for (int j = 0; j < M; ++j) odd += c[(size_t) j] * xh[j];
        odd *= 2.0;                                          // interpolated odd phase 2x sample (gain 2 for zero stuffing)
        for (int j = M / 2; j > 0; --j) re[j] = re[j - 1];
        re[0] = f (even);
        double z = 0.5 * re[M / 2];
        for (int j = 0; j < M; ++j) z += c[(size_t) j] * ro[j];
        for (int j = M - 1; j > 0; --j) ro[j] = ro[j - 1];
        ro[0] = f (odd);
        return xh[M] + z;
    }
    double xh[M + 1] {}, ro[M] {}, re[M / 2 + 1] {};
};

// ---- The compressor ------------------------------------------------------------------------------------------
class Compressor
{
public:
    // oversample: 2x for the nonlinear stages (switch off only to measure what it brings)
    void prepare (double sampleRate, bool oversample = true)
    {
        fs = sampleRate > 8000.0 ? sampleRate : 44100.0;
        osOn = oversample && fs < 80000.0;      // above 80 kHz the nonlinear products fall outside the audio band anyway
        latency = osOn ? 2 * ResidualOS::kDelay : 0;
        (void) law::table();
        smooth = 1.0 - std::exp (-1.0 / (0.010 * fs));            // 10 ms parameter smoothing
        dcR = 1.0 - 6.283185307179586 * 10.0 / fs;                // 10 Hz DC blocker
        const double hpf[4] = { 0.0, 60.0, 120.0, 240.0 };
        for (int i = 0; i < 4; ++i) hpA[i] = hpf[i] > 0 ? 1.0 - std::exp (-6.283185307179586 * hpf[i] / fs) : 0.0;
        for (int i = 0; i < 6; ++i)
        {
            const auto& t = timeConst (i);
            attCoef[i] = 1.0 - std::exp (-1.0 / (t.attMs * 0.001 * fs));
            relCoef[i] = t.autoKind ? 0.0 : 1.0 - std::exp (-1.0 / (t.relS * fs));
        }
        susUp = 1.0 - std::exp (-1.0 / (0.8 * fs));
        susDown = 1.0 - std::exp (-1.0 / (3.0 * fs));
        lmEnergy = 1.0 - std::exp (-1.0 / (1.0 * fs));
        lmGlide = 1.0 - std::exp (-1.0 / (2.0 * fs));
        modeStep = 1.0 / (0.004 * fs);
        bypassStep = 1.0 / (0.010 * fs);
        reset();
        snap();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            gr[c] = sus[c] = lp[c] = dcX[c] = dcY[c] = 0.0;
            grNowV[c] = 0.0;
            tubeOs[c].reset(); ceilOs[c].reset();
            for (auto& v : dry[c]) v = 0.0;
        }
        eD = eW = 0.0; lm = 1.0;
        modeG = 1.0; modeNow = p.mode; modeTarget = p.mode; dryPos = 0;
    }

    void setParams (const Params& np)
    {
        Params q = np;
        for (int c = 0; c < 2; ++c)
        {
            q.inDb[c] = finite (q.inDb[c]) ? clampd (q.inDb[c], 0.0, 24.0) : 0.0;
            q.thKnob[c] = finite (q.thKnob[c]) ? clampd (q.thKnob[c], 0.0, 10.0) : 2.0;
            q.tc[c] = std::max (0, std::min (5, q.tc[c]));
        }
        if (q.link)   // linked: channel one's knobs rule both channels, whatever the host sends for channel two
        {
            q.inDb[1] = q.inDb[0]; q.thKnob[1] = q.thKnob[0]; q.tc[1] = q.tc[0];
        }
        q.outDb = finite (q.outDb) ? clampd (q.outDb, -12.0, 12.0) : 0.0;
        q.mix = finite (q.mix) ? clampd (q.mix, 0.0, 1.0) : 1.0;
        q.hp = std::max (0, std::min (3, q.hp));
        q.mode = q.mode ? 1 : 0;
        if (! q.levelMatch && p.levelMatch) { lm = 1.0; eD = eW = 0.0; }
        p = q;
        for (int c = 0; c < 2; ++c) { tIn[c] = dbToLin (p.inDb[c]); tThr[c] = thresholdDb (p.thKnob[c]); }
        tOut = dbToLin (p.outDb);
        tMix = p.mix;
        modeTarget = p.mode;
        bypassTarget = p.bypass ? 1.0 : 0.0;
    }

    int latencySamples() const { return latency; }

    // Process in place. r may be null (mono): the same signal feeds both channels and only channel 0 is written.
    void process (float* l, float* r, int n)
    {
        double ip = 0, op = 0, g0 = 0, g1 = 0, oc0 = 0, oc1 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double raw0 = l[i], raw1 = r ? (double) r[i] : raw0;
            const double d0 = finite (raw0) ? raw0 : 0.0, d1 = finite (raw1) ? raw1 : 0.0;
            ip = std::max (ip, std::max (std::fabs (d0), std::fabs (d1)));
            dry[0][(size_t) dryPos] = d0; dry[1][(size_t) dryPos] = d1;
            const size_t rp = (size_t) ((dryPos - latency) & (kDry - 1));
            const double dd0 = dry[0][rp], dd1 = dry[1][rp];
            dryPos = (dryPos + 1) & (kDry - 1);

            // parameter smoothing (about 10 ms), snapping when close so silence stays exactly silent
            for (int c = 0; c < 2; ++c)
            {
                curIn[c] = follow (curIn[c], tIn[c], 1.0e-7);
                curThr[c] = follow (curThr[c], tThr[c], 1.0e-6);
            }
            curOut = follow (curOut, tOut, 1.0e-7);
            curMix = follow (curMix, tMix, 1.0e-7);

            // Left/Right or Lat/Vert with a short fade when the mode changes
            if (modeNow != modeTarget)
            {
                modeG -= modeStep;
                if (modeG <= 0.0) { modeG = 0.0; modeNow = modeTarget; lp[0] = lp[1] = 0.0; }
            }
            else if (modeG < 1.0) modeG = std::min (1.0, modeG + modeStep);
            double s0 = d0, s1 = d1;
            if (modeNow == 1) { s0 = (d0 + d1) * 0.5; s1 = (d0 - d1) * 0.5; }
            const double x[2] = { s0 * curIn[0], s1 * curIn[1] };

            // detector
            double lvl[2];
            const double a = hpA[p.hp];
            for (int c = 0; c < 2; ++c)
            {
                double d = x[c];
                if (a > 0.0) { lp[c] += a * (d - lp[c]); d -= lp[c]; }
                lvl[c] = linToDb (std::fabs (d));
            }
            if (p.link)
            {
                const double t = law::grDb (std::max (lvl[0], lvl[1]) - curThr[0]);
                stepGr (0, t); stepGr (1, t);
            }
            else
            {
                stepGr (0, law::grDb (lvl[0] - curThr[0]));
                stepGr (1, law::grDb (lvl[1] - curThr[1]));
            }
            g0 = std::max (g0, gr[0]); g1 = std::max (g1, gr[1]);

            // gain stage, tube colour, DC block, output
            double y[2];
            for (int c = 0; c < 2; ++c)
            {
                const double u = x[c] * dbToLin (-gr[c]);
                const double v = tubeOs[c].process (u, tubeNl, osOn);   // the stages are 2x oversampled, delayed by 8 samples each
                const double o = v - dcX[c] + dcR * dcY[c];
                dcX[c] = v; dcY[c] = flush (o);
                y[c] = o * curOut;
                (c ? oc1 : oc0) = std::max (c ? oc1 : oc0, std::fabs (y[c]));
            }
            double w0 = y[0], w1 = y[1];
            if (modeNow == 1) { w0 = y[0] + y[1]; w1 = y[0] - y[1]; }
            w0 *= modeG; w1 *= modeG;

            if (p.levelMatch)   // slow RMS match of the processed signal to the dry signal
            {
                eD += ((d0 * d0 + d1 * d1) * 0.5 - eD) * lmEnergy;
                eW += ((w0 * w0 + w1 * w1) * 0.5 - eW) * lmEnergy;
                eD = flush (eD, 1.0e-30); eW = flush (eW, 1.0e-30);
                if (eD > 1.0e-9 && eW > 1.0e-9)
                {
                    const double t = clampd (std::sqrt (eD / eW), 0.0625, 16.0);
                    lm += (t - lm) * lmGlide;
                }
                w0 *= lm; w1 *= lm;
            }
            // soft ceiling on the processed signal only, so Mix 0 is exactly the dry signal
            w0 = ceilOs[0].process (w0, ceilingNl, osOn);
            w1 = ceilOs[1].process (w1, ceilingNl, osOn);
            double o0 = curMix >= 1.0 ? w0 : dd0 * (1.0 - curMix) + w0 * curMix;
            double o1 = curMix >= 1.0 ? w1 : dd1 * (1.0 - curMix) + w1 * curMix;
            if (! finite (o0) || ! finite (o1)) { reset(); o0 = o1 = 0.0; }
            if (bypassG != bypassTarget)
                bypassG = bypassG < bypassTarget ? std::min (bypassTarget, bypassG + bypassStep) : std::max (bypassTarget, bypassG - bypassStep);
            if (bypassG >= 1.0) { o0 = dd0; o1 = dd1; }
            else if (bypassG > 0.0) { o0 += (dd0 - o0) * bypassG; o1 += (dd1 - o1) * bypassG; }
            op = std::max (op, std::max (std::fabs (o0), std::fabs (o1)));
            l[i] = (float) clampd (o0, -1.0, 1.0);        // safety net only: the oversampled ceiling can ring on a huge step
            if (r) r[i] = (float) clampd (o1, -1.0, 1.0);
        }
        grNowV[0] = gr[0]; grNowV[1] = gr[1];
        mInPk = std::max (mInPk, ip); mOutPk = std::max (mOutPk, op);
        mGrPk[0] = std::max (mGrPk[0], g0); mGrPk[1] = std::max (mGrPk[1], g1);
        mOcPk[0] = std::max (mOcPk[0], oc0); mOcPk[1] = std::max (mOcPk[1], oc1);
    }

    // Meters. Peaks are collected since the last call of takePeaks().
    double grNow (int c) const { return grNowV[c]; }
    double levelMatchGain() const { return lm; }
    struct Peaks { double in, out, gr[2], oc[2]; };
    Peaks takePeaks()
    {
        Peaks k { mInPk, mOutPk, { mGrPk[0], mGrPk[1] }, { mOcPk[0], mOcPk[1] } };
        mInPk = mOutPk = 0; mGrPk[0] = mGrPk[1] = 0; mOcPk[0] = mOcPk[1] = 0;
        return k;
    }
    const Params& params() const { return p; }

private:
    static double flush (double v, double eps = 1.0e-18) { return std::fabs (v) < eps ? 0.0 : v; }
    double follow (double cur, double target, double snapEps) const
    {
        const double d = target - cur;
        return std::fabs (d) < snapEps ? target : cur + d * smooth;
    }
    void snap()
    {
        for (int c = 0; c < 2; ++c) { curIn[c] = tIn[c]; curThr[c] = tThr[c]; }
        curOut = tOut; curMix = tMix;
    }
    double relTime (int c) const
    {
        const auto& t = timeConst (p.link ? p.tc[0] : p.tc[c]);
        if (! t.autoKind) return t.relS;
        const double s = sus[c];
        if (t.autoKind == 5) return 2.0 + 8.0 * s;
        return s < 0.5 ? 0.3 + 9.7 * (s / 0.5) : 10.0 + 15.0 * ((s - 0.5) / 0.5);
    }
    void stepGr (int c, double target)
    {
        const int ti = p.link ? p.tc[0] : p.tc[c];
        const double cur = gr[c];
        double v;
        if (target > cur) v = cur + (target - cur) * attCoef[ti];
        else
        {
            const double k = timeConst (ti).autoKind ? 1.0 - std::exp (-1.0 / (relTime (c) * fs)) : relCoef[ti];
            v = cur + (target - cur) * k;
        }
        const bool up = cur > 2.0;
        if (up) sus[c] += (1.0 - sus[c]) * susUp;
        else if (cur < 0.5) sus[c] += (0.0 - sus[c]) * susDown;
        sus[c] = flush (sus[c], 1.0e-12);
        gr[c] = flush (v, 1.0e-12);
    }

    double fs = 48000.0, smooth = 0.002, dcR = 0.999, hpA[4] {}, attCoef[6] {}, relCoef[6] {};
    double susUp = 0, susDown = 0, lmEnergy = 0, lmGlide = 0, modeStep = 0.01;
    Params p;
    double tIn[2] { 1, 1 }, tThr[2] { -6.4, -6.4 }, tOut = 1.26, tMix = 1.0;
    double curIn[2] { 1, 1 }, curThr[2] { -6.4, -6.4 }, curOut = 1.26, curMix = 1.0;
    double gr[2] {}, sus[2] {}, lp[2] {}, dcX[2] {}, dcY[2] {}, grNowV[2] {};
    double eD = 0, eW = 0, lm = 1.0, modeG = 1.0, bypassG = 0.0, bypassTarget = 0.0, bypassStep = 0.01;
    static constexpr int kDry = 64;
    double dry[2][kDry] {};
    int dryPos = 0, latency = 0;
    bool osOn = true;
    ResidualOS tubeOs[2], ceilOs[2];
    int modeNow = 0, modeTarget = 0;
    double mInPk = 0, mOutPk = 0, mGrPk[2] {}, mOcPk[2] {};
};

} // namespace fur
