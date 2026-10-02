<#
A/B check of sr_post_effects (bloom / light rays) with tools\bench.ps1.

Runs tools\bench.ps1 in pairs, --sr_post_effects=true and =false, swapping
the order in every other pair (on/off, off/on, ...) so warm-up and heat do not
always favour the same side. For each run it keeps logs\game.log as
logs\post_effects_<Name>_<on|off>_<i>.log and checks what the filter did:
  active    the log reports skipped passes and resolves
  no-match  the filter ran but skipped nothing (the table does not match)
  inactive  the option had no effect (build without tools\setup_gpu_source.ps1)

Then it compares the idle and forward screenshots of each pair
(logs\bench_<Name>_<on|off>_<i>_<scenario>.png): mean luma of both, mean
absolute difference, share of changed pixels, and an amplified difference
image logs\post_effects_<Name>_<i>_<scenario>_diff.png. A near-black or
near-white "off" image is flagged. The screenshots are taken at different
moments of each run, so small differences are expected everywhere; look at
the images themselves before calling the option correct: HUD present, scene
present, no frozen or garbage glow.

-Trace also records a short trace with the option off
(tools\capture_gpu_trace.ps1) and summarizes it with
tools\post_effects_trace.py: skipped events per frame, skipped resolves that
also clear EDRAM, and the draw order of one frame.

Results: logs\post_effects_<Name>.csv (one row per run and scenario) and
logs\post_effects_<Name>_summary.txt. Nothing here was run by its author:
the numbers are whatever this machine measures.

Usage:
  powershell -File tools\post_effects_check.ps1
  powershell -File tools\post_effects_check.ps1 -Pairs 3 -Trace
  powershell -File tools\post_effects_check.ps1 -Name postfx_rov -ExtraArgs "--render_target_path_d3d12=rov"
