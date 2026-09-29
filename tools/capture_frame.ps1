<#
Captures one open-world frame with RenderDoc for GPU profiling.

Launches the game through renderdoccmd (intro skipped), presses Start at the
title screen to start a new game, waits for it to settle and creates
the trigger file that port/src/renderdoc_capture.cpp polls, which captures
three whole guest frames. The capture is written to
logs/rdc/<Name>_frame<N>.rdc. Any running instance of the game is closed first.

Usage: tools\capture_frame.ps1 -Name baseline [-ExtraArgs "..."]
#>
param(
  [string]$Name = "frame",
  [string]$ExtraArgs = "",
  [int]$Settle = 25
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = "$root\port\out\build\win-amd64-release\superman_returns.exe"
$log = "$root\logs\game.log"
$rdc = "$root\logs\rdc"
$renderdoc = 'C:\Program Files\RenderDoc\renderdoccmd.exe'
if (-not (Test-Path -LiteralPath $renderdoc)) {
  $renderdoc = Get-ChildItem "$root\.tools\renderdoc" -Recurse -Filter renderdoccmd.exe `
    -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '\\x86\\' } |
    Select-Object -First 1 -ExpandProperty FullName
}
if (-not $renderdoc) { throw 'RenderDoc not found (install it or unpack the portable ZIP in .tools/renderdoc)' }
New-Item -ItemType Directory -Force $rdc | Out-Null
Add-Type -AssemblyName System.Windows.Forms, System.Drawing

if (-not ([System.Management.Automation.PSTypeName]'SrCaptureInput').Type) {
  Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SrCaptureInput {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
}
function Press($proc, $vk) {
  [SrCaptureInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
  Start-Sleep -Milliseconds 300
  [SrCaptureInput]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120
  [SrCaptureInput]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
}

if (Get-Process superman_returns -ErrorAction SilentlyContinue) {
  throw 'Close the running game before starting a RenderDoc capture'
}

function HasGameplayFrame() {
  $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
  try {
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try { $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size) }
    finally { $graphics.Dispose() }
    $blueHud = 0; $redHud = 0
    for ($i = 0; $i -lt 30; $i++) {
      $x = [int]($bounds.Width * (.12 + $i * .012))
      $blue = $bitmap.GetPixel($x, [int]($bounds.Height * .18))
      if ($blue.B - $blue.R -gt 12 -and $blue.B - $blue.G -gt 5 -and
          $blue.R -gt 80) { $blueHud++ }
      $red = $bitmap.GetPixel($x, [int]($bounds.Height * .21))
      if ($red.R - $red.B -gt 20 -and $red.R - $red.G -gt 15 -and
          $red.R -gt 80) { $redHud++ }
    }
    return $blueHud -ge 20 -and $redHud -ge 8
  } finally { $bitmap.Dispose() }
}
if (Test-Path $log) { Clear-Content $log }
$env:SR_LOG_FPS = '1'
$trigger = Join-Path $rdc 'trigger'
$env:SR_RDC_TRIGGER = $trigger
$gameArgs = @(
  "--game_data_root=$root\game", "--log_file=$log",
  '--render_target_path_d3d12=rtv', '--depth_float24_convert_in_pixel_shader=true',
  '--mnk_mode', '--sr_skip_intro=true'
) + ($ExtraArgs -split ' ' | Where-Object { $_ })
& $renderdoc capture -d (Split-Path $exe) -c "$rdc\$Name" $exe @gameArgs | Out-Null

$proc = $null
for ($i = 0; $i -lt 30 -and -not $proc; $i++) {
  Start-Sleep 1
  $proc = Get-Process superman_returns -ErrorAction SilentlyContinue
}
if (-not $proc) { throw "game did not start" }
try {
  $deadline = (Get-Date).AddSeconds(90)
  do {
    Start-Sleep 2
    if ($proc.HasExited) { throw "game exited before the title screen" }
    $frames = @(Get-Content $log | Select-String 'guest fps').Count
  } until ($frames -ge 5 -or (Get-Date) -gt $deadline)
  Start-Sleep 5
  Press $proc 0x0D
  Start-Sleep 3
  Press $proc 0x20
  Start-Sleep $Settle
  $deadline = (Get-Date).AddSeconds(90)
  while (-not (HasGameplayFrame) -and (Get-Date) -lt $deadline) {
    if ($proc.HasExited) { throw 'game exited before gameplay' }
    [SrCaptureInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    Start-Sleep 2
  }
  if ((Get-Date) -ge $deadline) { throw 'gameplay HUD did not appear before timeout' }
  Start-Sleep 5
  New-Item -ItemType File -Force $trigger | Out-Null
  $deadline = (Get-Date).AddSeconds(60)
  while (-not (Select-String -Path $log -Pattern 'renderdoc: captured' -Quiet) -and (Get-Date) -lt $deadline) { Start-Sleep 2 }
  Start-Sleep 10
} finally {
  if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
Get-ChildItem "$rdc\$Name*.rdc" | Sort-Object LastWriteTime | Select-Object -Last 1 FullName, Length
