// Scene.h: Looper FX scenes and the Looper's side of their timing. A scene holds, per track, mute, speed, filter and
// level. FxTiming applies the FX panel's mutes and speeds on downbeats (at once when nothing is synced) and a scene request
// on the next downbeat, then glides the track levels to the scene's over one bar. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <algorithm>
#include <cstdint>

namespace loop
{
struct SceneTrack
{
    bool mute = false;
    int speed = 1;        // 0 = x0.5, 1 = x1, 2 = x2
    float filter = 0.f;   // -1..1
    float level = 1.f;    // the Looper's LEVEL knob, 0..2
};

struct Scene
{
    bool saved = false;
    SceneTrack t[5];
};

inline double speedOf (int index) { return index == 0 ? 0.5 : index == 2 ? 2.0 : 1.0; }

struct FxTiming
{
    bool mute[5] = {};                    // applied
    int speed[5] = { 1, 1, 1, 1, 1 };     // applied, as speed indexes
    uint32_t applied = 0;                 // the last scene request applied (0 = none yet)

    // One frame, before the tracks run. want*: the FX panel's switches. reqId / req: the newest scene request (reqId 0 =
    // none). ackId: the request the panel has taken over into its switches; until it catches up with `applied`, the
    // switches still show the old mix and are not applied. barLen: frames in a bar. level: the LEVEL knobs now.
    // Returns true on the frame a scene is applied.
    bool step (const bool* wantMute, const int* wantSpeed, uint32_t reqId, const Scene& req, uint32_t ackId,
               bool downbeat, bool synced, int barLen, const float* level)
    {
        if (reqId != 0 && reqId != applied && reqId != pendingId) { pending = req; pendingId = reqId; }
        if (glideN < glideLen) ++glideN;
        if (! downbeat && synced) return false;
        if (ackId == applied)
            for (int i = 0; i < 5; ++i) { mute[i] = wantMute[i]; speed[i] = std::min (2, std::max (0, wantSpeed[i])); }
        if (pendingId == 0) return false;
        for (int i = 0; i < 5; ++i)
        {
            mute[i] = pending.t[i].mute;
            speed[i] = std::min (2, std::max (0, pending.t[i].speed));
            from[i] = level[i];
            to[i] = pending.t[i].level;
        }
        glideLen = std::max (1, barLen);
        glideN = 0;
        applied = pendingId;
        pendingId = 0;
        return true;
    }

    // Level glide: true while a scene's levels are still on their way; value() is track i's level this frame.
    bool gliding() const { return glideN < glideLen; }
    // The last frame (and no glide at all) is exactly the scene's level: rounding would leave 1.70000029 for 1.7 at -O0,
    // and a zero glideLen would divide by zero.
    float value (int i) const
    {
        if (glideN + 1 >= glideLen) return to[i];
        return from[i] + (to[i] - from[i]) * (float) (glideN + 1) / (float) glideLen;
    }

    // No FX panel: everything back to neutral at once.
    void neutral()
    {
        for (int i = 0; i < 5; ++i) { mute[i] = false; speed[i] = 1; }
        pendingId = 0;
        glideN = glideLen = 0;
    }

private:
    Scene pending;
    uint32_t pendingId = 0;
    float from[5] = {}, to[5] = {};
    int glideN = 0, glideLen = 0;
};
}
