// Pure C++17 ports (no JUCE) of the DSP of Studio 87 plugins that were written on top of JUCE's dsp module:
// Meowbass, Fat Cat and Cat Crusher. The algorithms, constants and control mappings are those of the plugins
// (github.com/SpeakingColors/meowbass, FatCat, catcrusher-vst); only the JUCE helpers were replaced:
//   juce::dsp::Oversampling       -> the zero latency polyphase half-band filters of Hairball (hb::Oversampler4, Os2 below)
//   juce::dsp::StateVariableTPT   -> hb::Svf, juce LinkwitzRiley -> mx::LR, SmoothedValue -> Sm (one pole, 15 to 20 ms)
// Because the replacement oversamplers add no delay, these units report zero latency and the dry path is not delayed.
// No allocation after construction, no locks.
#pragma once
#include "HairballCore.h"
#include "MxCore.h"
#include <algorithm>
#include <cmath>

namespace rp
{
inline float dbg (float db) { return std::pow (10.0f, db * 0.05f); }
inline float lim (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// one pole smoother, time constant in ms
struct Sm
{
    float v = 0.f, t = 0.f, a = 1.f;
    void setup (double fs, double ms) { a = (float) (1.0 - std::exp (-1.0 / (0.001 * ms * fs))); }
    void snap (float x) { v = t = x; }
    void set (float x) { t = x; }
    float next() { v += (t - v) * a; return v; }
};

// 2x oversampler from the same half-band pair as hb::Oversampler4 (7 coefficients, no latency)
struct Os2
{
    hb::HbHalf<7> up, dn;
    Os2() { static const double c[7] = { 0.031727, 0.120683, 0.251108, 0.404553, 0.566765, 0.732056, 0.905389 }; up.set (c); dn.set (c); }
    void reset() { up.reset(); dn.reset(); }
    template <class F> inline float run (float x, F&& fn)
    {
        double e, o; up.up ((double) x, e, o);
        const double a = fn ((float) e), b = fn ((float) o);
        return (float) dn.down (a, b);
    }
};

// ============================================== Meowbass ==============================================
struct BassParams { float freq = 80.f, harm = -12.f, dyn = -40.f, orig = 0.f, in = 0.f, out = 0.f, resp = 40.f, odd = 0.5f, hdrive = 30.f; int hfilt = 0; bool hq = true; };

class MeowBassCore
{
public:
    void prepare (double sr)
    {
        fs = sr; for (auto* s : { &gIn, &gOrig, &gHarm, &gOut }) s->setup (sr, 20.0); sDyn.setup (sr, 20.0); sDrive.setup (sr, 10.0);
        reset (); first = true;
    }
    void reset() { for (auto& s : split) s.reset(); hpE.reset(); lpE.reset(); hpO.reset(); lpO.reset(); env = 0.f; dcState = 0.f; mPrev = 0.f; lastHq = -1; }
    void process (float* l, float* r, int n, const BassParams& p)
    {
        float* io[2] = { l, r };
        if (first) { gIn.snap (dbg (p.in)); gOut.snap (dbg (p.out)); gOrig.snap (dbg (p.orig)); gHarm.snap (dbg (p.harm)); sDyn.snap (p.dyn); sDrive.snap (p.hdrive); first = false; }
        gIn.set (dbg (p.in)); gOut.set (dbg (p.out)); gOrig.set (dbg (p.orig)); gHarm.set (dbg (p.harm)); sDyn.set (p.dyn); sDrive.set (p.hdrive);
        const float f0 = p.freq, resp = p.resp, odd = p.odd; const int mode = p.hfilt;
        const float lpHz = mode == 0 ? 4.f * f0 : mode == 1 ? 8.f * f0 : 3.f * f0;
        const float hpOHz = mode == 2 ? 2.f * f0 : 1.4f * f0, hpEHz = (mode == 2 ? 1.0f : mode == 1 ? 0.7f : 0.8f) * f0;
        for (auto& s : split) s.set (fs, f0);
        const bool hq = p.hq; const double fsH = hq ? 2.0 * fs : fs;
        hpO.set (true, fsH, hpOHz); hpE.set (true, fsH, hpEHz); lpO.set (false, fsH, lpHz); lpE.set (false, fsH, lpHz);
        if ((int) hq != lastHq) { hpE.reset(); lpE.reset(); hpO.reset(); lpO.reset(); lastHq = (int) hq; }
        const float aA = std::exp (-1.f / (0.001f * std::max (0.2f, resp * 0.04f) * (float) fs)), aR = std::exp (-1.f / (0.001f * resp * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            const float inG = gIn.next(), origG = gOrig.next(), harmG = gHarm.next(), outG = gOut.next(), dyn = sDyn.next(), gD = 1.f + sDrive.next() * 0.01f * 8.f;
            float lo[2] {}, hi[2] {};
            for (int c = 0; c < 2; ++c) split[c].split (io[c][i] * inG, lo[c], hi[c]);
            const float m = 0.5f * (lo[0] + lo[1]);
            const float a = std::fabs (m) > env ? aA : aR; env = a * env + (1.f - a) * std::fabs (m);
            const float dn = lim (dyn, -100.f, 100.f) * 0.01f, ex = dn >= 0.f ? dn * 3.f : dn * 1.5f;
            const float lg = std::log (std::max (env, 0.003f) / (ex > 0.f ? 0.3f : 0.25f));
            const float pre = lim (std::exp (ex * lg), 0.02f, 6.f), post = lim (std::exp (ex * (ex < 0.f ? 1.f : 0.5f) * lg), 0.f, ex > 0.f ? 4.f : 5.f);
            const float dcK = hq ? 0.001f : 0.002f;
            auto gen = [&] (float mm)
            {
                float ev = std::fabs (mm * gD * pre); dcState += dcK * (ev - dcState); ev = std::tanh ((ev - dcState) * 1.2f);
                const float od = std::tanh (4.f * gD * mm * pre) * 0.5f;
                return (1.f - odd) * lpE.tick (hpE.tick (ev * 2.f * post * 0.375f)) + odd * lpO.tick (hpO.tick (od * post * 1.5f));
            };
            float h;
            if (hq) { gen (0.5f * (mPrev + m)); h = gen (m); } else h = gen (m);
            mPrev = m;
            h *= harmG * 4.f * 0.5f;
            for (int c = 0; c < 2; ++c) io[c][i] = (hi[c] + lo[c] * origG + h) * outG;
        }
    }
private:
    struct Bq
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
        void set (bool high, double fs, float f)
        {
            const double w = 2.0 * 3.14159265358979323846 * lim (f, 5.f, (float) (0.45 * fs)) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071), a0 = 1.0 + al;
            const double nb0 = high ? (1.0 + c) * 0.5 : (1.0 - c) * 0.5, nb1 = high ? -(1.0 + c) : (1.0 - c);
            b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb0 / a0); a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
        }
        void reset() { z1 = z2 = 0.f; }
        float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    double fs = 48000.0; mx::LR split[2]; Bq hpE, lpE, hpO, lpO; float env = 0.f, dcState = 0.f, mPrev = 0.f; int lastHq = -1; bool first = true;
    Sm gIn, gOrig, gHarm, gOut, sDyn, sDrive;
};

// ================================================= Fat Cat =================================================
namespace fc
{
struct SatShape
{
    float g = 1.f, p = 1.6f, norm = 1.f;
    SatShape() = default;
    explicit SatShape (float drive, float boost = 1.f)
    {
        const float t = lim ((drive - 0.75f) / 0.25f, 0.f, 1.f), sm = t * t * (3.f - 2.f * t);
        g = 1.f + 22.f * drive * boost; p = 1.6f + 4.4f * sm;
        norm = std::pow (1.f + std::pow (g, p), 1.f / p) / g;
    }
    float operator() (float x) const { const float u = x * g; return u / std::pow (1.f + std::pow (std::fabs (u), p), 1.f / p) * norm; }
};
inline float purrMakeup (float drive, float xs, float fuzz, float ref)
{
    float a = 0.f, b = 0.f;
    for (int i = 0; i < 64; ++i) { const float x = ref * std::sin (6.2831853f * (float) i / 64.f), y = mx::purrShaper (x, drive, xs, fuzz); a += x * x; b += y * y; }
    return lim (b > 1.0e-9f ? std::sqrt (a / b) : 1.f, 0.25f, 4.f);
}
struct FatShaper
{
    bool pur = false, stage2 = false; SatShape base; float d = 0.f, x = 0.f, z = 0.f, x1 = 0.f, z1 = 0.f, mk = 1.f;
    void set (float drive, float xs, float gz, bool purrOne)
    {
        pur = purrOne; d = drive; x = xs; z = gz;
        if (pur) { mk = purrMakeup (d, x, z, 0.7f); stage2 = false; }
        else
        {
            base = SatShape (drive, 1.4f); x1 = std::min (2.f, xs * 1.4f); z1 = std::min (2.f, gz * 1.4f);
            stage2 = xs > 0.f || gz > 0.f; mk = stage2 ? purrMakeup (0.f, x1, z1, 0.95f) : 1.f;
        }
    }
    float operator() (float v) const
    {
        if (pur) return mx::purrShaper (v, d, x, z) * mk;
        const float b = base (v);
        return stage2 ? mx::purrShaper (b, 0.f, x1, z1) * mk : b;
    }
    float nast() const { return pur ? std::max ({ mx::sstep ((d - 0.8f) * 5.f), mx::sstep ((x - 0.8f) * 5.f), mx::sstep ((z - 0.8f) * 5.f) }) : 0.f; }
};
inline float softClipHidden (float v)
{
    constexpr float k = 0.7f, c = 0.97f; const float m = std::fabs (v);
    return m <= k ? v : std::copysign (k + (c - k) * std::tanh ((m - k) / (c - k)), v);
}
inline float colourFreq (float color) { return 150.f * std::pow (40.f, color * 0.01f); }
} // namespace fc

struct FatParams { float drive = 40.f, color = 0.f, out = 0.f, xs = 0.f, grit = 0.f, fuzz = 0.f, turbo = 0.f, mix = 100.f; bool purr = false; };

class FatCatCore
{
public:
    static constexpr int kChunk = 32, kTurbo = 4096;
    void prepare (double sr)
    {
        fs = sr; for (auto* s : { &sDrive, &sXs, &sGrit, &sFuzz, &sColor, &sTurbo, &sMix, &sOut }) s->setup (sr, 20.0);
        reset (); first = true;
        for (auto& f : fuzzHp) f.set (sr, 1500.0, 0.7071);
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { os[c].reset(); osFz[c].reset(); osClip[c].reset(); band[c].reset(); fuzzHp[c].reset(); nastLp[c].reset(); dc[c] = 0.f; }
        std::fill (std::begin (turboRing), std::end (turboRing), 0.f); turboPos = 0; env = 0.f;
    }
    void process (float* l, float* r, int n, const FatParams& p)
    {
        float* io[2] = { l, r };
        if (first) { sDrive.snap (p.drive); sXs.snap (p.xs); sGrit.snap (p.grit); sFuzz.snap (p.fuzz); sColor.snap (p.color); sTurbo.snap (p.turbo); sMix.snap (p.mix); sOut.snap (p.out); first = false; }
        sDrive.set (p.drive); sXs.set (p.xs); sGrit.set (p.grit); sFuzz.set (p.fuzz); sColor.set (p.color); sTurbo.set (p.turbo); sMix.set (p.mix); sOut.set (p.out);
        const float aA = std::exp (-1.f / (0.001f * (float) fs)), aR = std::exp (-1.f / (0.05f * (float) fs));
        for (int s0 = 0; s0 < n; s0 += kChunk)
        {
            const int len = std::min (kChunk, n - s0);
            float drive = 0, xs = 0, gz = 0, fuzz = 0, color = 0;
            for (int i = 0; i < len; ++i) { drive = sDrive.next(); xs = sXs.next(); gz = sGrit.next(); fuzz = sFuzz.next(); color = sColor.next(); }
            drive *= 0.01f; xs *= 0.01f; gz *= 0.01f; fuzz *= 0.01f;
            shaper.set (drive, xs, gz, p.purr);
            const float colourK = color * 0.01f * 1.6f, nast = shaper.nast();
            for (int c = 0; c < 2; ++c)
            {
                band[c].set (fs, fc::colourFreq (color), 1.4);
                if (nast > 0.f) nastLp[c].set (fs, std::max (3000.f, 14000.f - 10000.f * nast), 0.7071); else nastLp[c].reset();
            }
            for (int i = 0; i < len; ++i)
            {
                const int idx = s0 + i; float dry[2], y[2], pk = 0.f;
                for (int c = 0; c < 2; ++c)
                {
                    dry[c] = io[c][idx]; band[c].tick (dry[c]);
                    y[c] = dry[c] + colourK * band[c].bp; pk = std::max (pk, std::fabs (y[c]));
                }
                const float a = pk > env ? aA : aR; env = a * env + (1.f - a) * pk;
                const float inv = 1.f / (env + 1.0e-5f);
                const float mix = sMix.next() * 0.01f, outG = dbg (sOut.next()), dl = std::min ((float) (kTurbo - 2), sTurbo.next() * 0.01f * 0.012f * (float) fs);
                float wet[2];
                for (int c = 0; c < 2; ++c)
                {
                    const float xn = y[c] * inv;
                    float w = os[c].run (xn, [&] (float v) { return shaper (lim (v, -8.f, 8.f)); });
                    if (nast > 0.f) { nastLp[c].tick (w); w = nastLp[c].lp; }
                    w *= env; dc[c] += 0.0005f * (w - dc[c]); w -= dc[c];
                    if (fuzz > 0.f)
                    {
                        fuzzHp[c].tick (w);
                        const float arg = fuzzHp[c].hp / (env + 1.0e-5f) * (4.f + 24.f * fuzz);
                        const float t = osFz[c].run (arg, [] (float v) { return std::tanh (v); });
                        w += fuzz * 0.5f * t * env;
                    }
                    wet[c] = (w * mix + dry[c] * (1.f - mix)) * outG;
                }
                turboRing[turboPos] = wet[1];
                const float rp = (float) turboPos - dl, rf = rp < 0.f ? rp + (float) kTurbo : rp; const int i0 = (int) rf, i1 = (i0 + 1) % kTurbo; const float fr = rf - (float) i0;
                wet[1] = turboRing[i0] * (1.f - fr) + turboRing[i1] * fr; turboPos = (turboPos + 1) % kTurbo;
                for (int c = 0; c < 2; ++c) io[c][idx] = osClip[c].run (wet[c], [] (float v) { return fc::softClipHidden (v); });
            }
        }
    }
private:
    double fs = 48000.0; bool first = true; float env = 0.f, dc[2] {}; int turboPos = 0; float turboRing[kTurbo] {};
    hb::Oversampler4 os[2], osFz[2]; Os2 osClip[2]; hb::Svf band[2], fuzzHp[2], nastLp[2]; fc::FatShaper shaper;
    Sm sDrive, sXs, sGrit, sFuzz, sColor, sTurbo, sMix, sOut;
};

// ============================================== Cat Crusher ==============================================
namespace cc
{
inline float distort (float x, float tube, float mech, int type = 0, bool smooth = true)
{
    const float tg = 1.f + tube * 0.01f * 24.f, u = x * tg, au = std::fabs (u);
    const float e = 0.04f * std::max (1.f, tg * 0.25f);
    const float sgn = smooth ? u / std::sqrt (u * u + e * e) : (u >= 0.f ? 1.f : -1.f);
    float y;
    switch (type)
    {
        case 1:  y = std::tanh (u); break;
        case 2:  { const float a = u / (1.f + au), b = u / (1.f + 0.55f * au) * 1.25f; y = smooth ? 0.5f * ((1.f + sgn) * a + (1.f - sgn) * b) : (u >= 0.f ? a : b); y = lim (y, -1.2f, 1.f); break; }
        case 3:  y = u / std::pow (1.f + std::pow (au, 1.6f), 1.f / 1.6f); break;
        case 4:  { const float b = -0.8f * (1.f - std::exp (0.5f * std::min (u, 0.f))), pa = 1.f - std::exp (-std::max (u, 0.f)); y = smooth ? 0.5f * ((1.f + sgn) * pa + (1.f - sgn) * b) : (u >= 0.f ? 1.f - std::exp (-u) : b); break; }
        case 5:  y = u / std::pow (1.f + std::pow (au, 6.f), 1.f / 6.f); break;
        case 6:  { const float a = std::tanh (2.f * u + 0.35f) - std::tanh (0.35f); y = a / std::pow (1.f + std::pow (std::fabs (1.6f * a), 4.f), 0.25f); break; }
        case 7:  { const float w = u / std::sqrt (1.f + (u / 3.f) * (u / 3.f)); y = std::asinh (2.5f * w) * 0.37f; break; }
        case 8:  y = std::asinh (1.5f * u) * 0.55f; break;
        case 9:  { const float sa = smooth ? std::sqrt (u * u + e * e) - e : au; y = 0.65f * std::tanh (u) + 0.35f * std::tanh (sa); break; }
        default: y = std::tanh (u + 0.12f * tube * 0.01f) - std::tanh (0.12f * tube * 0.01f); break;
    }
    y /= (1.f + 0.18f * (tg - 1.f)) * 0.9f + 0.1f;
    if (mech > 0.f)
    {
        const float m = mech * 0.01f, mg = 1.f + m * 14.f;
        const float z = std::sin (lim (y * mg * 0.9f, -1.6f, 1.6f));
        y = y + (z / (0.6f + 0.4f * mg * 0.2f) - y) * lim (m * 1.6f, 0.f, 1.f);
    }
    return y;
}
} // namespace cc

struct CrushParams { float tube = 30.f, comp = 30.f, vol = 0.f, mech = 0.f, cut = 20000.f, res = 10.f, mix = 100.f; int type = 0, ftype = 0; bool phat = false, don = true, fon = true, con = true; };

class CatCrusherCore
{
public:
    void prepare (double sr)
    {
        fs = sr; for (auto* s : { &smTube, &smMech, &smCut, &smRes, &smAmt, &smVol, &smMix }) s->setup (sr, 15.0);
        reset (); first = true;
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { os[c].reset(); osClip[c].reset(); fA[c].reset(); fB[c].reset(); shelf[c].reset(); dc[c] = 0.f; }
        pIn = pOut = kIn = kOut = 0.f; mkGain = 1.f; env = 0.f; grMeter = 0.f;
    }
    float grDb() const { return grMeter; }
    void process (float* l, float* r, int n, const CrushParams& p)
    {
        float* io[2] = { l, r };
        if (first) { smTube.snap (p.tube); smMech.snap (p.mech); smCut.snap (p.cut); smRes.snap (p.res); smAmt.snap (p.comp / 100.f); smVol.snap (dbg (p.vol)); smMix.snap (p.mix / 100.f); first = false; }
        smTube.set (p.tube); smMech.set (p.mech); smCut.set (p.cut); smRes.set (p.res); smAmt.set (p.comp / 100.f); smVol.set (dbg (p.vol)); smMix.set (p.mix / 100.f);
        for (int c = 0; c < 2; ++c) shelf[c].lowShelf (fs, 90.0, p.phat ? 6.0206 : 0.0);
        const float aE = std::exp (-1.f / (0.4f * (float) fs)), aK = std::exp (-1.f / (0.25f * (float) fs));
        const float gA = std::exp (-1.f / (0.05f * (float) fs)), gR = std::exp (-1.f / (0.3f * (float) fs));
        const float aA = std::exp (-1.f / (0.003f * (float) fs)), aR = std::exp (-1.f / ((p.phat ? 0.22f : 0.12f) * (float) fs));
        float grMax = 0.f; float fc = 20000.f, q = 0.7f;
        for (int i = 0; i < n; ++i)
        {
            const float tube = smTube.next(), mech = smMech.next(), cut = smCut.next(), res = smRes.next(), amount = smAmt.next(), mix = smMix.next(), outG = smVol.next();
            float dry[2] = { io[0][i], io[1][i] }, x[2] = { dry[0], dry[1] };
            if (p.don)
                for (int c = 0; c < 2; ++c)
                {
                    const float y = os[c].run (x[c], [&] (float v) { return cc::distort (v, tube, mech, p.type, true); });
                    dc[c] += 0.0005f * (y - dc[c]); x[c] = y - dc[c];
                }
            if (p.fon)
            {
                if ((i & 15) == 0)
                {
                    fc = lim (cut, 20.f, (float) fs * 0.45f); q = p.ftype == 2 ? 0.7f + res * 0.03f : 0.55f + res * 0.01f * 7.f;
                    for (int c = 0; c < 2; ++c) { fA[c].set (fs, fc, q); fB[c].set (fs, fc, p.ftype == 2 ? q : 0.707); }
                }
                for (int c = 0; c < 2; ++c)
                {
                    fA[c].tick (x[c]); const float a1 = p.ftype == 1 ? fA[c].hp : p.ftype == 2 ? fA[c].bp : fA[c].lp;
                    fB[c].tick (a1); float b1 = p.ftype == 1 ? fB[c].hp : p.ftype == 2 ? fB[c].bp : fB[c].lp;
                    if (p.ftype == 2) b1 *= 1.f / (q * q);
                    x[c] = b1;
                }
            }
            if (p.con)
            {
                const float thr = -amount * 30.f, ratio = 1.f + amount * 7.f;
                float pk = 0.f, inPw = 0.f, inPk = 0.f;
                for (int c = 0; c < 2; ++c)
                {
                    const float raw = x[c]; inPw += raw * raw; inPk = std::max (inPk, std::fabs (raw));
                    if (p.phat) x[c] = shelf[c].tick (x[c]);
                    pk = std::max (pk, std::fabs (x[c]));
                }
                inPw *= 0.5f;
                const float a = pk > env ? aA : aR; env = a * env + (1.f - a) * pk;
                const float db = mx::gainToDb (env, -100.f);
                const float grDb_ = db > thr ? (db - thr) * (1.f - 1.f / ratio) : 0.f; grMax = std::max (grMax, grDb_);
                const float g = dbg (-grDb_);
                float outPw = 0.f, outPk = 0.f;
                for (int c = 0; c < 2; ++c) { x[c] *= g; outPw += x[c] * x[c]; outPk = std::max (outPk, std::fabs (x[c])); }
                outPw *= 0.5f;
                pIn = aE * pIn + (1.f - aE) * inPw; pOut = aE * pOut + (1.f - aE) * outPw;
                kIn = std::max (inPk, kIn * aK); kOut = std::max (outPk, kOut * aK);
                const float tg = (pOut > 1e-10f && pIn > 1e-10f) ? lim (std::min (std::sqrt (pIn / pOut), kOut > 1e-6f ? 4.f * kIn / kOut : 1.f), 1.f, 12.f) : 1.f;
                const float kk = tg > mkGain ? gA : gR; mkGain = kk * mkGain + (1.f - kk) * tg;
                for (int c = 0; c < 2; ++c) { x[c] *= mkGain; if (p.phat) x[c] = std::tanh (x[c] * 0.9f) / 0.9f * 0.95f; }
            }
            for (int c = 0; c < 2; ++c)
            {
                const float o = (x[c] * mix + dry[c] * (1.f - mix)) * outG;
                io[c][i] = osClip[c].run (o, [] (float v) { const float k = 0.7f, cl = 0.97f, m = std::fabs (v); return m > k ? std::copysign (k + (cl - k) * std::tanh ((m - k) / (cl - k)), v) : v; });
            }
        }
        grMeter = p.con ? grMax : 0.f;
    }
private:
    double fs = 48000.0; bool first = true; float env = 0.f, dc[2] {}, grMeter = 0.f, pIn = 0.f, pOut = 0.f, kIn = 0.f, kOut = 0.f, mkGain = 1.f;
    hb::Oversampler4 os[2]; Os2 osClip[2]; hb::Svf fA[2], fB[2]; hb::Biquad shelf[2];
    Sm smTube, smMech, smCut, smRes, smAmt, smVol, smMix;
};
} // namespace rp
