// Track.h: one loop track. State machine (record, play, overdub, stop), its audio in pool blocks, reverse, one-shot,
// 5 ms fades and one level of copy-on-write undo. Runs on the audio thread: no allocation, blocks come from the pool.
// No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "BlockPool.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace loop
{
constexpr int MAX_BLOCKS = 2048;   // 134 M frames: 10 min even at 192 kHz

// NaN / inf -> 0 and +-12 V clamp. Bit test, so fast-math flags cannot fold it away.
inline float clean (float x)
{
    uint32_t u;
    std::memcpy (&u, &x, 4);
    if ((u & 0x7f800000u) == 0x7f800000u) return 0.f;
    return std::min (12.f, std::max (-12.f, x));
}

struct Track
{
    enum State { EMPTY, RECORDING, PLAYING, OVERDUBBING, STOPPED };
    enum Pending { NONE, START_REC, START_PLAY };

    State state = EMPTY;
    Pending pending = NONE;   // waits for the next downbeat when synced
    bool reverse = false, oneShot = false, dubAfterRec = false;   // panel / menu settings
    bool justClosed = false;  // true for the one frame a loop closed (the module may make it the master)
    unsigned version = 0;     // bumps whenever the audio changes; save writes changed tracks only

    int len = 0;              // frames in the loop
    int pos = 0;              // next frame to play
    int nBlocks = 0;
    Block* blocks[MAX_BLOCKS] = {};
    Block* undo[MAX_BLOCKS] = {};   // pre-pass copies of the blocks this pass touched (after UNDO: the pass, for redo)

    void setSampleRate (float sr, float maxSeconds)
    {
        fadeLen = std::max (1, (int) (0.005f * sr));
        fadeStep = 1.f / fadeLen;
        maxLen = (int) std::min ((double) maxSeconds * sr, (double) MAX_BLOCKS * BLOCK);
    }

    bool running() const { return state == PLAYING || state == OVERDUBBING || pending == START_PLAY; }
    bool hasUndo() const { return undoCount > 0; }
    bool isUndone() const { return undone; }

    // ---- buttons (audio thread, between process() calls)
    void pressRec (BlockPool& pool)
    {
        if (clearing || swapPending) return;
        switch (state)
        {
            case EMPTY: pending = pending == START_REC ? NONE : START_REC; break;
            case RECORDING: closing = true; break;
            case PLAYING: if (! oneShot) { beginPass (pool); break; }   // 1SHOT: REC/PLAY retriggers it
            // fallthrough
            case STOPPED: pending = pending == START_PLAY ? NONE : START_PLAY; break;
            case OVERDUBBING: state = PLAYING; break;
        }
    }
    void pressStop()
    {
        if (pending != NONE) { pending = NONE; return; }
        if (state == RECORDING) { closing = true; stopAfterClose = true; }
        else if (state == PLAYING || state == OVERDUBBING) state = STOPPED;
    }
    void pressUndo()
    {
        if (state == OVERDUBBING) state = PLAYING;
        if (undoCount > 0 && ! clearing) swapPending = true;   // fades out, swaps, fades back in
    }
    void armPlay() { if (state == STOPPED) pending = START_PLAY; }   // ALL start
    void stopAll()                                                    // ALL stop
    {
        if (pending == START_PLAY) pending = NONE;
        if (state == PLAYING || state == OVERDUBBING) state = STOPPED;
    }
    void rewind() { if (state == PLAYING || state == OVERDUBBING) jump(); }   // RESET
    void requestClear (BlockPool& pool)
    {
        pending = NONE;
        if (state == RECORDING) freeAll (pool);   // nothing audible yet
        else if (state != EMPTY) clearing = true; // fade out first
    }

    // ---- one frame
    void process (float inL, float inR, bool downbeat, bool synced, BlockPool& pool, float& outL, float& outR)
    {
        justClosed = false;
        outL = outR = 0.f;
        const bool now = downbeat || ! synced;
        if (pending == START_REC && now) { pending = NONE; state = RECORDING; len = 0; pos = 0; closing = stopAfterClose = false; }
        if (pending == START_PLAY && now) { pending = NONE; state = PLAYING; jump(); }   // still fading out from a stop: no jump at gain
        if (state == RECORDING && closing && now) close (pool);
        if (jumping && outGain <= 0.f) land();

        if (state == RECORDING)
        {
            if (len >= maxLen || ! ensureBlock (len / BLOCK, pool)) close (pool);   // full or starved: close here
            else
            {
                write (len, inL, inR);
                len++;
                return;
            }
        }

        const bool on = (state == PLAYING || state == OVERDUBBING) && ! swapPending && ! clearing && ! jumping;
        outGain = slew (outGain, on ? 1.f : 0.f);
        dubGain = slew (dubGain, on && state == OVERDUBBING ? 1.f : 0.f);
        if (len > 0 && outGain > 0.f)
        {
            Block* b = blocks[pos / BLOCK];
            const int i = pos % BLOCK;
            const float l = b->l[i], r = b->r[i];
            outL = l * outGain;
            outR = r * outGain;
            if (dubGain > 0.f)
            {
                if (copyOnWrite (pos / BLOCK, pool)) write (pos, l + dubGain * clean (inL), r + dubGain * clean (inR));   // a NaN in must not wipe the loop
                else   // starved: the pass ends; a track the user stopped stays stopped
                {
                    if (state == OVERDUBBING) state = PLAYING;
                    dubGain = 0.f;
                }
            }
            bool wrapped = false;
            if (reverse) { if (--pos < 0) { pos = len - 1; wrapped = true; } }
            else if (++pos == len) { pos = 0; wrapped = true; }
            if (wrapped && oneShot && state == PLAYING && ! jumping) state = STOPPED;   // slews out: an overdub may have covered the seam
        }
        if (jumping) jumped++;
        if (swapPending && outGain <= 0.f) swapUndo();
        if (clearing && outGain <= 0.f) freeAll (pool);
    }

    // ---- load (UI thread, engine not running this module): takes ownership of blocks made by pool.make()
    void adopt (Block* const* src, int n, int frames, BlockPool& pool)
    {
        freeAll (pool);
        int k = 0;
        for (; k < std::min (n, MAX_BLOCKS) && src[k]; ++k) blocks[k] = src[k];   // a failed make() ends the loop there
        for (int i = k; i < n; ++i) pool.give (src[i]);
        nBlocks = k;
        len = std::min (frames, nBlocks * BLOCK);
        state = len > 0 ? STOPPED : EMPTY;
        version++;
    }

    // Returns every block to the pool. Audio thread (or UI thread while the engine does not run this module).
    void freeAll (BlockPool& pool)
    {
        const int n = nBlocks;
        nBlocks = len = pos = undoCount = 0;
        for (int i = 0; i < n; ++i)   // unpublish, then give: the push's release carries the null to a save under freeLock
        {
            Block *b = blocks[i], *u = undo[i];
            blocks[i] = undo[i] = nullptr;
            pool.give (b);
            pool.give (u);
        }
        state = EMPTY;
        pending = NONE;
        closing = stopAfterClose = clearing = swapPending = undone = jumping = false;
        outGain = dubGain = 0.f;
        version++;
    }

private:
    int maxLen = 5 * 60 * 48000, fadeLen = 240, undoCount = 0, jumped = 0;
    float fadeStep = 1.f / 240, outGain = 0.f, dubGain = 0.f;
    bool closing = false, stopAfterClose = false, clearing = false, swapPending = false, undone = false, jumping = false;

    int startPos() const { return reverse ? len - 1 : 0; }

    // To the top without a click (RESET, start, 1SHOT retrigger): fade out, jump, fade in.
    void jump() { jumping = true; jumped = 0; }
    void land()   // where an instant jump would be by now, so the track stays in time with the downbeat
    {
        const int m = jumped % len;
        pos = reverse ? len - 1 - m : m;
        jumping = false;
    }
    float slew (float g, float target) const { return g < target ? std::min (target, g + fadeStep) : std::max (target, g - fadeStep); }

    void write (int p, float l, float r)   // every content write: a save mid-pass must not skip the rest of it
    {
        Block* b = blocks[p / BLOCK];
        b->l[p % BLOCK] = clean (l);
        b->r[p % BLOCK] = clean (r);
        version++;   // unsigned: wraps safely; the module only tests it for equality
    }
    float& at (int p, bool right) { Block* b = blocks[p / BLOCK]; return right ? b->r[p % BLOCK] : b->l[p % BLOCK]; }

    bool ensureBlock (int b, BlockPool& pool)
    {
        if (b < nBlocks) return true;
        if (b >= MAX_BLOCKS) return false;
        Block* nb = pool.take();
        if (! nb) return false;
        blocks[nBlocks++] = nb;
        return true;
    }

    bool copyOnWrite (int b, BlockPool& pool)
    {
        if (undo[b]) return true;
        Block* u = pool.take();
        if (! u) return false;
        std::memcpy (u, blocks[b], sizeof (Block));
        undo[b] = u;
        undoCount++;
        return true;
    }

    void beginPass (BlockPool& pool)   // the previous pass (or its redo) becomes permanent
    {
        for (int i = 0; i < nBlocks; ++i) { Block* u = undo[i]; undo[i] = nullptr; pool.give (u); }   // unpublish, then give
        undoCount = 0;
        undone = false;
        state = OVERDUBBING;
        version++;
    }

    void close (BlockPool& pool)
    {
        closing = false;
        if (len == 0) { freeAll (pool); return; }
        const int n = std::min (fadeLen, len / 2);   // bake the seam fades in: no click where the loop wraps
        for (int i = 0; i < n; ++i)
        {
            const float g = (float) i / n;
            at (i, false) *= g; at (i, true) *= g;
            at (len - 1 - i, false) *= g; at (len - 1 - i, true) *= g;
        }
        justClosed = true;
        version++;
        pos = startPos();
        state = PLAYING;
        if (stopAfterClose) state = STOPPED;
        else if (dubAfterRec) beginPass (pool);
        stopAfterClose = false;
    }

    void swapUndo()
    {
        for (int i = 0; i < nBlocks; ++i)
            if (undo[i]) std::swap (blocks[i], undo[i]);
        undone = ! undone;
        swapPending = false;
        version++;
    }
};
}
