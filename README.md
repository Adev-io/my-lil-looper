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

## Building

Rack SDK 2.6.6 and MSYS2 (Windows) or a Unix shell: `make` builds, `make test` runs the core checks, `make dist` packs
the plugin. On Windows from PowerShell: `pwsh -File tools/mk.ps1 test`. Panel art: `py -3.11 tools/panel.py`
(needs fontTools) writes `res/` and `src/layout.hpp`.

MIT licence. Author: Ari Klopfer / A_Development.
