// Scratch Cat FX: small DSP blocks (pure C++, no JUCE): filters, EQ, FFT, FX slots. Copied from scratchcatduo-vst.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <complex>
#include <cstdint>

namespace scfx {
constexpr double kPi = 3.14159265358979323846;
inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// RBJ biquad, transposed direct form II.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void norm(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = (float)(B0 / A0); b1 = (float)(B1 / A0); b2 = (float)(B2 / A0); a1 = (float)(A1 / A0); a2 = (float)(A2 / A0);
    }
    void peak(double sr, double f, double gDb, double q) {
        double A = std::pow(10.0, gDb / 40.0), w = 2 * kPi * f / sr, al = std::sin(w) / (2 * q), c = std::cos(w);
        norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf(double sr, double f, double gDb) {
        double A = std::pow(10.0, gDb / 40.0), w = 2 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
        double al = s / 2 * std::sqrt(2.0), t = 2 * std::sqrt(A) * al;
        norm(A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
             (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    void highShelf(double sr, double f, double gDb) {
        double A = std::pow(10.0, gDb / 40.0), w = 2 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
        double al = s / 2 * std::sqrt(2.0), t = 2 * std::sqrt(A) * al;
        norm(A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
             (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }
    float tick(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

// State variable filter (TPT), lowpass / highpass / bandpass outputs.
struct Svf {
    float g = 0.1f, k = 1.f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0, lp = 0, bp = 0, hp = 0;
    void set(double sr, double f, double q) {
        f = std::min(f, sr * 0.45); g = (float) std::tan(kPi * f / sr); k = (float)(1.0 / q);
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick(float x) {
        float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; bp = v1; lp = v2; hp = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

// Crossfader curves. x = 0 is all deck A, x = 1 is all deck B.
//   0 Smooth: linear (gain A = 1-x, B = x)
//   1 Sharp:  cut curve, both decks at full level over most of the travel, fast cut near the ends
//   2 Const:  constant power (cos / sin)
inline void crossGains(int curve, float x, float& gA, float& gB) {
    x = clampf(x, 0.f, 1.f);
    if (curve == 0) { gA = 1.f - x; gB = x; }
    else if (curve == 1) { gA = clampf((1.f - x) * 10.f, 0.f, 1.f); gB = clampf(x * 10.f, 0.f, 1.f); }   // full level until the last 10 percent of travel
    else { gA = (float) std::cos(x * kPi / 2); gB = (float) std::sin(x * kPi / 2); }
}

// Radix-2 FFT (in place), n a power of two.
inline void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPi / (double) len;
        const std::complex<float> wl((float) std::cos(ang), (float) std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

// Channel strip: trim, 3 band EQ with kill, bipolar colour filter (left of centre low pass, right of centre high pass).
struct ChannelStrip {
    double sr = 44100;
    Biquad lo[2], mid[2], hi[2];
    Svf cf[2];
    float lastLo = 99, lastMid = 99, lastHi = 99, lastCol = 99;
    float colMode = 0;     // -1 lowpass, 0 off, +1 highpass
    void prepare(double s) { sr = s; for (int c = 0; c < 2; ++c) { lo[c].reset(); mid[c].reset(); hi[c].reset(); cf[c].reset(); } lastLo = lastMid = lastHi = lastCol = 99; }
    // gains in dB, -26 (or lower) means kill. colour -1..1.
    void setParams(float loDb, float midDb, float hiDb, float colour) {
        auto g = [](float d) { return d <= -25.9f ? -60.f : d; };
        if (loDb != lastLo)   { lastLo = loDb;   for (int c = 0; c < 2; ++c) { Biquad t; t.lowShelf(sr, 100.0, g(loDb)); t.z1 = lo[c].z1; t.z2 = lo[c].z2; lo[c] = t; } }
        if (midDb != lastMid) { lastMid = midDb; for (int c = 0; c < 2; ++c) { Biquad t; t.peak(sr, 1000.0, g(midDb), 0.7); t.z1 = mid[c].z1; t.z2 = mid[c].z2; mid[c] = t; } }
        if (hiDb != lastHi)   { lastHi = hiDb;   for (int c = 0; c < 2; ++c) { Biquad t; t.highShelf(sr, 8000.0, g(hiDb)); t.z1 = hi[c].z1; t.z2 = hi[c].z2; hi[c] = t; } }
        if (colour != lastCol) {
            lastCol = colour;
            if (std::fabs(colour) < 0.03f) colMode = 0;
            else if (colour < 0) { colMode = -1; const double f = 20000.0 * std::pow(120.0 / 20000.0, (double) -colour); for (auto& s2 : cf) s2.set(sr, f, 1.2); }
            else { colMode = 1; const double f = 30.0 * std::pow(9000.0 / 30.0, (double) colour); for (auto& s2 : cf) s2.set(sr, f, 1.2); }
        }
    }
    float tick(int ch, float x) {
        // A band at exactly 0 dB is passed through untouched (the filter still runs so its state stays current when the knob moves).
        // With fused multiply-add builds the 0 dB filter left about -94 dBFS of rounding residue, which broke the neutral pass through test.
        { const float y = lo[ch].tick(x);  if (lastLo  != 0.f) x = y; }
        { const float y = mid[ch].tick(x); if (lastMid != 0.f) x = y; }
        { const float y = hi[ch].tick(x);  if (lastHi  != 0.f) x = y; }
        if (colMode != 0) { cf[ch].tick(x); x = colMode < 0 ? cf[ch].lp : cf[ch].hp; }
        return x;
    }
};

// ---- FX slots ----
enum FxType { FxOff = 0, FxFilter, FxEcho, FxReverb, FxFlanger, FxCrush, FxTypeCount };
inline const char* fxName(int t) {
    static const char* n[] = { "Off", "Filter", "Echo", "Reverb", "Flanger", "Crush" };
    return n[t < 0 || t >= FxTypeCount ? 0 : t];
}

struct Delay {
    std::vector<float> b; size_t w = 0;
    void init(size_t n) { b.assign(n + 4, 0.f); w = 0; }
    void push(float x) { b[w] = x; if (++w >= b.size()) w = 0; }
    float read(float d) const {   // d samples back, linear interpolation
        float p = (float) w - d; while (p < 0) p += (float) b.size();
        size_t i = (size_t) p; float f = p - (float) i; size_t j = i + 1 >= b.size() ? 0 : i + 1;
        return b[i] * (1 - f) + b[j] * f;
    }
};

// One effect slot. p1 and p2 are 0..1, p2 is always the dry/wet.
// Filter: p1 sweeps low pass to high pass. Echo: p1 picks the time (1/4, 1/2, 3/4, 1 beat). Reverb: p1 = size, p2 = dry/wet. Flanger: p1 = depth, p2 = dry/wet. Crush: p1 = amount, p2 = dry/wet.
struct FxSlot {
    double sr = 44100;
    int type = FxOff; bool on = false;
    float p1 = 0.5f, p2 = 0.4f;
    float bpm = 120.f, echoBeats = 0.75f;
    float wetS = 0.f;                // smoothed dry/wet, so switching on and off never clicks
    Svf flt[2];
    Delay ed[2], fd[2];
    // reverb: 8 combs, 4 allpasses per side
    struct Comb { std::vector<float> b; size_t i = 0; float st = 0; };
    struct Ap { std::vector<float> b; size_t i = 0; };
    Comb combs[2][8]; Ap aps[2][4];
    float lfo = 0.f, crushHold[2] = { 0, 0 }; int crushCnt = 0;

    void prepare(double s) {
        sr = s; wetS = 0.f;
        for (auto& f : flt) f.reset();
        for (auto& d : ed) d.init((size_t)(sr * 2.5));
        for (auto& d : fd) d.init((size_t)(sr * 0.02));
        static const int ct[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, at[4] = { 556, 441, 341, 225 };
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 8; ++i) { combs[c][i].b.assign((size_t)((ct[i] + c * 23) * sr / 44100.0) + 1, 0.f); combs[c][i].i = 0; combs[c][i].st = 0; }
            for (int i = 0; i < 4; ++i) { aps[c][i].b.assign((size_t)((at[i] + c * 23) * sr / 44100.0) + 1, 0.f); aps[c][i].i = 0; }
        }
        lfo = 0; crushCnt = 0;
    }
    void process(float* l, float* r, int n) {
        const float target = on && type != FxOff ? p2 : 0.f;
        float* ch[2] = { l, r };
        const float sm = 1.f - std::exp(-1.f / (0.01f * (float) sr));
        if (type == FxFilter) {
            // p1 sweeps a low pass (left half, open at 0) into a high pass (right half), like a DJ filter knob
            double f = p1 < 0.5f ? 18000.0 * std::pow(0.015, 1.0 - (double) p1 * 2.0) : 20.0 * std::pow(900.0, ((double) p1 - 0.5) * 2.0);
            if (p1 < 0.02f) f = 18000.0;
            for (auto& s2 : flt) s2.set(sr, f, 1.0 / 0.35);
        }
        static const float kEchoBeats[4] = { 0.25f, 0.5f, 0.75f, 1.f };      // echo time comes from the Amount knob, in beats
        const float beats = kEchoBeats[std::min(3, (int) (clampf(p1, 0.f, 1.f) * 4.f))];
        const float dly = clampf((float)(60.0 / bpm * beats * sr), 100.f, (float)(sr * 2.4));
        const float room = 0.7f + 0.28f * clampf(p1, 0.f, 1.f);
        for (int i = 0; i < n; ++i) {
            wetS += (target - wetS) * sm;
            const float w = wetS;
            if (w < 0.0005f && target == 0.f) { if (type == FxEcho) { ed[0].push(ch[0][i]); ed[1].push(ch[1][i]); } continue; }
            float in[2] = { ch[0][i], ch[1][i] }, out[2] = { in[0], in[1] };
            if (type == FxFilter) {
                for (int c = 0; c < 2; ++c) { flt[c].tick(in[c]); out[c] = p1 >= 0.5f ? flt[c].hp : flt[c].lp; }
            } else if (type == FxEcho) {
                const float dl = ed[0].read(dly), dr = ed[1].read(dly);
                ed[0].push(in[0] + dl * 0.55f); ed[1].push(in[1] + dr * 0.55f);
                out[0] = dl; out[1] = dr;
            } else if (type == FxReverb) {
                const float x = (in[0] + in[1]) * 0.015f;
                for (int c = 0; c < 2; ++c) {
                    float acc = 0;
                    for (int k = 0; k < 8; ++k) {
                        auto& cb = combs[c][k]; float y = cb.b[cb.i];
                        cb.st = y * 0.6f + cb.st * 0.4f;        // damping
                        cb.b[cb.i] = x + cb.st * room; if (++cb.i >= cb.b.size()) cb.i = 0; acc += y;
                    }
                    for (int k = 0; k < 4; ++k) {
                        auto& a = aps[c][k]; float bo = a.b[a.i]; float y = -acc + bo;
                        a.b[a.i] = acc + bo * 0.5f; if (++a.i >= a.b.size()) a.i = 0; acc = y;
                    }
                    out[c] = acc * 3.f;
                }
            } else if (type == FxFlanger) {
                lfo += (0.3f + 0.4f * clampf(p1, 0.f, 1.f)) / (float) sr; if (lfo >= 1.f) lfo -= 1.f;
                const float m = 0.5f + 0.5f * std::sin(2.f * (float) kPi * lfo);
                const float d = (0.001f + 0.004f * m) * (float) sr;          // p1 is the sweep rate
                for (int c = 0; c < 2; ++c) { const float y = fd[c].read(d); fd[c].push(in[c] + y * 0.6f); out[c] = in[c] + y; }
            } else if (type == FxCrush) {
                const int hold = 1 + (int)(clampf(p1, 0.f, 1.f) * 20.f);
                const float steps = std::pow(2.f, 3.f + 9.f * (1.f - clampf(p1, 0.f, 1.f)));
                if (crushCnt % hold == 0) for (int c = 0; c < 2; ++c) crushHold[c] = std::round(in[c] * steps) / steps;
                ++crushCnt; out[0] = crushHold[0]; out[1] = crushHold[1];
            }
            ch[0][i] = in[0] * (1.f - w) + out[0] * w;
            ch[1][i] = in[1] * (1.f - w) + out[1] * w;
        }
    }
};
}
