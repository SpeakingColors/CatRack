// Scratch Cat FX: the effect engine. DAW input -> record buffer -> scratch deck -> trim, EQ, colour, deck fader -> crossfader
// (the other side is the DAW input played straight) -> 3 slot FX chain (on the scratch deck or on the master) -> PurrOne colour -> master.
// With nothing pressed the deck is in live mode and the sound passes straight through (zero latency, bit exact at neutral).
#pragma once
#include "ScfxDeck.h"
#include "ScfxPurrColour.h"
#include <atomic>
#include <array>

namespace scfx {

struct FxParams {
    float trimDb = 0, eqLo = 0, eqMid = 0, eqHi = 0, colour = 0, masterDb = 0;
    bool killLo = false, killMid = false, killHi = false;   // kills silence a band on everything you hear
    float deckFader = 1.f;                                  // scratch deck channel fader, linear 0..1.25
    float xf = 0.f; int xCurve = 0;                         // crossfader -1 (scratch deck) .. +1 (DAW input), curve 0 smooth, 1 sharp, 2 constant power
    int fxTarget = 0;                                       // 0 effects on the scratch deck, 1 effects on the master
    int fxType[3] = { FxFilter, FxEcho, FxReverb }; bool fxOn[3] = { false, false, false };
    float fxP1[3] = { 0.5f, 0.45f, 0.6f }, fxP2[3] = { 0.5f, 0.4f, 0.25f };
    float chainMix = 0.6f, echoBeats = 0.75f;
    bool purrOn = false; float purrDrive = 0, purrXs = 0, purrFuzz = 0, purrMix = 1.f;   // PurrOne colour: Drive, Transistor, Fuzz
    double hostBpm = 120.0;
};

class Engine {
public:
    Deck deck;
    std::atomic<float> peakIn { 0.f }, peakOut { 0.f }, peakL { 0.f }, peakR { 0.f }, vuDeck { 0.f };
    // Rolling picture of the DAW input for the live waveform: one bin per 1/32 s, newest bin at histIdx.
    static constexpr int kHist = 2048; static constexpr int kHistPerSec = 32;
    std::array<std::atomic<float>, kHist> hist;
    std::atomic<unsigned> histIdx { 0 };
    Engine() { for (auto& h : hist) h.store(0.f); }

    void prepare(double sr) {
        fs = sr; deck.prepare(sr); strip.prepare(sr); dryStrip.prepare(sr); for (auto& f : fx) f.prepare(sr);
        deck.useLiveInput(true); trimS = masterS = deckS = 1.f; gaS = 1.f; gbS = 0.f; histAcc = 0.f; histCnt = 0; histBin = std::max(1, (int) (sr / kHistPerSec));
        for (auto& h : hist) { h.store(0.f); }
        histIdx.store(0);
    }
    // Scratch gestures. touch = hand on the platter; speed in "normal speeds" (1 = forward at normal speed, -2 = backwards twice as fast).
    void touch(bool on, double speed) { deck.setJog(on, speed * Deck::kPlatterRevPerSec); }
    void brake() { deck.brake(); }
    void backspin() { deck.backspin(); }
    void goLive() { deck.goLive(); }
    bool isLive() const { return deck.inLiveMode(); }

