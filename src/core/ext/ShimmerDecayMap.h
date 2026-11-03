// Decay knob display: the knob stores the internal decay value (0..1.55); this maps it to the tail length in seconds for the chosen
// reverb type and back (typing a time). Tables are RT60 measured on the real engine with tests/measure (default damping, shimmer off);
// between the points the value is interpolated, past the last point extended. Plate (and Gated) stop at 20 s: the plate loop is
// almost closed at the top of its range.
#pragma once
#include <algorithm>
#include <cmath>

namespace wm::engine
{
inline constexpr float kDecayMax = 1.55f;
inline int decayCurveOf(int type) { switch (type) { case 0: case 7: return 0; case 3: return 2; case 4: return 3; case 5: return 4; case 6: return 5; default: return 1; } }
inline float decaySeconds(int type, float d)
{
    static const float X[10] = { 0.f, .1f, .3f, .5f, .7f, .9f, 1.f, 1.2f, 1.425f, 1.55f };
    static const float Y[6][10] = {
        { .75f, .94f, 1.35f, 2.01f, 3.48f, 9.23f, 20.f, 20.f, 20.f, 20.f },        // plate, gated
        { .25f, .36f, 1.00f, 2.32f, 4.23f, 6.82f, 8.37f, 11.98f, 16.91f, 19.65f },  // hall, velvet, reverse
        { .30f, .33f, .52f, .98f, 1.66f, 2.60f, 3.14f, 4.42f, 6.12f, 7.07f },       // room
        { .30f, .34f, .78f, 1.73f, 3.09f, 5.01f, 6.10f, 8.71f, 12.23f, 14.2f },     // chamber
        { 1.5f, 1.64f, 2.65f, 3.97f, 5.52f, 7.33f, 8.42f, 10.59f, 13.49f, 15.1f },  // arena
        { .40f, .44f, .57f, 1.21f, 1.98f, 3.07f, 3.64f, 5.07f, 6.96f, 8.0f } };     // spring
    const float* y = Y[decayCurveOf(type)]; d = std::min(kDecayMax, std::max(0.f, d));
    int i = 0; while (i < 8 && d > X[i + 1]) ++i;
    return y[i] + (y[i + 1] - y[i]) * (d - X[i]) / (X[i + 1] - X[i]);
}
inline float decayFromSeconds(int type, float t)
{
    float lo = 0.f, hi = kDecayMax;
    for (int k = 0; k < 30; ++k) { const float m = 0.5f * (lo + hi); (decaySeconds(type, m) < t ? lo : hi) = m; }
    return 0.5f * (lo + hi);
}
}
