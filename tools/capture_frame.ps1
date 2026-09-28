<#
Captures one open-world frame with RenderDoc for GPU profiling.

Launches the game through renderdoccmd (intro skipped), presses Start at the
title screen to load the most recent save, waits for it to settle and creates
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
New-Item -ItemType Directory -Force $rdc | Out-Null

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

Get-Process superman_returns -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
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
  Start-Sleep $Settle
  New-Item -ItemType File -Force $trigger | Out-Null
  $deadline = (Get-Date).AddSeconds(60)
  while (-not (Select-String -Path $log -Pattern 'renderdoc: captured' -Quiet) -and (Get-Date) -lt $deadline) { Start-Sleep 2 }
  Start-Sleep 10
} finally {
  if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
Get-ChildItem "$rdc\$Name*.rdc" | Sort-Object LastWriteTime | Select-Object -Last 1 FullName, Length
