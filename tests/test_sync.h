// test_sync.h: downbeats from the master loop and from clock bars.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "Sync.h"

using namespace loop;

void testSync()
{
    Sync s;
    int beats = 0;
    for (int i = 0; i < 100; ++i) beats += s.tick (false, false, false);
    check (beats == 0 && ! s.synced(), "sync: no master, no clock: never a downbeat", beats);
    s.setMaster (4);
    std::vector<int> at;
    for (int i = 0; i < 9; ++i) if (s.tick (false, false, false)) at.push_back (i);
    check (at.size() == 2 && at[0] == 3 && at[1] == 7, "sync: master 4 closed this frame: downbeats 3 and 7 frames later", at.empty() ? -1 : at[0]);
    Sync c;
    c.perBar = 2;
    at.clear();
    for (int i = 0; i < 40; ++i) if (c.tick (true, i % 5 == 0, false)) at.push_back (i);
    check (at.size() == 4 && at[0] == 0 && at[1] == 10 && at[3] == 30, "sync: 2 pulses per bar: every other clock edge", at.size());
    c.tick (true, true, false);   // pulse 1 of 2
    check (c.tick (true, true, true), "sync: RESET makes the next pulse a downbeat");
    check (c.synced(), "sync: clocked is synced");
}
