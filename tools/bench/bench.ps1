<#
Open-world benchmark for the Superman Returns recomp.

Boots the game with the intro skipped, starts a new game at the title screen,
lets it settle, then measures two scenarios:
  idle     - standing still for 20 s
  forward  - holding W (move forward) for 20 s
and appends one CSV row per scenario to logs/bench_results.csv
(name, scenario, avg/min guest FPS, CPU cores in use, GPU 3D %, args).
Screenshots of each scenario go to logs/bench_<name>_<scenario>.png.

Any running instance of the game is closed first. The game runs windowed unless
ExtraArgs sets --fullscreen: the HUD check reads a screen capture, which does not
see a fullscreen Vulkan window. It looks for the HUD wherever the window is, and
alternates Space (A) and Enter (Start) at most -MaxSkips times to confirm a late
dialog or skip the opening cinematic. -Exe selects the executable (default: the
win-amd64-release build).

The HUD is detected (bench_hud.ps1) by the wait loop before the run and checked again
right after the forward window, while W is held, with a short poll: the game fades
the HUD after ~20 s without input, so it is not checked after idle. A run that is not
on gameplay (menu, title) throws and leaves logs/bench_<name>_not_gameplay.png.

Usage: tools\bench\bench.ps1 -Name baseline [-ExtraArgs "--native_2x_msaa=false"]
#>
param(
  [Parameter(Mandatory)] [string]$Name,
  [string]$ExtraArgs = "",
  [string]$Exe = "",
  [int]$MaxSkips = 6,
  [int]$TitleTimeout = 60,
  [int]$Settle = 25,
  [int]$Window = 20,
  [int]$WorldTimeout = 90,
  [string]$WindowMetadata = '',
  [string]$CacheAuditCommands = "",
  [switch]$FullGraphicsChecks,
  [switch]$GraphicsMenuChecks,
  [switch]$NativeAptMenuChecks,
  [switch]$NativeKeyboardChecks,
  [switch]$RayShadowChecks,
  [switch]$RequireNoRunningGame
)
$ErrorActionPreference = 'Stop'
if($CacheAuditCommands) {$ExtraArgs += " --sr_native_cache_audit_commands=$CacheAuditCommands"}
if($FullGraphicsChecks -and -not $GraphicsMenuChecks){throw 'FullGraphicsChecks requires GraphicsMenuChecks'}
if($RayShadowChecks -and -not $GraphicsMenuChecks){throw 'RayShadowChecks requires GraphicsMenuChecks'}
. "$PSScriptRoot/bench_window_metadata.ps1"
$script:benchWindows=New-Object System.Collections.ArrayList
if($WindowMetadata -and (Test-Path -LiteralPath $WindowMetadata)){throw 'Window metadata already exists; use a new label'}
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$exe = if ($Exe) { $Exe } else { "$root\port\out\build\win-amd64-release\superman_returns.exe" }
$log = "$root\logs\game.log"
$progress = "$root\logs\bench_progress.txt"
$results = "$root\logs\bench_results.csv"
New-Item -ItemType Directory -Force "$root\logs" | Out-Null

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
. "$PSScriptRoot\bench_hud.ps1"
if (-not ([System.Management.Automation.PSTypeName]'SrBenchInput').Type) {
  Add-Type @"
using System; using System.Runtime.InteropServices;
public struct SrRect { public int Left, Top, Right, Bottom; }
public struct SrPoint { public int X, Y; }
public static class SrBenchInput {
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,EntryPoint="MoveFileExW",SetLastError=true)] public static extern bool MoveFileEx(string source,string destination,uint flags);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out SrRect r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref SrPoint p);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from,uint to,bool attach);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int command);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
}

# Window rectangles and CopyFromScreen must share physical pixels under desktop scaling.
[SrBenchInput]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null

function Step($message) {
  "{0:HH:mm:ss} [{1}] {2}" -f (Get-Date), $Name, $message | Out-File -Append -Encoding utf8 $progress
}

function Focus($proc) {
  if($env:SR_BENCH_GAMEPAD_ONLY -eq '1') { return }
  $proc.Refresh();$handle=$proc.MainWindowHandle
  [SrBenchInput]::ShowWindow($handle,9) | Out-Null
  [SrBenchInput]::SetForegroundWindow($handle) | Out-Null
  if([SrBenchInput]::GetForegroundWindow() -ne $handle) {
    [uint32]$focusProcess=0
    $focusThread=[SrBenchInput]::GetWindowThreadProcessId([SrBenchInput]::GetForegroundWindow(),[ref]$focusProcess)
    $callerThread=[SrBenchInput]::GetCurrentThreadId()
    $attached=[SrBenchInput]::AttachThreadInput($callerThread,$focusThread,$true)
    try {[SrBenchInput]::SetForegroundWindow($handle) | Out-Null}
    finally {if($attached){[SrBenchInput]::AttachThreadInput($callerThread,$focusThread,$false) | Out-Null}}
  }
  Start-Sleep -Milliseconds 300
  if([SrBenchInput]::GetForegroundWindow() -ne $handle) {throw 'Game focus unavailable; physical keyboard input was not sent'}
}

