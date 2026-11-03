// CatRack core tests: no JUCE needed. Run with: sh tests/run.sh
// What is MEASURED here: stability and bounded output of every hosted unit at extreme settings, exact bypass, the
// reported latency against the real one, parallel lanes lining up, state text round trip and rejection of bad text,
// knob to sound time, aliasing and level match of the three colour stages.
// What is NOT measured: how any unit sounds, CPU in a host, behaviour in a real DAW.
#include "../src/core/Rack.h"
#include <cstdio>
#include <random>
#include <string>

using namespace catr;
static int fails = 0, checks = 0;
#define CHECK(c, msg) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); } } while (0)

static Ctl defaultCtl (const Structure& s)
{
    Ctl c;
    for (int i = 0; i < kMaxUnits; ++i)
    {
        if (s.slotType[i] < 0) continue;
        const auto& d = unitTable()[s.slotType[i]];
        for (int k = 0; k < d.nParams; ++k) c.slot[i].p[k] = d.p[k].def;
    }
    return c;
}
static Structure chainOf (std::initializer_list<std::initializer_list<int>> rows)
{
    Structure s; int slot = 0;
    for (auto& r : rows) { Stage& g = s.st[s.nStages++]; for (int t : r) { s.slotType[slot] = t; g.slot[g.n++] = slot; ++slot; } }
    return s;
}
static void fillSine (std::vector<float>& l, std::vector<float>& r, double fs, double f, float a)
{
    for (size_t i = 0; i < l.size(); ++i) { l[i] = a * (float) std::sin (2 * kPi * f * (double) i / fs); r[i] = l[i] * 0.9f; }
}

static void testUnitsStable()
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> U (0.0f, 1.0f);
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int t = 0; t < kNumUnits; ++t)
        {
            if (! hosted (t)) continue;
            auto u = makeUnit (t);
            u->prepare (fs, kChunk);
            const auto& d = unitTable()[t];
            bool ok = true; float peak = 0;
            for (int blk = 0; blk < 120; ++blk)
            {
                float v[kMaxPd] {};
                for (int k = 0; k < d.nParams; ++k) v[k] = fromNorm (d.p[k], blk < 3 ? (blk == 0 ? 0.0f : 1.0f) : U (rng));
                u->setParams (v);
                float l[kChunk], r[kChunk];
                for (int i = 0; i < kChunk; ++i) { const float x = blk % 40 < 3 ? 0.0f : (blk % 7 == 0 ? 1.0f : 0.5f) * (2 * U (rng) - 1); l[i] = x; r[i] = -x * 0.8f; }
                u->process (l, r, kChunk);
                for (int i = 0; i < kChunk; ++i) { if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) ok = false; peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (r[i]))); }
            }
            char m[160]; std::snprintf (m, sizeof m, "unit %s finite and bounded at %.0f Hz (peak %.1f)", d.name, fs, peak);
            CHECK (ok && peak < 200.0f, m);
        }
}

static void testBypassAndOff()
{
    const double fs = 48000;
    Structure s = chainOf ({ { U_PAW }, { U_PUNCH }, { U_TMS, U_FET }, { U_ECH } });
    Rack rack; rack.prepare (fs, 512);
    Structure got = rack.applyStructure (s);
    const int lat = got.totalLat;
    std::printf ("  chain latency (paw + punch + tms/fet + echo): %d samples\n", lat);
    std::vector<float> l (48000), r (48000);
    std::mt19937 rng (3); std::uniform_real_distribution<float> U (-0.5f, 0.5f);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = U (rng); r[i] = U (rng); }
    const auto l0 = l, r0 = r;
    Ctl c = defaultCtl (got);
    c.bypass = true;
    // run a while with bypass off first so the crossfade is exercised, then bypass on
    Ctl c2 = c; c2.bypass = false;
    std::vector<float> a (l), b (r);
    rack.process (a.data(), b.data(), 24000, c2);
    rack.process (a.data() + 24000, b.data() + 24000, 24000, c);
    // after the 10 ms fade the output must be the input delayed by lat, bit exact
    bool exact = true;
    for (int i = 24000 + 600; i < 48000; ++i) if (a[(size_t) i] != l0[(size_t) (i - lat)] || b[(size_t) i] != r0[(size_t) (i - lat)]) { exact = false; break; }
    CHECK (exact, "bypass output is the input delayed by the reported latency, bit exact");
    // all units off: also the same
    Rack rack2; rack2.prepare (fs, 512); rack2.applyStructure (s);
    Ctl off = defaultCtl (got);
    for (auto& sl : off.slot) sl.on = false;
    std::vector<float> a2 (l0), b2 (r0);
    rack2.process (a2.data(), b2.data(), 48000, off);
    bool exact2 = true;
    for (int i = lat + 20; i < 48000; ++i) if (a2[(size_t) i] != l0[(size_t) (i - lat)] || b2[(size_t) i] != r0[(size_t) (i - lat)]) { exact2 = false; break; }
    CHECK (exact2, "every unit off: output is the input delayed by the reported latency, bit exact");
}

