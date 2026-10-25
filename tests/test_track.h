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
    r.frame (0.5f);
    const unsigned saved = a.version;   // a save mid-pass
    r.run (299, 0.5f);
    a.pressRec (*r.pool);   // back to play
    const unsigned left = a.version;
    r.run (300, 0.f);
    check (a.version != saved && a.version != left, "overdub: the rest of the pass and its fade tail mark the track changed");
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
    r.run (10, 0.f);
    for (int i = 0; i < a.len; ++i) a.blocks[0]->l[i] = a.blocks[0]->r[i] = 1.f;   // as if an overdub covered the seam
    a.pressRec (*r.pool);
    float prev = r.outL[0], step = 0.f;
    for (int i = 0; i < 300; ++i) { r.frame (0.f); step = std::max (step, std::abs (r.outL[0] - prev)); prev = r.outL[0]; }
    check (a.state == Track::STOPPED && step <= 0.2001f, "one-shot: the end fades out, no click", step);
}

// Max step between neighbouring outputs of track 0 over n frames.
float stepOver (Rig& r, int n, float in)
{
    float prev = r.outL[0], step = 0.f;
    for (int i = 0; i < n; ++i) { r.frame (in); step = std::max (step, std::abs (r.outL[0] - prev)); prev = r.outL[0]; }
    return step;
}

void testRestart()
{
    Rig r;
    Track& a = *r.t[0];
    a.pressRec (*r.pool); r.run (300, 1.f); a.pressRec (*r.pool);
    while (a.pos != 50) r.frame (0.f);
    a.pressRec (*r.pool);   // overdub
    r.run (50, 0.5f);
    a.rewind();
    const float step = stepOver (r, 40, 0.5f);
    const int p = a.pos;
    a.pressRec (*r.pool); r.run (20, 0.5f);   // leave overdub: its fade tail ends the layer
    float edge = 0.f;
    for (int q = 6; q < 295; ++q) edge = std::max (edge, std::abs (r.read (0, q) - r.read (0, q - 1)));
    check (p == 40 && step <= 0.2001f && edge <= 0.1001f, "reset: fades out, jumps to the top in time, fades in; no edge in the layer", step);
    Rig s;
    Track& b = *s.t[0];
    b.pressRec (*s.pool); s.run (300, 1.f); b.pressRec (*s.pool); s.run (100, 0.f);
    s.sync.clear();   // unsynced: the start is immediate
    b.pressStop(); s.run (1, 0.f);
    b.pressRec (*s.pool);   // start again inside the stop fade; lands past the baked seam fade
    const float restart = stepOver (s, 20, 0.f);
    check (b.state == Track::PLAYING && restart <= 0.2001f, "start inside the stop fade: fades out before the jump", restart);
    Rig o;
    Track& c = *o.t[0];
    c.pressRec (*o.pool); o.run (300, 1.f); c.pressRec (*o.pool); o.frame (0.f);
    o.sync.clear();   // unsynced: the retrigger is immediate
    c.oneShot = true;
    while (c.pos != 30) o.frame (0.f);
    c.pressRec (*o.pool);   // retrigger
    const float re = stepOver (o, 40, 0.f);
    check (c.state == Track::PLAYING && c.pos == 40 && re <= 0.2001f, "one-shot: REC/PLAY while playing restarts from the top, no click", c.pos);
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
    {
        Rig n;
        Track& a = *n.t[0];
        a.pressRec (*n.pool); n.run (300, 1.f); a.pressRec (*n.pool); n.run (10, 0.f);
        a.pressRec (*n.pool); n.run (20, 0.f);
        const int p = a.pos;
        n.frame (std::numeric_limits<float>::quiet_NaN());
        check (near (n.read (0, p), 1.f), "clean: a NaN input while overdubbing keeps the loop sample", n.read (0, p));
    }
    Rig r (2000.f);
    Track& a = *r.t[0];
    a.pressRec (*r.pool);
    for (int i = 0; i < (int) (BlockPool::SPARE + 2) * BLOCK && (i == 0 || a.state == Track::RECORDING); ++i)
        a.process (0.1f, 0.1f, false, false, *r.pool, r.outL[0], r.outR[0]);
    check (a.state == Track::PLAYING && a.len == (int) BlockPool::SPARE * BLOCK, "starved: recording closes at the last block", a.len);
    Rig s (2000.f);
    Track& d = *s.t[0];
    d.pressRec (*s.pool); s.run (BLOCK + 100, 1.f); d.pressRec (*s.pool);
    while (d.pos != BLOCK - 10) s.frame (0.f);
    d.pressRec (*s.pool);   // overdub up to a block edge
    while (d.pos != BLOCK - 2) s.frame (0.5f);
    std::vector<Block*> hog;
    while (Block* b = s.pool->take()) hog.push_back (b);
    d.pressStop();          // the dub fade tail crosses into the next block, starved
    s.run (10, 0.5f);
    check (d.state == Track::STOPPED, "starved: a dub tail after STOP ends the dub, the track stays stopped", d.state);
    for (Block* b : hog) s.pool->give (b);
    Block* src[3] = { s.pool->make(), nullptr, s.pool->make() };   // the second make() failed
    Track& e = *s.t[1];
    e.adopt (src, 3, 3 * BLOCK, *s.pool);
    check (e.nBlocks == 1 && e.len == BLOCK && e.blocks[0] == src[0], "load: a failed block ends the loop there", e.nBlocks);
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