#>
param(
  [ValidatePattern('^[A-Za-z0-9_-]+$')] [string]$Name = 'postfx',
  [ValidateRange(1, 10)] [int]$Pairs = 2,
  [string]$ExtraArgs = '',
  [switch]$Trace,
  [ValidateRange(1, 600)] [int]$TraceFrames = 30,
  [ValidateRange(0, 255)] [int]$Threshold = 24
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$logs = Join-Path $root 'logs'
$exe = Join-Path $root 'port\out\build\win-amd64-release\superman_returns.exe'
$gameLog = Join-Path $logs 'game.log'
$benchCsv = Join-Path $logs 'bench_results.csv'
$outCsv = Join-Path $logs "post_effects_$Name.csv"
$summary = Join-Path $logs "post_effects_${Name}_summary.txt"
if (-not (Test-Path -LiteralPath $exe)) { throw 'Run build.cmd first' }
New-Item -ItemType Directory -Force $logs | Out-Null
foreach ($path in @($outCsv, $summary)) {
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}

Add-Type -AssemblyName System.Drawing
if (-not ([System.Management.Automation.PSTypeName]'SrPostFxDiff').Type) {
  # Windows PowerShell 5.1 has everything in System.Drawing; PowerShell 7
  # forwards the types to these assemblies.
  $drawing = @('System.Drawing')
  if ($PSVersionTable.PSEdition -eq 'Core') {
    $drawing = @('System.Drawing.Common', 'System.Drawing.Primitives',
      'System.Runtime.InteropServices')
    try {
      [void][System.Reflection.Assembly]::Load('System.Private.Windows.Core')
      $drawing += 'System.Private.Windows.Core'
    } catch { }
  }
  Add-Type -ReferencedAssemblies $drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class SrPostFxDiff {
  static byte[] Pixels(Bitmap bmp, out int stride) {
    Rectangle rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
    BitmapData data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    try {
      stride = data.Stride;
      byte[] buffer = new byte[data.Stride * data.Height];
      Marshal.Copy(data.Scan0, buffer, 0, buffer.Length);
      return buffer;
    } finally { bmp.UnlockBits(data); }
  }
  // {mean luma A, mean luma B, mean abs difference, % pixels over threshold}
  public static double[] Compare(string a, string b, string diffPath, int threshold) {
    using (Bitmap ba = new Bitmap(a))
    using (Bitmap bb = new Bitmap(b)) {
      if (ba.Width != bb.Width || ba.Height != bb.Height)
        throw new ArgumentException("screenshots differ in size");
      int w = ba.Width, h = ba.Height, sa, sb;
      byte[] pa = Pixels(ba, out sa);
      byte[] pb = Pixels(bb, out sb);
      using (Bitmap diff = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
        Rectangle rect = new Rectangle(0, 0, w, h);
        BitmapData dd = diff.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        byte[] pd = new byte[dd.Stride * h];
        double lumaA = 0, lumaB = 0, sum = 0;
        long changed = 0;
        for (int y = 0; y < h; y++) {
          for (int x = 0; x < w; x++) {
            int ia = y * sa + x * 4, ib = y * sb + x * 4, id = y * dd.Stride + x * 4;
            lumaA += 0.114 * pa[ia] + 0.587 * pa[ia + 1] + 0.299 * pa[ia + 2];
            lumaB += 0.114 * pb[ib] + 0.587 * pb[ib + 1] + 0.299 * pb[ib + 2];
            int most = 0;
            for (int c = 0; c < 3; c++) {
              int d = Math.Abs(pa[ia + c] - pb[ib + c]);
              sum += d;
              if (d > most) most = d;
              pd[id + c] = (byte)Math.Min(255, d * 4);
            }
            pd[id + 3] = 255;
            if (most > threshold) changed++;
          }
        }
        Marshal.Copy(pd, 0, dd.Scan0, pd.Length);
        diff.UnlockBits(dd);
        diff.Save(diffPath, ImageFormat.Png);
        double n = (double)w * h;
        return new double[] { lumaA / n, lumaB / n, sum / (n * 3), 100.0 * changed / n };
      }
    }
  }
}
"@
}

$lines = New-Object System.Collections.Generic.List[string]
function Say($text) {
  Write-Output $text
  $lines.Add($text)
}

# What the filter did in the run whose log is $path.
function Filter-Status($path, $state) {
  if (-not (Test-Path -LiteralPath $path)) { return @{ status = 'no-log'; passes = ''; resolves = '' } }
  $text = Get-Content -LiteralPath $path
  $counts = @($text | Select-String 'post effects off: skipped (\d+) passes, (\d+) resolves')
  $started = @($text | Select-String 'post effects off, skipping').Count -gt 0
  $noGpu = @($text | Select-String 'sr_post_effects=false requires').Count -gt 0
  if ($state -eq 'on') {
    $s = if ($started) { 'UNEXPECTED-off' } else { 'stock' }
    return @{ status = $s; passes = ''; resolves = '' }
  }
  if ($counts.Count -gt 0) {
    $last = $counts[-1].Matches[0].Groups
    $p = [int64]$last[1].Value; $r = [int64]$last[2].Value
    $s = if ($p -gt 0) { 'active' } else { 'no-match' }
    return @{ status = $s; passes = $p; resolves = $r }
  }
  if ($noGpu -or -not $started) { return @{ status = 'inactive'; passes = ''; resolves = '' } }
  # Started, but the run ended before the first report (every 600 frames).
  return @{ status = 'started'; passes = ''; resolves = '' }
}

$runs = @()
for ($i = 1; $i -le $Pairs; $i++) {
  $order = if ($i % 2 -eq 1) { @('on', 'off') } else { @('off', 'on') }
  foreach ($state in $order) {
    $runName = "${Name}_${state}_$i"
    $value = if ($state -eq 'on') { 'true' } else { 'false' }
    # sr_post_effects lives in the Xenos command processor; the native renderer ignores it.
    $bench = "--sr_renderer=xenos --sr_post_effects=$value $ExtraArgs".Trim()
    $failed = ''
    try {
      & (Join-Path $PSScriptRoot 'bench.ps1') -Name $runName -ExtraArgs $bench | Out-Null
    } catch {
      $failed = "$_"
    }
    $kept = Join-Path $logs "post_effects_$runName.log"
    if (Test-Path -LiteralPath $gameLog) { Copy-Item -LiteralPath $gameLog $kept -Force }
    $filter = Filter-Status $kept $state
    $runs += [pscustomobject]@{
      pair = $i; state = $state; name = $runName; failed = $failed
      filter = $filter.status; passes = $filter.passes; resolves = $filter.resolves
    }
    $line = "pair $i $state : filter=$($filter.status)"
    if ($filter.passes -ne '') { $line += " passes=$($filter.passes) resolves=$($filter.resolves)" }
    if ($failed) { $line += " BENCH FAILED: $failed" }
    Say $line
  }
}

# Frame rate: the rows bench.ps1 appended for these runs.
$bench = @()
if (Test-Path -LiteralPath $benchCsv) {
  $names = $runs | ForEach-Object { $_.name }
  $bench = @(Import-Csv -LiteralPath $benchCsv | Where-Object { $names -contains $_.name })
}
$rows = @()
foreach ($run in $runs) {
  foreach ($scenario in @('idle', 'forward')) {
    $b = @($bench | Where-Object { $_.name -eq $run.name -and $_.scenario -eq $scenario } |
      Select-Object -Last 1)
    $rows += [pscustomobject]@{
      pair = $run.pair; state = $run.state; scenario = $scenario
      avg_fps = if ($b.Count) { $b[0].avg_fps } else { '' }
      min_fps = if ($b.Count) { $b[0].min_fps } else { '' }
      gpu_pct = if ($b.Count) { $b[0].gpu_pct } else { '' }
      filter = $run.filter; passes = $run.passes; resolves = $run.resolves
      luma = ''; mean_diff = ''; changed_pct = ''; note = $run.failed
    }
  }
}

# Screenshots: off against on of the same pair and scenario.
foreach ($row in @($rows | Where-Object { $_.state -eq 'off' })) {
  $on = Join-Path $logs ("bench_{0}_on_{1}_{2}.png" -f $Name, $row.pair, $row.scenario)
  $off = Join-Path $logs ("bench_{0}_off_{1}_{2}.png" -f $Name, $row.pair, $row.scenario)
  if (-not ((Test-Path -LiteralPath $on) -and (Test-Path -LiteralPath $off))) {
    $row.note = ($row.note + ' no screenshot pair').Trim()
    continue
  }
  $diff = Join-Path $logs ("post_effects_{0}_{1}_{2}_diff.png" -f $Name, $row.pair, $row.scenario)
  $m = [SrPostFxDiff]::Compare($on, $off, $diff, $Threshold)
  $row.luma = [math]::Round($m[1], 1)
  $row.mean_diff = [math]::Round($m[2], 1)
  $row.changed_pct = [math]::Round($m[3], 1)
  $onRow = $rows | Where-Object { $_.state -eq 'on' -and $_.pair -eq $row.pair -and
    $_.scenario -eq $row.scenario } | Select-Object -First 1
  if ($onRow) { $onRow.luma = [math]::Round($m[0], 1) }
  if ($m[1] -lt 10) { $row.note = ($row.note + ' OFF IMAGE NEARLY BLACK').Trim() }
  elseif ($m[1] -gt 245) { $row.note = ($row.note + ' OFF IMAGE NEARLY WHITE').Trim() }
}
$rows | Export-Csv -NoTypeInformation -Encoding utf8 $outCsv

Say ''
Say 'scenario  state  avg_fps  min_fps  gpu%  luma  mean_diff  changed%  note'
foreach ($row in $rows) {
  Say ('{0,-8}  {1,-5}  {2,7}  {3,7}  {4,4}  {5,4}  {6,9}  {7,8}  {8}' -f $row.scenario,
    $row.state, $row.avg_fps, $row.min_fps, $row.gpu_pct, $row.luma, $row.mean_diff,
    $row.changed_pct, $row.note)
}
Say ''
foreach ($scenario in @('idle', 'forward')) {
  $avg = @{}
  foreach ($state in @('on', 'off')) {
    $values = @($rows | Where-Object { $_.scenario -eq $scenario -and $_.state -eq $state -and
      $_.avg_fps -ne '' } | ForEach-Object { [double]$_.avg_fps })
    $avg[$state] = if ($values.Count) { ($values | Measure-Object -Average).Average } else { $null }
  }
  if ($avg['on'] -and $avg['off']) {
    Say ('{0}: on {1:N1} FPS, off {2:N1} FPS ({3:+0.0;-0.0}%) over {4} pair(s)' -f $scenario,
      $avg['on'], $avg['off'], (100 * ($avg['off'] / $avg['on'] - 1)), $Pairs)
  } else {
    Say "${scenario}: missing measurements"
  }
}
if (@($runs | Where-Object { $_.state -eq 'off' -and $_.filter -ne 'active' }).Count) {
  Say 'WARNING: some "off" runs did not report skipped passes; see the filter column above.'
}

if ($Trace) {
  $traceName = "${Name}_off_trace"
  $traceArgs = "--sr_post_effects=false $ExtraArgs".Trim()
  Say ''
  Say "trace with the option off ($TraceFrames frames):"
  try {
    & (Join-Path $PSScriptRoot 'capture_gpu_trace.ps1') -Name $traceName -Gameplay `
      -Frames $TraceFrames -ExtraArgs $traceArgs | Out-Null
    $report = Join-Path $logs "post_effects_${Name}_trace.txt"
    & python (Join-Path $PSScriptRoot 'post_effects_trace.py') `
      (Join-Path $logs "gpu_trace_$traceName.csv") | Out-File -Encoding utf8 $report
    if ($LASTEXITCODE -ne 0) { Say "  the filter skipped nothing, see $report" }
    Get-Content -LiteralPath $report | Select-Object -First 25 | ForEach-Object { Say "  $_" }
    Say "  full report: $report"
  } catch {
    Say "  TRACE FAILED: $_"
  }
}

Say ''
Say "Screenshots: logs\bench_${Name}_<on|off>_<pair>_<idle|forward>.png; diffs: logs\post_effects_${Name}_*_diff.png"
Say "Rows: $outCsv"
$lines | Out-File -Encoding utf8 $summary
