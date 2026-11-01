// DjFilter.h: one knob, low-pass to the left, high-pass to the right, off in the middle. A stereo topology-preserving
// state-variable filter (Zavalishin), Q 0.9. Within +-0.02 of centre the input comes back bit for bit; from there the
// filtered signal fades in over the next 0.06 of travel, so crossing the centre never clicks. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <algorithm>
#include <cmath>

namespace loop
{
struct DjFilter
{
    static constexpr float DEAD = 0.02f, FADE = 0.06f, Q = 0.9f;

    void setSampleRate (float sr)
    {
        rate = sr;
        smooth = 1.f - std::exp (-1.f / (0.01f * sr));   // 10 ms knob smoothing: no zipper
    }

    // One frame. knob: -1 (low-pass at 80 Hz) .. 0 (off) .. +1 (high-pass at 8 kHz).
    void process (float knob, float& l, float& r)
    {
        x += smooth * (std::min (1.f, std::max (-1.f, knob)) - x);
        const float t = std::min (1.f, std::max (0.f, (std::fabs (x) - DEAD) / (1.f - DEAD)));
        const bool hp = x > 0.f;
        const float fc = std::min (hp ? 20.f * std::pow (400.f, t) : 20000.f * std::pow (0.004f, t), 0.45f * rate);
        const float g = std::tan (3.14159265f * fc / rate), k = 1.f / Q;
        const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float* io[2] = { &l, &r };
        const float wet = std::min (1.f, std::max (0.f, (std::fabs (x) - DEAD) / FADE));
        for (int c = 0; c < 2; ++c)   // the state always runs, so the filter is warm when it fades in
        {
            const float v0 = *io[c];
            const float v3 = v0 - ic2[c], v1 = a1 * ic1[c] + a2 * v3, v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = flush (2.f * v1 - ic1[c]);
            ic2[c] = flush (2.f * v2 - ic2[c]);
            const float y = hp ? v0 - k * v1 - v2 : v2;
            if (wet > 0.f) *io[c] = v0 + wet * (y - v0);   // wet == 0: the input untouched
        }
    }

    void reset() { x = 0.f; ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.f; }

private:
    float rate = 48000.f, smooth = 0.002f, x = 0.f;
    float ic1[2] = {}, ic2[2] = {};
    static float flush (float v) { return std::fabs (v) < 1e-20f ? 0.f : v; }   // no denormals in a silent tail
};
}
