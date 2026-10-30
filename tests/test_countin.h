// test_countin.h: the count-in and timed take, run against a real Sync and Track in the module's order
// (clock tick, count-in step, tracks).
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "CountIn.h"
#include "test_track.h"

using namespace loop;

// One clock edge every `gap` frames, `perBar` edges per bar, so a bar lasts perBar * gap frames. Track 0 is the target.
struct CiRig
{
    Rig r;
    CountIn ci;
    int gap, perBar, frame = 0, presses = 0;
    CiRig (int bars, int perBar_ = 4, int gap_ = 5) : gap (gap_), perBar (perBar_)
    {
        ci.bars = bars;
        r.sync.perBar = perBar;
    }
    Track& t() { return *r.t[0]; }
    // start: press the pad on the first of these frames. Returns the frame count.
    void run (int n, bool start = false, bool reset = false)
    {
        for (int i = 0; i < n; ++i)
        {
            const bool edge = frame % gap == 0;
            const bool down = r.sync.tick (true, edge, false);
            const bool free = t().state == Track::EMPTY && t().pending == Track::NONE;
            const bool rec = t().state == Track::RECORDING;
            if (ci.step (start && i == 0, edge, r.sync.edgePos, perBar, free, rec, reset && i == 0) == CountIn::PRESS)
            {
                t().pressRec (*r.pool);
                ++presses;
            }
            for (int k = 0; k < 5; ++k) r.t[k]->process (0.5f, 0.5f, down, r.sync.synced(), *r.pool, r.outL[k], r.outR[k]);
            ++frame;
        }
    }
};

void testCountIn()
{
    {   // 4 pulses a bar, 5 frames a pulse: a bar is 20 frames, downbeats at 0, 20, 40 ...
        CiRig c (2);
        c.run (7);
        check (c.ci.phase == CountIn::IDLE && c.presses == 0, "countin: idle until pressed");
        c.run (1, true);   // frame 7, mid bar
        check (c.ci.phase == CountIn::WAIT && c.ci.beat == -1, "countin: pressed mid-bar waits for the downbeat");
        c.run (13);        // frames 8..20 : the downbeat at 20 is processed on the 13th frame
        check (c.ci.phase == CountIn::COUNT && c.ci.beat == 0, "countin: the next downbeat starts the count at 1", c.ci.beat);
        c.run (5);
        check (c.ci.beat == 1, "countin: beat 2 lit", c.ci.beat);
        c.run (5);
        check (c.ci.beat == 2 && c.presses == 0, "countin: beat 3 lit, REC not pressed yet", c.ci.beat);
        c.run (5);   // frame 35: the edge on beat 4
        check (c.ci.beat == 3 && c.presses == 1 && c.t().pending == Track::START_REC, "countin: REC pressed on 4, track armed", c.presses);
        check (c.ci.phase == CountIn::TAKE && c.ci.armed, "countin: take armed, waiting for the downbeat");
        c.run (5);   // frame 40: the downbeat
        check (c.t().state == Track::RECORDING && c.ci.take == 1 && ! c.ci.armed, "countin: recording starts on the downbeat after 4", c.ci.take);
        c.run (35);  // to frame 75: the edge on beat 4 of bar 2
        check (c.presses == 2 && c.ci.closing && c.t().state == Track::RECORDING, "countin: closing REC pressed on 4 of the last bar", c.presses);
        c.run (5);   // frame 80: the downbeat that closes it
        check (c.t().state == Track::PLAYING && c.t().len == 40, "countin: a 2 bar take is exactly 40 frames long", c.t().len);
        check (c.ci.phase == CountIn::IDLE && c.ci.beat == -1, "countin: back to idle after the take");
    }
    {   // 16 pulses a bar, one frame a pulse: beats come every 4th pulse
        CiRig c (1, 16, 1);
        c.run (3);
        c.run (1, true);
        c.run (13);   // frames 4..16: the downbeat at 16
        check (c.ci.phase == CountIn::COUNT && c.ci.beat == 0, "countin 16/bar: count starts on the downbeat", c.ci.beat);
        c.run (4);
        check (c.ci.beat == 1, "countin 16/bar: beat 2 after 4 pulses", c.ci.beat);
        c.run (8);    // frame 28 = beat 4's first pulse
        check (c.ci.beat == 3 && c.presses == 1, "countin 16/bar: REC pressed on the first pulse of beat 4", c.presses);
        c.run (4);    // frame 32: downbeat, recording starts
        check (c.t().state == Track::RECORDING, "countin 16/bar: recording on the downbeat");
        c.run (16);   // to frame 48: closes
        check (c.t().state == Track::PLAYING && c.t().len == 16, "countin 16/bar: a 1 bar take is 16 frames", c.t().len);
    }
    {   // pressing again while waiting or counting cancels, nothing is recorded
        CiRig c (2);
        c.run (1, true);
        c.run (1, true);
        check (c.ci.phase == CountIn::IDLE && c.presses == 0, "countin: second press while waiting cancels");
        c.run (1, true);
        c.run (25);
        check (c.ci.phase == CountIn::COUNT, "countin: ... and it can start again", c.ci.phase);
        c.run (1, true);
        check (c.ci.phase == CountIn::IDLE && c.presses == 0 && c.t().state == Track::EMPTY, "countin: press during the count cancels before REC");
    }
    {   // the target must be free: a track that already holds a loop is left alone
        CiRig c (1);
        c.t().pressRec (*c.r.pool);   // start a recording by hand (clocked: armed, starts on the downbeat)
        c.run (2);
        c.run (1, true);
        check (c.ci.phase == CountIn::IDLE, "countin: not started on a busy track");
    }
    {   // press during the take: close early on the next downbeat
        CiRig c (4);
        c.run (1, true);
        c.run (60);   // well into the take
        check (c.ci.phase == CountIn::TAKE && c.t().state == Track::RECORDING, "countin: in the take", c.ci.phase);
        const int before = c.presses;
        c.run (1, true);
        check (c.presses == before + 1 && c.ci.closing, "countin: press in the take closes it early");
        c.run (30);
        check (c.t().state == Track::PLAYING && c.t().len % 20 == 0 && c.ci.phase == CountIn::IDLE, "countin: early close lands on a bar", c.t().len);
    }
    {   // the user closes the track by hand mid-take: the count-in gives up, no extra REC
        CiRig c (4);
        c.run (1, true);
        c.run (45);   // armed at 35, recording since 40
        c.t().pressRec (*c.r.pool);
        const int before = c.presses;
        c.run (60);
        check (c.ci.phase == CountIn::IDLE && c.presses == before, "countin: manual close ends the count-in without another press", c.presses);
        check (c.t().state == Track::PLAYING, "countin: the manually closed loop plays");
    }
    {   // RESET while armed: the arm is undone and the track stays empty
        CiRig c (2);
        c.run (1, true);
        c.run (36);   // REC pressed at frame 35, armed
        check (c.t().pending == Track::START_REC, "countin: armed before the reset");
        c.run (1, false, true);
        check (c.ci.phase == CountIn::IDLE && c.t().pending == Track::NONE && c.t().state == Track::EMPTY, "countin: RESET cancels an armed take");
    }
}