# Publish one complete command, retrying the brief Windows reader sharing window.
function Write-BenchInputLine([string]$line) {
  $path=$env:SR_BENCH_INPUT
  $pending=$path+'.pending'
  [System.IO.File]::WriteAllText($pending,$line+[Environment]::NewLine,[System.Text.Encoding]::ASCII)
  for($attempt=0;$attempt -lt 100;++$attempt) {
    if([SrBenchInput]::MoveFileEx($pending,$path,9)) {return}
    Start-Sleep -Milliseconds 5
  }
  throw 'Cannot publish isolated benchmark input command'
}

function Key($vk, [switch]$Down, [switch]$Up) {
  if($env:SR_BENCH_GAMEPAD_ONLY -eq '1') {
    if(-not $env:SR_BENCH_INPUT) {throw 'Isolated pad test requires SR_BENCH_INPUT'}
    $buttons=switch($vk){0x0D{16};0x20{4096};0x1B{8192};0x47{32768};default{0}}
    $forward=if($vk -eq 0x57 -and -not $Up){32767}else{0}
    if($Up){$buttons=0}
    $hold=if($Down){30000}else{250}
    $id=[DateTime]::UtcNow.Ticks
    Write-BenchInputLine "$id $hold $buttons $forward"
    if(-not $Down){Start-Sleep -Milliseconds 450}
    return
  }
  if (-not $Up) { [SrBenchInput]::keybd_event($vk, 0, 0, [UIntPtr]::Zero) }
  if (-not $Down) { Start-Sleep -Milliseconds 120; [SrBenchInput]::keybd_event($vk, 0, 2, [UIntPtr]::Zero) }
}

function Screenshot($path) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  try {
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    try { $gfx.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size) }
    finally { $gfx.Dispose() }
    $bmp.Save($path)
  } finally { $bmp.Dispose() }
}

# Client area of the game window in screen coordinates, clipped to the primary
# screen (the window may sit anywhere and even hang over the screen edge).
function Game-Region($proc) {
  $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $proc.Refresh()
  $handle = $proc.MainWindowHandle
  $rect = New-Object SrRect
  $origin = New-Object SrPoint
  if ($handle -eq [IntPtr]::Zero -or -not [SrBenchInput]::GetClientRect($handle, [ref]$rect)) { return $screen }
  [SrBenchInput]::ClientToScreen($handle, [ref]$origin) | Out-Null
  $client = New-Object System.Drawing.Rectangle $origin.X, $origin.Y, $rect.Right, $rect.Bottom
  $visible = [System.Drawing.Rectangle]::Intersect($client, $screen)
  if ($visible.Width -lt 200 -or $visible.Height -lt 200) { return $screen }
  return $visible
}

# Gameplay is on screen when the Superman HUD (a thin blue and a thin red bar, top
# left of the game image) is found (Test-SupermanHud, which scans the top left of the
# bitmap it gets, so the whole client area is captured). The title, load prompt and
# opening cinematic can all show a bright rendered scene without it. The capture
# covers the game window wherever it is, so it works windowed and fullscreen.
function Has-Gameplay-Frame($proc) {
  $region = Game-Region $proc
  $bmp = New-Object System.Drawing.Bitmap $region.Width, $region.Height
  try {
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    try { $gfx.CopyFromScreen($region.Location, [System.Drawing.Point]::Empty, $region.Size) }
    finally { $gfx.Dispose() }
    return (Test-SupermanHud $bmp)
  } finally {
    $bmp.Dispose()
  }
}

# The renderer logs the guest's actual pause state when Superman enters/leaves the
# in-game pause menu (state 4/3). This is a stronger gameplay signal than the HUD
# heuristic and also lets the benchmark recover if an Enter used to skip the opening
# cinematic actually opened the pause menu after gameplay had already started.
function Get-LatestGuestPauseLine() {
  return @(Select-String -LiteralPath $log -Pattern 'native graphics guest pause:' -ErrorAction SilentlyContinue) |
    Select-Object -Last 1
}

function Has-VerifiedGameplayRunning() {
  $lastPause = Get-LatestGuestPauseLine
  return $lastPause -and $lastPause.Line -match 'paused=false.*valid=true.*state=3'
}

