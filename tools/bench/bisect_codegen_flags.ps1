<#
Tests the codegen register options of port\superman_returns_manifest.toml one
at a time. Turned on together they left the game on a black screen without a
single frame; this finds out which of them (if any) boots on its own.

For the baseline (every option false, unless -NoBaseline) and then for each
option alone, the script:
  1. writes the manifest with only that option set to true;
  2. runs the codegen (.tools\rexglue-sdk\win-amd64\bin\rexglue.exe codegen,
     from port\, as the CMake codegen step does) and counts REX_FATAL in
     port\generated\default\;
  3. runs build.cmd;
  4. starts the game with --sr_skip_intro=true and SR_LOG_FPS=1 and waits for
     -FpsLines "guest fps" lines in its log (one line = frames presented in a
     2 s window); no line before -BootTimeout = no frame, exit = crash;
  5. if it booted, runs tools\bench\bench.ps1 -Name codegen_<option>.
The original manifest bytes are restored at the end, also on failure or
Ctrl+C, and the executable is rebuilt from them (skip with -NoRebuild; the
binary left behind is then built with the last option tested). The
committed manifest is never changed by this script: do not commit the
temporary edits if the script is interrupted hard (check git diff).

Every option needs a full codegen and a rebuild of the generated code, so
expect a long run (each step's time is logged).

Output, all under logs\ (ignored by Git):
  codegen_bisect.csv              one row per option
  codegen_bisect\<option>_*.log   codegen, build and game logs
  codegen_bisect\manifest.backup.toml

Usage:
  powershell -File tools\bench\bisect_codegen_flags.ps1
  powershell -File tools\bench\bisect_codegen_flags.ps1 -Flags ctr_as_local,xer_as_local -SkipBench
#>
param(
  [ValidateSet('cr_as_local', 'ctr_as_local', 'xer_as_local', 'reserved_as_local',
    'non_argument_as_local')]
  [string[]]$Flags = @('cr_as_local', 'ctr_as_local', 'xer_as_local', 'reserved_as_local',
    'non_argument_as_local'),
  [switch]$NoBaseline,
  [switch]$SkipBench,
  [switch]$NoRebuild,
  [ValidateRange(30, 1800)] [int]$BootTimeout = 180,
  [ValidateRange(1, 100)] [int]$FpsLines = 5
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$port = Join-Path $root 'port'
$manifest = Join-Path $port 'superman_returns_manifest.toml'
$generated = Join-Path $port 'generated\default'
$rexglue = Join-Path $root '.tools\rexglue-sdk\win-amd64\bin\rexglue.exe'
$buildCmd = Join-Path $root 'build.cmd'
$exe = Join-Path $port 'out\build\win-amd64-release\superman_returns.exe'
$game = Join-Path $root 'game'
$logs = Join-Path $root 'logs'
$out = Join-Path $logs 'codegen_bisect'
$csv = Join-Path $logs 'codegen_bisect.csv'
$backup = Join-Path $out 'manifest.backup.toml'
$allFlags = @('cr_as_local', 'ctr_as_local', 'xer_as_local', 'reserved_as_local',
  'non_argument_as_local')

foreach ($path in @($rexglue, $buildCmd, (Join-Path $game 'default.xex'))) {
  if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path (see README, Preparação no Windows)" }
}
if (Get-Process superman_returns -ErrorAction SilentlyContinue) {
  throw 'Close the running game first'
}
New-Item -ItemType Directory -Force $out | Out-Null

# The manifest is edited as text so everything else (comments, hooks, the
# function list) stays byte-identical. Every option must currently be false.
$original = [System.IO.File]::ReadAllBytes($manifest)
$text = [System.Text.Encoding]::UTF8.GetString($original)
foreach ($flag in $allFlags) {
  if ($text -notmatch "(?m)^\s*$flag\s*=\s*false\s*$") {
    throw "$flag is not 'false' in the manifest; restore it (git checkout port/superman_returns_manifest.toml)"
  }
}
[System.IO.File]::WriteAllBytes($backup, $original)

function Step($message) {
  "{0:HH:mm:ss} {1}" -f (Get-Date), $message | Tee-Object -Append -FilePath (Join-Path $out 'progress.txt')
}

function Write-Manifest($enabled) {
  $t = $text
  if ($enabled) {
    $t = [regex]::Replace($t, "(?m)^(\s*)$enabled(\s*)=(\s*)false", "`${1}$enabled`${2}=`${3}true")
  }
  [System.IO.File]::WriteAllBytes($manifest, [System.Text.Encoding]::UTF8.GetBytes($t))
}

# cmd.exe runs the tool so its output (stdout and stderr) goes to one log
# file and a non-zero exit code does not throw in Windows PowerShell.
function Run-Logged($file, $arguments, $workDir, $log) {
  $cmdLine = "`"`"$file`" $arguments > `"$log`" 2>&1`""
  $p = Start-Process -FilePath $env:ComSpec -ArgumentList '/d', '/c', $cmdLine `
    -WorkingDirectory $workDir -NoNewWindow -Wait -PassThru
  return $p.ExitCode
}

function Codegen-And-Build($label) {
  $r = @{ codegen = ''; rex_fatal = ''; build = ''; codegen_s = ''; build_s = '' }
  $t0 = Get-Date
  $code = Run-Logged $rexglue "codegen `"$manifest`"" $port (Join-Path $out "${label}_codegen.log")
  $r.codegen_s = [int]((Get-Date) - $t0).TotalSeconds
  $r.codegen = if ($code -eq 0) { 'ok' } else { "failed($code)" }
  if (Test-Path -LiteralPath $generated) {
    $r.rex_fatal = @(Get-ChildItem -LiteralPath $generated -Recurse -Include *.cpp, *.h |
      Select-String -SimpleMatch 'REX_FATAL').Count
  }
  Step "$label codegen $($r.codegen) in $($r.codegen_s) s, REX_FATAL=$($r.rex_fatal)"
  if ($code -ne 0) { return $r }
  $t0 = Get-Date
  $code = Run-Logged $buildCmd '' $root (Join-Path $out "${label}_build.log")
  $r.build_s = [int]((Get-Date) - $t0).TotalSeconds
  $r.build = if ($code -eq 0 -and (Test-Path -LiteralPath $exe)) { 'ok' } else { "failed($code)" }
  Step "$label build $($r.build) in $($r.build_s) s"
  return $r
}

function Boot-Test($label) {
  $log = Join-Path $out "${label}_game.log"
  if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
  $env:SR_LOG_FPS = '1'
  $gameArgs = @("--game_data_root=`"$game`"", "--log_file=`"$log`"", '--sr_skip_intro=true')
  $t0 = Get-Date
  $proc = Start-Process -FilePath $exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path $exe) -PassThru
  $r = @{ boot = ''; fps_lines = 0; first_fps = ''; max_fps = ''; boot_s = ''; exit_code = '' }
  try {
    $deadline = $t0.AddSeconds($BootTimeout)
    $fps = @()
    while ($true) {
      Start-Sleep 2
      if (Test-Path -LiteralPath $log) {
        $fps = @(Get-Content -LiteralPath $log | Select-String 'guest fps: ([\d.]+)' |
          ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
      }
      $proc.Refresh()
      if ($fps.Count -ge $FpsLines) { $r.boot = 'booted'; break }
      if ($proc.HasExited) { $r.boot = 'crashed'; $r.exit_code = $proc.ExitCode; break }
      if ((Get-Date) -gt $deadline) {
        $r.boot = if ($fps.Count) { 'slow' } else { 'no-frames' }
        break
      }
    }
    $r.boot_s = [int]((Get-Date) - $t0).TotalSeconds
    $r.fps_lines = $fps.Count
    if ($fps.Count) {
      $r.first_fps = $fps[0]
      $r.max_fps = ($fps | Measure-Object -Maximum).Maximum
    }
  } finally {
    $proc.Refresh()
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
  }
  Step "$label boot $($r.boot) after $($r.boot_s) s, $($r.fps_lines) fps lines, max $($r.max_fps)"
  return $r
}

function Bench($label) {
  $r = @{ idle_avg = ''; idle_min = ''; forward_avg = ''; forward_min = ''; bench = '' }
  $name = "codegen_$label"
  try {
    & (Join-Path $PSScriptRoot 'bench.ps1') -Name $name | Out-Null
    $r.bench = 'ok'
  } catch {
    $r.bench = "failed: $_"
  }
  $benchLog = Join-Path $logs 'game.log'
  if (Test-Path -LiteralPath $benchLog) {
    Copy-Item -LiteralPath $benchLog (Join-Path $out "${label}_bench_game.log") -Force
  }
  $results = Join-Path $logs 'bench_results.csv'
  if (Test-Path -LiteralPath $results) {
    $rows = @(Import-Csv -LiteralPath $results | Where-Object { $_.name -eq $name })
    foreach ($scenario in @('idle', 'forward')) {
      $row = @($rows | Where-Object { $_.scenario -eq $scenario } | Select-Object -Last 1)
      if ($row.Count) {
        $r["${scenario}_avg"] = $row[0].avg_fps
        $r["${scenario}_min"] = $row[0].min_fps
      }
    }
  }
  Step "$label bench $($r.bench) idle $($r.idle_avg)/$($r.idle_min) forward $($r.forward_avg)/$($r.forward_min)"
  return $r
}

$cases = @()
if (-not $NoBaseline) { $cases += 'baseline' }
$cases += $Flags
$results = @()
$completed = $false
try {
  foreach ($case in $cases) {
    Step "=== $case ==="
    Write-Manifest $(if ($case -eq 'baseline') { $null } else { $case })
    $row = [ordered]@{ option = $case }
    $build = Codegen-And-Build $case
    foreach ($k in 'codegen', 'rex_fatal', 'codegen_s', 'build', 'build_s') { $row[$k] = $build[$k] }
    $boot = @{ boot = 'not-run'; fps_lines = ''; first_fps = ''; max_fps = ''; boot_s = ''; exit_code = '' }
    $bench = @{ bench = 'not-run'; idle_avg = ''; idle_min = ''; forward_avg = ''; forward_min = '' }
    if ($build.build -eq 'ok') {
      $boot = Boot-Test $case
      if ($boot.boot -eq 'booted' -and -not $SkipBench) { $bench = Bench $case }
    }
    foreach ($k in 'boot', 'boot_s', 'fps_lines', 'first_fps', 'max_fps', 'exit_code') { $row[$k] = $boot[$k] }
    foreach ($k in 'bench', 'idle_avg', 'idle_min', 'forward_avg', 'forward_min') { $row[$k] = $bench[$k] }
    $results += [pscustomobject]$row
    # Rewritten after every option so an interrupted run keeps what it has.
    $results | Export-Csv -NoTypeInformation -Encoding utf8 $csv
  }
  $completed = $true
} finally {
  [System.IO.File]::WriteAllBytes($manifest, $original)
  Step 'manifest restored'
  if (-not $completed) {
    Step 'interrupted: the executable may still be built with a tested option; run build.cmd'
  }
}

if (-not $NoRebuild) {
  Step '=== rebuild from the restored manifest ==='
  $final = Codegen-And-Build 'restored'
  if ($final.build -ne 'ok') { Step 'WARNING: the rebuild from the restored manifest failed' }
} else {
  Step 'NoRebuild: the executable is built with the last option tested; run build.cmd'
}

$results | Format-Table option, codegen, rex_fatal, build, boot, fps_lines, max_fps,
  idle_avg, forward_avg -AutoSize | Out-String | Write-Output
Write-Output "Results: $csv"