static int peakIndex (const std::vector<float>& x) { int bi = 0; float bv = 0; for (size_t i = 0; i < x.size(); ++i) if (std::fabs (x[i]) > bv) { bv = std::fabs (x[i]); bi = (int) i; } return bi; }

static void testLatencyMeasured()
{
    const double fs = 48000;
    // linear phase / pure delay units at low level: the click must arrive at the reported latency
    Structure s = chainOf ({ { U_PUNCH }, { U_LAP }, { U_FET }, { U_TMS } });
    Rack rack; rack.prepare (fs, 512);
    Structure got = rack.applyStructure (s);
    Ctl c = defaultCtl (got);
    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    l[100] = 0.002f; r[100] = 0.002f;
    rack.process (l.data(), r.data(), 4096, c);
    const int pk = peakIndex (l);
    std::printf ("  impulse peak at %d, reported latency %d (click at 100)\n", pk - 100, got.totalLat);
    CHECK (std::abs ((pk - 100) - got.totalLat) <= 2, "reported latency equals the measured delay of a quiet click");
    // a pair: lanes with different latency must line up (punch has 32, clw has 0)
    Structure p = chainOf ({ { U_PUNCH, U_CLW } });
    Rack r2; r2.prepare (fs, 512);
    Structure g2 = r2.applyStructure (p);
    Ctl c2 = defaultCtl (g2);
    c2.slot[0].lane = 0; c2.slot[1].lane = 0; c2.stageMix[0] = 100;
    std::vector<float> a (4096, 0.0f), b (4096, 0.0f);
    a[200] = 0.002f; b[200] = 0.002f;
    r2.process (a.data(), b.data(), 4096, c2);
    int n = 0; for (size_t i = 0; i < a.size(); ++i) if (std::fabs (a[i]) > 0.0008f) ++n;
    std::printf ("  pair latency %d, samples above threshold %d\n", g2.totalLat, n);
    CHECK (g2.totalLat == pp::Compressor::kLatency, "a pair reports the larger lane latency");
    CHECK (n >= 1 && n <= 3 && std::abs (peakIndex (a) - 200 - g2.totalLat) <= 2, "both lanes of a pair arrive at the same time (one clean click, no echo)");
}

static void testStateText()
{
    Structure s = chainOf ({ { U_PAW }, { U_PUNCH, U_PURR }, { U_PFZ } });
    const std::string t = s.toText();
    Structure back;
    CHECK (Structure::fromText (t, back) && back.toText() == t, "structure text round trip");
    Structure x;
    CHECK (! Structure::fromText ("CR1:0.99", x), "unknown unit type refused");
    CHECK (! Structure::fromText ("CR1:0.3+1.3+2.3", x), "three units in one row refused");
    CHECK (! Structure::fromText ("CR1:0.3|0.4", x), "same slot twice refused");
    CHECK (! Structure::fromText ("CR1:13.3", x), "slot out of range refused");
    CHECK (! Structure::fromText ("junk", x), "wrong header refused");
    CHECK (Structure::fromText ("CR1:", x) && x.nStages == 0, "empty rack accepted");
    Structure big; int slot = 0;
    for (int i = 0; i < 13; ++i) { big.st[big.nStages].n = 1; big.st[big.nStages].slot[0] = slot % 12; big.slotType[slot % 12] = 0; ++slot; if (big.nStages < 11) ++big.nStages; }
    std::string many = "CR1:"; for (int i = 0; i < 13; ++i) { if (i) many += "|"; many += std::to_string (i % 12) + ".0"; }
    CHECK (! Structure::fromText (many, x), "more than twelve units refused");
    const auto& d = unitTable()[U_PURR].p[0];
    CHECK (std::fabs (fromNorm (d, toNorm (d, 5.5f)) - 5.5f) < 0.11f, "control value to 0..1 and back");
}

