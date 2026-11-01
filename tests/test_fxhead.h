// test_fxhead.h: the Looper FX read head against a plain timeline (one frame per sample, a bar every BAR frames).
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "FxHead.h"

using namespace loop;

// A timeline over a loop of LEN frames with a downbeat every BAR frames, and one FxHead reading it.
struct HeadRig
{
    static const int BAR = 100, LEN = 400;
    FxHead h;
    int pos = 0;
    bool rev = false;
    FxHead::Out out { 0, 0, 0 };
    HeadRig (float sr = 1000.f) { h.sampleRate = sr; h.sliceT = BAR / 4; }
    void frame()
    {
        h.sinceBeat = rev ? (LEN - pos) % BAR : pos % BAR;
        out = h.step (pos, LEN, rev);
        pos = rev ? (pos == 0 ? LEN - 1 : pos - 1) : (pos + 1) % LEN;
    }
    void run (int n) { for (int i = 0; i < n; ++i) frame(); }
};

void testFxHeadNeutral()
{
    HeadRig r;
    bool same = true, noFade = true;
    for (int i = 0; i < 3 * HeadRig::LEN; ++i)
    {
        const int p = r.pos;
        r.frame();
        same = same && r.out.read == (double) p;
        noFade = noFade && r.out.w == 0.f;
    }
    check (same && noFade, "fxhead: x1 with nothing held reads the timeline exactly");
    r.pos = 250;   // RESET-style jump of the timeline
    r.frame();
    check (r.out.read == 250.0 && r.out.w == 0.f, "fxhead: a timeline jump is followed without a crossfade (the track fades it)");
}

void testFxHeadSpeed()
{
    HeadRig r;
    r.run (HeadRig::BAR);   // to the downbeat at frame 100
    r.h.speed = 2.0;
    bool grid = true, step2 = true;
    double last = -1;
    for (int i = 0; i < 16 * HeadRig::BAR; ++i)
    {
        const bool downbeat = r.pos % HeadRig::BAR == 0;
        r.frame();
        if (downbeat) grid = grid && std::fmod (r.out.read, (double) HeadRig::BAR) == 0.0;
        if (last >= 0) { double s = r.out.read - last; if (s < 0) s += HeadRig::LEN; step2 = step2 && s == 2.0; }
        last = r.out.read;
    }
    check (grid && step2, "fxhead: x2 reads 2 frames per frame and stays on the bar grid for 16 bars");

    HeadRig h;
    h.run (HeadRig::BAR);
    h.h.speed = 0.5;
    bool half = true, gridHalf = true;
    last = -1;
    for (int i = 0; i < 16 * HeadRig::BAR; ++i)
    {
        const bool downbeat = h.pos % HeadRig::BAR == 0;
        h.frame();
        if (downbeat) gridHalf = gridHalf && std::fmod (h.out.read, HeadRig::BAR / 2.0) == 0.0;
        if (last >= 0) { double s = h.out.read - last; if (s < 0) s += HeadRig::LEN; half = half && s == 0.5; }
        last = h.out.read;
    }
    check (half && gridHalf, "fxhead: x0.5 reads half a frame per frame, downbeats on half bars of the loop");

    h.run (HeadRig::BAR);   // after 17 bars at x0.5 the head is half a bar off the timeline
    h.h.speed = 1.0;
    const int p = h.pos;
    h.frame();
    check (h.out.read == (double) p && h.out.w == 1.f, "fxhead: back to x1 the head is on the timeline again, crossfaded");

    HeadRig b;
    b.rev = true;
    b.run (HeadRig::BAR);
    b.h.speed = 2.0;
    b.frame();
    const double a = b.out.read;
    b.frame();
    double s = a - b.out.read;
    if (s < 0) s += HeadRig::LEN;
    check (s == 2.0, "fxhead: reverse at x2 runs backwards 2 frames per frame", s);
}

void testFxHeadStutter()
{
    HeadRig r;
    r.run (137);   // 37 frames past the downbeat at 100; slices of 25: the grid point is frame 125
    r.h.stut = true;
    r.frame();
    const double first = r.out.read;
    bool loops = first == 137.0;   // the press frame itself: no jump yet
    double lo = 1e9, hi = -1;
    for (int i = 0; i < 100; ++i)
    {
        r.frame();
        lo = std::fmin (lo, r.out.read);
        hi = std::fmax (hi, r.out.read);
    }
    loops = loops && lo == 125.0 && hi == 149.0;
    check (loops, "fxhead: stutter repeats the slice that began on the last grid point", lo);
    r.h.stut = false;
    const int p = r.pos;
    r.frame();
    check (r.out.read == (double) p && r.out.w == 1.f, "fxhead: on release the head is where it would have been");
}

void testFxHeadTape()
{
    HeadRig r;   // 1 kHz: 0.4 s = 400 frames
    r.run (50);
    r.h.tape = true;
    r.run (410);
    const double stopped = r.out.read;
    r.frame();
    check (r.out.read == stopped, "fxhead: tape stop reaches a standstill within 0.4 s");
    r.h.tape = false;
    r.run (205);
    const int p = r.pos;
    r.frame();
    check (r.out.read == (double) p, "fxhead: after release it spins up and lands in time");
}

void testFxHeadCrossfade()
{
    HeadRig r (48000.f);
    r.h.sliceT = 200;   // slice 100..299, held 1200 frames: wraps at 200, 400, 600, 800 and 1000 frames
    r.run (100);
    r.h.stut = true;
    int jumps = 0, fadeFrames = 0;
    for (int i = 0; i < 1200; ++i)
    {
        r.frame();
        if (r.out.w == 1.f) ++jumps;
        if (r.out.w > 0.f) ++fadeFrames;
    }
    check (jumps == 5 && fadeFrames == 5 * 48, "fxhead: each slice wrap crossfades for 1 ms (48 frames at 48 kHz)", fadeFrames);
}
