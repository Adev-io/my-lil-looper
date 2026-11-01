// test_djfilter.h: the DJ filter's bypass, its two ends and its stability.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "DjFilter.h"
#include <random>

using namespace loop;

// Gain in dB of a sine through the filter at `knob`, after 1 s to settle.
static double djGain (float knob, double hz, float sr)
{
    DjFilter f;
    f.setSampleRate (sr);
    double in2 = 0, out2 = 0;
    const int settle = (int) sr, n = (int) sr / 2;
    for (int i = 0; i < settle + n; ++i)
    {
        const float s = (float) std::sin (2 * 3.14159265358979 * hz * i / sr);
        float l = s, r = s;
        f.process (knob, l, r);
        if (i >= settle) { in2 += (double) s * s; out2 += (double) l * l; }
    }
    return 10 * std::log10 (out2 / in2);
}

void testDjFilter()
{
    {
        DjFilter f;
        f.setSampleRate (48000);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (-5.f, 5.f);
        bool exact = true;
        for (int i = 0; i < 48000; ++i)
        {
            const float a = u (rng), b = u (rng);
            float l = a, r = b;
            f.process (i < 24000 ? 0.f : 0.015f, l, r);
            exact = exact && l == a && r == b;
        }
        check (exact, "djfilter: within +-0.02 of centre the input comes back bit for bit");
    }
    const double lpHigh = djGain (-1.f, 10000, 48000), lpLow = djGain (-1.f, 30, 48000);
    check (lpHigh < -24 && std::fabs (lpLow) < 1.5, "djfilter: full left cuts 10 kHz and passes 30 Hz", lpHigh);
    const double hpLow = djGain (1.f, 100, 48000), hpHigh = djGain (1.f, 16000, 48000);
    check (hpLow < -24 && std::fabs (hpHigh) < 1.5, "djfilter: full right cuts 100 Hz and passes 16 kHz", hpLow);

    bool stable = true;
    for (float sr : { 44100.f, 48000.f, 192000.f })
    {
        DjFilter f;
        f.setSampleRate (sr);
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> u (-10.f, 10.f);
        const int n = (int) sr * 2;
        for (int i = 0; i < n; ++i)
        {
            const float knob = (float) std::sin (2 * 3.14159265358979 * 3.0 * i / sr);   // sweeps the whole knob, both ends
            float l = u (rng), r = u (rng);
            f.process (knob, l, r);
            stable = stable && std::isfinite (l) && std::isfinite (r) && std::fabs (l) < 100.f && std::fabs (r) < 100.f;
        }
    }
    check (stable, "djfilter: finite and bounded over the whole knob with full-scale noise at 44.1, 48 and 192 kHz");
}
