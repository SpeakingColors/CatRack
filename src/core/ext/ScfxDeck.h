// Scratch Cat FX: one deck on the DAW input. Records the incoming audio and lets you scratch it with vinyl style speed
// changes, brake, backspin and the scratch moves. Core copied from scratchcatduo-vst (same author, same licence). Pure C++.
#pragma once
#include "ScfxAnalysis.h"
#include <memory>
#include <atomic>
#include <array>
#include <string>

namespace scfx {

struct Track {
    std::vector<float> l, r;
    double sr = 44100;
    double bpm = 0, firstBeat = 0;       // firstBeat in seconds
    KeyResult key;
    std::string name;
    std::vector<float> peaks;            // min,max pairs for the overview
    size_t frames() const { return l.size(); }
};

enum class Move { None, Roll, Censor, Stutter, Transform };

class Deck {
public:
    static constexpr double kPlatterRevPerSec = 100.0 / 3.0 / 60.0;   // 33 1/3 rpm is normal speed

    void prepare(double hostSr) {
        fs = hostSr; ringSize = (size_t)(hostSr * 20.0); ringL.assign(ringSize, 0.f); ringR.assign(ringSize, 0.f); writeIdx = 0; headA.store(0);
        vel = 0; for (auto& g : grains) g.on = false; hopCnt = 0; gateG = 1.f;
    }
    // Returns the track that was loaded before, so the caller can free it off the audio thread.
    std::shared_ptr<const Track> setTrack(std::shared_ptr<const Track> t) {
        auto old = std::move(trk); trk = std::move(t); pos = 0; vel = 0; playing = false; cuePoint = 0; loopOn = false; hot.fill(-1.0); mode = Mode::Normal; move = Move::None; live = false;
        if (trk) { src = Source::File; if (trk->bpm > 0) { /* first cue at the first beat */ cuePoint = trk->firstBeat * trk->sr; pos = cuePoint; } }
        return old;
    }
    void useLiveInput(bool b) { if (b) { src = Source::Live; live = true; playing = false; vel = 0; pos = (double) writeIdx; } else { src = Source::File; live = false; } }
    bool isLiveInput() const { return src == Source::Live; }
    bool inLiveMode() const { return src == Source::Live && live; }
    void goLive() { if (src == Source::Live) { live = true; playing = false; vel = 0; pos = (double) writeIdx; } }

    // ---- controls (audio thread, or tests) ----
    void setPlaying(bool p) { if (!hasAudio()) return; if (src == Source::Live && live && p) return; playing = p; if (p) mode = Mode::Normal; cueHeld = false; }
    void togglePlay() { setPlaying(!playing); }
    bool isPlaying() const { return playing; }
    void setPitch(float fader, float range) { pitch = fader; pitchRange = range; }     // fader -1..1
    void setKeyLock(bool b) { keylock = b; }
    void setQuant(bool b) { quant = b; }
    void setSync(bool on, double masterBpm) { sync = on; masterBpmV = masterBpm; }
    bool isSyncOn() const { return sync; }
    void setBend(float b) { bend = clampf(b, -1.f, 1.f); }
    void setVinylTime(float sec) { vinylTc = std::max(0.005f, sec); }
    // jog: while touching, speed follows the hand. velocity in platter turns per second (positive = forward).
    void setJog(bool touching, double turnsPerSec) {
        if (touching && !jogTouch && src == Source::Live && live) { live = false; pos = (double) writeIdx - 1.0; playing = false; }
        jogTouch = touching; jogVel = turnsPerSec / kPlatterRevPerSec; if (touching) mode = Mode::Normal;
    }
    bool touching() const { return jogTouch; }
    // tempo factor actually in use (1 = original)
    double tempo() const {
        double t = 1.0 + (double) pitch * (double) pitchRange;
        if (sync && trk && trk->bpm > 0 && masterBpmV > 0) { t = masterBpmV / trk->bpm; if (t > 1.5) t *= 0.5; else if (t < 0.67) t *= 2.0; t = std::min(1.5, std::max(0.5, t)); }
        return t;
    }
    double currentBpm() const { return trk && trk->bpm > 0 ? trk->bpm * tempo() : 0.0; }
    double beatLenSrc() const { return trk && trk->bpm > 0 ? trk->sr * 60.0 / trk->bpm : 0.0; }
    double snap(double p) const {
        const double bl = beatLenSrc(); if (!quant || bl <= 0) return p;
        const double g0 = trk->firstBeat * trk->sr; return g0 + std::round((p - g0) / bl) * bl;
    }
    void cue(bool down) {
        if (!hasAudio() || src == Source::Live) return;
        if (down) {
            if (playing) { pos = cuePoint; playing = false; vel = 0; }
            else if (std::fabs(pos - cuePoint) < 2.0 * trk->sr / 1000.0 * 5.0) { playing = true; cueHeld = true; }    // at the cue point: preview while held
            else { cuePoint = snap(pos); pos = cuePoint; }
        } else if (cueHeld) { cueHeld = false; playing = false; pos = cuePoint; vel = 0; }
    }
    void hotCue(int i, bool down) {
        if (!down || i < 0 || i >= 8 || !hasAudio() || src == Source::Live) return;
        if (hot[i] < 0) hot[i] = snap(pos); else { pos = hot[i]; if (!playing) setPlaying(true); }
    }
    void setHotCuePos(int i, double p) { if (i >= 0 && i < 8) hot[i] = p; }
    void clearHotCue(int i) { if (i >= 0 && i < 8) hot[i] = -1.0; }
    double hotCuePos(int i) const { return hot[i]; }
    void loopIn() { loopA = snap(pos); loopB = -1; loopOn = false; }
    void loopOut() { if (loopA >= 0 && pos > loopA + 64) { loopB = snap(pos); if (loopB <= loopA) loopB = pos; loopOn = true; } }
    void loopExit() { loopOn = false; }
    void beatLoop(double beats) {
        const double bl = beatLenSrc(); if (bl <= 0) return;
        loopA = snap(pos); loopB = loopA + bl * beats; loopOn = true;
    }
    void brake() { if (playing && !jogTouch) mode = Mode::Brake; }
    void backspin() { if (!jogTouch && hasAudio()) { mode = Mode::Backspin; spinV = -9.0; playing = true; } }
    // Moves held with a pad: roll length in beats applies to Roll and Stutter.
    void startMove(Move m, double beats = 0.25) {
        if (!hasAudio() || src == Source::Live) return;
        endMove(); move = m; moveBeats = beats; shadow = pos;
        if (m == Move::Roll) { const double bl = std::max(beatLenSrc(), trk->sr * 0.25); loopA = pos; loopB = pos + bl * beats; savedLoopOn = loopOn; loopOn = true; }
        if (m == Move::Stutter) { stutCnt = 0; if (!playing) setPlaying(true); }
    }
    void endMove() {
        if (move == Move::Roll) { loopOn = savedLoopOn; pos = shadow; }
        else if (move == Move::Censor) { pos = shadow; }
        move = Move::None; gateG = 1.f;
    }

