// test_wav.h: loading a loop into a track, WAV files, resampling.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "Wav.h"
#include "test_track.h"

using namespace loop;

void testAdoptAndWav()
{
    Rig r;
    Block* b[1] = { r.pool->make() };
    b[0]->l[3] = 0.75f;
    r.t[2]->adopt (b, 1, 10, *r.pool);
    check (r.t[2]->state == Track::STOPPED && r.t[2]->len == 10 && near (r.read (2, 3), 0.75f), "adopt: a loaded loop comes back stopped");

    const char* path = "../build/test.wav";
    const float l[3] = { 0.1f, -0.2f, 0.3f }, rr[3] = { 1.f, 2.f, -3.f };
    wav::Writer w;
    check (w.open (path, 44100), "wav: open for writing");
    w.write (l, rr, 3);
    check (w.close(), "wav: close");
    std::vector<float> L, R;
    int sr = 0;
    check (wav::read (path, L, R, sr) && sr == 44100 && L.size() == 3 && L[1] == -0.2f && R[2] == -3.f, "wav: round trip", (double) L.size());
    check (! wav::read ("../build/missing.wav", L, R, sr), "wav: missing file is false");
    std::vector<float> x = { 0.f, 1.f, 2.f, 3.f };
    std::vector<float> y = wav::resample (x, 4, 8);
    check (y.size() == 8 && near (y[1], 0.5f) && near (y[2], 1.f), "wav: linear resample 2x", (double) y.size());
    std::remove (path);
}

// Hand-built WAVs (as a shared patch could carry): header, fmt chunk, raw words.
static void put32 (std::FILE* f, uint32_t v) { std::fwrite (&v, 4, 1, f); }
static void put16 (std::FILE* f, uint16_t v) { std::fwrite (&v, 2, 1, f); }
static void fmtChunk (std::FILE* f, uint16_t ch, uint32_t sr)
{
    std::fwrite ("fmt ", 1, 4, f); put32 (f, 16); put16 (f, 3); put16 (f, ch); put32 (f, sr); put32 (f, sr * 4 * ch); put16 (f, 4 * ch); put16 (f, 32);
}
static std::FILE* wavHeader (const char* p) { std::FILE* f = std::fopen (p, "wb"); std::fwrite ("RIFF", 1, 4, f); put32 (f, 0); std::fwrite ("WAVE", 1, 4, f); return f; }
static void monoData (std::FILE* f, int n) { std::fwrite ("data", 1, 4, f); put32 (f, 4 * n); for (int i = 0; i < n; ++i) put32 (f, 0); std::fclose (f); }
static bool readsAt (uint32_t sr)
{
    const char* p = "../build/rate.wav";
    std::FILE* f = wavHeader (p); fmtChunk (f, 1, sr); monoData (f, 4);
    std::vector<float> l, r; int got = 0;
    return wav::read (p, l, r, got) && got == (int) sr;
}

void testWavHardening()
{
    const char* p = "../build/bad.wav";
    std::vector<float> L, R;
    int sr = 0;
    {   // a chunk claiming 4 GB: its length must not wrap to 0 and expose the bytes after it as chunks
        std::FILE* f = wavHeader (p); fmtChunk (f, 1, 48000); std::fwrite ("LIST", 1, 4, f); put32 (f, 0xFFFFFFFFu); monoData (f, 4);
        check (! wav::read (p, L, R, sr), "wav: a chunk longer than the file is rejected");
    }
    {   // JUNK of 0xFFFFFFF8 bytes: a relative 32-bit seek of -8 would land on itself forever
        std::FILE* f = wavHeader (p); fmtChunk (f, 2, 48000); std::fwrite ("JUNK", 1, 4, f); put32 (f, 0xFFFFFFF8u); std::fclose (f);
        check (! wav::read (p, L, R, sr), "wav: a chunk walk cannot loop");
    }
    {   // data claiming 4 GB in a 56-byte file
        std::FILE* f = wavHeader (p); fmtChunk (f, 2, 48000); std::fwrite ("data", 1, 4, f); put32 (f, 0xFFFFFFF0u); put32 (f, 0); std::fclose (f);
        check (! wav::read (p, L, R, sr), "wav: data longer than the file is rejected");
    }
    {
        std::FILE* f = wavHeader (p); fmtChunk (f, 1, 48000); monoData (f, 100);
        const bool capped = wav::read (p, L, R, sr, 10) && L.size() == 10 && R.size() == 10;
        check (capped && wav::read (p, L, R, sr, 10, 24000) && L.size() == 21, "wav: the frame cap holds (and scales to the target rate, + 1)");
    }
    {   // odd chunk padded to even, fmt longer than 16: still a good file
        std::FILE* f = wavHeader (p); std::fwrite ("LIST", 1, 4, f); put32 (f, 3); std::fwrite ("abc\0", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); put32 (f, 18); put16 (f, 3); put16 (f, 2); put32 (f, 44100); put32 (f, 44100 * 8); put16 (f, 8); put16 (f, 32); put16 (f, 0);
        std::fwrite ("data", 1, 4, f); put32 (f, 8); const float s[2] = { 0.5f, -0.5f }; std::fwrite (s, 4, 2, f); std::fclose (f);
        check (wav::read (p, L, R, sr) && sr == 44100 && L.size() == 1 && L[0] == 0.5f && R[0] == -0.5f, "wav: padded odd chunk and long fmt parse");
    }
    check (! readsAt (1) && ! readsAt (999) && ! readsAt (768001) && ! readsAt (0xFFFFFFFFu) && readsAt (1000) && readsAt (768000),
           "wav: sample rates outside 1000..768000 are rejected");
    check (wav::resample (std::vector<float> (1, 1.f), 96000, 44100).size() == 1, "wav: resample never empties a loop");
    {
        const std::vector<float> x = { 0.f, 1.f, 2.f, 3.f };
        const std::vector<float> y = wav::resample (x, 7);
        check (y.size() == 7 && y[0] == 0.f && near (y[2], 4.f / 7 * 2), "wav: resample to an exact length", (double) y.size());
    }
    {
        wav::Writer w;
        check (! w.close(), "wav: close without open is false");
        check (w.open (p, 48000) && w.close() && ! w.close(), "wav: a second close is a no-op");
    }
    {
        const float a[3] = { 1.f, 2.f, 3.f };
        wav::Writer w;
        w.open (p, 48000);
        w.write (a, a, 3);
    }   // the destructor closes
    check (wav::read (p, L, R, sr) && L.size() == 3 && R[2] == 3.f, "wav: the destructor closes the file");
    std::remove (p);
    std::remove ("../build/rate.wav");
}
