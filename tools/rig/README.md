# Rack rig (Windows, dev only)

Drives an isolated VCV Rack 2 Free to check the module without touching %LOCALAPPDATA%\Rack2 (`-Extra` only copies
plugins out of it). Run from the repo root.

1. `make dist` (MSYS2), then `pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/smoke.vcv -Extra Fundamental` → prints `pid=` and `dir=`.
2. `pwsh -File tools/rig/cap.ps1 -ProcId <pid> -Name <name>` → full-window PNG in Desktop\screenshots.
3. `click.ps1 -ProcId <pid> -ImgX <x> -ImgY <y> [-Button Right]`, `type.ps1 -ProcId <pid> -Text 5` (Enter is sent too).
4. `stop.ps1 -Dir <dir>` closes that Rack and deletes the dir.

click.ps1 moves the real cursor for ~0.4 s: don't run it while someone is using the mouse. Read click coordinates from
a full-window cap.ps1 shot, not a crop. Never trigger a native dialog: it blocks the window and the scripts cannot close it.
`patches/smoke.vcv`: Fundamental VCO sine into Looper IN L, Looper OUT L into Fundamental Scope. A copy is saved over
by the save/reload check, so launch from a copy in %TEMP% when testing saves.
Close any other running Rack first: while another Rack.exe is up, the rig's Rack exits within seconds of launch.
