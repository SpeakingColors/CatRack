// Scratch Cat FX: track analysis (BPM and beat grid, musical key, waveform peaks). Pure C++, runs on a background thread.
#pragma once
#include "ScfxDsp.h"
#include <string>

namespace scfx {

struct BpmResult { double bpm = 0; double firstBeat = 0; double confidence = 0; };   // firstBeat in seconds

// Mono, decimated to about 11 kHz by averaging.
inline std::vector<float> decimateMono(const float* l, const float* r, size_t n, double sr, double& outSr, size_t startFrame = 0, size_t maxFrames = 0) {
    const int f = std::max(1, (int) std::lround(sr / 11025.0));
    outSr = sr / f;
    size_t end = maxFrames ? std::min(n, startFrame + maxFrames) : n;
    std::vector<float> m;
    if (end <= startFrame) return m;
    m.reserve((end - startFrame) / f + 1);
    for (size_t i = startFrame; i + f <= end; i += f) {
        float a = 0; for (int k = 0; k < f; ++k) a += 0.5f * (l[i + k] + (r ? r[i + k] : l[i + k]));
        m.push_back(a / f);
    }
    return m;
}

// Spectral flux onset envelope (frames of `hop` samples).
inline std::vector<float> onsetEnvelope(const std::vector<float>& x, int win, int hop, double sr0, std::vector<float>* lowOut = nullptr) {
    std::vector<float> env, envLow;
    if (x.size() < (size_t) win * 2) return env;
    std::vector<float> w(win); for (int i = 0; i < win; ++i) w[i] = 0.5f - 0.5f * (float) std::cos(2 * kPi * i / win);
    std::vector<float> prev(win / 2, 0.f);
    std::vector<std::complex<float>> buf(win);
    for (size_t p = 0; p + win <= x.size(); p += hop) {
        for (int i = 0; i < win; ++i) buf[i] = { x[p + i] * w[i], 0.f };
        fft(buf);
        float flux = 0, fl = 0;
        const int kLow = std::max(2, (int)(200.0 * win / sr0)), kHigh = std::min(win / 2, (int)(5000.0 * win / sr0));
        for (int k = 1; k < win / 2; ++k) {
            const float m = std::log1p(100.f * std::abs(buf[k]) / (float) win * 4.f);
            const float d = m - prev[k]; prev[k] = m;
            if (d > 0 && k < kHigh) { if (k < kLow) { fl += d; flux += 3.f * d; } else flux += d; }
        }
        env.push_back(flux); envLow.push_back(fl);
    }
    // remove slow mean so only the peaks count
    auto detrend = [](const std::vector<float>& e) {
        const int ml = 16; std::vector<float> o(e.size());
        for (size_t i = 0; i < e.size(); ++i) {
            float s = 0; int c = 0;
            for (int k = -ml; k <= ml; ++k) { long j = (long) i + k; if (j >= 0 && j < (long) e.size()) { s += e[j]; ++c; } }
            o[i] = std::max(0.f, e[i] - s / c);
        }
        return o; };
    if (lowOut) *lowOut = detrend(envLow);
    return detrend(env);
}

inline float interp(const std::vector<float>& e, double p) {
    if (p < 0 || p >= (double) e.size() - 1) return 0.f;
    size_t i = (size_t) p; float f = (float)(p - (double) i); return e[i] * (1 - f) + e[i + 1] * f;
}

// BPM between 70 and 180 (octave folded), refined to about 0.05 BPM, with the position of a beat.
inline BpmResult analyseBpm(const float* l, const float* r, size_t n, double sr) {
    BpmResult res;
    double dsr; const size_t startF = n > (size_t)(sr * 90) ? (size_t)(sr * 15) : 0;   // skip an intro on long tracks
    auto m = decimateMono(l, r, n, sr, dsr, startF, (size_t)(sr * 75));
    const int win = 1024, hop = 128; const double fr = dsr / hop;            // envelope frames per second
    std::vector<float> low;
    auto envFull = onsetEnvelope(m, win, hop, dsr, &low);
    if (envFull.size() < (size_t)(fr * 8)) return res;
    auto& env = envFull;
    // for the grid (fine tempo and where the beats fall) the low band counts most: the kick is the grid in dance music
    auto meanOf = [](const std::vector<float>& v) { double a = 0; for (float x : v) a += x; return v.empty() ? 1.0 : std::max(1e-9, a / (double) v.size()); };
    std::vector<float> grid(env.size());
    { const double mf = meanOf(envFull), ml2 = meanOf(low); for (size_t i = 0; i < grid.size(); ++i) grid[i] = (float)(low[i] / ml2 + 0.15 * envFull[i] / mf); }
    // coarse: autocorrelation over lags for 60..200 BPM
    const int lagLo = (int) std::floor(fr * 60.0 / 200.0), lagHi = (int) std::ceil(fr * 60.0 / 60.0);
    std::vector<double> ac(lagHi + 2, 0.0);
    for (int lag = lagLo; lag <= lagHi; ++lag) {
        double s = 0; for (size_t i = 0; i + lag < env.size(); ++i) s += (double) env[i] * env[i + lag];
        ac[lag] = s / (double)(env.size() - lag);
    }
    double bestScore = -1, bestBpm = 0;
    for (int lag = lagLo + 1; lag < lagHi; ++lag) {
        if (!(ac[lag] > ac[lag - 1] && ac[lag] >= ac[lag + 1])) continue;
        // parabolic peak
        const double a = ac[lag - 1], b = ac[lag], c = ac[lag + 1], den = a - 2 * b + c, off = den != 0 ? 0.5 * (a - c) / den : 0;
        const double bpm = 60.0 * fr / (lag + off);
        // prior: log-gaussian around 125 BPM, and add support from the double-period peak
        const double pr = std::exp(-0.5 * std::pow(std::log2(bpm / 125.0) / 0.6, 2));
        const int l2 = lag * 2; const double sup = l2 <= lagHi ? ac[l2] : 0;
        const double score = (b + 0.5 * sup) * pr;
        if (score > bestScore) { bestScore = score; bestBpm = bpm; }
    }
    if (bestBpm <= 0) return res;
    while (bestBpm < 70) bestBpm *= 2;
    while (bestBpm >= 180) bestBpm /= 2;
    // refine with a comb over +-3 BPM, 0.05 steps, 48 phases
    auto comb = [&](double bpm, double& phaseOut) {
        const double per = fr * 60.0 / bpm; double best = -1, bp = 0;
        for (int ph = 0; ph < 48; ++ph) {
            const double p0 = per * ph / 48.0; double s = 0;
            for (double t = p0; t < (double) grid.size() - 2; t += per) s += interp(grid, t);
            if (s > best) { best = s; bp = p0; }
        }
        phaseOut = bp; return best / std::max(1.0, (double) grid.size() / per);
    };
    double bestC = -1, refined = bestBpm, ph = 0;
    for (double b = bestBpm - 3.0; b <= bestBpm + 3.0; b += 0.05) { double p; const double c = comb(b, p); if (c > bestC) { bestC = c; refined = b; ph = p; } }
    // check the octave neighbours too (a half or double tempo can fit the same grid better)
    res.bpm = std::round(refined * 20.0) / 20.0;
    double mean = 0; for (float v : grid) mean += v; mean /= (double) grid.size();
    res.confidence = mean > 0 ? std::min(1.0, bestC / (mean * 6.0)) : 0;
    // time of the first beat in the whole track: phase measured in the analysed window, shifted back to the start
    const double perS = 60.0 / res.bpm, wStart = (double) startF / sr, tw = ph / fr + (double) win / dsr * 0.5;
    double t0 = wStart + tw; t0 = std::fmod(t0, perS); if (t0 < 0) t0 += perS;
    res.firstBeat = t0;
    return res;
}

// ---- key ----
struct KeyResult { int tonic = -1; bool minor = false; double score = 0; };   // tonic 0 = C ... 11 = B
inline KeyResult analyseKey(const float* l, const float* r, size_t n, double sr) {
    KeyResult kr;
    double dsr; const size_t startF = n > (size_t)(sr * 90) ? (size_t)(sr * 15) : 0;
    auto m = decimateMono(l, r, n, sr, dsr, startF, (size_t)(sr * 60));
    const int win = 8192, hop = 4096;
    if (m.size() < (size_t) win) return kr;
    std::vector<float> w(win); for (int i = 0; i < win; ++i) w[i] = 0.5f - 0.5f * (float) std::cos(2 * kPi * i / win);
    double chroma[12] = {}; std::vector<std::complex<float>> buf(win);
    for (size_t p = 0; p + win <= m.size(); p += hop) {
        for (int i = 0; i < win; ++i) buf[i] = { m[p + i] * w[i], 0.f };
        fft(buf);
        for (int k = 1; k < win / 2; ++k) {
            const double f = k * dsr / win; if (f < 100.0 || f > 2000.0) continue;
            const double mag = std::abs(buf[k]); if (mag <= 0) continue;
            const double midi = 69.0 + 12.0 * std::log2(f / 440.0);
            const int pc = ((int) std::lround(midi) % 12 + 12) % 12;
            if (std::fabs(midi - std::round(midi)) > 0.35) continue;      // only bins close to a note centre
            chroma[pc] += std::sqrt(mag);
        }
    }
    // Krumhansl-Kessler probe tone profiles
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&](const double* prof, int tonic) {
        double mc = 0, mp = 0; for (int i = 0; i < 12; ++i) { mc += chroma[i]; mp += prof[i]; } mc /= 12; mp /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) { const double a = chroma[(i + tonic) % 12] - mc, b = prof[i] - mp; sxy += a * b; sxx += a * a; syy += b * b; }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };
    double best = -2;
    for (int t = 0; t < 12; ++t) for (int mi = 0; mi < 2; ++mi) {
        const double c = corr(mi ? mnr : maj, t);
        if (c > best) { best = c; kr.tonic = t; kr.minor = mi == 1; kr.score = c; }
    }
    return kr;
}

