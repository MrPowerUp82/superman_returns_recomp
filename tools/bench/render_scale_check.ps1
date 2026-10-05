<#
Compare 100/75/50 percent internal resolution in alternating order.
Runs the existing gameplay benchmark (which launches/closes the game), keeps
each log and screenshot, and rejects runs with resolve errors. FPS alone is
never evidence that a reduced resolution rendered correctly.

Usage: powershell -File tools\bench\render_scale_check.ps1 -Pairs 2
#>
param(
  [ValidatePattern('^[A-Za-z0-9_-]+$')] [string]$Name = 'scale',
  [ValidateRange(1, 10)] [int]$Pairs = 2,
  [ValidateRange(5, 120)] [int]$Window = 20
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$report = Join-Path $logs "render_scale_${Name}.csv"
if (Test-Path -LiteralPath $report) { throw "Report already exists: $report. Choose another -Name." }
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$exeHash = (Get-FileHash (Join-Path $root 'port/out/build/win-amd64-release/superman_returns.exe')).Hash
for ($pair = 1; $pair -le $Pairs; $pair++) {
  $scales = if ($pair % 2) { @(100, 75, 50) } else { @(50, 75, 100) }
  foreach ($scale in $scales) {
    $run = "${Name}_${pair}_${scale}"
    $savedLog = Join-Path $logs "render_scale_${run}.log"
    if (Test-Path -LiteralPath $savedLog) { throw "Run already exists: $savedLog" }
    $argsText = "--sr_preset=custom --sr_renderer=xenos --sr_post_effects=true --sr_render_scale=$scale"
    $benchRows = @()
    $failure = ''
    try {
      $benchRows = @(& (Join-Path $PSScriptRoot 'bench.ps1') -Name $run -ExtraArgs $argsText -Window $Window)
    } catch { $failure = $_.Exception.Message }
    if (Test-Path -LiteralPath (Join-Path $logs 'game.log')) {
      Copy-Item -LiteralPath (Join-Path $logs 'game.log') -Destination $savedLog
    }
    $text = if (Test-Path -LiteralPath $savedLog) { Get-Content -LiteralPath $savedLog -Raw } else { '' }
    $resolveErrors = [regex]::Matches($text, 'Resolve region[^\r\n]*outside').Count
    $adapter = [regex]::Match($text, 'DXGI adapter: ([^\r\n]+)').Groups[1].Value
    $sizes = ([regex]::Matches($text, 'render scale \d+%: [^\r\n]+') |
      ForEach-Object { $_.Value }) -join ' | '
    if ($scale -ne 100 -and -not $sizes -and -not $failure) { $failure = 'No render scale hook activity in log' }
    $status = if ($failure) { 'failed' } elseif ($resolveErrors) { 'invalid-resolve' } else { 'needs-visual-review' }
    if (-not $benchRows.Count) { $benchRows = @([pscustomobject]@{ scenario='none'; avg_fps=$null; min_fps=$null }) }
    foreach ($row in $benchRows) {
      [pscustomobject]@{
        pair=$pair; scale=$scale; scenario=$row.scenario; avg_fps=$row.avg_fps
        min_fps=$row.min_fps; status=$status; resolve_errors=$resolveErrors
        cpu=$cpu; adapter=$adapter; executable_sha256=$exeHash
        dimensions=$sizes; log=$savedLog; failure=$failure
      } | Export-Csv -LiteralPath $report -Append -NoTypeInformation -Encoding utf8
    }
    Write-Host "$run : $status ($resolveErrors resolve errors)"
  }
}
Write-Host "Report: $report"
Write-Host 'Review the bench screenshots before accepting any FPS improvement. Generated frames are not guest FPS.'
