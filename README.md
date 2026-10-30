# My lil Looper

A five-track stereo live looper for VCV Rack 2. Record a loop, layer on top of it, and run up to five loops at once,
all locked to the first loop or to a clock. By A_Development.

## Tracks

Each of the five columns is one stereo track.

| Control | What it does |
|---|---|
| **REC/PLAY** pad | Empty: record. Recording: close the loop and play. Playing: overdub (layer on top). Overdubbing: back to play. Stopped: play from the top. |
| **STOP** pad | Stop the track (the audio stays). Hold for 2 seconds to clear it. |
| **UNDO** | Undo the last overdub pass. Press again to redo it. Lit magenta: undo ready. Lit lime: redo ready. |
| **REV** | Play the track backwards. |
| **1SHOT** | Play once from the top, then stop. REC/PLAY restarts it, even while it plays (on the next downbeat when synced). |
| **LEVEL** | Track volume, up to +6 dB. |
| **IN** | The track's own input (poly: channel 1 left, 2 right; mono feeds both). Overrides the main IN L/R for this track. |
| **OUT** | The track on its own, after LEVEL (poly: channel 1 left, 2 right). |

Pad ring: blinking magenta = armed (waiting for the downbeat), magenta = recording, pulsing magenta = overdubbing,
lime = playing, dim lime = stopped with audio.

## Sync

The first loop you record sets the master length. Every later record or play starts on the master's downbeat, and every
later loop closes on a downbeat too, so its length is a whole multiple of the master (x1, x2, x3 ...).

Patch a **CLOCK** and the downbeats are clock bars instead (right-click: *Clock pulses per bar*, default 4).
**RESET** makes the next clock pulse the downbeat and sends every playing track back to the top (a 5 ms fade, so no click).
RESET while a track is recording moves the downbeat, so that loop may not close on a whole multiple of the master.
With a clock, tracks jump at RESET and the next clock pulse is the downbeat: reset while the clock runs.
Loops are not time-stretched: change the clock tempo after recording and the loops drift against it.

**ALL** stops everything that plays; when nothing plays, it starts every track that has audio, together, on the next downbeat.

## Clear

Each track has a **CLEAR** button under its LEVEL knob; **CLEAR ALL** sits beside them. One press clears at once, with the
same 5 ms fade as everything else. (Holding STOP for 2 seconds, or the CLEAR jack, still clear a track.)

## Count-in take

For recording a loop hands-free, with a count-in you can see. Needs a **CLOCK** with at least 4 pulses per bar.

Press **GO** (or send a trigger to **TRIG**). It waits for the next downbeat, then lamps **1 2 3 4** light in turn with the
beat. REC is pressed for you on **4**, so recording starts on the next bar's 1. It records **BARS** bars (1, 2, 4 or 8),
presses REC again on 4 of the last bar, and the loop closes on the downbeat and plays. The take is always a whole number of bars.

- It records on the first empty track. Clear a track first if all five hold loops.
- The GO ring lights lime the moment you press it, then shows the phase: blinking magenta while waiting for the bar or armed,
  lime while counting, steady magenta while recording.
- Lamps: all four dim lime while it waits for the bar. Then lime for 1 2 3, magenta for 4 (the REC press), and dim magenta
  with the beat's lamp bright through the take.
- A refused press (no clock, under 4 pulses a bar, or no empty track) blinks all four lamps magenta for over a second.
- Press GO while waiting or counting to cancel. Press it during the take to close it on the next downbeat.
- RESET cancels a count-in. Closing, stopping or clearing the track by hand also ends it.

## Jacks on the right

| Jack | |
|---|---|
| IN L / IN R | Main input, recorded by any track without its own IN. R follows L when only L is patched. |
| OUT L / OUT R | Mix of all tracks. |
| CLOCK, RESET | See Sync. |
| REC, STOP, CLEAR | Poly triggers: channel n presses track n's REC/PLAY, STOP, or clears it. A mono cable reaches track 1 only. |
| LEVEL | Poly CV: channel n scales track n, 10 V = full level. A mono cable reaches track 1 only. |

## Right-click menu

- **Record then overdub**: closing a recording goes straight into overdub (REC > DUB > PLAY).
- **Input monitor**: adds IN L/R to the main out. Off by default (patch the dry signal to your mixer instead).
- **Clock pulses per bar**, **Max length per track** (1, 2, 5 or 10 minutes; a recording that reaches it closes).
- Memory in use.

## Saving

Loops are saved with the patch (one WAV per track in the patch's storage folder) and come back stopped when you reopen
it; press ALL to start them. A patch saved while a track is still recording leaves that track empty.
Changing Rack's sample rate mid-session keeps the loops in sync but shifts the pitch of loops recorded before the change.
After saving and reopening, those come back at the right pitch, and loops recorded after the change come back shifted
by the rate ratio.
Undo of delete, and Rack's crash recovery, bring back the loops as of the last patch save.
Duplicate (Ctrl+D) copies the loops; copy and paste of a selection copies the settings only.

## My lil Looper CV (expander)

Place **My lil Looper CV** directly to the right of the Looper. Lane n follows track n: while track n records, lane n
records its poly **CV IN** (1 to 16 channels); while track n plays, lane n plays the take back from **CV OUT**, in step with
the audio (reverse, one-shot, RESET and the count-in included). Sing a line into My lil Mouth, merge its NOTE, GATE and VEL
into lane 1 and record track 1: the loop keeps your voice, the lane keeps the notes for any synth.

- CV is recorded on the first take only. Overdub, undo and redo change the audio, never the CV.
- Stopped or empty track: CV OUT is 0 V on every channel, so gates close. While recording, CV OUT passes CV IN through.
- The channel count is CV IN's when the take starts. CV IN unpatched then: the lane stays empty.
- CV jumps at the loop point (no fades; a fade would bend the pitch). Stored at about 3 kHz and held: pulses shorter than
  about 0.3 ms can be missed.
- Lane light: magenta while recording, lime while playing. The face dims when no Looper sits on the left.
- Lanes are saved with the patch (one WAV per lane) and come back with their track's loop. A lane moved to another Looper
  does not play there.

## Building

Rack SDK 2.6.6 and MSYS2 (Windows) or a Unix shell: `make` builds, `make test` runs the core checks, `make dist` packs
the plugin. On Windows from PowerShell: `pwsh -File tools/mk.ps1 test`. Panel art: `py -3.11 tools/panel.py`
(needs fontTools) writes `res/` and `src/layout.hpp`; `py -3.11 tools/panel_cv.py` writes the expander's `res/LooperCV.svg`
and `src/layout_cv.hpp`.

MIT licence. Author: Ari Klopfer / A_Development.
