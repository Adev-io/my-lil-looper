// test_djfilter.h: the DJ filter's bypass, its two ends and its stability.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "DjFilter.h"
#include <random>
#include <vector>

using namespace loop;

// Largest sample-to-sample change of a[from..to).
static double maxStep (const std::vector<float>& a, int from, int to)
{
    double m = 0;
    for (int i = std::max (from, 1); i < to; ++i) m = std::max (m, (double) std::fabs (a[i] - a[i - 1]));
    return m;
}

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

    // A steady input, the knob at -1 for 1 s, flicked to +1, then to 0. After a flick the largest step may only be what
    // the input moves (1.5x), what the settled output moves, or the slope of a 5 ms fade over the input's amplitude.
    for (int src = 0; src < 2; ++src)
    {
        const int sr = 48000, win = sr / 5;
        const float amp = src == 0 ? 12.f : 1.f;   // DC 12 V, or a 100 Hz sine
        DjFilter f;
        f.setSampleRate ((float) sr);
        std::vector<float> in (3 * sr), out (3 * sr);
        for (int i = 0; i < 3 * sr; ++i)
        {
            const float s = src == 0 ? amp : amp * (float) std::sin (2 * 3.14159265358979 * 100.0 * i / sr);
            const float knob = i < sr ? -1.f : i < 2 * sr ? 1.f : 0.f;
            float l = s, r = s;
            f.process (knob, l, r);
            in[i] = s;
            out[i] = l;
        }
        double worst = 0;
        bool clean = true;
        for (int j : { sr, 2 * sr })
        {
            const double step = maxStep (out, j, j + win);
            const double allowed = 1.5 * maxStep (in, j, j + win) + maxStep (out, j + win - sr / 20, j + win) + amp / (0.005 * sr);
            worst = std::max (worst, step);
            clean = clean && step <= allowed;
        }
        bool exact = true;
        for (int i = 2 * sr + sr / 2; i < 3 * sr; ++i) exact = exact && out[i] == in[i];
        check (clean, src == 0 ? "djfilter: DC flicked -1 -> +1 -> 0 makes no click" : "djfilter: a sine flicked -1 -> +1 -> 0 makes no click", worst);
        check (exact, src == 0 ? "djfilter: DC at 0 after the flicks comes back bit for bit" : "djfilter: a sine at 0 after the flicks comes back bit for bit");
    }

    // DC through the flip: low-pass passes it, high-pass blocks it. Each end settles within 0.5 s.
    {
        DjFilter f;
        f.setSampleRate (48000);
        float l = 0.f, r = 0.f;
        for (int i = 0; i < 48000; ++i) { l = r = 12.f; f.process (-1.f, l, r); }
        for (int i = 0; i < 24000; ++i) { l = r = 12.f; f.process (1.f, l, r); }
        const float blocked = l;
        check (std::fabs (blocked) < 0.01f, "djfilter: DC at -1 then +1 settles to 0 within 0.5 s", blocked);
        for (int i = 0; i < 24000; ++i) { l = r = 12.f; f.process (-1.f, l, r); }
        check (std::fabs (l - 12.f) < 0.01f, "djfilter: DC back at -1 settles to the DC value within 0.5 s", l);
    }
}