function Resume-VerifiedGameplayIfPaused($proc) {
  $lastPause = Get-LatestGuestPauseLine
  if(-not $lastPause -or $lastPause.Line -notmatch 'paused=true.*valid=true.*state=4') { return $false }
  $resumeBefore = @(Select-String -LiteralPath $log -Pattern 'native graphics guest pause: paused=false.*valid=true.*state=3').Count
  Focus $proc; Key 0x0D
  $deadline = (Get-Date).AddSeconds(5)
  do {
    Start-Sleep -Milliseconds 250
    $resumeNow = @(Select-String -LiteralPath $log -Pattern 'native graphics guest pause: paused=false.*valid=true.*state=3').Count
  } while($resumeNow -le $resumeBefore -and (Get-Date) -lt $deadline -and -not $proc.HasExited)
  if($resumeNow -gt $resumeBefore) {
    Step 'gameplay qualified by guest pause state; resumed accidental pause from skip input'
    return $true
  }
  return $false
}

# Saves the game window's client area as PNG (the image gate compares it with a reference).
function Save-GameRegion($proc, $path) {
  $region = Game-Region $proc
  $bmp = New-Object System.Drawing.Bitmap $region.Width, $region.Height
  try {
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    try { $gfx.CopyFromScreen($region.Location, [System.Drawing.Point]::Empty, $region.Size) }
    finally { $gfx.Dispose() }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  } finally { $bmp.Dispose() }
}

# Throws (leaving a screenshot) unless the HUD is seen in one of up to 4 samples, one
# second apart (a single sample can miss it on D3D12), so a run on a menu or the
# title screen cannot go unnoticed.
function Assert-Gameplay($proc) {
  for ($i = 0; $i -lt 4; $i++) {
    if ($i -gt 0) { Start-Sleep 1 }
    if ((Has-Gameplay-Frame $proc) -or (Has-VerifiedGameplayRunning)) { return }
  }
  Screenshot "$root\logs\bench_${Name}_not_gameplay.png"
  throw "scene is not gameplay during measurement (menu or title?)"
}

# True when the whole screen capture is black. A fullscreen game window (notably a
# Vulkan one) may not show up in a GDI screen capture, which breaks HUD detection.
function Capture-IsBlack() {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  try {
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    try { $gfx.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size) }
    finally { $gfx.Dispose() }
    for ($y = 1; $y -le 9; $y++) {
      for ($x = 1; $x -le 16; $x++) {
        $pixel = $bmp.GetPixel([int]($x * $b.Width / 17), [int]($y * $b.Height / 10))
        if (($pixel.R + $pixel.G + $pixel.B) -gt 3) { return $false }
      }
    }
    return $true
  } finally { $bmp.Dispose() }
}

