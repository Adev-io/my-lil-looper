// test_track.h: one track and five together, run frame by frame like the module.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "BlockPool.h"
#include "Sync.h"
#include "Track.h"

using namespace loop;

const float SR = 1000.f;   // 5 ms fades = 5 frames

// A pool, a sync and up to five tracks, run frame by frame the way the module runs them.
struct Rig
{
    BlockPool* pool = new BlockPool();   // heap: the freed queue alone is 256 KB
    Sync sync;
    Track* t[5];
    float outL[5] = {}, outR[5] = {};
    bool beat = false;   // last frame's downbeat
    Rig (float maxSeconds = 60.f)
    {
        pool->refill();
        for (int i = 0; i < 5; ++i) { t[i] = new Track(); t[i]->setSampleRate (SR, maxSeconds); }
    }
    ~Rig()
    {
        for (int i = 0; i < 5; ++i) { t[i]->freeAll (*pool); delete t[i]; }
        delete pool;
    }
    void frame (float in, bool clockPatched = false, bool edge = false, bool reset = false)
    {
        beat = sync.tick (clockPatched, edge, reset);
        for (int i = 0; i < 5; ++i)
        {
            t[i]->process (in, in, beat, sync.synced(), *pool, outL[i], outR[i]);
            if (t[i]->justClosed && ! sync.synced()) sync.setMaster (t[i]->len);
        }
    }
    void run (int n, float in) { for (int i = 0; i < n; ++i) frame (in); }
    float read (int track, int p) { Track& k = *t[track]; return k.blocks[p / BLOCK]->l[p % BLOCK]; }
};

void testFirstLoopIsMaster()
{
    Rig r;
    Track& a = *r.t[0];
    a.pressRec (*r.pool);
    r.frame (1.f);
    check (a.state == Track::RECORDING, "track: unsynced record starts at once");
    r.run (99, 1.f);
    a.pressRec (*r.pool);
    r.frame (1.f);
    check (a.state == Track::PLAYING && a.len == 100, "track: unsynced close is immediate, length = frames recorded", a.len);
    check (r.sync.masterLen == 100, "track: first closed loop sets the master length", r.sync.masterLen);
    bool aligned = true;
    for (int i = 0; i < 300; ++i)
    {
        const bool top = a.pos == 0;
        r.frame (0.f);
        aligned = aligned && r.beat == top;
    }
    check (aligned, "track: master downbeats land on the first loop's frame 0");
}

void testSecondTrackMultiple()
{
    Rig r;
    Track &a = *r.t[0], &b = *r.t[1];
    a.pressRec (*r.pool); r.run (100, 1.f); a.pressRec (*r.pool); r.frame (0.f);
    r.run (37, 0.f);
    b.pressRec (*r.pool);
    int waited = 0;
    while (b.state != Track::RECORDING) { r.frame (0.5f); ++waited; }
    check (r.beat && waited == 63, "track 2: armed record starts on the next downbeat", waited);
    r.run (130, 0.5f);
    b.pressRec (*r.pool);
    while (b.state == Track::RECORDING) r.frame (0.5f);
    check (b.len == 200 && r.beat, "track 2: close waits for the downbeat: length = 2 x master", b.len);
}

void testClockBars()
{
    Rig r;
    r.sync.perBar = 2;
    Track& a = *r.t[0];
    int f = 0;
    auto tick = [&] (float in) { r.frame (in, true, f % 7 == 3, false); ++f; };
    for (int i = 0; i < 5; ++i) tick (0.f);
    a.pressRec (*r.pool);
    while (a.state != Track::RECORDING) tick (1.f);
    check (r.beat, "clock: record starts on a bar");
    for (int i = 0; i < 20; ++i) tick (1.f);
    a.pressRec (*r.pool);
    while (a.state == Track::RECORDING) tick (1.f);
    check (a.len == 28, "clock: loop closes on a bar: 2 bars of 14 frames", a.len);
    check (r.sync.masterLen == 0, "clock: a clocked loop does not set a master");
}

void testOverdubUndoRedo()
{
    Rig r;
    Track& a = *r.t[0];
    a.pressRec (*r.pool); r.run (300, 1.f); a.pressRec (*r.pool);
    r.run (150, 0.f);
    check (near (r.outL[0], 1.f), "overdub: loop plays back what was recorded", r.outL[0]);
    a.pressRec (*r.pool);   // overdub
    check (a.state == Track::OVERDUBBING, "overdub: REC/PLAY while playing");
    r.run (300, 0.5f);
    a.pressRec (*r.pool);   // back to play
    r.run (300, 0.f);
    check (near (r.read (0, 75), 1.5f), "overdub: layer adds to the loop", r.read (0, 75));
    check (a.hasUndo(), "overdub: the pass is undoable");
    while (a.pos != 60) r.frame (0.f);   // undo mid-loop, away from the layer's own fade edges at 150
    float prev = r.outL[0], step = 0.f;
    a.pressUndo();
    for (int i = 0; i < 40; ++i) { r.frame (0.f); step = std::max (step, std::abs (r.outL[0] - prev)); prev = r.outL[0]; }
    check (near (r.read (0, 75), 1.f) && a.isUndone(), "undo: the layer is gone", r.read (0, 75));
    check (step <= 0.3001f, "undo: fades out and in, no step bigger than the fade (1.5 x 0.2)", step);
    a.pressUndo();
    r.run (40, 0.f);
    check (near (r.read (0, 75), 1.5f) && ! a.isUndone(), "redo: the layer is back", r.read (0, 75));
}