    // ---- state for the UI ----
    // The last `seconds` of the DAW input, copied into a track (message thread safe: only reads samples older than the current block).
    // Returns the number of frames copied (0 if nothing recorded yet). Caller must not run this during prepare().
    size_t grabLast(double seconds, Track& t, unsigned long long endHead = 0) const {
        const unsigned long long latest = headA.load(std::memory_order_acquire);
        const unsigned long long head = endHead > 0 && endHead <= latest && latest - endHead < ringSize / 2 ? endHead : latest;
        size_t want = (size_t) std::max(0.0, seconds * fs);
        want = std::min<size_t>(want, ringSize > 8192 ? ringSize - 8192 : 0);
        if (head < want) want = (size_t) head;
        if (want < 1024 || ringSize == 0) return 0;
        t.l.resize(want); t.r.resize(want);
        for (size_t i = 0; i < want; ++i) { const size_t k = (size_t) ((head - want + i) % ringSize); t.l[i] = ringL[k]; t.r[i] = ringR[k]; }
        t.sr = fs; return want;
    }
    unsigned long long headFrames() const { return headA.load(std::memory_order_acquire); }
    void setTrackLoop(bool b) { loopTrackOn = b; }
    // Live source only: how far behind "now" the read head is, in seconds (0 while live).
    double behindSeconds() const { return src == Source::Live ? std::max(0.0, ((double) writeIdx - pos) / fs) : 0.0; }
    double positionSeconds() const { return src == Source::File && trk ? pos / trk->sr : (double) pos / fs; }
    double lengthSeconds() const { return trk ? (double) trk->frames() / trk->sr : 0.0; }
    double speed() const { return vel; }
    const std::shared_ptr<const Track>& track() const { return trk; }
    double cueSeconds() const { return trk ? cuePoint / trk->sr : 0.0; }
    bool loopActive() const { return loopOn; }
    double loopStartSec() const { return trk && loopA >= 0 ? loopA / trk->sr : -1; }
    double loopEndSec() const { return trk && loopB >= 0 ? loopB / trk->sr : -1; }
    double beatPhase() const {            // 0..1 position inside the current beat, -1 if there is no grid
        const double bl = beatLenSrc(); if (bl <= 0 || src != Source::File) return -1;
        double p = (pos - trk->firstBeat * trk->sr) / bl; return p - std::floor(p);
    }
    // Move this deck by the shortest way (at most half a beat) so its beat lands where the other deck's beat is.
    // Returns false if either deck has no beat grid. The tempo is not touched: sync does that.
    bool alignBeatTo(const Deck& o) {
        const double a = beatPhase(), b = o.beatPhase(); if (a < 0 || b < 0) return false;
        double d = b - a; if (d > 0.5) d -= 1.0; else if (d < -0.5) d += 1.0;
        pos = std::max(0.0, std::min((double) trk->frames() - 1.0, pos + d * beatLenSrc())); return true;
    }
    void seekSeconds(double s) { if (trk && src == Source::File) pos = std::max(0.0, std::min((double) trk->frames() - 1.0, s * trk->sr)); }

