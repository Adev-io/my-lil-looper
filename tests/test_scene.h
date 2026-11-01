// test_scene.h: the Looper's side of the FX timing: mutes and speeds on downbeats, scene recall and the level glide.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "Scene.h"

using namespace loop;

void testSceneTiming()
{
    const float level[5] = { 1, 1, 1, 1, 1 };
    bool mute[5] = {};
    int speed[5] = { 1, 1, 1, 1, 1 };
    Scene none;
    {
        FxTiming t;
        mute[2] = true;
        speed[3] = 2;
        t.step (mute, speed, 0, none, 0, false, false, 100, level);
        check (t.mute[2] && t.speed[3] == 2, "scene: with nothing synced mutes and speeds apply at once");
    }
    FxTiming t;
    mute[2] = false;
    speed[3] = 1;
    t.step (mute, speed, 0, none, 0, true, true, 100, level);
    mute[2] = true;
    bool waited = true;
    for (int i = 0; i < 99; ++i) { t.step (mute, speed, 0, none, 0, false, true, 100, level); waited = waited && ! t.mute[2]; }
    t.step (mute, speed, 0, none, 0, true, true, 100, level);
    check (waited && t.mute[2], "scene: synced, a mute waits for the downbeat");

    Scene s;
    s.saved = true;
    s.t[0].mute = true;
    s.t[1].speed = 0;
    s.t[4].level = 0.f;
    bool early = false;
    for (int i = 0; i < 50; ++i) early = t.step (mute, speed, 7, s, 0, false, true, 100, level) || early;
    const bool on = t.step (mute, speed, 7, s, 0, true, true, 100, level);
    check (! early && on && t.applied == 7 && t.mute[0] && t.speed[1] == 0, "scene: a recall waits for the downbeat, then sets mutes and speeds");

    bool linear = true;
    float last = 1.f;
    int frames = 0;
    while (t.gliding())
    {
        const float v = t.value (4);
        linear = linear && v < last + 1e-6f;
        last = v;
        ++frames;
        t.step (mute, speed, 7, s, 0, false, true, 100, level);
    }
    check (linear && frames == 100 && last == 0.f, "scene: levels glide to the scene's over exactly one bar", frames);

    t.step (mute, speed, 7, s, 0, true, true, 100, level);
    check (t.mute[0] && t.speed[1] == 0, "scene: the panel's old switches are not applied before it takes the scene over");
    mute[0] = true;
    speed[1] = 0;
    mute[2] = false;
    t.step (mute, speed, 7, s, 7, true, true, 100, level);
    check (t.mute[0] && ! t.mute[2], "scene: once taken over, the panel's switches rule again");

    t.neutral();
    bool clear = true;
    for (int i = 0; i < 5; ++i) clear = clear && ! t.mute[i] && t.speed[i] == 1;
    check (clear && ! t.gliding(), "scene: no FX panel, everything neutral at once");
}
