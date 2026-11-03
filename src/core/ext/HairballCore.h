// Hairball core: a freeze-and-transform grain engine. Pure C++17, no JUCE, no allocation while processing.
// Two ways to use it:
//   Instrument: a loaded sample, played by MIDI, with the Granulizer-style grain controls.
//   FX: the audio that passes through is kept in a ring buffer; Freeze stops recording and the grains loop what was
//       captured, or Lag keeps them reading just behind the live audio. captureToSample() turns the buffer into a sample.
// This is not a granular synthesiser: it is a destroyer and transformer for audio that already exists.
#pragma once
#include "HairballParams.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hb
{
constexpr float kPi = 3.14159265358979f;
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

struct Rng
{
    uint32_t s = 2463534242u;
    float next01() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); }
    float bi() { return next01() * 2.0f - 1.0f; }
};

struct Params
{
    float v[pCount];
    Params() { setDefaults(); }
    void setDefaults() { for (int i = 0; i < pCount; ++i) v[i] = kParams[i].def; }
    float operator[] (int i) const { return v[i]; }
    float& operator[] (int i) { return v[i]; }
    void set (const char* id, float x) { const int i = indexOf (id); if (i >= 0) v[i] = clampf (x, kParams[i].lo, kParams[i].hi); }
};

// ---------- small DSP blocks ----------
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void norm (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    void peak (double sr, double f, double gDb, double q)
    {
        const double A = std::pow (10.0, gDb / 40.0), w = 2 * kPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, double f, double gDb)
    {
        const double A = std::pow (10.0, gDb / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), t = 2 * std::sqrt (A) * al;
        norm (A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
              (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    void highShelf (double sr, double f, double gDb)
    {
        const double A = std::pow (10.0, gDb / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), t = 2 * std::sqrt (A) * al;
        norm (A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
              (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }
    float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

struct Svf
{
    float g = 0.1f, k = 1.f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0, lp = 0, bp = 0, hp = 0;
    void set (double sr, double f, double q)
    { f = std::min (f, sr * 0.45); g = (float) std::tan (kPi * f / sr); k = (float) (1.0 / q); a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void tick (float x)
    { const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; bp = v1; lp = v2; hp = x - k * v1 - v2; }
    void reset() { ic1 = ic2 = 0; }
};

// PurrOne colour stages: Drive (symmetric clip), Transistor (asymmetric, even harmonics), Fuzz (lopsided, nasty above 80 percent).
struct Colour
{
    static float smoothstep01 (float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }
    static float asymTanh (float u, float nl) { return u >= 0 ? std::tanh (u) : nl * std::tanh (u / nl); }
    static float shaper (float x, float drive, float xs, float fuzz)
    {
        const float dn = smoothstep01 ((drive - 0.8f) * 5.0f), xn = smoothstep01 ((xs - 0.8f) * 5.0f), fn = smoothstep01 ((fuzz - 0.8f) * 5.0f);
        float y = x;
        if (drive > 0.001f)
        {
            y = std::tanh (x * (1.0f + drive * 5.0f + dn * 30.0f));
            if (dn > 0.0f) y += dn * (std::tanh (y * (2.0f + 10.0f * dn)) - y);
        }
        if (xs > 0.001f)
        {
            const float nl = std::max (0.2f, 1.0f - 0.3f * xs - 0.25f * xn);
            const float u = y * (1.0f + xs * 5.0f + xn * 30.0f);
            y = asymTanh (u, nl);
            y += (0.3f * xs + 0.25f * xn) * y * y;
            if (xn > 0.0f) { const float y2 = asymTanh (y * (2.0f + 10.0f * xn) + 0.15f * xn, nl); y += xn * (y2 - y); }
        }
        if (fuzz > 0.001f)
        {
            const float fz = 1.0f + fuzz * 14.0f + fn * 70.0f, bf = 0.5f * fuzz;
            y = std::tanh (y * fz + bf) - std::tanh (bf);
            if (fn > 0.0f)
            {
                const float r = std::sqrt (y * y + 0.02f) - 0.1414f;
                y += fn * 0.7f * (r - 0.3f);
                y = std::tanh (y * (1.0f + 0.8f * fn));
            }
        }
        return y;
    }
    // Gain that keeps the level about the same as the clean path, so colour adds grit and not volume.
    static float makeup (float drive, float xs, float fuzz, float ref = 0.35f)
    {
        double a = 0, b = 0;
        for (int i = 0; i < 64; ++i)
        {
            const float x = ref * std::sin (6.2831853f * (float) i / 64.0f);
            const float y1 = shaper (x, drive, xs, fuzz);
            a += (double) x * x; b += (double) y1 * y1;
        }
        const float m = b > 1e-9 ? (float) std::sqrt (a / b) : 1.0f;
        return clampf (m, 0.25f, 4.0f);
    }
};

// 4x oversampling for the Colour stages: two cascaded polyphase allpass half-band filters (no latency, no allocation).
// Stage 1 (1x to 2x): 7 coefficients, about 100 dB image rejection, flat to 20 kHz at 48 kHz. Stage 2 (2x to 4x): 3 coefficients, about 94 dB.
// The coefficients were fitted for the polyphase structure H(z) = 0.5 (A0(z^2) + z^-1 A1(z^2)); the test checks gain, flatness and alias level.
struct HbAllpass
{
    double a = 0, x1 = 0, y1 = 0;
    inline double tick (double x) { const double y = a * (x - y1) + x1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};
template <int N>
struct HbHalf
{
    HbAllpass ap[N]; double prevOdd = 0;
    void set (const double* c) { for (int i = 0; i < N; ++i) ap[i].a = c[i]; }
    void reset() { for (auto& f : ap) f.reset(); prevOdd = 0; }
    // one low-rate sample in, two high-rate samples out (even, odd)
    inline void up (double x, double& e, double& o)
    { double a = x, b = x; for (int i = 0; i < N; ++i) { if ((i & 1) == 0) a = ap[i].tick (a); else b = ap[i].tick (b); } e = a; o = b; }
    // two high-rate samples in (even, odd), one low-rate sample out
    inline double down (double xe, double xo)
    { double a = xe, b = prevOdd; for (int i = 0; i < N; ++i) { if ((i & 1) == 0) a = ap[i].tick (a); else b = ap[i].tick (b); } prevOdd = xo; return 0.5 * (a + b); }
};
struct Oversampler4
{
    HbHalf<7> up1, dn1; HbHalf<3> up2, dn2;
    Oversampler4()
    {
        static const double c1[7] = { 0.031727, 0.120683, 0.251108, 0.404553, 0.566765, 0.732056, 0.905389 };
        static const double c2[3] = { 0.062742, 0.2638, 0.665913 };
        up1.set (c1); dn1.set (c1); up2.set (c2); dn2.set (c2);
    }
    void reset() { up1.reset(); dn1.reset(); up2.reset(); dn2.reset(); }
    // Runs fn (float to float) at 4x the sample rate. fn must be memoryless or keep its own state for the 4x rate.
    template <class F> inline float run (float x, F&& fn)
    {
        double e, o, p0, p1, p2, p3;
        up1.up ((double) x, e, o);                       // 2x samples e, o
        up2.up (e, p0, p1); up2.up (o, p2, p3);           // 4x samples p0..p3, in time order through one filter
        p0 = fn ((float) p0); p1 = fn ((float) p1); p2 = fn ((float) p2); p3 = fn ((float) p3);
        const double h0 = dn2.down (p0, p1), h1 = dn2.down (p2, p3);
        return (float) dn1.down (h0, h1);
    }
};

// ---------- the engine ----------
class Engine
{
public:
    static constexpr int kVoices = 8, kGrains = 24, kChunk = 32, kMaxRingSeconds = 8;

    struct Buf { std::vector<float> l, r; size_t n = 0; double ratio = 1.0; };
    // A sample prepared away from the audio thread (plugin): swapSample() takes it over without allocating.
    struct Sample : Buf { std::vector<size_t> detected; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ringCap = (size_t) (kMaxRingSeconds * sr) + 8;
        live.l.assign (ringCap, 0.f); live.r.assign (ringCap, 0.f);
        live.ratio = 1.0; live.n = ringCap;
        reset();
        for (int i = 0; i < 1024 + 1; ++i) win[i] = 0.5f - 0.5f * std::cos (kPi * (float) i / 1024.0f);
    }

    void reset()
    {
        for (auto& v : voices) v = Voice();
        liveW = 0; frozen = false; ringN = (size_t) (4.0 * sr); lowState[0] = lowState[1] = 0; dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0; os[0].reset(); os[1].reset();
        for (int c = 0; c < 2; ++c) { lowSh[c].reset(); midBq[c].reset(); highSh[c].reset(); filt[c].reset(); sideLp = 0; }
        coefsValid = false; smInit = false; danceEnv = 0; danceGain = 1.0f; lastGr = 0.0f;
    }

    void setParams (const Params& p) { P = p; }
    Params& params() { return P; }

    // Load a sample (any rate). Mono input: pass the same pointer for both channels or nullptr for R.
    void loadSample (const float* l, const float* r, size_t n, double fileSr)
    {
        smp.l.assign (l, l + n); smp.r.assign (r ? r : l, (r ? r : l) + n);
        smp.n = n; smp.ratio = fileSr / sr;
        detectTransients();
        for (auto& v : voices) { v.head = 0; for (auto& g : v.g) g.on = false; }
    }
    size_t sampleLength() const { return smp.n; }
    const std::vector<float>& sampleL() const { return smp.l; }

    // FX mode: copy the capture buffer (oldest first) into the sample slot, so it can be played with MIDI or saved as a wav.
    void captureToSample()
    {
        const size_t N = ringN; smp.l.assign (N, 0.f); smp.r.assign (N, 0.f); smp.n = N; smp.ratio = 1.0;
        const size_t w = liveW % ringCap;   // ring in use is [0, ringN) wrapped around liveW % ringN
        const size_t wr = liveW % N;
        (void) w;
        for (size_t i = 0; i < N; ++i) { const size_t k = (wr + i) % N; smp.l[i] = live.l[k]; smp.r[i] = live.r[k]; }
        detectTransients();
    }
    void exportSample (std::vector<float>& l, std::vector<float>& r) const { l = smp.l; r = smp.r; }

    void setTransients (const std::vector<size_t>& t) { trans = t; std::sort (trans.begin(), trans.end()); regions = t; }

    // Onset detection on the sample: energy rise over a 256 sample hop, adaptive threshold, 80 ms minimum gap.
    void detectTransients() { detect (smp, sr, detected); }
    // Onset detection: energy rise over a 256 sample hop, adaptive threshold, 80 ms minimum gap. Static so the plugin can run it off the audio thread.
    static void detect (const Buf& b, double engineSr, std::vector<size_t>& out)
    {
        out.clear();
        if (b.n < 2048) return;
        const size_t hop = 256, frames = b.n / hop;
        std::vector<float> e (frames);
        for (size_t f = 0; f < frames; ++f) { double s = 0; for (size_t i = 0; i < hop; ++i) { const float x = 0.5f * (b.l[f * hop + i] + b.r[f * hop + i]); s += (double) x * x; } e[f] = (float) std::sqrt (s / hop); }
        const size_t minGap = (size_t) (0.08 * b.ratio * engineSr / hop) + 1; size_t last = 0; bool any = false;
        for (size_t f = 2; f + 1 < frames; ++f)
        {
            double avg = 0; const size_t a0 = f > 24 ? f - 24 : 0; for (size_t k = a0; k < f; ++k) avg += e[k]; avg /= (double) (f - a0);
            const bool onset = e[f] > 1.8f * (float) avg + 0.01f && e[f] > e[f - 1] && e[f - 1] <= e[f - 2] * 1.5f + 1e-4f;
            if (onset && (!any || f - last >= minGap)) { out.push_back (f * hop); last = f; any = true; }
        }
    }
    // Build a Sample from raw audio (any rate; r may be null for mono). Allocates: call it on the message thread.
    static void makeSample (Sample& s, const float* l, const float* r, size_t n, double fileSr, double engineSr)
    {
        s.l.assign (l, l + n); s.r.assign (r ? r : l, (r ? r : l) + n); s.n = n; s.ratio = fileSr / engineSr; detect (s, engineSr, s.detected);
    }
    // Audio thread: take over a prepared sample. Swaps vectors, never allocates; the old sample is left in s for the caller to free elsewhere.
    void swapSample (Sample& s)
    {
        std::swap (smp.l, s.l); std::swap (smp.r, s.r); std::swap (smp.n, s.n); std::swap (smp.ratio, s.ratio); std::swap (detected, s.detected);
        for (auto& v : voices) { v.head = 0; for (auto& g : v.g) g.on = false; }
    }
    // Audio thread: copy the capture ring (oldest first) into dst without allocating (dst must have capacity reserved for the longest ring).
    bool captureInto (Buf& dst) const
    {
        const size_t N = ringN; if (N < 64 || dst.l.capacity() < N || dst.r.capacity() < N) return false;
        dst.l.resize (N); dst.r.resize (N); dst.n = N; dst.ratio = 1.0; const size_t wr = liveW % N;
        for (size_t i = 0; i < N; ++i) { const size_t k = (wr + i) % N; dst.l[i] = live.l[k]; dst.r[i] = live.r[k]; }
        return true;
    }
    // Display helpers (plugin editor): read-only views, benign races on the float data.
    const Buf& liveBuf() const { return live; }
    size_t ringLength() const { return ringN; }
    size_t liveWritten() const { return liveW; }
    float lastGainReductionDb() const { return lastGr; }
    // Normalised grain positions (0..1 of the sample, or of the ring while in FX mode), for the display. Returns how many were written.
    int grainMarks (float* out, int maxOut) const
    {
        const bool fx = P[pMode] >= 0.5f; int k = 0;
        const double nn = fx ? (double) ringN : (double) smp.n; if (nn < 16) return 0;
        for (auto& v : voices) for (auto& g : v.g)
        {
            if (!g.on || k >= maxOut) continue;
            double x; if (fx) { const double ahead = std::fmod ((double) (liveW % ringN) - g.idx + nn, nn); x = 1.0 - ahead / nn; } else x = g.idx / nn;
            out[k++] = (float) clampf ((float) x, 0.f, 1.f);
        }
        return k;
    }
    // Playing positions of the voices that are on (0..1), for the display. Returns how many were written.
    int voiceHeads (float* out, int maxOut) const
    {
        int k = 0; const bool fx = P[pMode] >= 0.5f;
        if (fx) { const double nn = (double) ringN; if (nn > 16 && maxOut > 0) out[k++] = (float) clampf ((float) (1.0 - voices[0].lag / nn), 0.f, 1.f); return k; }
        const double nn = (double) smp.n; if (nn < 16) return 0;
        for (auto& v : voices) if (v.on && k < maxOut) out[k++] = (float) clampf ((float) (v.head / nn), 0.f, 1.f);
        return k;
    }
    // Position of the riser shown on the graphs for the newest voice.
    const std::vector<size_t>& detectedTransients() const { return detected; }

    void noteOn (int note, float vel)
    {
        if (P[pMode] >= 0.5f) { Voice& f = voices[0]; f.note = note; f.penvT = 0; f.riseT = 0; return; }   // FX mode: one permanent voice, a note retriggers the envelopes and sets the riser key
        int idx = -1; const int maxV = (int) P[pVoices];
        for (int i = 0; i < maxV && i < kVoices; ++i) if (!voices[i].on) { idx = i; break; }
        if (idx < 0) { float best = -1.0f; for (int i = 0; i < maxV && i < kVoices; ++i) if (voices[i].age > best || idx < 0) { best = voices[i].age; idx = i; } }
        if (idx < 0) idx = 0;
        Voice& v = voices[idx]; const double prevSemis = v.semis; const bool was = v.on && v.env > 0.01f; const float prevEnv = v.env;
        v = Voice(); v.on = true; v.note = note; v.vel = vel; v.stage = 1; v.env = was ? prevEnv : 0.0f;
        v.semis = was ? prevSemis : 0.0; v.rnd.s = 99991u + (uint32_t) (note * 7919 + idx * 31);
        v.age = 0; v.cnt = 0;
        placeHead (v, note);
    }
    void noteOff (int note) { for (auto& v : voices) if (v.on && v.note == note && v.stage < 3) v.stage = 3; }
    void allNotesOff() { for (auto& v : voices) if (v.on) v.stage = 3; }

    int activeVoices() const { int n = 0; for (auto& v : voices) if (v.on) ++n; return n; }
    int activeGrains() const { int n = 0; for (auto& v : voices) for (auto& g : v.g) if (g.on) ++n; return n; }
    float riserProgress() const { float m = 0; for (auto& v : voices) if (v.on) m = std::max (m, v.riseU); return m; }
    // Playing position of the newest voice in 0..1 of the sample (or of the capture while frozen) for the display.
    float playhead() const
    {
        const Voice& v = voices[0];
        if (P[pMode] >= 0.5f) { const double L = (double) ringN; return (float) clampf ((float) (1.0 - v.lag / L), 0.f, 1.f); }
        return smp.n ? (float) clampf ((float) (v.head / (double) smp.n), 0.f, 1.f) : 0.f;
    }
    bool isFrozen() const { return frozen; }
    int latencySamples() const { return 0; }   // nothing looks ahead: dry stays aligned, wet is a transformation, not a delayed copy

    // inL/inR may be null in Instrument mode. Output buffers must not alias nothing special; in place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        int pos = 0;
        while (pos < n)
        {
            const int c = std::min (kChunk, n - pos);
            chunk (inL ? inL + pos : nullptr, inR ? inR + pos : nullptr, outL + pos, outR + pos, c);
            pos += c;
        }
    }

private:
    struct Grain { bool on = false; double idx = 0, rate = 1; int age = 0, len = 1, att = 1; float gl = 1, gr = 1, norm = 1; };
    struct Voice
    {
        bool on = false; int note = 60, stage = 0; float vel = 1, env = 0, age = 0, riseU = 0, riseT = 0, penvT = 0;
        double head = 0, lag = 1, semis = 0, lastHead = 0, acc = 0, lfo = 0; int cnt = 0; bool odd = false; Rng rnd; Grain g[kGrains];
    };

    double sr = 48000;
    Params P;
    Sample smp; Buf live; size_t ringCap = 0, ringN = 0, liveW = 0; bool frozen = false;
    std::vector<size_t> trans, regions, detected;
    Voice voices[kVoices];
    float win[1025];
    Rng rng;

    // chain state
    Oversampler4 os[2]; Biquad lowSh[2], midBq[2], highSh[2]; Svf filt[2]; float lowState[2] = { 0, 0 }, sideLp = 0, dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 };
    float smMix = 1, smOut = 1, smWidth = 1, smBassMono = 0, smLow = 0, smMid = 0, smMidF = 1000, smHigh = 0, smCut = 12000, smReso = 0.7f, smDrive = 0, smX = 0, smFuzz = 0, smDance = 0;
    bool smInit = false, coefsValid = false; float cLow = 1e9f, cMid = 1e9f, cMidF = 1e9f, cMidQ = 1e9f, cHigh = 1e9f, cCut = 1e9f, cRes = 1e9f, mk = 1.0f, mkD = -1, mkX = -1, mkF = -1;
    float danceEnv = 0, danceGain = 1.0f, lastGr = 0.0f;

    static float lerp (float a, float b, float t) { return a + (b - a) * t; }
    float readWin (float x) const { x = clampf (x, 0.f, 1.f) * 1024.0f; const int i = (int) x; const float f = x - (float) i; return win[i] + (win[std::min (i + 1, 1024)] - win[i]) * f; }

    void placeHead (Voice& v, int note)
    {
        const float startPct = P[pStart] * 0.01f; const int keyTo = (int) P[pKeyTo];
        const double n = (double) smp.n;
        double h = startPct * n;
        if (keyTo == 1 && note >= 72) h = clampf ((float) (note - 72) / 24.0f, 0.f, 1.f) * n;
        else if (keyTo == 2 && note >= 72) h = h + (double) (note - 72) * (P[pGrainLen] + P[pAttack]) * 0.001 * sr * smp.ratio;
        else if (keyTo == 3 && note >= 72)
        {
            const auto& t = transList();
            const size_t k = (size_t) (note - 72); if (k < t.size()) h = (double) t[k];
        }
        v.head = std::fmod (h, std::max (1.0, n)); v.lastHead = v.head;
    }
    const std::vector<size_t>& transList() const { return (P[pTransMode] > 0.5f && P[pTransMode] < 1.5f && !regions.empty()) ? regions : detected; }

    float sampleAt (const Buf& b, double idx, int ch, bool wrap) const
    {
        const float* d = ch ? b.r.data() : b.l.data(); const long N = (long) b.n; if (N < 4) return 0.f;
        const long i1 = (long) std::floor (idx); const float f = (float) (idx - (double) i1);
        float y[4];
        for (int k = 0; k < 4; ++k) { long i = i1 - 1 + k; if (wrap) { i %= N; if (i < 0) i += N; } else i = i < 0 ? 0 : (i >= N ? N - 1 : i); y[k] = d[i]; }
        const float c0 = y[1], c1 = 0.5f * (y[2] - y[0]), c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3], c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    void chunk (const float* inL, const float* inR, float* outL, float* outR, int c)
    {
        const bool fx = P[pMode] >= 0.5f;
        const float mixT = P[pMix] * 0.01f;
        // --- capture (always while in FX mode, so Freeze can grab what just played)
        const size_t N = ringN = std::min (ringCap - 8, (size_t) (P[pLiveLen] * sr));
        const bool effFreeze = fx && (P[pFreeze] >= 0.5f || P[pHold] >= 0.5f);
        if (effFreeze && !frozen) { frozen = true; Voice& v = voices[0]; v.lag = std::max ((double) P[pCaptureOffset] * 0.001 * sr + 16.0, std::min ((double) N, v.lag)); v.penvT = 0; }
        if (!effFreeze && frozen) { frozen = false; }
        if (fx && inL)
        {
            if (!frozen) for (int i = 0; i < c; ++i) { live.l[liveW % N] = inL[i]; live.r[liveW % N] = inR ? inR[i] : inL[i]; ++liveW; }
        }
        else if (fx && !inL && !frozen) liveW += (size_t) c;

        // --- smoothing of the continuous parameters (about 10 ms)
        const float a = 1.0f - std::exp (-(float) c / (float) (0.010 * sr));
        auto sm = [&] (float& s, float t) { if (!smInit) s = t; else s += (t - s) * a; };
        const bool nasty = P[pNasty] >= 0.5f; const float cl = nasty ? 2.0f : 1.0f;
        sm (smMix, mixT); sm (smOut, dbToLin (P[pOut])); sm (smWidth, P[pWidth] * 0.01f); sm (smBassMono, P[pBassMono]);
        sm (smLow, P[pLowG]); sm (smMid, P[pMidG]); sm (smMidF, P[pMidF]); sm (smHigh, P[pHighG]);
        sm (smReso, P[pReso]); sm (smDrive, std::min (P[pDrive] * 0.01f, cl)); sm (smX, std::min (P[pTransistor] * 0.01f, cl)); sm (smFuzz, std::min (P[pFuzz] * 0.01f, cl)); sm (smDance, P[pDance] * 0.01f);

        // --- instrument voices or the one FX voice
        float gl[kChunk], gr[kChunk]; std::memset (gl, 0, sizeof gl); std::memset (gr, 0, sizeof gr);
        float riseMax = 0.f;
        if (smMix > 1e-5f || !smInit)
        {
            if (fx) { Voice& v = voices[0]; v.on = true; v.stage = 2; v.env = 1.0f; renderVoice (v, c, gl, gr, true, N); riseMax = v.riseU; }
            else for (int vi = 0; vi < kVoices; ++vi) { Voice& v = voices[vi]; if (v.on) { renderVoice (v, c, gl, gr, false, N); riseMax = std::max (riseMax, v.riseU); } }
        }
        else { for (auto& v : voices) { for (auto& g : v.g) g.on = false; } }

        // --- chain
        wetChain (gl, gr, c, riseMax);

        // --- dry/wet and safety limiter (limiter only on the wet path, so mix 0 stays bit exact)
        const bool lim = P[pSafeLim] >= 0.5f;
        for (int i = 0; i < c; ++i)
        {
            float wl = gl[i], wr = gr[i];
            if (lim) { wl = softLim (wl); wr = softLim (wr); }
            const float dl = inL ? inL[i] : 0.f, dr = inR ? inR[i] : dl;
            const float m = smMix;
            outL[i] = m >= 0.99999f ? wl : dl * (1.0f - m) + wl * m;
            outR[i] = m >= 0.99999f ? wr : dr * (1.0f - m) + wr * m;
        }
        smInit = true;
    }
    static float softLim (float x) { const float ax = std::fabs (x); if (ax <= 0.9f) return x; const float y = 0.9f + 0.1f * std::tanh ((ax - 0.9f) * 10.0f); return x < 0 ? -y : y; }

    void renderVoice (Voice& v, int c, float* gl, float* gr, bool fx, size_t N)
    {
        const Buf& src = fx ? live : smp;
        const bool haveSrc = fx ? (liveW > 64) : (smp.n > 64);
        const float dt = 1.0f / (float) sr;
        const float attS = std::max (0.0008f, P[pAttack] * 0.001f) * (float) sr;
        const bool loop = P[pLoop] >= 0.5f;
        const float envA = P[pAmpA], envD = P[pAmpD], envS = P[pAmpS] * 0.01f, envR = P[pAmpR];
        const double glideC = P[pGlide] > 0.5f ? 1.0 - std::exp (-1.0 / (0.001 * P[pGlide] * sr)) : 1.0;
        const bool keyPitch = (int) P[pKeyTo] == 0;
        const double targetSemis = (!fx && keyPitch) ? (double) (v.note - (int) P[pRoot]) : 0.0;
        const float lfoInc = P[pLfoSpeed] * dt;

        for (int i = 0; i < c; ++i)
        {
            // amp envelope (instrument only)
            if (!fx)
            {
                if (v.stage == 1) { v.env += (envA <= 0.5f) ? 1.0f : 1000.0f / (envA * (float) sr); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } }
                else if (v.stage == 2) { const float tgt = envS; if (v.env > tgt) { v.env -= (envD <= 0.5f) ? 1.0f : (1.0f - tgt) * 1000.0f / (envD * (float) sr); if (v.env < tgt) v.env = tgt; } }
                else if (v.stage == 3) { v.env -= (envR <= 0.5f) ? 1.0f : std::max (0.0001f, v.env) * 6.9f * 1000.0f / (envR * (float) sr) + 1e-7f; if (v.env <= 0.0005f) { v.env = 0; v.on = false; for (auto& g : v.g) g.on = false; return; } }
            }
            v.age += dt; v.riseT += dt; v.penvT += dt;

            // riser progress
            const bool riseOn = fx ? (P[pRiseOn] >= 0.5f) : (P[pRiseOn] >= 0.5f);
            if (riseOn) { const float u = clampf (v.riseT / std::max (0.05f, P[pRiseTime]), 0.f, 1.f); v.riseU = std::pow (u, P[pRiseCurve]); }
            else { v.riseT = 0; v.riseU = std::max (0.f, v.riseU - dt * 4.0f); }
            if (!riseOn && v.riseU <= 0.f) v.riseT = 0;

            // pitch
            v.semis += (targetSemis - v.semis) * glideC;
            const float pe = P[pPitchEnvAmt] * std::pow (std::max (0.0f, 1.0f - clampf (v.penvT * 1000.0f / std::max (5.0f, P[pPitchEnvTime]), 0.f, 1.f)), P[pPitchEnvCurve]);
            const double semis = v.semis + P[pPitch] + P[pFine] * 0.01 + pe + v.riseU * P[pRisePitch];

            // LFO and wave spacing
            v.lfo += lfoInc; if (v.lfo >= 1.0) v.lfo -= 1.0;
            float ws = P[pWaveSpacing] * 0.01f + P[pLfoDepth] * 0.01f * std::sin (2.0f * kPi * (float) v.lfo);
            const bool hold = P[pHold] >= 0.5f; if (hold) ws = 0.f;

            // move the position
            if (haveSrc)
            {
                if (fx)
                {
                    if (!frozen) { v.lag += 1.0 - (double) ws; v.lag = clampf ((float) v.lag, (float) (P[pCaptureOffset] * 0.001 * sr) + 16.0f, (float) (N - 4096)); }
                    else { v.lag -= (double) ws; while (v.lag < 1.0) v.lag += (double) N; while (v.lag > (double) N) v.lag -= (double) N; }
                }
                else
                {
                    v.head += (double) ws * smp.ratio; const double n = (double) smp.n;
                    const double loopStart = clampf (P[pStart] * 0.01f, 0.f, 0.99f) * n;
                    if (loop) { if (v.head >= n) v.head = loopStart + std::fmod (v.head - n, std::max (1.0, n - loopStart)); else if (v.head < 0) v.head = n + std::fmod (v.head, std::max (1.0, n)); }
                    else if (v.head >= n || v.head < 0) { if (v.stage < 3) v.stage = 3; v.head = clampf ((float) v.head, 0.f, (float) n - 1.f); }
                }
            }

            // spawn grains
            if (!haveSrc) v.cnt = 0;
            if (--v.cnt <= 0 && haveSrc)
            {
                const float u = v.riseU;
                const float holdMs0 = P[pGrainLen];
                float holdMs = holdMs0 * std::pow (std::min (1.0f, 3.0f / holdMs0), u * P[pRiseGrain] * 0.01f);
                float holdS = holdMs * 0.001f * (float) sr, att = attS;
                const float spc = P[pSpacing] * 0.01f;
                const float nominal = holdS + att;                      // end to end with a clean crossfade
                const float w = u * P[pRiseTone] * 0.01f;
                float hop = nominal * lerp (spc, 1.0f, w);
                float excess = std::max (1.0f, nominal / std::max (1.0f, hop));
                if (w > 0.0f)
                {
                    const float hz = (!fx && P[pRiseKey] >= 0.5f) ? 440.0f * std::pow (2.0f, (float) (v.note - 69) / 12.0f) : P[pRiseHz];
                    const float hopTone = (float) sr / std::max (20.0f, hz);
                    hop = std::exp (lerp (std::log (std::max (8.0f, hop)), std::log (hopTone), w));
                    holdS = lerp (holdS, 1.0f, w); att = lerp (att, hop, w);
                    excess = lerp (excess, 1.0f, w);
                }
                hop = std::max (8.0f, hop);
                int len = (int) (holdS + 2.0f * att); len = std::max (len, 16);

                // where to read from
                double start = 0; float lenUse = (float) len;
                const double rate = std::pow (2.0, semis / 12.0) * (fx ? 1.0 : smp.ratio);
                const float rnd = P[pRand] * 0.01f; const float jit = rnd > 0 ? v.rnd.bi() * rnd * 0.25f * (float) sr : 0.f;
                if (fx)
                {
                    double lag = v.lag + jit; const double need = (double) len * std::max (0.0, rate - 1.0) + 64.0;
                    lag = frozen ? lag : std::max (lag, need); while (lag < 1) lag += (double) N; while (lag > (double) N) lag -= (double) N;
                    start = (double) (liveW % N) - lag; while (start < 0) start += (double) N;
                }
                else
                {
                    start = v.head + (double) jit * smp.ratio;
                    const int tm = (int) P[pTransMode];
                    if (tm > 0)
                    {
                        const auto& t = transList(); const double lo = std::min (v.lastHead, v.head), hi = std::max (v.lastHead, v.head);
                        auto it = std::lower_bound (t.begin(), t.end(), (size_t) std::max (0.0, lo));
                        if (it != t.end() && (double) *it <= hi + 1.0) { start = (double) *it; lenUse = std::max (16.0f, P[pTransHold] * 0.001f * (float) sr + 2.0f * attS); }
                    }
                    v.lastHead = v.head;
                    const double n = (double) smp.n; if (start < 0) start += n; if (start >= n) start = std::fmod (start, n);
                }
                // find a free grain (or steal the oldest)
                Grain* gp = nullptr; int oldest = -1;
                for (auto& g : v.g) { if (!g.on) { gp = &g; break; } if (g.age > oldest) { oldest = g.age; gp = &g; } }
                Grain& g = *gp;
                g.on = true; g.idx = start; g.rate = rate; g.age = 0; g.len = (int) lenUse; g.att = std::max (1, (int) std::min (att, lenUse * 0.5f));
                const float pan = P[pPan] * 0.01f; v.odd = !v.odd;
                g.gl = v.odd ? 1.0f : 1.0f - pan; g.gr = v.odd ? 1.0f - pan : 1.0f;
                {   // overlapping grains of the same audio add up coherently (normal playback), grains of shifted or shuffled audio add up like noise
                    const float coh = (1.0f - rnd) / (1.0f + 8.0f * (float) std::fabs (rate / (fx ? 1.0 : smp.ratio) - 1.0));
                    g.norm = std::pow (excess, -(0.5f + 0.5f * coh));
                }
                v.acc += (double) hop; const int whole = (int) v.acc; v.acc -= (double) whole; v.cnt += std::max (1, whole);   // fractional hop kept, so tones stay in tune
            }

            // render grains
            float sl = 0, sr_ = 0;
            for (auto& g : v.g)
            {
                if (!g.on) continue;
                const float fi = g.age < g.att ? readWin ((float) g.age / (float) g.att) : 1.0f;
                const int rem = g.len - g.age; const float fo = rem < g.att ? readWin ((float) rem / (float) g.att) : 1.0f;
                const float gain = fi * fo * g.norm;
                float xl, xr;
                if (fx) { xl = sampleAt (live, g.idx, 0, true); xr = sampleAt (live, g.idx, 1, true); }
                else
                {
                    if (!loop && (g.idx >= (double) smp.n - 1 || g.idx < 0)) { g.on = false; continue; }
                    xl = sampleAt (smp, g.idx, 0, loop); xr = sampleAt (smp, g.idx, 1, loop);
                }
                sl += xl * gain * g.gl; sr_ += xr * gain * g.gr;
                g.idx += g.rate;
                if (fx) { if (g.idx >= (double) N) g.idx -= (double) N; else if (g.idx < 0) g.idx += (double) N; }
                else if (loop) { const double n = (double) smp.n; if (g.idx >= n) g.idx -= n; else if (g.idx < 0) g.idx += n; }
                if (++g.age >= g.len) g.on = false;
            }
            const float e = v.env * (fx ? 1.0f : (0.4f + 0.6f * v.vel));
            gl[i] += sl * e; gr[i] += sr_ * e;
        }
        (void) src;
    }

    void wetChain (float* gl, float* gr, int c, float riseU)
    {
        // M/S width and bass mono
        if (std::fabs (smWidth - 1.0f) > 1e-3f || smBassMono > 20.0f)
        {
            const float lpA = smBassMono > 20.0f ? 1.0f - std::exp (-2.0f * kPi * smBassMono / (float) sr) : 0.0f;
            for (int i = 0; i < c; ++i)
            {
                const float m = 0.5f * (gl[i] + gr[i]); float s = 0.5f * (gl[i] - gr[i]);
                if (lpA > 0.0f) { sideLp += (s - sideLp) * lpA; s -= sideLp; }          // only the part above the corner stays in the side
                s *= smWidth; gl[i] = m + s; gr[i] = m - s;
            }
        }
        // EQ, coefficients updated per chunk when they moved
        const float cutT = P[pFiltType] > 0.5f ? clampf (smCutoffEff (riseU), 20.0f, 20000.0f) : 20000.0f;
        if (std::fabs (smLow - cLow) > 0.01f) { cLow = smLow; for (auto& b : lowSh) b.lowShelf (sr, 120.0, cLow); }
        if (std::fabs (smMid - cMid) > 0.01f || std::fabs (smMidF - cMidF) > 0.5f || std::fabs (P[pMidQ] - cMidQ) > 0.01f) { cMid = smMid; cMidF = smMidF; cMidQ = P[pMidQ]; for (auto& b : midBq) b.peak (sr, cMidF, cMid, cMidQ); }
        if (std::fabs (smHigh - cHigh) > 0.01f) { cHigh = smHigh; for (auto& b : highSh) b.highShelf (sr, 8000.0, cHigh); }
        const bool eqOn = std::fabs (smLow) > 0.01f || std::fabs (smMid) > 0.01f || std::fabs (smHigh) > 0.01f || std::fabs (cLow) > 0.01f || std::fabs (cMid) > 0.01f || std::fabs (cHigh) > 0.01f;
        if (eqOn) for (int i = 0; i < c; ++i) { gl[i] = highSh[0].tick (midBq[0].tick (lowSh[0].tick (gl[i]))); gr[i] = highSh[1].tick (midBq[1].tick (lowSh[1].tick (gr[i]))); }
        // filter
        const int ft = (int) P[pFiltType];
        if (ft > 0)
        {
            if (std::fabs (cutT - cCut) > 0.5f || std::fabs (smReso - cRes) > 0.005f) { cCut = cutT; cRes = smReso; for (auto& f : filt) f.set (sr, cCut, cRes); }
            for (int i = 0; i < c; ++i)
            {
                filt[0].tick (gl[i]); filt[1].tick (gr[i]);
                gl[i] = ft == 1 ? filt[0].lp : (ft == 2 ? filt[0].hp : filt[0].bp * 2.0f);
                gr[i] = ft == 1 ? filt[1].lp : (ft == 2 ? filt[1].hp : filt[1].bp * 2.0f);
            }
        }
        // colour
        if (smDrive > 0.001f || smX > 0.001f || smFuzz > 0.001f)
        {
            if (std::fabs (smDrive - mkD) > 0.01f || std::fabs (smX - mkX) > 0.01f || std::fabs (smFuzz - mkF) > 0.01f) { mkD = smDrive; mkX = smX; mkF = smFuzz; mk = Colour::makeup (mkD, mkX, mkF); }
            for (int i = 0; i < c; ++i)
            {
                float* ch[2] = { &gl[i], &gr[i] };
                for (int k = 0; k < 2; ++k)
                {
                    const float dD = smDrive, dX = smX, dF = smFuzz;
                    const float y = os[k].run (*ch[k], [dD, dX, dF] (float v) { return Colour::shaper (v, dD, dX, dF); }) * mk;   // 4x oversampled, no latency
                    const float o = y - dcX[k] + 0.9995f * dcY[k]; dcX[k] = y; dcY[k] = o; *ch[k] = o;   // removes the offset the asymmetric stages add
                }
            }
        }        else { os[0].reset(); os[1].reset(); }
        // Dance glue: compress everything above 150 Hz, leave the bass alone
        if (smDance > 0.002f)
        {
            const float lpA = 1.0f - std::exp (-2.0f * kPi * 150.0f / (float) sr);
            const float thr = -6.0f - 24.0f * smDance, ratio = 2.0f + 4.0f * smDance, mkDb = (-thr) * (1.0f - 1.0f / ratio) * 0.35f;
            const float atk = 1.0f - std::exp (-1.0f / (0.002f * (float) sr)), rel = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
            for (int i = 0; i < c; ++i)
            {
                lowState[0] += (gl[i] - lowState[0]) * lpA; lowState[1] += (gr[i] - lowState[1]) * lpA;
                const float hl = gl[i] - lowState[0], hr = gr[i] - lowState[1];
                const float pk = std::max (std::fabs (hl), std::fabs (hr));
                danceEnv += (pk - danceEnv) * (pk > danceEnv ? atk : rel);
                const float db = 20.0f * std::log10 (std::max (1e-6f, danceEnv));
                const float gr_db = db > thr ? -(db - thr) * (1.0f - 1.0f / ratio) : 0.0f;
                lastGr = gr_db;
                const float g = dbToLin (gr_db + mkDb * smDance);
                gl[i] = lowState[0] + hl * g; gr[i] = lowState[1] + hr * g;
            }
        }
        else lastGr = 0.0f;
        for (int i = 0; i < c; ++i) { gl[i] *= smOut; gr[i] *= smOut; }
    }
    float smCutoffEff (float riseU) const { return P[pCutoff] * std::pow (2.0f, riseU * P[pRiseFilter]); }
};
} // namespace hb
