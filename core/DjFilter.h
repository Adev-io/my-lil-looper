// DjFilter.h: one knob, low-pass to the left, high-pass to the right, off in the middle. A stereo topology-preserving
// state-variable filter (Zavalishin), Q 0.9. Within +-0.02 of centre the input comes back bit for bit; from there the
// filtered signal fades in over the next 0.06 of travel, so crossing the centre never clicks. The wet amount (5 ms) and
// the cutoff (20 ms, in log Hz) are smoothed, so a flick moves the filter without a step. No Rack dependency.
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
        smooth = 1.f - std::exp (-1.f / (0.01f * sr));       // 10 ms knob smoothing: no zipper
        wetSmooth = 1.f - std::exp (-1.f / (0.005f * sr));   // 5 ms wet fade: no step when the knob flicks
        fcSmooth = 1.f - std::exp (-1.f / (0.02f * sr));     // 20 ms cutoff glide: the state follows it, no jump
        stale = true;
    }

    // One frame. knob: -1 (low-pass at 80 Hz) .. 0 (off) .. +1 (high-pass at 8 kHz).
    void process (float knob, float& l, float& r)
    {
        x += smooth * (std::min (1.f, std::max (-1.f, knob)) - x);
        const bool hp = x > 0.f;
        // the target cutoff (pow, log) only when the knob moved or changed side
        if (stale || hp != cachedHp || std::fabs (x - cachedX) > 1e-6f)
        {
            const float t = std::min (1.f, std::max (0.f, (std::fabs (x) - DEAD) / (1.f - DEAD)));
            targetLf = std::log (hp ? 20.f * std::pow (400.f, t) : 20000.f * std::pow (0.004f, t));
            cachedX = x;
            cachedHp = hp;
        }
        // the cutoff glides in log Hz, so the low-pass and high-pass sides meet without a jump at the centre; it lands
        // on the target exactly, so a settled knob keeps the same cutoff as before
        lf = std::fabs (targetLf - lf) < 1e-4f ? targetLf : lf + fcSmooth * (targetLf - lf);
        // tan only while the cutoff is still moving; a settled knob keeps its coefficients
        if (stale || std::fabs (lf - cachedLf) > 1e-6f)
        {
            const float fc = std::min ((float) std::exp (lf), 0.45f * rate);
            const float g = std::tan (3.14159265f * fc / rate), k = 1.f / Q;
            a1 = 1.f / (1.f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            cachedLf = lf;
            stale = false;
        }
        // signed: negative = low-pass share, positive = high-pass share; both shares are zero at the centre
        const float target = (hp ? 1.f : -1.f) * std::min (1.f, std::max (0.f, (std::fabs (x) - DEAD) / FADE));
        wet += wetSmooth * (target - wet);
        if (std::fabs (target - wet) < 1e-4f) wet = target;   // lands exactly: the bypass is bit for bit, a float step never stalls
        const float lpShare = std::max (0.f, -wet), hpShare = std::max (0.f, wet), k = 1.f / Q;
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c)   // the state always runs, so the filter is warm when it fades in
        {
            const float v0 = *io[c];
            const float v3 = v0 - ic2[c], v1 = a1 * ic1[c] + a2 * v3, v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = flush (2.f * v1 - ic1[c]);
            ic2[c] = flush (2.f * v2 - ic2[c]);
            const float yHp = v0 - k * v1 - v2;
            if (wet != 0.f) *io[c] = v0 + lpShare * (v2 - v0) + hpShare * (yHp - v0);   // wet == 0: the input untouched
        }
    }

    void reset()
    {
        x = wet = 0.f;
        lf = targetLf = std::log (20000.f);
        stale = true;
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.f;
    }

private:
    float rate = 48000.f, smooth = 0.002f, wetSmooth = 0.001f, fcSmooth = 0.001f;
    float x = 0.f, wet = 0.f;
    double targetLf = 9.9035, lf = 9.9035, cachedLf = 0.0;   // log Hz; double, a float glide stalls short of its target
    float cachedX = 0.f;
    float a1 = 0.f, a2 = 0.f, a3 = 0.f;
    bool cachedHp = false, stale = true;
    float ic1[2] = {}, ic2[2] = {};
    static float flush (float v) { return std::fabs (v) < 1e-20f ? 0.f : v; }   // no denormals in a silent tail
};
}
