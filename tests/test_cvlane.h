// test_cvlane.h: the Looper CV expander's lane following a track, frame by frame like the two modules.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "CvLane.h"
#include "test_track.h"

using namespace loop;

// The test CV: 0.1 V per frame, inside the +-12 V every stored value is clamped to.
static float cvv (int i) { return 0.1f * (float) i; }

// Track 0 of a Rig and one lane, run the way the Looper and the expander run them: the track first, then its message
// (sent with one frame of delay in Rack; the lane does not care) to the lane.
struct CvRig
{
    Rig r;
    CvLane* lane = new CvLane();
    BlockPool* lanePool = new BlockPool();
    float out[CV_MAX_CH] = {};
    int outCh = 0;
    CvMsg msg;
    double rate = SR;
    CvRig() { lanePool->refill(); }
    ~CvRig() { lane->clear (*lanePool); delete lane; delete lanePool; }
    void frame (const float* in, int inCh, bool valid = true)
    {
        r.frame (0.f);
        if (t().state == Track::EMPTY && t().pending == Track::NONE && r.sync.masterLen > 0) r.sync.clear();   // as the Looper does
        msg = cvMsg (*r.t[0]);
        outCh = lane->step (msg, valid, in, inCh, rate, *lanePool, out);
    }
    void frame1 (float v, int inCh = 1) { const float in[3] = { v, -v, 0.5f * v }; frame (in, inCh); }
    Track& t() { return *r.t[0]; }
    void record (int frames, int inCh = 1)   // unsynced: record `frames` frames of CV = cvv (frame index), then close
    {
        t().pressRec (*r.pool);
        for (int i = 0; i < frames; ++i) frame1 (cvv (i), inCh);
        t().pressRec (*r.pool);
    }
};

void testTrackTakeAndHead()
{
    CvRig k;
    const uint32_t t0 = k.t().take;
    k.t().pressRec (*k.r.pool);
    k.frame1 (0.f);
    check (k.t().take == t0 + 1 && k.t().head == 0 && k.msg.mode == CvMsg::REC, "track: take goes up when a recording starts, head 0");
    for (int i = 1; i < 10; ++i) k.frame1 (0.f);
    check (k.t().head == 9 && k.msg.pos == 9 && k.msg.len == 10, "track: head follows the recording", k.t().head);
    k.t().pressRec (*k.r.pool);
    k.frame1 (0.f);
    check (k.msg.mode == CvMsg::PLAY && k.msg.pos == 0 && k.msg.len == 10, "track: closed loop reads from 0", k.msg.pos);
    k.t().pressRec (*k.r.pool);   // overdub
    k.frame1 (0.f);
    check (k.t().state == Track::OVERDUBBING && k.t().take == t0 + 1 && k.msg.mode == CvMsg::PLAY, "track: overdub keeps the take and plays CV");
    k.t().pressUndo();
    for (int i = 0; i < 30; ++i) k.frame1 (0.f);
    check (k.t().take == t0 + 1, "track: undo keeps the take");
    k.t().pressStop();
    k.frame1 (0.f);
    check (k.msg.mode == CvMsg::IDLE, "track: a stopped track is idle while it fades out");
    k.t().pressRec (*k.r.pool);   // play from the top
    for (int i = 0; i < 20; ++i) k.frame1 (0.f);
    check (k.t().take == t0 + 1 && k.msg.mode == CvMsg::PLAY, "track: play again keeps the take");
}

void testCvRoundTrip()
{
    CvRig k;
    k.record (50, 3);
    k.frame1 (0.f, 3);   // the close frame plays frame 0
    bool same = k.outCh == 3;
    for (int i = 0; i < 120; ++i)
    {
        same = same && k.msg.mode == CvMsg::PLAY && k.out[0] == cvv (k.msg.pos) && k.out[1] == -cvv (k.msg.pos) && k.out[2] == 0.5f * cvv (k.msg.pos);
        k.frame1 (99.f, 3);   // CV IN is ignored while the track plays
    }
    check (same, "cv: 3 channels play back at the track's play head");

    k.t().reverse = true;
    bool rev = true;
    int last = k.msg.pos, down = 0;
    for (int i = 0; i < 60; ++i)
    {
        k.frame1 (0.f, 3);
        rev = rev && k.out[0] == cvv (k.msg.pos);
        down += k.msg.pos == last - 1 || (last == 0 && k.msg.pos == 49);
        last = k.msg.pos;
    }
    check (rev && down >= 55, "cv: reverse reads the take backwards", down);
    k.t().reverse = false;

    k.t().pressRec (*k.r.pool);   // overdub with a different CV
    for (int i = 0; i < 60; ++i) k.frame1 (-5.f, 3);
    k.t().pressRec (*k.r.pool);
    k.frame1 (0.f, 3);
    check (k.out[0] == cvv (k.msg.pos), "cv: overdub leaves the CV alone", k.out[0]);

    k.t().pressStop();
    k.frame1 (0.f, 3);
    check (k.outCh == 3 && k.out[0] == 0.f && k.out[1] == 0.f, "cv: stopped track -> 0 V on every channel");
}

