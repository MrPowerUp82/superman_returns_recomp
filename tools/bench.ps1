<#
Open-world benchmark for the Superman Returns recomp.

Boots the game with the intro skipped, presses Start at the title screen
(loads the most recent save), lets it settle, then measures two scenarios:
  idle     - standing still for 20 s
  forward  - holding W (move forward) for 20 s
and appends one CSV row per scenario to logs/bench_results.csv
(name, scenario, avg/min guest FPS, CPU cores in use, GPU 3D %, args).
Screenshots of each scenario go to logs/bench_<name>_<scenario>.png.

Any running instance of the game is closed first.

Usage: tools\bench.ps1 -Name baseline [-ExtraArgs "--native_2x_msaa=false"]
#>
param(
  [Parameter(Mandatory)] [string]$Name,
  [string]$ExtraArgs = "",
  [int]$TitleTimeout = 60,
  [int]$Settle = 25,
  [int]$Window = 20
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = "$root\port\out\build\win-amd64-release\superman_returns.exe"
$log = "$root\logs\game.log"
$progress = "$root\logs\bench_progress.txt"
$results = "$root\logs\bench_results.csv"
New-Item -ItemType Directory -Force "$root\logs" | Out-Null

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if (-not ([System.Management.Automation.PSTypeName]'SrBenchInput').Type) {
  Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SrBenchInput {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
}

function Step($message) {
  "{0:HH:mm:ss} [{1}] {2}" -f (Get-Date), $Name, $message | Out-File -Append -Encoding utf8 $progress
}

function Focus($proc) {
  [SrBenchInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
  Start-Sleep -Milliseconds 300
}

function Key($vk, [switch]$Down, [switch]$Up) {
  if (-not $Up) { [SrBenchInput]::keybd_event($vk, 0, 0, [UIntPtr]::Zero) }
  if (-not $Down) { Start-Sleep -Milliseconds 120; [SrBenchInput]::keybd_event($vk, 0, 2, [UIntPtr]::Zero) }
}

function Screenshot($path) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save($path)
  $bmp.Dispose()
}

function Measure-Window($proc, $scenario) {
  $lines0 = @(Get-Content $log).Count
  $proc.Refresh(); $cpu0 = $proc.TotalProcessorTime.TotalSeconds
  $gpu = Get-Counter "\GPU Engine(pid_$($proc.Id)*engtype_3D)\Utilization Percentage" `
    -SampleInterval $Window -MaxSamples 1 -ErrorAction SilentlyContinue
  $proc.Refresh(); $cores = ($proc.TotalProcessorTime.TotalSeconds - $cpu0) / $Window
  $gpuPct = ($gpu.CounterSamples | Measure-Object CookedValue -Sum).Sum
  $fps = @(Get-Content $log | Select-Object -Skip $lines0 | Select-String 'guest fps: ([\d.]+)' |
    ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
  Screenshot "$root\logs\bench_${Name}_$scenario.png"
  $row = [pscustomobject]@{
    time = Get-Date -Format s; name = $Name; scenario = $scenario
    avg_fps = [math]::Round(($fps | Measure-Object -Average).Average, 1)
    min_fps = [math]::Round(($fps | Measure-Object -Minimum).Minimum, 1)
    cores = [math]::Round($cores, 1); gpu_pct = [math]::Round($gpuPct, 0); args = $ExtraArgs
  }
  $row | Export-Csv -Append -NoTypeInformation -Encoding utf8 $results
  Step "$scenario avg=$($row.avg_fps) min=$($row.min_fps) cores=$($row.cores) gpu=$($row.gpu_pct)"
  $row
}

Get-Process superman_returns -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
if (Test-Path $log) { Clear-Content $log }
$env:SR_LOG_FPS = '1'
$gameArgs = @(
  "--game_data_root=`"$root\game`"", "--log_file=`"$log`"",
  '--render_target_path_d3d12=rtv', '--depth_float24_convert_in_pixel_shader=true',
  '--mnk_mode', '--sr_skip_intro=true'
) + ($ExtraArgs -split ' ' | Where-Object { $_ })
$proc = Start-Process -FilePath $exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path $exe) -PassThru
Step "started pid $($proc.Id) args: $ExtraArgs"

try {
  # Title screen is up once the game presents frames steadily.
  $deadline = (Get-Date).AddSeconds($TitleTimeout)
  do {
    Start-Sleep 2
    if ($proc.HasExited) { throw "game exited before the title screen" }
    $frames = @(Get-Content $log | Select-String 'guest fps: ([\d.]+)').Count
  } until ($frames -ge 5 -or (Get-Date) -gt $deadline)
  Start-Sleep 5
  Focus $proc; Key 0x0D
  Step "pressed Start"
  Start-Sleep $Settle
  if ($proc.HasExited) { throw "game exited while loading the save" }

  Measure-Window $proc 'idle'
  Focus $proc; Key 0x57 -Down
  try { Measure-Window $proc 'forward' } finally { Key 0x57 -Up }
} catch {
  Step "FAILED: $_"
  throw
} finally {
  if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
