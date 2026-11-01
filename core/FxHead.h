// FxHead.h: a playing track's read head under the Looper FX moves. The track's `pos` stays the timeline (one frame per
// sample, in sync); FxHead turns it into a fractional read position: speed (x0.5 / x2, anchored where the speed changed,
// back on the timeline at x1), stutter (repeat the slice that began on the last grid point) and tape stop (rate down to 0,
// spin up, land in time). A jump of the read head crossfades for 1 ms from the old trajectory. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace loop
{
struct FxHead
{
    // Set by the module before each frame (speed only changes on a downbeat: the module quantizes it).
    double speed = 1.0;        // 0.5, 1 or 2
    bool stut = false;         // stutter held
    bool tape = false;         // tape stop held
    int sliceT = 1;            // timeline frames per stutter slice
    int sinceBeat = 0;         // timeline frames since the last downbeat
    float sampleRate = 48000.f;

    struct Out
    {
        double read;   // where to read this frame
        double old;    // the trajectory before a jump, faded out over 1 ms
        float w;       // weight of `old`; 0 = no crossfade
    };

    // The track did not play this frame: the next frame starts from the timeline again.
    void reset()
    {
        first = true;
        anchored = stutOn = tapeOn = false;
        rate = 1.0;
        xn = 0;
    }

    // One frame of a playing track. pos: the timeline frame read this frame; len > 0; rev: playing backwards.
    Out step (int pos, int len, bool rev)
    {
        const double d = rev ? -1.0 : 1.0;
        // The track stepped pos after the last frame with that frame's direction: predict with lastRev, not rev.
        const bool timelineJump = ! first && pos != wrapI (lastPos + (lastRev ? -1 : 1), len);   // RESET, retrigger, play start

        // The read head without stutter or tape stop.
        if (speed == 1.0) anchored = false;
        else if (! anchored || speed != lastSpeed || rev != lastRev || timelineJump)
        {
            start = (! anchored || timelineJump) ? pos : wrapD (base + lastD * lastSpeed, len);
            e = 0;
            anchored = true;
        }
        base = anchored ? wrapD (start + d * speed * (double) e++, len) : pos;

        // Stutter: the slice that began on the grid point at or before the press, repeated while held. A reverse change
        // is a new slice around the head: the slice runs the other way, so it is captured again.
        const int slice = std::max (1, sliceT);   // a zero or negative slice must not divide by zero
        const bool stutNow = stut && ! tapeOn;
        if (stutNow && (! stutOn || rev != lastRev))
        {
            const int back = sinceBeat % slice;
            sliceStart = wrapD (base - d * speed * back, len);
            k = back;
        }
        stutOn = stutNow;
        double read = stutOn ? wrapD (sliceStart + d * speed * (double) (k++ % slice), len) : base;

        // Tape stop: from wherever the head is, slow to 0 in 0.4 s; on release spin up in 0.2 s and land in time.
        // The advance below moves tapeRead one step, so it starts at the last read, not at this frame's position.
        if (tape && ! tapeOn) { tapeOn = true; tapeRead = first ? read : lastRead; rate = 1.0; stutOn = false; }
        if (tapeOn)
        {
            rate = tape ? std::fmax (0.0, rate - 1.0 / (0.4 * sampleRate)) : std::fmin (1.0, rate + 1.0 / (0.2 * sampleRate));
            tapeRead = wrapD (tapeRead + d * speed * rate, len);
            if (! tape && rate >= 1.0) tapeOn = false;   // landed: back on `read`, crossfaded below
            else read = tapeRead;
        }

        // A jump of the read head (not of the timeline: the track fades those itself) crossfades from the old trajectory.
        if (! first && ! timelineJump)
        {
            const double expected = wrapD (lastRead + lastVel, len);
            const int fade = std::max (1, (int) std::lround (0.001 * sampleRate));
            // Half a frame: a 1-frame step back (sliceT 2) is a jump; steady speeds and the tape ramp are not
            if (dist (read, expected, len) > 0.5) { xOld = expected; xVel = lastVel; xn = fade; xN = fade; }
            else if (xn > 0) xOld = wrapD (xOld + xVel, len);
        }
        else xn = 0;
        const float w = xn > 0 ? (float) xn / (float) xN : 0.f;
        if (xn > 0) --xn;

        lastVel = d * speed * (tapeOn ? rate : 1.0);   // the intended step, not a jump just made
        lastRead = read;
        lastPos = pos;
        lastSpeed = speed;
        lastRev = rev;
        lastD = d;
        first = false;
        return { read, xOld, w };
    }

    // After step(): the head is the timeline (x1, no stutter, no tape stop). Only then may an overdub write: a head that
    // merely crosses the timeline (x2 does once a loop) must not drop a single written frame into the loop.
    bool onTimeline() const { return ! anchored && ! stutOn && ! tapeOn; }

private:
    bool first = true, anchored = false, stutOn = false, tapeOn = false, lastRev = false;
    double start = 0, base = 0, sliceStart = 0, tapeRead = 0, rate = 1, lastRead = 0, lastVel = 1, lastSpeed = 1, lastD = 1;
    double xOld = 0, xVel = 0;
    int xn = 0, xN = 1;   // crossfade frames left, of xN
    int64_t e = 0, k = 0;
    int lastPos = 0;

    static int wrapI (int p, int len) { return p < 0 ? p + len : p >= len ? p - len : p; }
    static double wrapD (double p, int len)
    {
        p = std::fmod (p, (double) len);
        if (p < 0) p += len;
        return p >= len ? 0.0 : p;   // a hair below 0 rounds up to len; Track would index past the loop
    }
    static double unwrap (double step, int len)   // a step across the loop seam, as the short way round
    {
        if (step > len / 2.0) return step - len;
        if (step < -len / 2.0) return step + len;
        return step;
    }
    static double dist (double a, double b, int len) { return std::fabs (unwrap (a - b, len)); }
};
}
