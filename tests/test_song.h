// test_song.h: My lil Looper Song's engine: recording a performance by bar and playing it back bar-exact.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "Song.h"

using namespace loop;

// A bar of B frames: the downbeat on frame 0 of each bar.
struct SongRig
{
    static const int B = 10;
    SongEngine e;
    int frame = 0;
    int current = -1;
    SongEngine::Out out;
    // One frame; returns its downbeat flag.
    bool tick (int applied = -1, bool rec = false, bool play = false, bool loop = false, bool linked = true)
    {
        const bool db = frame % B == 0;
        out = e.step (linked, db, applied, current, rec, play, loop);
        if (applied >= 0) current = applied;
        ++frame;
        return db;
    }
    void toBar() { while (frame % B != 0) tick(); }   // run to just before the next downbeat
};

void testSongRecord()
{
    SongRig r;
    r.tick();             // frame 0 is a downbeat
    r.current = 2;
    r.tick (-1, true);    // REC pressed mid bar: armed
    check (r.e.mode == SongEngine::REC_ARMED, "song: REC arms, waiting for the downbeat");
    r.toBar();
    r.tick();             // downbeat: recording starts at bar 0 with the scene in effect
    check (r.e.mode == SongEngine::RECORDING && r.e.bar == 0 && r.e.song.sceneAt (0) == 2, "song: recording starts on the downbeat, bar 0 holds the scene in effect");
    r.toBar(); r.tick();  // bar 1
    r.toBar(); r.tick (5);   // bar 2 starts, scene 5 applied on its downbeat
    r.tick (6);              // a second change in bar 2 replaces the first
    check (r.e.song.sceneAt (2) == 6 && r.e.song.n == 2, "song: a change lands on its bar; a second change in a bar replaces it", r.e.song.n);
    r.tick (-1, true);       // stop: waits for the downbeat
    check (r.e.mode == SongEngine::REC_STOPPING, "song: REC again stops on the next downbeat");
    r.toBar(); r.tick (7);   // the stopping downbeat: bars 0..2 ran; a scene applied on it is not stored
    check (r.e.mode == SongEngine::STOPPED && r.e.song.len == 3 && r.e.song.sceneAt (3) == -1, "song: length is the bars that ran", r.e.song.len);

    SongRig c;
    c.tick();             // past the downbeat on frame 0
    c.tick (-1, true);
    for (int i = 0; i < 2 * SongRig::B; ++i) c.tick (-1, false, false, false, false);
    check (c.e.mode == SongEngine::REC_ARMED, "song: no FX / Looper linked: nothing starts");
    c.tick (-1, true);
    check (c.e.mode == SongEngine::STOPPED, "song: REC while armed cancels");
}

void testSongCaps()
{
    SongRig r;
    r.tick (-1, true);
    r.toBar(); r.tick();
    for (int i = 0; i < SONG_MAX_EVENTS + 5 && r.e.mode == SongEngine::RECORDING; ++i) { r.toBar(); r.tick (i % 8); }
    check (r.e.mode == SongEngine::STOPPED && r.e.song.n == SONG_MAX_EVENTS, "song: recording stops at 256 changes", r.e.song.n);

    SongRig b;
    b.tick (-1, true);
    b.toBar(); b.tick();
    for (int i = 0; i < SONG_MAX_BARS + 5 && b.e.mode == SongEngine::RECORDING; ++i) { b.toBar(); b.tick(); }
    check (b.e.mode == SongEngine::STOPPED && b.e.song.len == SONG_MAX_BARS, "song: recording stops at 999 bars", b.e.song.len);
}

// Record a 4-bar song: scene 1 from the start, scene 3 on bar 2.
static void record4 (SongRig& r)
{
    r.tick();             // past the downbeat on frame 0
    r.current = 1;
    r.tick (-1, true);
    r.toBar(); r.tick();
    r.toBar(); r.tick();
    r.toBar(); r.tick (3);
    r.toBar(); r.tick();
    r.tick (-1, true);
    r.toBar(); r.tick();
}

void testSongPlay()
{
    SongRig r;
    record4 (r);
    check (r.e.song.len == 4, "song: the test song has 4 bars", r.e.song.len);
    r.tick (-1, false, true);   // PLAY: the bar-0 scene is asked for at once
    check (r.e.mode == SongEngine::PLAY_ARMED && r.out.request == 1, "song: PLAY asks for the bar-0 scene at once", r.out.request);
    int reqBar = -2;
    for (int i = 0; i < 4 * SongRig::B; ++i)
    {
        const bool db = r.tick (-1, false, false, false);
        if (db && r.out.request == 3) reqBar = r.e.bar;
    }
    check (reqBar == 1, "song: the bar-2 scene is asked for on the downbeat that starts bar 1 (one bar early)", reqBar);
    r.toBar();
    r.tick();
    check (r.e.mode == SongEngine::STOPPED && r.out.end, "song: LOOP off: stops after its last bar, END fires");

    SongRig l;
    record4 (l);
    l.tick (-1, false, true, true);
    bool loopReq = false, ended = false, wrapped = false;
    for (int i = 0; i < 5 * SongRig::B; ++i)
    {
        const bool db = l.tick (-1, false, false, true);
        if (db && l.e.bar == 3 && l.out.request == 1) loopReq = true;
        if (db && l.out.end) { ended = true; wrapped = l.e.bar == 0 && l.e.mode == SongEngine::PLAYING; }
    }
    check (loopReq && ended && wrapped, "song: LOOP on: the last bar asks for the bar-0 scene, then it starts over with END");

    l.tick (-1, false, true, true);
    check (l.e.mode == SongEngine::STOPPED, "song: PLAY again stops");
    l.tick (-1, false, true, true);
    l.tick (-1, true);
    check (l.e.mode == SongEngine::REC_ARMED, "song: REC while playing stops it and arms recording");

    SongRig d;
    record4 (d);
    d.toBar();
    d.tick (-1, false, true);   // PLAY on the downbeat frame itself
    check (d.e.mode == SongEngine::PLAY_ARMED && d.out.request == 1, "song: PLAY on a downbeat still asks for the bar-0 scene, starts on the next", d.out.request);

    SongRig empty;
    empty.tick (-1, false, true);
    check (empty.e.mode == SongEngine::STOPPED && empty.out.request == -1, "song: PLAY with no song does nothing");

    SongRig p;
    record4 (p);
    p.tick (-1, false, true);
    p.toBar(); p.tick();
    const int at = p.e.bar;
    for (int i = 0; i < 3 * SongRig::B; ++i) p.tick (-1, false, false, false, false);
    check (p.e.bar == at && p.e.mode == SongEngine::PLAYING, "song: no downbeats (unlinked): the song pauses");
}
