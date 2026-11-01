// FxLink.hpp: the messages between My lil Looper FX (left) and the Looper, and between My lil Looper Song (left of the FX)
// and the FX, one each way, every frame. Each reader owns its two buffers; a message counts only while the neighbour is
// the right model and `seq` moves.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include "Scene.h"
#include <cstdint>

// FX -> Looper: the panel's moves and the newest scene request.
struct FxLink
{
    uint32_t seq = 0;
    float filter[5] = {};              // -1..1, knob + FILTER CV
    int speed[5] = { 1, 1, 1, 1, 1 };  // 0 = x0.5, 1 = x1, 2 = x2
    bool mute[5] = {};
    bool stut[5] = {};                 // held: button or STUT gate
    bool tape[5] = {};                 // held: button or TAPE gate
    int stutDiv = 16;                  // slices per bar: 4, 8, 16 or 32
    uint32_t reqId = 0;                // newest scene request, 0 = none
    uint32_t ackId = 0;                // the request the panel has taken over into its switches
    loop::Scene req;
};

// Looper -> FX: what the panel shows and needs for its own glides.
struct FxBack
{
    uint32_t seq = 0;
    int barLen = 1;                          // frames in a bar
    float level[5] = { 1, 1, 1, 1, 1 };      // the LEVEL knobs now
    uint32_t applied = 0;                    // the last scene request applied
    bool downbeat = false;                   // this frame was a Looper downbeat
    float mainOut = 0.f;                     // -1..1, for the face
};

// FX -> Song: the Looper's downbeats, passed on, and the scene the FX took over this frame.
struct SongBack
{
    uint32_t seq = 0;
    bool looper = false;     // the FX has a Looper on its right this frame
    bool downbeat = false;   // this frame carries a Looper downbeat
    int barLen = 1;          // frames in a bar
    int applied = -1;        // the scene the FX took over this frame (it lands on a downbeat), -1 none
    int current = -1;        // the scene in effect, -1 none
};

// Song -> FX: a scene request, handled like a pad press.
struct SongLink
{
    uint32_t seq = 0;
    uint32_t reqSeq = 0;     // up for each request
    int reqScene = -1;
};
