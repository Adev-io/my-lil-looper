# Click at screenshot pixel coordinates (from cap.ps1's full-window image). Rack reads the real cursor, so the cursor
# moves there for ~0.4 s (plus -HoldMs for a held button), the button messages are posted to Rack's window only, then the cursor goes back.
param([Parameter(Mandatory)][int]$ProcId, [Parameter(Mandatory)][int]$ImgX, [Parameter(Mandatory)][int]$ImgY, [ValidateSet('Left', 'Right')][string]$Button = 'Left', [int]$HoldMs = 100)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class RigClick { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y); [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
public struct RECT { public int L, T, R, B; } public struct POINT { public int X, Y; } }
"@
# Only a rig Rack (started by launch.ps1 with a *-rig-* user dir) showing its own window: never the user's Rack, never
# a message box, whose small window would put the cursor over whatever lies behind it.
$proc = Get-Process -Id $ProcId
$cmd = (Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId").CommandLine
if ($proc.ProcessName -ne 'Rack' -or $cmd -notmatch '-rig-\d{8}-\d{6}') { throw "pid $ProcId is not a rig Rack" }
if ($proc.MainWindowTitle -notlike 'VCV Rack*') { throw "pid $ProcId shows '$($proc.MainWindowTitle)', not the rack" }
[RigClick]::SetProcessDPIAware() | Out-Null
$hwnd = $proc.MainWindowHandle
$r = New-Object RigClick+RECT; [RigClick]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$o = New-Object RigClick+POINT; [RigClick]::ClientToScreen($hwnd, [ref]$o) | Out-Null
$cx = $ImgX - ($o.X - $r.L); $cy = $ImgY - ($o.Y - $r.T)
$was = New-Object RigClick+POINT; [RigClick]::GetCursorPos([ref]$was) | Out-Null
$lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xffff))
$down, $up, $mk = if ($Button -eq 'Left') { 0x0201, 0x0202, 1 } else { 0x0204, 0x0205, 2 }
try {
    [RigClick]::SetCursorPos($o.X + $cx, $o.Y + $cy) | Out-Null
    [RigClick]::PostMessage($hwnd, 0x0200, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 150
    [RigClick]::PostMessage($hwnd, $down, [IntPtr]$mk, $lp) | Out-Null; Start-Sleep -Milliseconds $HoldMs   # -HoldMs: a held button
    [RigClick]::PostMessage($hwnd, $up, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 150
} finally { [RigClick]::SetCursorPos($was.X, $was.Y) | Out-Null }
"$Button click at $ImgX,$ImgY"
