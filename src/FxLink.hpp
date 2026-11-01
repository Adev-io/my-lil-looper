// FxLink.hpp: the messages between My lil Looper FX (left) and the Looper, one each way, every frame. Each reader owns
// its two buffers; a message counts only while the neighbour is the right model and `seq` moves.
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
    float mainOut = 0.f;                     // -1..1, for the face
};
