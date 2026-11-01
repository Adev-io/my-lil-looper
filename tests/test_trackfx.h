// test_trackfx.h: a track playing through its FX read head: interpolated reads, no overdub off the timeline.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "test_track.h"

using namespace loop;

void testTrackFx()
{
    Rig r;   // 1 kHz, unsynced
    Track& t = *r.t[0];
    t.pressRec (*r.pool);
    for (int i = 0; i < 100; ++i) r.frame (0.01f * i);   // a ramp: frame i holds 0.01 * i
    t.pressRec (*r.pool);
    r.run (20, 0.f);   // closed, faded in, at frame 19
    t.fx.speed = 0.5;
    r.frame (0.f);
    const float a = r.outL[0];
    const int ha = t.head;
    r.frame (0.f);
    const float b = r.outL[0];
    check (near (b - a, 0.005, 1e-5) && t.head == ha, "track fx: x0.5 reads between frames (linear interpolation), head = floor (read)", b - a);

    t.fx.speed = 2.0;
    r.frame (0.f);
    t.pressRec (*r.pool);   // overdub
    bool untouched = true;
    for (int i = 0; i < 100; ++i)
    {
        r.frame (3.f);
        for (int p = 10; p < 90; ++p) untouched = untouched && near (r.read (0, p), 0.01 * p, 1e-6);
    }
    check (t.state == Track::OVERDUBBING && untouched, "track fx: no overdub writes while the head is off the timeline (x2)");
    t.fx.speed = 1.0;
    r.run (10, 3.f);
    bool wrote = false;
    for (int p = 10; p < 90; ++p) wrote = wrote || ! near (r.read (0, p), 0.01 * p, 1e-6);
    check (wrote, "track fx: back at x1 the overdub writes again");
}