void testUndoMidOverdub()
{
    Rig r;
    Track& a = *r.t[0];
    a.pressRec (*r.pool); r.run (300, 1.f); a.pressRec (*r.pool); r.run (10, 0.f);
    a.pressRec (*r.pool);
    r.run (100, 2.f);
    a.pressUndo();
    r.run (40, 2.f);
    check (a.state == Track::PLAYING, "undo mid-overdub: back to play");
    bool same = true;
    for (int p = 20; p < 280; ++p) same = same && near (r.read (0, p), 1.f);
    check (same, "undo mid-overdub: the pass is discarded");
}

void testReverseAndOneShot()
{
    Rig r;
    Track& a = *r.t[0];
    a.pressRec (*r.pool);
    for (int i = 0; i < 100; ++i) r.frame (i / 100.f);
    a.pressRec (*r.pool);
    a.reverse = true;
    r.run (20, 0.f);
    const float x0 = r.outL[0];
    r.frame (0.f);
    check (r.outL[0] < x0, "reverse: plays backwards", x0 - r.outL[0]);
    a.reverse = false;
    a.oneShot = true;
    a.pressStop();
    r.run (10, 0.f);
    a.pressRec (*r.pool);   // play from stopped (synced: next downbeat)
    int played = 0;
    for (int i = 0; i < 400; ++i)
    {
        r.frame (0.f);
        if (a.state == Track::PLAYING) ++played;
        else if (played > 0) break;
    }
    check (a.state == Track::STOPPED && played > 0 && played <= 100, "one-shot: plays one pass, then stops", played);
}

void testSeamFade()
{
    Rig r;
    Track& a = *r.t[0];
    a.pressRec (*r.pool); r.run (50, 1.f); a.pressRec (*r.pool);
    r.run (10, 0.f);   // past the start fade-in
    float prev = r.outL[0], step = 0.f;
    for (int i = 0; i < 200; ++i) { r.frame (0.f); step = std::max (step, std::abs (r.outL[0] - prev)); prev = r.outL[0]; }
    check (step <= 0.2001f, "seam: the wrap is faded (no step over 1 / fade length)", step);
    check (near (r.read (0, 0), 0.f) && near (r.read (0, 25), 1.f), "seam: fade baked in at the ends only", r.read (0, 25));
}

void testCleanAndStarve()
{
    check (clean (std::numeric_limits<float>::quiet_NaN()) == 0.f && clean (std::numeric_limits<float>::infinity()) == 0.f,
           "clean: NaN and inf become 0");
    check (clean (100.f) == 12.f && clean (-100.f) == -12.f && clean (0.25f) == 0.25f, "clean: clamps to 12 V");
    Rig r (2000.f);
    Track& a = *r.t[0];
    a.pressRec (*r.pool);
    for (int i = 0; i < (int) (BlockPool::SPARE + 2) * BLOCK && (i == 0 || a.state == Track::RECORDING); ++i)
        a.process (0.1f, 0.1f, false, false, *r.pool, r.outL[0], r.outR[0]);
    check (a.state == Track::PLAYING && a.len == (int) BlockPool::SPARE * BLOCK, "starved: recording closes at the last block", a.len);
}

void testBlockEdgeAndMaxLength()
{
    Rig r (2000.f);
    Track& a = *r.t[0];
    a.pressRec (*r.pool);
    for (int i = 0; i < BLOCK + 10; ++i) r.frame ((float) (i % 1000) / 1000.f);
    a.pressRec (*r.pool);
    r.frame (0.f);
    check (a.nBlocks == 2 && near (r.read (0, BLOCK - 1), ((BLOCK - 1) % 1000) / 1000.f) && near (r.read (0, BLOCK), (BLOCK % 1000) / 1000.f),
           "blocks: frames on both sides of a block edge", a.nBlocks);
    Rig m (0.05f);
    Track& b = *m.t[0];
    b.pressRec (*m.pool);
    m.run (80, 1.f);
    check (b.len == 50 && b.state == Track::PLAYING, "max length: recording closes at the cap", b.len);
}

void testStopClearAll()
{
    Rig r;
    Track &a = *r.t[0], &b = *r.t[1];
    a.pressRec (*r.pool); r.run (100, 1.f); a.pressRec (*r.pool); r.frame (0.f);
    r.run (10, 0.f);
    b.pressRec (*r.pool);
    while (b.state != Track::RECORDING) r.frame (1.f);
    r.run (20, 1.f);
    b.pressStop();
    while (b.state == Track::RECORDING) r.frame (1.f);
    check (b.state == Track::STOPPED && b.len == 100, "stop while recording (synced): closes on the downbeat, then stops", b.len);
    a.stopAll(); b.stopAll();
    r.run (20, 0.f);
    a.armPlay(); b.armPlay();
    int fa = -1, fb = -1;
    for (int i = 0; i < 300 && (fa < 0 || fb < 0); ++i)
    {
        r.frame (0.f);
        if (fa < 0 && a.state == Track::PLAYING) fa = i;
        if (fb < 0 && b.state == Track::PLAYING) fb = i;
    }
    check (fa >= 0 && fa == fb && r.t[0]->pos == r.t[1]->pos, "ALL: armed tracks start together", fa);
    a.requestClear (*r.pool);
    r.run (10, 0.f);
    check (a.state == Track::EMPTY && a.len == 0, "clear: fades out, then empty");
    check (r.pool->freed.size() == 1, "clear: the block goes back to the pool", r.pool->freed.size());
    a.dubAfterRec = true;
    a.pressRec (*r.pool);
    while (a.state != Track::RECORDING) r.frame (1.f);
    r.run (10, 1.f);
    a.pressRec (*r.pool);
    while (a.state == Track::RECORDING) r.frame (1.f);
    check (a.state == Track::OVERDUBBING, "record then overdub: the close goes straight to overdub");
}
