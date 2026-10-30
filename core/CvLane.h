// CvLane.h: one lane of the Looper CV expander. It follows its audio track's play head, which the Looper sends every frame
// (CvMsg): records a poly CV while the track records, plays it back while the track plays, 0 V otherwise. CV is recorded
// on the first take only; overdub, undo and redo leave it alone. One poly sample every D frames (about 3 kHz), held on
// playback, in pool blocks used as flat float storage. Audio thread: no allocation. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "BlockPool.h"
#include "Track.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace loop
{
// What the Looper tells the expander about one track, every frame.
struct CvMsg
{
    enum Mode : uint8_t { IDLE, REC, PLAY };
    uint8_t mode = IDLE;
    int pos = 0;         // the frame the track wrote (REC) or read (PLAY) this frame
    int len = 0;         // frames in the loop (while recording: so far)
    uint32_t take = 0;   // Track::take
};

// The expander message. seq goes up every frame the Looper writes it: an unchanged seq means the Looper stopped (removed,
// bypassed) and the message is stale.
struct CvLink
{
    uint32_t seq = 0;
    CvMsg lane[5];
};

// The message for one track, after its process() this frame. A track fading out after STOP still reads: that is IDLE.
// A jump (RESET, 1SHOT, play start) fades out first, and the frame its gain reaches 0 reads nothing (head -1) while the
// track still plays: that frame is PLAY too, or a held gate blips off.
inline CvMsg cvMsg (const Track& t)
{
    CvMsg m;
    const bool play = t.state == Track::PLAYING || t.state == Track::OVERDUBBING;
    if (t.state == Track::RECORDING && t.head >= 0) m.mode = CvMsg::REC;
    else if (play && t.len > 0) m.mode = CvMsg::PLAY;
    else m.mode = CvMsg::IDLE;
    m.pos = t.head >= 0 ? t.head : std::min (std::max (0, t.pos), std::max (0, t.len - 1));
    m.len = t.len;
    m.take = t.take;
    return m;
}

constexpr int CV_MAX_CH = 16;
constexpr int CV_FLOATS = 2 * BLOCK;   // floats in one pool block
constexpr int CV_MAX_BLOCKS = 512;     // 67 M floats: 16 channels for 10 min at 3 kHz is 29 M

// Frames per stored sample at this engine rate: about 3 kHz.
inline int cvDecim (double rate) { return std::max (1, (int) std::lround (rate / 3000.0)); }

struct CvLane
{
    uint32_t take = 0;      // the take this lane holds (or follows)
    int nch = 0;            // channels stored, 0 = nothing
    int D = 1;              // frames per stored sample
    int trackLen = 0;       // the track's frames when this take was recorded: reads scale a resampled loop onto it
    int stored = 0;         // samples written, from 0 up
    int nBlocks = 0;
    Block* blocks[CV_MAX_BLOCKS] = {};
    unsigned version = 0;   // bumps whenever the stored CV changes; save writes changed lanes only

    // One frame. valid: the Looper's message is fresh. in: inCh channels of CV IN (0 = unpatched). rate: the engine rate.
    // Writes CV OUT to out and returns its channel count (at least 1).
    int step (const CvMsg& m, bool valid, const float* in, int inCh, double rate, BlockPool& pool, float* out)
    {
        if (! valid) return zeros (out);
        if (pendingLoad)   // the first fresh message after a load: keep the lane only if its take came back
        {
            pendingLoad = false;
            if (m.take != take || m.mode == CvMsg::REC || m.len == 0) clear (pool);
        }
        if (m.take != take)   // a recording started
        {
            clear (pool);
            take = m.take;
            if (m.mode == CvMsg::REC) { nch = std::min (std::max (0, inCh), CV_MAX_CH); D = cvDecim (rate); }
        }
        if (m.len == 0 && m.mode != CvMsg::REC && (nch > 0 || nBlocks > 0)) clear (pool);   // the track was cleared

        if (m.mode == CvMsg::REC)
        {
            trackLen = std::max (trackLen, m.pos + 1);
            if (nch > 0) write (m.pos, in, inCh, pool);
            const int n = std::max (1, std::min (inCh, CV_MAX_CH));
            for (int c = 0; c < n; ++c) out[c] = c < inCh ? in[c] : 0.f;   // pass-through: the synth plays while you sing
            return n;
        }
        if (m.mode == CvMsg::PLAY && nch > 0)
        {
            int64_t p = m.pos;
            if (m.len > 0 && trackLen > 0 && m.len != trackLen) p = p * trackLen / m.len;   // a loop resampled on load
            const int64_t s = p / D;
            if (s < stored)
            {
                const int64_t k = s * nch;
                for (int c = 0; c < nch; ++c) out[c] = floatAt (blocks, k + c);
                return nch;
            }
        }
        return zeros (out);
    }

    // Returns every block to the pool. Audio thread (or UI thread while the engine does not run this module).
    void clear (BlockPool& pool)
    {
        const int n = nBlocks;
        nBlocks = nch = trackLen = stored = 0;
        full = pendingLoad = false;
        for (int i = 0; i < n; ++i) { Block* b = blocks[i]; blocks[i] = nullptr; pool.give (b); }   // unpublish, then give
        version++;
    }

    // ---- load (UI thread, engine not running this module): takes ownership of blocks made by pool.make(), holding
    // `samples` samples of `channels` floats. The lane plays once the Looper reports `takeId` again.
    void adopt (Block* const* src, int n, int samples, int channels, int decim, int len, uint32_t takeId, BlockPool& pool)
    {
        clear (pool);
        int k = 0;
        for (; k < std::min (n, CV_MAX_BLOCKS) && src[k]; ++k) blocks[k] = src[k];
        for (int i = k; i < n; ++i) pool.give (src[i]);
        nBlocks = k;
        nch = std::min (std::max (0, channels), CV_MAX_CH);
        stored = nch > 0 ? (int) std::min ((int64_t) samples, (int64_t) k * CV_FLOATS / nch) : 0;
        D = std::max (1, decim);
        trackLen = len;
        take = takeId;
        pendingLoad = true;
        version++;
    }

    // The k-th float of a block list (sample s, channel c is k = s * nch + c). The save reads a copy of `blocks` with the
    // pool's freeLock held.
    static float floatAt (const Block* const* list, int64_t k)
    {
        const Block* b = list[k / CV_FLOATS];
        const int i = (int) (k % CV_FLOATS);
        return i < BLOCK ? b->l[i] : b->r[i - BLOCK];
    }

private:
    bool full = false;          // starved or at the cap: this take stores nothing more, its rest plays 0 V
    bool pendingLoad = false;   // adopted from a save, not yet checked against the Looper

    int zeros (float* out) const
    {
        const int n = std::max (1, nch);
        for (int c = 0; c < n; ++c) out[c] = 0.f;
        return n;
    }

    void write (int pos, const float* in, int inCh, BlockPool& pool)
    {
        if (full || pos % D != 0) return;
        const int64_t s = pos / D, k = s * nch, last = (k + nch - 1) / CV_FLOATS;
        if (s != stored) return;   // ponytail: a take is written front to back; a gap (the expander joined mid-take) stops it
        while (nBlocks <= last)
        {
            Block* b = nBlocks < CV_MAX_BLOCKS ? pool.take() : nullptr;
            if (! b) { full = true; return; }
            blocks[nBlocks++] = b;
        }
        for (int c = 0; c < nch; ++c) ref (k + c) = clean (c < inCh ? in[c] : 0.f);
        stored = (int) s + 1;
        version++;
    }

    float& ref (int64_t k)
    {
        Block* b = blocks[k / CV_FLOATS];
        const int i = (int) (k % CV_FLOATS);
        return i < BLOCK ? b->l[i] : b->r[i - BLOCK];
    }
};
}