    // Render n frames. in may be null. Output is added into nothing: it overwrites outL/outR.
    void process(float* outL, float* outR, int n, const float* inL, const float* inR) {
        const double srcRatio = (src == Source::File && trk) ? trk->sr / fs : 1.0;
        // keep the live input in the ring
        for (int i = 0; i < n; ++i) { ringL[writeIdx % ringSize] = inL ? inL[i] : 0.f; ringR[writeIdx % ringSize] = inR ? inR[i] : (inL ? inL[i] : 0.f); ++writeIdx; }   // always recording, so Capture works from any source
        headA.store((unsigned long long) writeIdx, std::memory_order_release);
        if (src == Source::Live && live) {
            for (int i = 0; i < n; ++i) { outL[i] = inL ? inL[i] : 0.f; outR[i] = inR ? inR[i] : (inL ? inL[i] : 0.f); }
            pos = (double) writeIdx; return;
        }
        if (!hasAudio()) { std::fill(outL, outL + n, 0.f); std::fill(outR, outR + n, 0.f); return; }
        const double t = tempo();
        const double bl = beatLenSrc();
        const float cFast = 1.f - std::exp(-1.f / (0.002f * (float) fs)), cVinyl = 1.f - std::exp(-1.f / (vinylTc * (float) fs));
        const bool useGrains = keylock && src == Source::File && std::fabs(t - 1.0) > 0.002;
        const int glen = 2048, ghop = glen / 4;
        for (int i = 0; i < n; ++i) {
            double target; float c = cVinyl;
            if (jogTouch) { target = jogVel; c = cFast; }
            else if (mode == Mode::Brake) { target = 0; c = 1.f - std::exp(-1.f / (0.35f * (float) fs)); if (std::fabs(vel) < 0.01) { playing = false; mode = Mode::Normal; vel = 0; } }
            else if (mode == Mode::Backspin) { spinV *= (1.0 - 2.2 / fs * 1.0); target = spinV; c = cFast * 4.f; if (spinV > -0.05) { mode = Mode::Normal; playing = false; vel = 0; spinV = 0; } }
            else if (move == Move::Censor) { target = -t; c = cFast; }
            else if (playing) { target = t * (1.0 + 0.08 * (double) bend); }
            else target = 0;
            vel += (target - vel) * c;
            if (!playing && !jogTouch && mode == Mode::Normal && move != Move::Censor && std::fabs(vel) < 1e-4) vel = 0;
            // sample
            float l = 0, r = 0;
            if (vel != 0.0 || jogTouch) {
                if (useGrains && vel > 0) {
                    if (hopCnt-- <= 0) { hopCnt = ghop; for (auto& g : grains) if (!g.on) { g.on = true; g.p = wsolaStart(pos, srcRatio); g.age = 0; break; } }
                    float al = 0, ar = 0;
                    for (auto& g : grains) if (g.on) {
                        const float w = 0.5f - 0.5f * std::cos(2.f * (float) kPi * (float) g.age / (float) glen);
                        float a, b; readAt(g.p, a, b); al += a * w; ar += b * w;
                        g.p += srcRatio; if (++g.age >= glen) g.on = false;
                    }
                    l = al * 0.5f; r = ar * 0.5f;
                } else readAt(pos, l, r);
            }
            // advance
            pos += vel * srcRatio;
            if (move == Move::Roll || move == Move::Censor) shadow += (move == Move::Roll ? t : t) * srcRatio * (playing ? 1.0 : 0.0);
            if (loopOn && loopB > loopA) { const double len = loopB - loopA; if (vel > 0 && pos >= loopB) pos -= len * std::max(1.0, std::floor((pos - loopA) / len)); else if (vel < 0 && pos < loopA) pos += len; }
            if (move == Move::Stutter && bl > 0 && ++stutCnt >= (int)(bl * moveBeats / srcRatio / std::max(0.1, t))) { stutCnt = 0; pos = cuePoint; }
            if (move == Move::Transform && bl > 0) { const double ph = std::fmod((pos - trk->firstBeat * trk->sr) / (bl * 0.25), 2.0); const float g = ph < 1.0 ? 1.f : 0.f; gateG += (g - gateG) * 0.01f; l *= gateG; r *= gateG; }
            if (src == Source::File) {
                const double nf = (double) trk->frames();
                if (loopTrackOn) {      // captured loops and files repeat, like the mockup
                    if (pos >= nf - 1) pos -= nf - 1; else if (pos < 0) pos += nf - 1;
                } else {
                    if (pos >= nf - 1) { pos = nf - 1; playing = false; vel = 0; }
                    if (pos < 0) { pos = 0; if (vel < 0) vel = 0; }
                }
            } else {
                if (pos > (double) writeIdx - 2) pos = (double) writeIdx - 2;
                const double oldest = (double) writeIdx - (double) ringSize + 4.0; if (pos < oldest) pos = oldest;
            }
            outL[i] = l; outR[i] = r;
        }
    }

private:
    enum class Source { File, Live };
    enum class Mode { Normal, Brake, Backspin };
    struct Grain { bool on = false; double p = 0; int age = 0; };
    bool hasAudio() const { return src == Source::Live || (trk && trk->frames() > 4); }
    // 4 point Hermite, out of range reads as silence
    // WSOLA: nudge a new grain's start (within +-kSearch samples) so it lines up in phase with where the youngest running grain
    // is about to continue; this removes the comb and flutter of plain overlap-add. Only used for file playback.
    static constexpr int kSearch = 288, kCorr = 640, kStep = 4;
    double wsolaStart(double want, double ratio) const {
        const Grain* ref = nullptr;
        for (const auto& g : grains) if (g.on && (!ref || g.age < ref->age)) ref = &g;
        if (!ref || src != Source::File || !trk) return want;
        const long long nf = (long long) trk->frames();
        auto x = [&](double p) -> float { const long long i = (long long) p; return (i < 0 || i >= nf) ? 0.f : trk->l[(size_t) i]; };
        float refv[kCorr / kStep]; double refE = 0;
        for (int k = 0; k < kCorr / kStep; ++k) { refv[k] = x(ref->p + (double) (k * kStep) * ratio); refE += (double) refv[k] * refv[k]; }
        if (refE < 1e-9) return want;
        double best = -1e30; int bestD = 0;
        for (int d = -kSearch; d <= kSearch; d += 2) {
            double c = 0, e = 1e-9;
            for (int k = 0; k < kCorr / kStep; ++k) { const float v = x(want + (double) d * ratio + (double) (k * kStep) * ratio); c += (double) v * refv[k]; e += (double) v * v; }
            const double score = c / std::sqrt(e);
            if (score > best) { best = score; bestD = d; }
        }
        return std::max(0.0, want + (double) bestD * ratio);
    }
    void readAt(double p, float& l, float& r) const {
        const long long i = (long long) std::floor(p); const float f = (float)(p - (double) i);
        float a[4], b[4];
        for (int k = 0; k < 4; ++k) {
            const long long j = i - 1 + k;
            if (src == Source::File) { if (j < 0 || j >= (long long) trk->frames()) { a[k] = b[k] = 0; } else { a[k] = trk->l[(size_t) j]; b[k] = trk->r.empty() ? a[k] : trk->r[(size_t) j]; } }
            else { if (j < 0 || j >= (long long) writeIdx || (long long) writeIdx - j > (long long) ringSize) { a[k] = b[k] = 0; } else { a[k] = ringL[(size_t) j % ringSize]; b[k] = ringR[(size_t) j % ringSize]; } }
        }
        auto h = [f](const float* y) { const float c0 = y[1], c1 = 0.5f * (y[2] - y[0]), c2 = y[0] - 2.5f * y[1] + 2.f * y[2] - 0.5f * y[3], c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]); return ((c3 * f + c2) * f + c1) * f + c0; };
        l = h(a); r = h(b);
    }

    double fs = 44100;
    std::shared_ptr<const Track> trk;
    Source src = Source::File; bool live = false;
    std::vector<float> ringL, ringR; size_t ringSize = 0; unsigned long long writeIdx = 0;
    std::atomic<unsigned long long> headA { 0 };
    bool loopTrackOn = true;
    double pos = 0, vel = 0;
    bool playing = false, cueHeld = false, keylock = false, quant = true, sync = false;
    float pitch = 0.f, pitchRange = 0.08f, bend = 0.f, vinylTc = 0.06f;
    double masterBpmV = 0;
    bool jogTouch = false; double jogVel = 0;
    double cuePoint = 0; std::array<double, 8> hot = { -1, -1, -1, -1, -1, -1, -1, -1 };
    double loopA = -1, loopB = -1; bool loopOn = false, savedLoopOn = false;
    Mode mode = Mode::Normal; double spinV = 0;
    Move move = Move::None; double moveBeats = 0.25, shadow = 0; int stutCnt = 0; float gateG = 1.f;
    std::array<Grain, 6> grains; int hopCnt = 0;
};
}