    // In place safe: outL/outR may be the same buffers as inL/inR (the deck copies its input first).
    static void xfGains(float xf, int curve, float& a, float& b) {
        const float t = 0.5f * (std::max(-1.f, std::min(1.f, xf)) + 1.f);
        if (curve == 1) { a = std::min(1.f, std::max(0.f, (1.f - t) * 20.f)); b = std::min(1.f, std::max(0.f, t * 20.f)); }
        else if (curve == 2) { a = std::cos(t * (float) kPi * 0.5f); b = std::sin(t * (float) kPi * 0.5f); }
        else { a = std::min(1.f, 2.f * (1.f - t)); b = std::min(1.f, 2.f * t); }
    }
    void process(const FxParams& p, int n, const float* inL, const float* inR, float* outL, float* outR) {
        if ((int) tmpL.size() < n) { tmpL.resize((size_t) n); tmpR.resize((size_t) n); dryL.resize((size_t) n); dryR.resize((size_t) n); }
        float pin = 0, pout = 0, pl = 0, pr = 0, pdeck = 0;
        for (int i = 0; i < n; ++i) pin = std::max(pin, std::max(std::fabs(inL[i]), std::fabs(inR ? inR[i] : inL[i])));
        for (int i = 0; i < n; ++i) {       // picture of the input, kept before the deck touches anything
            histAcc = std::max(histAcc, std::fabs(inL[i])); if (++histCnt >= histBin) { const unsigned w = histIdx.load(std::memory_order_relaxed); hist[w % kHist].store(histAcc, std::memory_order_relaxed); histIdx.store(w + 1, std::memory_order_release); histAcc = 0.f; histCnt = 0; }
        }
        // the DAW input straight, for the other side of the crossfader (copied now, outL may alias inL)
        for (int i = 0; i < n; ++i) { dryL[(size_t) i] = inL[i]; dryR[(size_t) i] = inR ? inR[i] : inL[i]; }
        deck.process(tmpL.data(), tmpR.data(), n, inL, inR);
        const float lo = p.killLo ? -60.f : p.eqLo, mid = p.killMid ? -60.f : p.eqMid, hi = p.killHi ? -60.f : p.eqHi;
        strip.setParams(lo, mid, hi, p.colour); dryStrip.setParams(lo, mid, hi, p.colour);
        const float sm = 1.f - std::exp(-1.f / (0.005f * (float) fs));
        // Deck on a loop or file: both sides of the crossfader are different sounds, mixed with the chosen curve.
        // Deck on the live DAW input (the ring): the deck is the same sound as the straight side, so the left half of the fader
        // is the deck alone (no doubling) and the right half cuts over to the straight DAW signal.
        float xa, xb;
        if (deck.isLiveInput()) { if (deck.inLiveMode()) { xa = 1.f; xb = 0.f; } else { const float t = std::max(0.f, std::min(1.f, p.xf)); xa = 1.f - t; xb = t; } }
        else xfGains(p.xf, p.xCurve, xa, xb);
        const float fader = std::max(0.f, std::min(1.25f, p.deckFader));
        for (int i = 0; i < n; ++i) {
            trimS += (dbToLin(p.trimDb) - trimS) * sm; deckS += (fader - deckS) * sm; gaS += (xa - gaS) * sm; gbS += (xb - gbS) * sm;
            const float g = deckS * gaS;
            float dl = strip.tick(0, tmpL[(size_t) i] * trimS) * g, dr = strip.tick(1, tmpR[(size_t) i] * trimS) * g;
            if (!std::isfinite(dl) || !std::isfinite(dr)) { strip.prepare(fs); dl = dr = 0.f; }   // never let one bad sample silence the output for good
            pdeck = std::max(pdeck, std::max(std::fabs(dl), std::fabs(dr)));
            float wl = 0.f, wr = 0.f;
            if (gbS > 1e-5f) {
                wl = dryStrip.tick(0, dryL[(size_t) i]) * gbS; wr = dryStrip.tick(1, dryR[(size_t) i]) * gbS;
                if (!std::isfinite(wl) || !std::isfinite(wr)) { dryStrip.prepare(fs); wl = wr = 0.f; }
            }
            tmpL[(size_t) i] = dl; tmpR[(size_t) i] = dr; dryL[(size_t) i] = wl; dryR[(size_t) i] = wr;
        }
        auto chain = [&](float* l, float* r) {
            for (int k = 0; k < 3; ++k) {
                if (fx[k].type != p.fxType[k]) { fx[k].type = p.fxType[k]; fx[k].prepare(fs); }
                fx[k].on = p.fxOn[k]; fx[k].p1 = p.fxP1[k]; fx[k].p2 = p.fxP2[k] * p.chainMix;
                fx[k].bpm = (float) std::max(30.0, p.hostBpm); fx[k].echoBeats = p.echoBeats;
                fx[k].process(l, r, n);
            }
        };
        if (p.fxTarget == 0) { chain(tmpL.data(), tmpR.data()); for (int i = 0; i < n; ++i) { outL[i] = tmpL[(size_t) i] + dryL[(size_t) i]; outR[i] = tmpR[(size_t) i] + dryR[(size_t) i]; } }
        else { for (int i = 0; i < n; ++i) { outL[i] = tmpL[(size_t) i] + dryL[(size_t) i]; outR[i] = tmpR[(size_t) i] + dryR[(size_t) i]; } chain(outL, outR); }
        if (p.purrOn && p.purrMix > 0.001f && (p.purrDrive > 0.001f || p.purrXs > 0.001f || p.purrFuzz > 0.001f)) {
            const float mk = purr::makeup(p.purrDrive, p.purrXs, p.purrFuzz, 0.35f);
            float* ch[2] = { outL, outR };
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) {      // 2x oversampled (linear up, average down)
                const float x = ch[c][i], m = 0.5f * (purrPrev[c] + x);
                const float y = 0.5f * (purr::shaper(m, p.purrDrive, p.purrXs, p.purrFuzz) + purr::shaper(x, p.purrDrive, p.purrXs, p.purrFuzz)) * mk;
                purrPrev[c] = x; ch[c][i] = x + (y - x) * p.purrMix;
            }
        }
        for (int i = 0; i < n; ++i) {
            masterS += (dbToLin(p.masterDb) - masterS) * sm;
            outL[i] = softClip(outL[i] * masterS); outR[i] = softClip(outR[i] * masterS);
            pl = std::max(pl, std::fabs(outL[i])); pr = std::max(pr, std::fabs(outR[i]));
        }
        pout = std::max(pl, pr);
        auto fall = [n, this](std::atomic<float>& a, float v) { const float old = a.load() * std::pow(0.2f, (float) n / (float) fs); a.store(std::max(old, v)); };
        fall(peakIn, pin); fall(peakOut, pout); fall(peakL, pl); fall(peakR, pr); fall(vuDeck, pdeck);
    }
    static float softClip(float x) {          // only acts above 0.95, normal levels stay bit exact
        const float a = std::fabs(x); if (a <= 0.95f) return x;
        const float s = x < 0 ? -1.f : 1.f; return s * (0.95f + 0.05f * std::tanh((a - 0.95f) / 0.05f));
    }
    ChannelStrip strip, dryStrip;
    FxSlot fx[3];
private:
    double fs = 44100;
    std::vector<float> tmpL, tmpR, dryL, dryR;
    float trimS = 1.f, masterS = 1.f, deckS = 1.f, gaS = 1.f, gbS = 0.f, purrPrev[2] = { 0.f, 0.f };
    float histAcc = 0.f; int histCnt = 0, histBin = 1500;
};
}
