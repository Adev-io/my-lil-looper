// test.cpp: checks the core with plain asserts. Run: make test
// Author: Ari Klopfer / A_Development. MIT
#include "check.h"
#include "test_pool.h"
#include "test_sync.h"
#include "test_track.h"
#include "FaceTap.h"

int main()
{
    testPool();
    testSync();
    testFirstLoopIsMaster();
    testSecondTrackMultiple();
    testClockBars();
    testOverdubUndoRedo();
    testUndoMidOverdub();
    testReverseAndOneShot();
    testSeamFade();
    testCleanAndStarve();
    testBlockEdgeAndMaxLength();
    testStopClearAll();
    std::printf ("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
