<#
Capture draw/copy/swap calls from the project's trace renderer.
The game window is opened and this script closes only the process it started.

Example:
  powershell -File tools\capture_gpu_trace.ps1 -Name title -StartFrame 180 -Frames 120
#>
param(
  [ValidatePattern('^[A-Za-z0-9_-]+$')] [string]$Name = 'title',
  [ValidateRange(0, 1000000)] [int]$StartFrame = 0,
  [ValidateRange(1, 10000)] [int]$Frames = 120,
  [ValidateRange(10, 3600)] [int]$TimeoutSeconds = 180,
  [switch]$Gameplay,
  [string]$ExtraArgs = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'port\out\build\win-amd64-release\superman_returns.exe'
$game = Join-Path $root 'game'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Run build.cmd first' }
if (-not (Test-Path -LiteralPath (Join-Path $game 'default.xex'))) {
  throw 'Extract the Xbox 360 game into game/ first'
}

$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$log = Join-Path $logs "gpu_trace_$Name.log"
$trace = Join-Path $logs "gpu_trace_$Name.csv"
$passes = Join-Path $logs "gpu_passes_$Name.csv"
$trigger = Join-Path $logs "gpu_trace_$Name.trigger"
$scene = Join-Path $logs "gpu_scene_$Name.png"
foreach ($path in @($log, $trace, $passes, $trigger, $scene)) {
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}

if ($Gameplay) {
  Add-Type -AssemblyName System.Windows.Forms, System.Drawing
  if (-not ([System.Management.Automation.PSTypeName]'SrTraceInput').Type) {
    Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SrTraceInput {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
  }
}

function Press($proc, $vk) {
  [SrTraceInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
  Start-Sleep -Milliseconds 300
  [SrTraceInput]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 120
  [SrTraceInput]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
}

function Capture-Gameplay-Frame() {
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
    if ($blueHud -ge 20 -and $redHud -ge 8) {
      $bitmap.Save($scene)
      return $true
    }
    return $false
  } finally { $bitmap.Dispose() }
}

$env:SR_LOG_FPS = '1'
$arguments = @(
  "--game_data_root=`"$game`"",
  "--log_file=`"$log`"",
  '--sr_skip_intro=true',
  '--mnk_mode',
  '--render_target_path_d3d12=rtv',
  '--depth_float24_convert_in_pixel_shader=true',
  '--sr_renderer=trace',
  "--sr_gpu_trace_path=`"$trace`"",
  "--sr_gpu_trace_start_frame=$StartFrame",
  "--sr_gpu_trace_frames=$Frames"
)
if ($Gameplay) { $arguments += "--sr_gpu_trace_trigger_path=`"$trigger`"" }
$arguments += ($ExtraArgs -split ' ' | Where-Object { $_ })
if (Get-Process superman_returns -ErrorAction SilentlyContinue) {
  throw 'Close the running game before starting a GPU trace'
}
$proc = Start-Process -FilePath $exe -ArgumentList $arguments `
  -WorkingDirectory (Split-Path $exe) -PassThru
$complete = $false
try {
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  if ($Gameplay) {
    $titleReady = $false
    while ((Get-Date) -lt $deadline) {
      Start-Sleep 2
      $proc.Refresh()
      if ($proc.HasExited) { throw "Game exited before title screen: $($proc.ExitCode)" }
      if ((Test-Path -LiteralPath $log) -and
          (@(Select-String -LiteralPath $log -Pattern 'guest fps:').Count -ge 5)) {
        $titleReady = $true
        break
      }
    }
    if (-not $titleReady) { throw 'Title screen did not appear before timeout' }
    Start-Sleep 5
    Press $proc 0x0D
    Start-Sleep 3
    Press $proc 0x20
    $nextSkip = (Get-Date).AddSeconds(6)
    while ((Get-Date) -lt $deadline) {
      Start-Sleep 2
      $proc.Refresh()
      if ($proc.HasExited) { throw "Game exited before gameplay: $($proc.ExitCode)" }
      [SrTraceInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
      if (Capture-Gameplay-Frame) { break }
      # Start skips the new-game opening cinematic; the HUD check runs first so
      # this never opens the pause menu once gameplay is visible.
      if ((Get-Date) -ge $nextSkip) {
        Press $proc 0x0D
        $nextSkip = (Get-Date).AddSeconds(8)
      }
    }
    if (-not (Test-Path -LiteralPath $scene)) { throw 'Gameplay HUD did not appear before timeout' }
    Start-Sleep 5
    New-Item -ItemType File -Path $trigger | Out-Null
  }
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 1
    $proc.Refresh()
    if ($proc.HasExited) { throw "Game exited before trace completion: $($proc.ExitCode)" }
    if ((Test-Path -LiteralPath $log) -and
        (Select-String -LiteralPath $log -SimpleMatch "finished tracing $Frames frames" -Quiet)) {
      $complete = $true
      break
    }
  }
  if (-not $complete) { throw "Timed out after $TimeoutSeconds seconds" }
} finally {
  $proc.Refresh()
  if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
  if (Test-Path -LiteralPath $trigger) { Remove-Item -LiteralPath $trigger }
}

& python (Join-Path $PSScriptRoot 'gpu_trace_report.py') $trace $passes
if ($LASTEXITCODE -ne 0) { throw 'Could not summarize GPU trace' }
Write-Output "Trace: $trace"
Write-Output "Passes: $passes"
if ($Gameplay) { Write-Output "Scene: $scene" }