// Camelot wheel: 8B = C major, 8A = A minor.
inline std::string camelot(const KeyResult& k) {
    if (k.tonic < 0) return "";
    static const int majNum[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };      // C Db D Eb E F F# G Ab A Bb B
    static const int minNum[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };      // Cm C#m Dm D#m Em Fm F#m Gm G#m Am Bbm Bm
    return std::to_string(k.minor ? minNum[k.tonic] : majNum[k.tonic]) + (k.minor ? "A" : "B");
}
inline std::string keyName(const KeyResult& k) {
    if (k.tonic < 0) return "";
    static const char* n[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return std::string(n[k.tonic]) + (k.minor ? "m" : "");
}
// Two keys mix well on the Camelot wheel when the number differs by at most one with the same letter, or the number is equal with the other letter.
inline bool keysCompatible(const KeyResult& a, const KeyResult& b) {
    if (a.tonic < 0 || b.tonic < 0) return false;
    auto ca = camelot(a), cb = camelot(b);
    int na = std::stoi(ca), nb = std::stoi(cb); const bool sameLetter = ca.back() == cb.back();
    int d = std::abs(na - nb); d = std::min(d, 12 - d);
    return sameLetter ? d <= 1 : d == 0;
}

// Overview peaks for drawing: `bins` pairs of (min, max) over the whole track.
inline std::vector<float> makePeaks(const float* l, const float* r, size_t n, int bins) {
    std::vector<float> p((size_t) bins * 2, 0.f);
    if (n == 0 || bins <= 0) return p;
    for (int b = 0; b < bins; ++b) {
        const size_t a = (size_t)((double) n * b / bins), e = std::max(a + 1, (size_t)((double) n * (b + 1) / bins));
        float mn = 0, mx = 0;
        const size_t step = std::max<size_t>(1, (e - a) / 64);
        for (size_t i = a; i < e && i < n; i += step) { float v = 0.5f * (l[i] + (r ? r[i] : l[i])); mn = std::min(mn, v); mx = std::max(mx, v); }
        p[(size_t) b * 2] = mn; p[(size_t) b * 2 + 1] = mx;
    }
    return p;
}
}