function Measure-Window($proc, $scenario) {
  $route="${Name}_$scenario"
  if($CacheAuditCommands) {
    "begin $route" | Set-Content -LiteralPath $CacheAuditCommands -Encoding ASCII
    Start-Sleep -Milliseconds 300
    if(-not (Select-String -LiteralPath $log -Pattern "route_begin name=$route" -Quiet)) {throw "Cache audit begin missing for $route"}
  }
  $lines0 = @(Get-Content $log).Count
  $proc.Refresh(); $cpu0 = $proc.TotalProcessorTime.TotalSeconds
  $windowBegin=[DateTimeOffset]::UtcNow
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  $gpu = Get-Counter "\GPU Engine(pid_$($proc.Id)*engtype_3D)\Utilization Percentage" `
    -SampleInterval $Window -MaxSamples 1 -ErrorAction SilentlyContinue
  # Counter availability or an early first sample must not shorten the window.
  $remaining = $Window - $timer.Elapsed.TotalSeconds
  if ($remaining -gt 0) { Start-Sleep -Milliseconds ([int][math]::Ceiling($remaining * 1000)) }
  $timer.Stop()
  if($CacheAuditCommands) {
    "end $route" | Set-Content -LiteralPath $CacheAuditCommands -Encoding ASCII
    Start-Sleep -Milliseconds 300
    if(-not (Select-String -LiteralPath $log -Pattern "route_end name=$route .*valid=1" -Quiet)) {throw "Cache audit end missing for $route"}
  }
  $windowEnd=[DateTimeOffset]::UtcNow
  $proc.Refresh(); $cores = ($proc.TotalProcessorTime.TotalSeconds - $cpu0) / $timer.Elapsed.TotalSeconds
  $gpuPct = ($gpu.CounterSamples | Measure-Object CookedValue -Sum).Sum
  $fps = @(Get-Content $log | Select-Object -Skip $lines0 | Select-String 'guest fps: ([\d.]+)' |
    ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [System.Globalization.CultureInfo]::InvariantCulture) })
  if (-not $fps.Count) { throw "No guest FPS samples during $scenario; benchmark is invalid" }
  if($WindowMetadata) {
    $metadata=New-BenchWindowMetadata $Name $scenario $windowBegin $windowEnd
    $metadata | Add-Member -NotePropertyName fps_samples -NotePropertyValue @($fps)
    [void]$script:benchWindows.Add($metadata)
  }
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

$existingGame=Get-Process superman_returns -ErrorAction SilentlyContinue
if($RequireNoRunningGame -and $existingGame) {throw 'A game instance is already running; leaving it untouched'}
if(-not $RequireNoRunningGame) {$existingGame | Stop-Process -Force}
Start-Sleep 2
if (Test-Path $log) { Clear-Content $log }
$env:SR_LOG_FPS = '1'
# The HUD check reads a screen capture, which only sees a windowed game window.
if ($ExtraArgs -notmatch '--fullscreen') { $ExtraArgs = ("$ExtraArgs --fullscreen=false").Trim() }
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
  Start-Sleep 3
  Focus $proc; Key 0x20
  Step "selected Start New Game"
  Start-Sleep $Settle
  if ($proc.HasExited) { throw "game exited while loading the save" }
  $worldDeadline = (Get-Date).AddSeconds($WorldTimeout)
  $nextSkip = Get-Date
  $skips = 0; $blackSamples = 0; $samples = 0
  while ($true) {
    Focus $proc
    $frameReady=Has-Gameplay-Frame $proc
    if(-not $frameReady) {
      $frameReady=Resume-VerifiedGameplayIfPaused $proc
      if(-not $frameReady -and (Has-VerifiedGameplayRunning)) {
        $frameReady=$true
        Step 'gameplay qualified by guest pause/resume state after HUD heuristic missed it'
      }
    }
    if($GraphicsMenuChecks -and $env:SR_BENCH_MOTION_CAPTURE_TRIGGER) {
      # Cinematic colors can satisfy the visual HUD heuristic. A motion run
      # must first prove the source-audited gameplay -> pause -> gameplay path.
      $pauseBefore=@(Select-String -LiteralPath $log -Pattern 'native graphics guest pause: paused=true.*state=4').Count
      $lastGuestPause=@(Select-String -LiteralPath $log -Pattern 'native graphics guest pause:') | Select-Object -Last 1
      $alreadyPaused=$lastGuestPause -and $lastGuestPause.Line -match 'paused=true.*state=4'
      if(-not $alreadyPaused) {Key 0x0D}
      $pauseDeadline=(Get-Date).AddSeconds(5)
      do {
        Start-Sleep -Milliseconds 250
        $pauseNow=@(Select-String -LiteralPath $log -Pattern 'native graphics guest pause: paused=true.*state=4').Count
      } while(-not $alreadyPaused -and $pauseNow -le $pauseBefore -and (Get-Date) -lt $pauseDeadline -and -not $proc.HasExited)
      $frameReady=$alreadyPaused -or $pauseNow -gt $pauseBefore
      if($frameReady) {
        $resumeBefore=@(Select-String -LiteralPath $log -Pattern 'native graphics guest pause: paused=false.*state=3').Count
        Key 0x0D
        $resumeDeadline=(Get-Date).AddSeconds(5)
        do {
          Start-Sleep -Milliseconds 250
          $resumeNow=@(Select-String -LiteralPath $log -Pattern 'native graphics guest pause: paused=false.*state=3').Count
        } while($resumeNow -le $resumeBefore -and (Get-Date) -lt $resumeDeadline -and -not $proc.HasExited)
        $frameReady=$resumeNow -gt $resumeBefore
        if($frameReady) {Step 'motion gameplay qualified by actual guest pause/resume; visual HUD alone is insufficient'}
      } else {Step 'visual HUD candidate rejected: no verified gameplay pause (cinematic/loading)'}
    }
    if ($frameReady) { break }
    if ($proc.HasExited) { throw "game exited before rendering the save" }
    $samples++; if (Capture-IsBlack) { $blackSamples++ }
    if ((Get-Date) -gt $worldDeadline) {
      Screenshot "$root\logs\bench_${Name}_not_gameplay.png"
      $why = if ($blackSamples -eq $samples) {
        "every screen capture was black: the game window cannot be captured (fullscreen?); run windowed (--fullscreen=false)"
      } else { "the Superman HUD was never seen" }
      throw "no gameplay frame after $WorldTimeout seconds ($why)"
    }
    # The new-game opening cinematic runs for minutes; Start skips it, and a late
    # "Load a game?" dialog needs A (Space) to select. Space and Enter alternate,
    # Space first. The HUD check above runs first so this never opens the pause
    # menu in gameplay. At most $MaxSkips presses: when the HUD cannot be
    # detected an unbounded loop would keep toggling the pause menu in gameplay.
    if ($skips -lt $MaxSkips -and (Get-Date) -ge $nextSkip) {
      if ($skips % 2 -eq 0) { Key 0x20; $keyName = 'Space (A)' }
      else { Key 0x0D; $keyName = 'Enter (Start)' }
      $skips++
      Step "pressed $keyName to confirm a dialog or skip the cinematic ($skips/$MaxSkips)"
      $nextSkip = (Get-Date).AddSeconds(8)
    }
    Start-Sleep 2
  }
  Step "visible frame detected"
  # Initial camera and pose: the one scene that does not vary between runs (the image gate compares it).
  Save-GameRegion $proc "$root\logs\bench_${Name}_start.png"
  Start-Sleep 5

  # The wait loop above detected the HUD seconds ago, so it is not checked again
  # before idle. The game fades the HUD after ~20 s without input, so it is not
  # checked after idle either; it is asserted right after the forward window while W
  # is still held (input keeps it visible). A menu or title cannot produce a clean
  # run: the HUD must still be visible after the forward window.
  if(-not $GraphicsMenuChecks) {
    Measure-Window $proc 'idle'
    Focus $proc; Key 0x57 -Down
    try {
      Measure-Window $proc 'forward'
      Assert-Gameplay $proc
    } finally { Key 0x57 -Up }
  }
  if($NativeAptMenuChecks) {
    if(-not $GraphicsMenuChecks -or -not $env:SR_BENCH_INPUT) {throw 'Native APT checks require GraphicsMenuChecks and an isolated input command file'}
    function Apt-Count([string]$pattern) {return @(Select-String -LiteralPath $log -Pattern $pattern).Count}
    function Apt-Pad([int]$buttons) {
      Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 150 $buttons 0"
      Start-Sleep -Milliseconds 900
    }
    function Apt-Wait([string]$pattern,[int]$before) {
      $deadline=[DateTime]::UtcNow.AddSeconds(30)
      while([DateTime]::UtcNow -lt $deadline -and -not $proc.HasExited) {
        if((Apt-Count $pattern) -gt $before) {return}
        Start-Sleep -Milliseconds 250
      }
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_failure.png"
      throw "Native APT event missing: $pattern"
    }
    if(-not(Apt-Count 'native APT menu: installed.*overlay=false')) {throw 'Native APT archive override was not installed'}
    $paused=Get-LatestGuestPauseLine
    if(-not $paused -or $paused.Line -notmatch 'paused=true.*state=4') {Apt-Pad 16}
    Start-Sleep -Seconds 2
    Save-GameRegion $proc "$root/logs/bench_${Name}_native_pause.png"
    Apt-Pad 1;Apt-Pad 1 # Resume -> Quit -> Graphics. Disabled Restart does not affect this path.
    Save-GameRegion $proc "$root/logs/bench_${Name}_native_entry.png"
    $opened=Apt-Count 'native APT graphics screen: open=true'
    Apt-Pad 4096;Apt-Wait 'native APT graphics screen: open=true' $opened
    Start-Sleep -Seconds 2
    Save-GameRegion $proc "$root/logs/bench_${Name}_native_graphics.png"
    if($FullGraphicsChecks) {
      function Apt-Change([int]$row,[int]$value,[int]$button) {
        $pattern="native APT graphics request: row=$row value=$value accepted=1"
        $before=Apt-Count $pattern;Apt-Pad $button;Apt-Wait $pattern $before
      }
      # Five original Toggle rows plus a PAGE Toggle. Every expected value is
      # attested by the tagged native callback, never inferred from button count.
      Apt-Change 0 1 8
      $before=Apt-Count 'native visible DXR executed:.*sun_pixels=[1-9].*fence_completed=true'
      Apt-Wait 'native visible DXR executed:.*sun_pixels=[1-9].*fence_completed=true' $before
      Apt-Pad 2;Apt-Change 1 1 4;Apt-Change 1 2 8
      Apt-Pad 2;Apt-Change 2 1 8
      Apt-Pad 2;Apt-Change 3 1 8
      $before=Apt-Count 'native visible DXR executed:.*reflection_pixels=[1-9].*fence_completed=true'
      Apt-Wait 'native visible DXR executed:.*reflection_pixels=[1-9].*fence_completed=true' $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_rt.png"
      Apt-Change 3 0 4;Apt-Pad 1;Apt-Change 2 0 4
      Apt-Pad 1;Apt-Pad 1;Apt-Change 0 0 4
      1..4 | ForEach-Object {Apt-Pad 2}
      foreach($mode in 1..4) {
        $pattern="native DLSS frames:.*applied=$($mode+1)"
        $before=Apt-Count $pattern;Apt-Change 4 $mode 8;Apt-Wait $pattern $before
        Save-GameRegion $proc "$root/logs/bench_${Name}_native_dlss_$mode.png"
      }
      foreach($mode in 3..0) {Apt-Change 4 $mode 4}
      Apt-Pad 2;Apt-Pad 8 # PAGE 2/3, keep focus on PAGE.
      1..5 | ForEach-Object {Apt-Pad 1}
      Apt-Change 5 1 8;Save-GameRegion $proc "$root/logs/bench_${Name}_native_api_restart.png"
      Apt-Change 5 0 4
      Apt-Pad 2
      $before=Apt-Count 'native graphics hot settings: output=1600x900 '
      Apt-Change 6 2 8;Apt-Wait 'native graphics hot settings: output=1600x900 ' $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_resolution.png"
      $before=Apt-Count 'native graphics hot settings: output=1280x720 '
      Apt-Change 6 1 4;Apt-Wait 'native graphics hot settings: output=1280x720 ' $before
      Apt-Pad 2;Apt-Change 7 2 8;Start-Sleep -Seconds 2
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_fps_60.png"
      Apt-Change 7 3 8;Start-Sleep -Seconds 2
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_fps_120.png"
      Apt-Change 7 2 4
      Apt-Pad 2;Apt-Change 8 1 8
      Apt-Pad 2
      $before=Apt-Count 'native graphics hot settings:.*MSAA=4 DLSS_request=0'
      Apt-Change 9 1 8;Apt-Wait 'native graphics hot settings:.*MSAA=4 DLSS_request=0' $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_msaa.png"
      Apt-Pad 2;Apt-Pad 8;Apt-Pad 1 # PAGE3/3 then the sole enabled option.
      $before=Apt-Count 'native graphics hot settings:.*scene=1600x900.*MSAA=4 DLSS_request=0'
      Apt-Change 10 3 8;Apt-Wait 'native graphics hot settings:.*scene=1600x900.*MSAA=4 DLSS_request=0' $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_scale.png"
      Apt-Change 10 2 4
      Apt-Pad 2;Apt-Pad 4;Apt-Pad 4;Apt-Pad 1 # PAGE1/3 -> DLSS.
      $before=Apt-Count 'native DLSS frames:.*applied=2'
      Apt-Change 4 1 8;Apt-Wait 'native DLSS frames:.*applied=2' $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_native_final_quality.png"
      Step 'native APT settings PASS: 11 options, three pages, exact requests, RT counters/fences, four NGX modes, hot resolution/MSAA/scale'
    }
    $resumes=Apt-Count 'native graphics guest pause: paused=false.*state=3'
    $closed=Apt-Count 'native APT graphics screen: open=false'
    Apt-Pad 8192;Apt-Wait 'native APT graphics screen: open=false' $closed
    if((Apt-Count 'native graphics guest pause: paused=false.*state=3') -ne $resumes) {throw 'Native B unexpectedly resumed gameplay'}
    Save-GameRegion $proc "$root/logs/bench_${Name}_native_back.png"
    # Original gameplay options share source assets with the clone; opening both
    # catches class/export/inner-resource cache collisions in one guest session.
    Apt-Pad 1;Apt-Pad 1;Apt-Pad 1;Apt-Pad 4096
    Start-Sleep -Seconds 2
    Save-GameRegion $proc "$root/logs/bench_${Name}_native_original_gameplay.png"
    Apt-Pad 8192;Apt-Pad 2;Apt-Pad 2;Apt-Pad 2
    $opened=Apt-Count 'native APT graphics screen: open=true'
    Apt-Pad 4096;Apt-Wait 'native APT graphics screen: open=true' $opened
    if($NativeKeyboardChecks) {
      $savedPadMode=$env:SR_BENCH_GAMEPAD_ONLY
      try {
        Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 100 0 0"
        Start-Sleep -Milliseconds 400
        $env:SR_BENCH_GAMEPAD_ONLY='0';Focus $proc
        $before=Apt-Count 'native APT graphics request: row=0 value=1 accepted=0'
        Key 0x48
        Apt-Wait 'native APT graphics request: row=0 value=1 accepted=0' $before
        Save-GameRegion $proc "$root/logs/bench_${Name}_native_keyboard.png"
        $closed=Apt-Count 'native APT graphics screen: open=false'
        Key 0x08;Apt-Wait 'native APT graphics screen: open=false' $closed
        Start-Sleep -Milliseconds 1000
        $opened=Apt-Count 'native APT graphics screen: open=true'
        Key 0x20;Apt-Wait 'native APT graphics screen: open=true' $opened
        Start-Sleep -Milliseconds 1000
        Key 0x0D;Apt-Wait 'native graphics guest pause: paused=false.*state=3' $resumes
        Step 'native APT keyboard PASS: physical H (test Right binding), unsupported setting rejected, Backspace back, Space open, Enter resume'
      } finally {$env:SR_BENCH_GAMEPAD_ONLY=$savedPadMode}
    } else {Apt-Pad 16;Apt-Wait 'native graphics guest pause: paused=false.*state=3' $resumes}
    Save-GameRegion $proc "$root/logs/bench_${Name}_native_resumed.png"
    Step 'native APT navigation PASS: original Pause/Toggle UI, A open, B back, selected entry preserved, Start resume'
  }
  if($GraphicsMenuChecks -and -not $NativeAptMenuChecks) {
    if(-not $env:SR_BENCH_INPUT) {throw 'GraphicsMenuChecks requires a dedicated SR_BENCH_INPUT command file'}
    function Count-GraphicsEvent([string]$pattern) {
      return @(Select-String -LiteralPath $log -Pattern $pattern).Count
    }
    if($env:SR_BENCH_RT_CAPTURE_TRIGGER) {
      'capture' | Set-Content -LiteralPath $env:SR_BENCH_RT_CAPTURE_TRIGGER -Encoding ASCII
      Start-Sleep -Seconds 2
    }
    if($env:SR_BENCH_MOTION_CAPTURE_TRIGGER) {
      # Exercise actual guest movement across the two captured frames.
      Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 1800 0 32767"
      Start-Sleep -Milliseconds 500
      'capture' | Set-Content -LiteralPath $env:SR_BENCH_MOTION_CAPTURE_TRIGGER -Encoding ASCII
      Start-Sleep -Seconds 3
      Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 100 0 0"
      Step 'two-frame motion capture requested during guest movement'
    }
    Step 'graphics checks only; no performance measurement recorded'
    $resumes=Count-GraphicsEvent 'native graphics guest pause: paused=false.*state=3'
    $lastPause=@(Select-String -LiteralPath $log -Pattern 'native graphics guest pause:') | Select-Object -Last 1
    Focus $proc
    if(-not $lastPause -or $lastPause.Line -notmatch 'paused=true') { Key 0x0D }
    Start-Sleep -Seconds 2
    if(-not(Count-GraphicsEvent 'native graphics guest pause: paused=true.*state=4')) {throw 'Verified guest pause was not entered'}
    Save-GameRegion $proc "$root/logs/bench_${Name}_pause.png"
    Key 0x47; Start-Sleep -Seconds 2
    if((Count-GraphicsEvent 'native graphics panel: open=true') -ne 1) {throw 'G did not open GRAPHICS OPTIONS'}
    Save-GameRegion $proc "$root/logs/bench_${Name}_graphics_keyboard.png"
    if($RayShadowChecks) {
      function Send-GraphicsPad([int]$buttons) {
        Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 300 $buttons 0"
        Start-Sleep -Milliseconds 800
      }
      function Wait-RayShadow([int]$applied,[int]$softness,[int]$before) {
        $pattern="native RT shadows executed:.*applied=$applied softness=$softness fence_completed=true"
        $deadline=[DateTime]::UtcNow.AddSeconds(40)
        while([DateTime]::UtcNow -lt $deadline) {
          if((Count-GraphicsEvent $pattern) -gt $before){return}
          Start-Sleep -Milliseconds 500
        }
        throw "RT shader execution was not confirmed: applied=$applied softness=$softness"
      }
      $before=Count-GraphicsEvent 'native RT shadows executed:.*applied=1 softness=2 fence_completed=true'
      Send-GraphicsPad 8 # Right on RT SHADOWS.
      Wait-RayShadow 1 2 $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_rt_high.png"
      Send-GraphicsPad 2 # Down to SHADOW SOFTNESS.
      $before=Count-GraphicsEvent 'native RT shadows executed:.*applied=1 softness=1 fence_completed=true'
      Send-GraphicsPad 4
      Wait-RayShadow 1 1 $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_rt_low.png"
      $before=Count-GraphicsEvent 'native RT shadows executed:.*applied=1 softness=2 fence_completed=true'
      Send-GraphicsPad 8
      Wait-RayShadow 1 2 $before
      Send-GraphicsPad 1 # Back to RT SHADOWS.
      $before=Count-GraphicsEvent 'native RT shadows executed:.*applied=0 softness=0 fence_completed=true'
      Send-GraphicsPad 4
      Wait-RayShadow 0 0 $before
      Step 'RT shadow menu On/Low/High/Off PASS: actual shader counter and queue fence confirmed'
    }
    if($FullGraphicsChecks) {
      function Full-Pad([int]$buttons) {
        Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 150 $buttons 0"
        Start-Sleep -Milliseconds 650
      }
      function Wait-GraphicsLog([string]$pattern,[int]$before) {
        $deadline=[DateTime]::UtcNow.AddSeconds(35)
        while([DateTime]::UtcNow -lt $deadline) {
          if((Count-GraphicsEvent $pattern) -gt $before){return}
          Start-Sleep -Milliseconds 300
        }
        throw "Graphics runtime event missing: $pattern"
      }
      # Start at row zero, with the explicit off/720p/30/1x options of the wrapper.
      1..4 | ForEach-Object {Full-Pad 2}
      foreach($mode in 1..4) {
        $pattern="native DLSS frames:.*applied=$($mode+1)"
        $before=Count-GraphicsEvent $pattern
        Full-Pad 8;Wait-GraphicsLog $pattern $before
        Save-GameRegion $proc "$root/logs/bench_${Name}_dlss_$mode.png"
      }
      1..4 | ForEach-Object {Full-Pad 4} # DLAA -> Off.
      Full-Pad 2;Full-Pad 8 # API -> Vulkan, deferred until restart.
      Save-GameRegion $proc "$root/logs/bench_${Name}_api_restart.png"
      Full-Pad 4 # Retain this test's D3D12 device/next startup.
      Full-Pad 2 # Resolution.
      $pattern='native graphics hot settings: output=1600x900 '
      $before=Count-GraphicsEvent $pattern;Full-Pad 8;Wait-GraphicsLog $pattern $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_resolution_1600.png"
      Full-Pad 4 # 1280x720.
      Full-Pad 2 # FPS.
      Full-Pad 8;Start-Sleep -Seconds 2 # 60.
      Save-GameRegion $proc "$root/logs/bench_${Name}_fps_60.png"
      Full-Pad 8;Start-Sleep -Seconds 2 # 120.
      Save-GameRegion $proc "$root/logs/bench_${Name}_fps_120.png"
      Full-Pad 4 # 60.
      Full-Pad 2;Full-Pad 8 # FXAA On.
      Save-GameRegion $proc "$root/logs/bench_${Name}_fxaa.png"
      Full-Pad 2 # MSAA 4x.
      $pattern='native graphics hot settings:.*MSAA=4 DLSS_request=0'
      $before=Count-GraphicsEvent $pattern;Full-Pad 8;Wait-GraphicsLog $pattern $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_msaa.png"
      Full-Pad 2 # Internal multiplier 125%.
      $pattern='native graphics hot settings:.*scene=1600x900.*MSAA=4 DLSS_request=0'
      $before=Count-GraphicsEvent $pattern;Full-Pad 8;Wait-GraphicsLog $pattern $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_internal_125.png"
      Full-Pad 4 # Restore 100% before temporal modes; the SDK rejects out-of-range multipliers.
      # Exercise hot MSAA exclusion, then persist a real five-mode value.
      1..6 | ForEach-Object {Full-Pad 1} # row4 DLSS.
      $pattern='native DLSS frames:.*applied=2'
      $before=Count-GraphicsEvent $pattern;Full-Pad 8;Wait-GraphicsLog $pattern $before
      Save-GameRegion $proc "$root/logs/bench_${Name}_final_quality.png"
      Step 'full graphics runtime checks PASS: all NGX modes/fences, API restart request, output, FPS, FXAA, MSAA, internal multiplier'
    }
    Key 0x0D; Start-Sleep -Seconds 1
    if((Count-GraphicsEvent 'native graphics guest pause: paused=false.*state=3') -ne $resumes) {throw 'Start leaked through the open graphics panel'}
    Key 0x1B; Start-Sleep -Seconds 2
    if((Count-GraphicsEvent 'native graphics panel: open=false') -ne 1) {throw 'Esc did not return from graphics panel'}
    if((Count-GraphicsEvent 'native graphics guest pause: paused=false.*state=3') -ne $resumes) {throw 'Esc unpaused the guest instead of returning to pause'}
    Save-GameRegion $proc "$root/logs/bench_${Name}_back_to_pause.png"
    # Synthetic pad drives the actual merged XInput path, before guest filtering.
    Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 700 32768 0"
    Start-Sleep -Seconds 2
    if((Count-GraphicsEvent 'native graphics panel: open=true') -ne 2) {throw 'Gamepad Y did not reopen GRAPHICS OPTIONS'}
    Save-GameRegion $proc "$root/logs/bench_${Name}_graphics_gamepad.png"
    Write-BenchInputLine "$([DateTime]::UtcNow.Ticks) 500 8192 0"
    Start-Sleep -Seconds 2
    if((Count-GraphicsEvent 'native graphics panel: open=false') -ne 2) {throw 'Gamepad B did not return from GRAPHICS OPTIONS'}
    if((Count-GraphicsEvent 'native graphics guest pause: paused=false.*state=3') -ne $resumes) {throw 'Gamepad B unpaused the guest'}
    Key 0x0D; Start-Sleep -Seconds 2
    if((Count-GraphicsEvent 'native graphics guest pause: paused=false.*state=3') -ne $resumes+1) {throw 'Guest did not resume normally after graphics input was released'}
    if($env:SR_BENCH_GAMEPAD_ONLY -eq '1') {
      Step 'graphics pause integration PASS: isolated synthetic Y/B/Start, suppression, same pause, normal resume; physical keyboard not tested'
    }else{
      Step 'graphics pause integration PASS: G/Esc, Y/B, Start suppression, same pause, normal resume'
    }
  }
} catch {
  Step "FAILED: $_"
  throw
} finally {
  if($GraphicsMenuChecks -and -not $proc.HasExited) {
    # Exercise the application's normal close/persistence hook in graphics
    # integration checks. Other benchmark modes retain their original teardown.
    if($proc.CloseMainWindow()) {
      if($proc.WaitForExit(15000)) { Step 'graphics normal window close completed' }
      else { Step 'graphics window close timed out; force termination required' }
    }
  }
  if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
  if($WindowMetadata) {
    [pscustomobject]@{schema=1;name=$Name;windows=@($script:benchWindows.ToArray())} |
      ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $WindowMetadata -Encoding UTF8
  }
}