void testCvPassThroughAndNewTake()
{
    CvRig k;
    k.t().pressRec (*k.r.pool);
    const float in[2] = { 3.f, 4.f };
    k.frame (in, 2);
    check (k.outCh == 2 && k.out[0] == 3.f && k.out[1] == 4.f && k.lane->nch == 2, "cv: recording passes CV IN through");
    for (int i = 0; i < 20; ++i) k.frame (in, 2);
    k.t().pressRec (*k.r.pool);
    k.frame (in, 2);
    k.t().requestClear (*k.r.pool);
    for (int i = 0; i < 10; ++i) k.frame (in, 2);
    check (k.t().state == Track::EMPTY && k.lane->nBlocks == 0 && k.lane->nch == 0 && k.outCh == 1 && k.out[0] == 0.f,
           "cv: clearing the track frees the lane");
    k.record (30, 1);
    k.frame1 (0.f, 1);
    check (k.lane->nch == 1 && k.outCh == 1 && k.out[0] == cvv (k.msg.pos), "cv: a new take reads its channel count again");
    k.frame1 (0.f, 1);
    const int pos = k.msg.pos;
    const float in3[3] = { 1.f, 2.f, 3.f };
    k.frame (in3, 3);
    check (k.outCh == 1 && k.out[0] == cvv (pos + 1), "cv: more channels patched later wait for the next take");
}

void testCvUnpatchedAndInvalid()
{
    CvRig k;
    k.record (20, 0);
    k.frame1 (0.f, 0);
    check (k.lane->nch == 0 && k.lane->nBlocks == 0 && k.outCh == 1 && k.out[0] == 0.f, "cv: CV IN unpatched at the take -> nothing stored, 1 channel 0 V");
    k.t().requestClear (*k.r.pool);
    for (int i = 0; i < 10; ++i) k.frame1 (0.f, 1);
    k.record (20, 1);
    k.frame1 (0.f);
    k.frame1 (0.f);
    const float in[1] = { 0.f };
    k.r.frame (0.f);
    k.outCh = k.lane->step (cvMsg (k.t()), false, in, 1, k.rate, *k.lanePool, k.out);
    check (k.out[0] == 0.f && k.lane->stored == 20, "cv: a stale message plays 0 V and keeps the lane", k.lane->stored);
    k.frame1 (0.f);
    check (k.out[0] == cvv (k.msg.pos), "cv: the next fresh message plays again");
}

void testCvDecimAndHold()
{
    check (cvDecim (48000) == 16 && cvDecim (192000) == 64 && cvDecim (44100) == 15 && cvDecim (3000) == 1 && cvDecim (1000) == 1,
           "cv: about 3 kHz storage: D = 16 at 48 kHz, 64 at 192 kHz, 1 at 3 kHz");
    CvRig k;
    k.rate = 48000;   // D = 16 on a 1 kHz rig: one stored sample per 16 frames
    k.record (64, 1);
    k.frame1 (0.f);
    bool held = true;
    for (int i = 0; i < 64; ++i)
    {
        held = held && k.out[0] == cvv (k.msg.pos / 16 * 16);
        k.frame1 (0.f);
    }
    check (k.lane->D == 16 && k.lane->stored == 4 && held, "cv: playback holds each stored sample for D frames", k.lane->stored);
}

void testCvStarve()
{
    CvRig k;
    k.lanePool->drain();
    k.lanePool->spare.push (k.lanePool->make());   // exactly one block: 131072 floats = 8192 samples of 16 channels
    float in[CV_MAX_CH];
    for (int c = 0; c < CV_MAX_CH; ++c) in[c] = 1.f;
    k.t().pressRec (*k.r.pool);
    for (int i = 0; i < 9000; ++i) { k.r.pool->refill(); k.frame (in, CV_MAX_CH); }
    k.t().pressRec (*k.r.pool);
    k.frame (in, CV_MAX_CH);
    check (k.t().len == 9000 && k.lane->stored == 8192, "cv: a starved lane stops storing; the audio track records on", k.lane->stored);
    bool tail = true, head = true;
    for (int i = 0; i < 9000; ++i)
    {
        if (k.msg.pos >= 8192) tail = tail && k.out[0] == 0.f && k.out[15] == 0.f;
        else head = head && k.out[0] == 1.f && k.out[15] == 1.f;
        k.frame (in, CV_MAX_CH);
    }
    check (head && tail, "cv: the stored part plays, the rest of the take is 0 V");
}

void testCvAdopt()
{
    CvRig k;
    k.record (100, 1);
    k.frame1 (0.f);
    const uint32_t take = k.t().take;
    BlockPool& p = *k.lanePool;
    Block* b[1] = { p.make() };
    for (int s = 0; s < 50; ++s) b[0]->l[s] = cvv (s);   // 50 samples, D = 1, recorded on a 50-frame track
    k.lane->adopt (b, 1, 50, 1, 1, 50, take, p);
    k.frame1 (0.f);
    check (k.lane->nch == 1 && k.out[0] == cvv (k.msg.pos / 2), "cv: an adopted lane plays when its take comes back, scaled to the track's length", k.out[0]);

    Block* c[1] = { p.make() };
    k.lane->adopt (c, 1, 50, 1, 1, 50, take + 7, p);
    k.frame1 (0.f);
    check (k.lane->nch == 0 && k.lane->nBlocks == 0 && k.out[0] == 0.f, "cv: an adopted lane of another take is dropped");

    Block* d[1] = { p.make() };
    k.lane->adopt (d, 1, 50, 1, 1, 50, take, p);
    const float in[1] = { 0.f };
    k.lane->step (k.msg, false, in, 1, k.rate, p, k.out);
    check (k.lane->nch == 1, "cv: an adopted lane waits for a fresh message before it decides");
    k.lane->clear (p);
}
