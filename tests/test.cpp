// test.cpp: checks the core with plain asserts. Run: make test
// Author: Ari Klopfer / A_Development. MIT
#include "check.h"
#include "test_pool.h"
#include "test_sync.h"
#include "test_track.h"
#include "test_countin.h"
#include "test_wav.h"
#include "test_cvlane.h"
#include "test_fxhead.h"
#include "test_trackfx.h"
#include "test_djfilter.h"
#include "test_scene.h"
#include "test_song.h"
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
    testRestart();
    testSeamFade();
    testCleanAndStarve();
    testBlockEdgeAndMaxLength();
    testStopClearAll();
    testAdoptAndWav();
    testWavHardening();
    testCountIn();
    testWavChannels();
    testTrackTakeAndHead();
    testCvRoundTrip();
    testCvPassThroughAndNewTake();
    testCvUnpatchedAndInvalid();
    testCvDecimAndHold();
    testCvStarve();
    testCvAdopt();
    testCvLoadThenRecord();
    testCvBlockStraddle();
    testCvJumpKeepsPlay();
    testFxHeadNeutral();
    testFxHeadSpeed();
    testFxHeadStutter();
    testFxHeadTape();
    testFxHeadCrossfade();
    testFxHeadReverseStutter();
    testFxHeadTapeStart();
    testFxHeadWrap();
    testFxHeadThreshold();
    testFxHeadSliceT();
    testTrackFx();
    testDjFilter();
    testSceneTiming();
    testSongRecord();
    testSongCaps();
    testSongPlay();
    std::printf ("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
