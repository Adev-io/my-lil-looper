// Song.h: My lil Looper Song. A song is a recorded performance: which Looper FX scene came on which bar. SongEngine records
// the scenes the FX applies, bar by bar (a bar = one Looper downbeat), and plays them back by asking the FX for each scene
// one bar early, so it lands on its exact bar. Stepped once per frame. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once

namespace loop
{
constexpr int SONG_MAX_EVENTS = 256, SONG_MAX_BARS = 999;

// Scene changes by bar (bars from 0). One change per bar at most.
struct SongData
{
    int len = 0;   // bars; 0 = no song
    int n = 0;
    int bar[SONG_MAX_EVENTS] = {};
    int scene[SONG_MAX_EVENTS] = {};

    int sceneAt (int b) const
    {
        for (int i = 0; i < n; ++i) if (bar[i] == b) return scene[i];
        return -1;
    }
    bool put (int b, int s)   // false when full
    {
        for (int i = 0; i < n; ++i) if (bar[i] == b) { scene[i] = s; return true; }
        if (n == SONG_MAX_EVENTS) return false;
        bar[n] = b;
        scene[n] = s;
        ++n;
        return true;
    }
};

struct SongEngine
{
    enum Mode { STOPPED, REC_ARMED, RECORDING, REC_STOPPING, PLAY_ARMED, PLAYING };
    Mode mode = STOPPED;
    SongData song;
    int bar = 0;   // the bar running while recording or playing

    struct Out
    {
        int request = -1;   // a scene to ask the FX for this frame, -1 none
        bool end = false;   // the song ended or looped this frame
    };

    // One frame. linked: an FX with a Looper is on the right. downbeat: a Looper downbeat this frame. applied: the scene
    // the FX took over this frame (-1 none). current: the scene in effect (-1 none). The presses are edges.
    Out step (bool linked, bool downbeat, int applied, int current, bool recPress, bool playPress, bool loop)
    {
        Out o;
        if (! linked) { downbeat = false; applied = -1; }
        if (recPress)
        {
            if (mode == RECORDING) mode = REC_STOPPING;
            else if (mode == REC_ARMED) mode = STOPPED;
            else if (mode != REC_STOPPING) mode = REC_ARMED;   // also stops a playing song
        }
        if (playPress)
        {
            if (mode == PLAYING || mode == PLAY_ARMED) mode = STOPPED;
            else if (mode == STOPPED && song.len > 0)
            {
                mode = PLAY_ARMED;
                o.request = song.sceneAt (0);   // lands on the next downbeat, which starts bar 0
            }
        }
        if (downbeat)
        {
            switch (mode)
            {
                case REC_ARMED:
                    song = SongData();
                    bar = 0;
                    if (current >= 0) song.put (0, current);
                    mode = RECORDING;
                    break;
                case RECORDING:
                    if (bar + 1 == SONG_MAX_BARS) finish();   // 999 bars ran: this downbeat starts none
                    else ++bar;
                    break;
                case REC_STOPPING:
                    song.len = bar + 1;   // this downbeat ends the running bar and starts none
                    mode = STOPPED;
                    break;
                case PLAY_ARMED:
                    if (playPress) break;   // armed this frame: its bar-0 request stands, the next downbeat starts bar 0
                    mode = PLAYING;
                    bar = 0;
                    o.request = ahead (loop);
                    break;
                case PLAYING:
                    if (++bar == song.len)
                    {
                        o.end = true;
                        if (! loop) { mode = STOPPED; break; }
                        bar = 0;
                    }
                    o.request = ahead (loop);
                    break;
                default: break;
            }
        }
        if (applied >= 0 && (mode == RECORDING || mode == REC_STOPPING) && ! song.put (bar, applied)) finish();
        return o;
    }

    void stop() { mode = STOPPED; bar = 0; }

private:
    void finish() { song.len = bar + 1; mode = STOPPED; }   // a cap reached: keep what was recorded

    // At the start of a bar: the scene for the next bar, so the FX applies it on the next downbeat.
    int ahead (bool loop) const
    {
        if (bar + 1 < song.len) return song.sceneAt (bar + 1);
        return loop ? song.sceneAt (0) : -1;
    }
};
}
