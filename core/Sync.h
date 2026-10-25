// Sync.h: where the downbeats are. Clock bars when CLOCK is patched, else the master loop's start. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once

namespace loop
{
struct Sync
{
    int masterLen = 0;   // frames; 0 = no master yet
    int masterPos = 0;   // the next frame's place in the master loop
    int perBar = 4;      // clock pulses per bar
    int pulses = 0;      // clock pulses since the last downbeat
    bool clocked = false;

    // A track is synced when its record/play starts and loop closes wait for a downbeat.
    bool synced() const { return clocked || masterLen > 0; }

    // The first loop closed this frame with `len` frames, and the track played its frame 0 this frame: the next frame is 1.
    void setMaster (int len) { masterLen = len; masterPos = len > 1 ? 1 : 0; }
    void clear() { masterLen = 0; masterPos = 0; }

    // One frame. Returns true when this frame is a downbeat.
    bool tick (bool clockPatched, bool clockEdge, bool reset)
    {
        if (clockPatched && ! clocked) pulses = 0;   // first edge after patching is a downbeat
        clocked = clockPatched;
        if (reset) { pulses = 0; masterPos = 0; }
        bool beat = false;
        if (masterLen > 0)
        {
            beat = masterPos == 0;
            masterPos = (masterPos + 1) % masterLen;
        }
        if (! clocked) return beat;
        if (! clockEdge) return false;
        const bool bar = pulses == 0;
        pulses = (pulses + 1) % perBar;
        return bar;
    }
};
}
