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
