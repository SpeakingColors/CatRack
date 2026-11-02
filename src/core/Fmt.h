#pragma once
// Control text, the same rules as the mockup (fmtVal and fmtShort). Pure C++17, no JUCE.
#include "UnitData.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace catr
{
inline std::string fmtNumber (float v, int dec)
{
    char b[48];
    if (dec > 0) std::snprintf (b, sizeof b, "%.*f", dec, (double) v);
    else std::snprintf (b, sizeof b, "%d", (int) std::lround ((double) v));
    return b;
}
inline std::string fmtVal (const ParamDef& d, float v)
{
    if (d.nNames > 0) { const int i = (int) std::lround ((double) v); return (i >= 0 && i < d.nNames) ? d.names[i] : ""; }
    if (std::fabs (v) < 0.05f && d.dec > 0) v = 0.0f;
    std::string u = d.unit;
    return std::string (u == " dB" && v > 0 ? "+" : "") + fmtNumber (v, d.dec) + u;
}
inline std::string fmtShort (const ParamDef& d, float v)
{
    if (d.nNames > 0)
    {
        const int i = (int) std::lround ((double) v);
        if (i < 0 || i >= d.nNames) return "";
        return d.sn[i][0] != '\0' ? d.sn[i] : d.names[i];
    }
    if (std::fabs (v) < 0.05f && d.dec > 0) v = 0.0f;
    std::string u = d.unit;
    return std::string (u == " dB" && v > 0 ? "+" : "") + fmtNumber (v, d.dec);
}

// The mockup's drawing model of an EQ unit: gain in dB at frequency f (an approximation, see ROADMAP).
inline float eqDisplayDb (int type, const float* p, float f)
{
    static const float kFreq[4] = { 20.0f, 30.0f, 60.0f, 100.0f };
    if (type == 9)   // Parametric EQ: lg, mg, hg, mf, q, out
    {
        const float w = 1.0f / p[4];
        const float bell = 1.0f / (1.0f + std::pow (std::log2 (f / p[3]) / (w * 0.6f), 2.0f));
        const float lo = 1.0f / (1.0f + std::pow (f / 120.0f, 2.0f)), hi = 1.0f / (1.0f + std::pow (6000.0f / f, 2.0f));
        return p[0] * lo + p[1] * bell + p[2] * hi;
    }
    // Pawltec: lb, hb, hc, lc, bw, lf
    const float fc = kFreq[std::max (0, std::min (3, (int) std::lround ((double) p[5])))], w = 0.45f + p[4] * 0.012f;
    auto lowS = [f] (float x) { return 1.0f / (1.0f + std::pow (f / x, 2.0f)); };
    const float bell = 1.0f / (1.0f + std::pow (std::log2 (f / 8000.0f) / w, 2.0f));
    const float hiS = 1.0f / (1.0f + std::pow (10000.0f / f, 2.0f));
    return p[0] * lowS (fc) - p[3] * lowS (fc * 1.8f) + p[1] * bell - p[2] * hiS;
}
} // namespace catr