static void testResponsiveness()
{
    const double fs = 48000;
    Structure s = chainOf ({ { U_GAN } });
    Rack rack; rack.prepare (fs, 512);
    Structure got = rack.applyStructure (s);
    Ctl c = defaultCtl (got);
    std::vector<float> l (4800, 0.5f), r (4800, 0.5f);
    rack.process (l.data(), r.data(), 4800, c);                     // settle
    c.slot[0].p[0] = -12.0f;                                         // Gain -12 dB (target 0.126 of 0.5)
    std::fill (l.begin(), l.end(), 0.5f); std::fill (r.begin(), r.end(), 0.5f);
    rack.process (l.data(), r.data(), 4800, c);
    int first = -1, t63 = -1;
    for (int i = 0; i < 4800; ++i)
    {
        if (first < 0 && std::fabs (l[(size_t) i] - 0.5f) > 0.002f) first = i;
        if (t63 < 0 && l[(size_t) i] < 0.5f - 0.632f * (0.5f - 0.5f * 0.2512f)) t63 = i;
    }
    std::printf ("  knob to sound: first change after %d samples, 63 percent after %.1f ms\n", first, 1000.0 * t63 / fs);
    CHECK (first >= 0 && first < kChunk, "a knob change is audible within one chunk");
    CHECK (t63 >= 0 && 1000.0 * t63 / fs < 25.0, "smoothing reaches 63 percent within 25 ms");
}

static double rmsOf (const std::vector<float>& x, size_t from) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) (x.size() - from)); }

static double resultAlias[32];
static void testColourStages()
{
    const double fs = 48000;
    for (int t : { (int) U_PDR, (int) U_PTR, (int) U_PFZ })
    {
        const auto& d = unitTable()[t];
        for (float drive : { 0.0f, 25.0f, 50.0f, 100.0f })
        {
            auto u = makeUnit (t); u->prepare (fs, kChunk);
            std::vector<float> l (48000), r (48000);
            fillSine (l, r, fs, 1000.0, 0.25f);
            float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
            v[0] = drive; u->setParams (v);
            const auto in = l;
            for (size_t i = 0; i < l.size(); i += kChunk) u->process (l.data() + i, r.data() + i, (int) std::min<size_t> (kChunk, l.size() - i));
            const double db = 20 * std::log10 (rmsOf (l, 12000) / rmsOf (in, 12000));
            char m[120]; std::snprintf (m, sizeof m, "%s drive %.0f: level within 3 dB of the input (%.2f dB)", d.name, drive, db);
            CHECK (std::fabs (db) < 3.0, m);
            if (drive == 0.0f) { bool same = true; for (size_t i = 12000; i < l.size(); ++i) if (l[i] != in[i - (size_t) u->latency()]) same = false; CHECK (same, "drive 0 is bit exact (the input, delayed by the reported latency)"); }
        }
        // aliasing: a 9.1 kHz sine at 0.5 through full drive. Only the first and second harmonic (9.1 and 18.2 kHz) are below
        // Nyquist at 48 kHz, so every other bin is a folded harmonic (alias) or DC. Reported as dB below the total power.
        auto u = makeUnit (t); u->prepare (fs, kChunk);
        const int N = 16384; std::vector<float> l (N + 8192), r (l.size());
        fillSine (l, r, fs, 9100.0, 0.5f);
        float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
        v[0] = 100.0f; u->setParams (v);
        for (size_t i = 0; i < l.size(); i += kChunk) u->process (l.data() + i, r.data() + i, (int) std::min<size_t> (kChunk, l.size() - i));
        std::vector<double> re (N), im (N, 0.0);
        for (int i = 0; i < N; ++i) re[(size_t) i] = l[(size_t) (8192 + i)] * (0.5 - 0.5 * std::cos (2 * kPi * i / N));
        for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (re[(size_t) i], re[(size_t) j]); }
        for (int len = 2; len <= N; len <<= 1)
        {
            const double ang = -2 * kPi / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    const double wr = std::cos (ang * k), wi = std::sin (ang * k);
                    const double ur = re[(size_t) (i + k)], ui = im[(size_t) (i + k)];
                    const double vr = re[(size_t) (i + k + len / 2)] * wr - im[(size_t) (i + k + len / 2)] * wi, vi = re[(size_t) (i + k + len / 2)] * wi + im[(size_t) (i + k + len / 2)] * wr;
                    re[(size_t) (i + k)] = ur + vr; im[(size_t) (i + k)] = ui + vi; re[(size_t) (i + k + len / 2)] = ur - vr; im[(size_t) (i + k + len / 2)] = ui - vi;
                }
        }
        double tot = 0, alias = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double p = re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k], f = (double) k * fs / N;
            tot += p;
            if (std::fabs (f - 9100.0) > 200.0 && std::fabs (f - 18200.0) > 200.0) alias += p;
        }
        const double adb = 10 * std::log10 (alias / tot + 1e-30);
        std::printf ("  %s at full drive, 9.1 kHz sine: aliased and other energy %.1f dB below the total (first order ADAA with 2x oversampling)\n", d.name, adb);
        resultAlias[t] = adb;
    }
}

