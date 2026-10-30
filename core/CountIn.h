// CountIn.h: a count-in and a timed take on one track. Press: wait for the next downbeat, light 1 2 3 4 with the clock,
// press REC on 4 (the track is armed and records from the next downbeat), count the bars, press REC on 4 of the last bar
// (the track closes on the next downbeat). The Looper's own sync does the quantizing; this only presses REC at the right beats.
// Needs a clock with at least 4 pulses per bar. No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <algorithm>

namespace loop
{
struct CountIn
{
    enum Phase { IDLE, WAIT, COUNT, TAKE };   // WAIT: for the downbeat that starts the count. TAKE: armed, recording, closing
    enum Act { NONE, PRESS };                 // PRESS: press REC on the target track now

    Phase phase = IDLE;
    int beat = -1;         // 0..3: the beat lit on the panel, -1 none
    int bars = 4;          // take length in bars
    int take = 0;          // TAKE: the bar being recorded, 1-based
    bool armed = false;    // TAKE: REC pressed, recording starts on the next downbeat
    bool closing = false;  // TAKE: the closing REC pressed, the take ends on the next downbeat

    void stop() { phase = IDLE; beat = -1; take = 0; armed = closing = false; last = -1; }

    // start: the pad or jack fired this frame. edge, pos, perBar: this frame's clock edge, its pulse in the bar (0 = the
    // downbeat) and the pulses per bar. free: a clock is patched and the target track is empty and not armed.
    // rec: the target track is recording. reset: the RESET jack fired (moves the downbeat: the count gives up).
    Act step (bool start, bool edge, int pos, int perBar, bool free, bool rec, bool reset)
    {
        Act act = NONE;
        if (reset || start)
        {
            if (phase == TAKE && armed) act = PRESS;                              // undo the arm: the press toggles it
            else if (phase == TAKE && ! reset && rec && ! closing) { closing = true; return PRESS; }   // close early
            else if (phase == TAKE && ! reset) return NONE;                       // already closing
            else if (phase == IDLE && start && ! reset) { if (free) { phase = WAIT; beat = -1; last = -1; } return NONE; }
            stop();
            return act;
        }
        if (! edge || phase == IDLE) return NONE;

        const int b = std::min (3, pos * 4 / std::max (1, perBar));
        const bool down = pos == 0, newBeat = b != last;
        last = b;
        switch (phase)
        {
            case WAIT:
                if (down) { phase = COUNT; beat = 0; }
                break;
            case COUNT:
                beat = b;
                if (b == 3 && newBeat)
                {
                    if (free) { phase = TAKE; armed = true; take = 0; act = PRESS; }
                    else stop();
                }
                break;
            case TAKE:
                beat = b;
                if (down) { if (closing) stop(); else if (armed) { armed = false; take = 1; } else ++take; }
                else if (! armed && ! closing && ! rec) stop();   // closed, stopped or cleared by hand
                else if (b == 3 && newBeat && ! armed && ! closing && take >= bars) { closing = true; act = PRESS; }
                break;
            default: break;
        }
        return act;
    }

private:
    int last = -1;   // the beat of the previous edge
};
}