static void testMeters()
{
    const double fs = 48000;
    Structure s = chainOf ({ { U_CLW } });
    Rack rack; rack.prepare (fs, 512);
    Structure got = rack.applyStructure (s);
    Ctl c = defaultCtl (got);
    std::vector<float> l (24000), r (24000);
    fillSine (l, r, fs, 200.0, 0.9f);
    rack.process (l.data(), r.data(), 24000, c);
    CHECK (rack.slotGr[0].load() > 3.0f, "the gain reduction meter moves on a loud signal");
    CHECK (rack.inPk[0].load() > 0.8f && rack.outPk[0].load() > 0.1f, "input and output peaks are captured");
}

// ---- units added from the other plugins (batch tests) ----------------------------------------------------------------
static int typeOfKey (const char* key) { for (int t = 0; t < kNumUnits; ++t) if (std::string (unitTable()[t].key) == key) return t; return -1; }
static int paramOf (int t, const char* id) { const auto& d = unitTable()[t]; for (int k = 0; k < d.nParams; ++k) if (std::string (d.p[k].id) == id) return k; return -1; }
// run n blocks of a stereo sine through a unit with the given control values; returns the last block
static void runUnit (IUnit& u, const float* v, int blocks, float amp, std::vector<float>& l, std::vector<float>& r, bool silent = false)
{
    l.assign (kChunk, 0.0f); r.assign (kChunk, 0.0f);
    static double ph = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kChunk; ++i) { l[i] = silent ? 0.0f : amp * (float) std::sin (ph); r[i] = l[i]; ph += 2 * kPi * 440.0 / 48000.0; }
        u.setParams (v); u.process (l.data(), r.data(), kChunk);
    }
}
static void testHostedBatch (std::initializer_list<const char*> keys)
{
    for (const char* key : keys)
    {
        const int t = typeOfKey (key); char m[200];
        CHECK (t >= 0 && hosted (t), key);
        if (t < 0) continue;
        const auto& d = unitTable()[t];
        // 1. mix at 0 leaves the sound alone (units that have a mix control)
        int mi = paramOf (t, "mix"); if (mi < 0) mi = paramOf (t, "dmix");
        if (mi >= 0)
        {
            auto u = makeUnit (t); u->prepare (48000.0, kChunk);
            float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
            v[mi] = 0.0f;
            std::vector<float> l, r; runUnit (*u, v, 400, 0.5f, l, r);
            double maxd = 0; int zero = 0;      // the input was a sine with left equal to right: both must still be that sine
            for (int i = 0; i < kChunk; ++i) { if (std::fabs (l[i]) < 1e-6f) ++zero; maxd = std::max (maxd, (double) std::fabs (l[i] - r[i])); }
            std::snprintf (m, sizeof m, "%s: mix 0 passes the sound (not silent, channels equal)", d.name);
            CHECK (maxd < 1e-3 && zero < kChunk / 2, m);
        }
        // 2. default settings make sound, finite, bounded
        {
            auto u = makeUnit (t); u->prepare (44100.0, kChunk);
            float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
            std::vector<float> l, r; runUnit (*u, v, 300, 0.3f, l, r);
            float pk = 0; bool fin = true;
            for (int i = 0; i < kChunk; ++i) { pk = std::max (pk, std::fabs (l[i])); fin = fin && std::isfinite (l[i]) && std::isfinite (r[i]); }
            std::snprintf (m, sizeof m, "%s: defaults give a finite, audible, bounded signal (peak %.3f)", d.name, pk);
            CHECK (fin && pk > 0.01f && pk < 4.0f, m);
        }
    }
}
static void testBatchA()
{
    testHostedBatch ({ "shm", "sdl", "hbl", "scr" });
    // Shimmer Reverb has a tail: a click, then silence, still rings half a second later
    {
        const int t = typeOfKey ("shm"); auto u = makeUnit (t); u->prepare (48000.0, kChunk);
        const auto& d = unitTable()[t]; float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
        std::vector<float> l (kChunk, 0.0f), r (kChunk, 0.0f); l[0] = r[0] = 0.8f; u->setParams (v); u->process (l.data(), r.data(), kChunk);
        double e = 0; for (int b = 0; b < 100; ++b) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); u->setParams (v); u->process (l.data(), r.data(), kChunk); if (b >= 90) for (float x : l) e += (double) x * x; }
        CHECK (e > 1e-9, "Shimmer Reverb still rings 0.5 s after a click");
    }
    // Shimmer Delay: a click comes back after the delay time
    {
        const int t = typeOfKey ("sdl"); auto u = makeUnit (t); u->prepare (48000.0, kChunk);
        const auto& d = unitTable()[t]; float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
        v[paramOf (t, "dtime")] = 100.0f; v[paramOf (t, "dmix")] = 100.0f; v[paramOf (t, "dshift")] = 0.0f; v[paramOf (t, "dshim")] = 0.0f;
        std::vector<float> l (kChunk), r (kChunk); double back = 0; int n = 0;
        for (int b = 0; b < 100; ++b) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); u->setParams (v); u->process (l.data(), r.data(), kChunk); }   // let the time knob settle
        for (int b = 0; b < 60; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); if (b == 0) l[0] = r[0] = 0.9f;
            u->setParams (v); u->process (l.data(), r.data(), kChunk);
            for (int i = 0; i < kChunk; ++i, ++n) if (n > 4000 && n < 6000) back = std::max (back, (double) std::fabs (l[i]));   // 100 ms is 4800 samples at 48 kHz
        }
        CHECK (back > 0.05, "Shimmer Delay: the repeat arrives about 100 ms after the click");
    }
    // Hairball: freeze keeps playing after the input stops
    {
        const int t = typeOfKey ("hbl"); auto u = makeUnit (t); u->prepare (48000.0, kChunk);
        const auto& d = unitTable()[t]; float v[kMaxPd] {}; for (int k = 0; k < d.nParams; ++k) v[k] = d.p[k].def;
        v[paramOf (t, "liveLen")] = 0.5f;
        std::vector<float> l, r; runUnit (*u, v, 400, 0.5f, l, r);          // the half second buffer is full
        v[paramOf (t, "freeze")] = 1.0f; runUnit (*u, v, 20, 0.5f, l, r);
        runUnit (*u, v, 60, 0.0f, l, r, true);
        double e = 0; for (float x : l) e += (double) x * x;
        CHECK (e > 1e-6, "Hairball: Freeze keeps the sound going after the input is silent");
    }
}

int main()
{
    std::printf ("CatRack core tests\n");
    testUnitsStable(); std::printf ("units stable: done\n");
    testBypassAndOff();
    testLatencyMeasured();
    testStateText();
    testResponsiveness();
    testColourStages();
    testMeters();
    testBatchA();
    std::printf ("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
