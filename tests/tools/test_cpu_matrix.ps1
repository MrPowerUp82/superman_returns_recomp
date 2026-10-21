param([string]$BenchExe='')
$ErrorActionPreference='Stop'
$cliBenchExe=$BenchExe
. "$PSScriptRoot/../../tools/bench/cpu_matrix.ps1" -Action Library
function Assert($Condition,$Message){if(-not $Condition){throw $Message}}
function Reject([scriptblock]$Body){$failed=$false;try{& $Body|Out-Null}catch{$failed=$true};Assert $failed 'Expected rejection'}
$oldCulture=[Threading.Thread]::CurrentThread.CurrentCulture
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('replay matrix '+[guid]::NewGuid())
New-Item -ItemType Directory -Path $scratch|Out-Null
try {
  [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]'pt-BR'
  $plan=@(New-ReplayMatrixPlan @('direct','contiguous-1','interleaved-8') 10)
  Assert ($plan.Count -eq 30) 'Thirty processes required'
  Assert (@($plan.id|Select-Object -Unique).Count -eq 30) 'Unique ids'
  foreach($block in 1..10){Assert (@($plan|Where-Object block -eq $block).Count -eq 3) 'Complete block'}
  Assert ($plan[0].variant -eq $plan[5].variant -and $plan[2].variant -eq $plan[3].variant) 'Reverse alternate blocks'
  Reject {New-ReplayMatrixPlan @('direct','direct') 10}
  Reject {New-ReplayMatrixPlan @('contiguous-9') 10}
  $env:SR_MATRIX_PRESENT='original';Remove-Item -LiteralPath Env:SR_MATRIX_ABSENT -ErrorAction SilentlyContinue
  Reject {Invoke-ReplayEnvironment @{SR_MATRIX_PRESENT='changed';SR_MATRIX_ABSENT='new'} {Assert ($env:SR_MATRIX_PRESENT -eq 'changed') 'Child environment';throw 'simulated'}}
  Assert ($env:SR_MATRIX_PRESENT -eq 'original' -and $null -eq [Environment]::GetEnvironmentVariable('SR_MATRIX_ABSENT','Process')) 'Restore present and absent'
  $plan=@(New-ReplayMatrixPlan @('direct') 10)
  $done=@(Invoke-ReplayMatrix $plan {param($run) if($run.block -eq 2){throw 'child failure'};$run.exit_code=0})
  Assert ($done[0].status -eq 'complete' -and $done[1].status -eq 'failed' -and $done[1].error -match 'child failure') 'Preserve child failure'
  Assert ($done[9].status -eq 'complete') 'Continue independent processes'
  $done=@(Invoke-ReplayMatrix @(New-ReplayMatrixPlan @('direct') 10) {param($run)$run.exit_code=7})
  Assert ($done[0].status -eq 'failed' -and $done[0].exit_code -eq 7) 'Nonzero exit is failure'
  Reject {Assert-ReplayDestination $scratch { @() }}
  Reject {Assert-ReplayDestination (Join-Path $scratch 'new') { @([pscustomobject]@{Id=123}) }}
  Assert-ReplayDestination (Join-Path $scratch 'new') { @() }
  . "$PSScriptRoot/../../tools/bench/bench_replay.ps1" -Action Library
  $identityTime=[DateTime]::Parse('2026-10-21T18:42:50.7255412Z',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)
  Assert ((Convert-ReplayUtc $identityTime).Ticks -eq $identityTime.ToUniversalTime().Ticks) 'Preserve typed PS7 JSON dates at process identity precision'
  Assert ((Convert-ReplayUtc '2026-10-21T18:42:50.7255412Z').Ticks -eq $identityTime.ToUniversalTime().Ticks) 'Preserve PS5 JSON string dates'
  $now=[DateTime]::UtcNow
  Assert (Test-ReplayImageTime $now $now) 'Current evidence'
  Assert (-not(Test-ReplayImageTime $now.AddSeconds(-11) $now)) 'Old evidence rejected'
  Reject {Assert-ReplayTrace $true (Join-Path $scratch 'absent.csv') @('normal log')}
  Reject {Assert-ReplayTrace $false '' @('replay_trace_invalid=1 reason=write')}
  $script:warmupEvents=@()
  Invoke-ReplayWarmup { $script:warmupEvents+=@('gameplay-ready') } {param($seconds)$script:warmupEvents+=@("sleep-$seconds")} { $script:warmupEvents+=@('fresh-start-evidence') }
  Assert (($script:warmupEvents -join ',') -eq 'gameplay-ready,sleep-25,fresh-start-evidence') 'Warmup follows gameplay readiness, even if launch/menu took over 25 seconds'
  $preview=@(& "$PSScriptRoot/../../tools/bench/cpu_matrix.ps1" -Phase Isolated -Name test_preview -BenchExe 'C:/path with space/bench.exe' -Runs 10 -Variants direct -PlanOnly)
  Assert ($preview.Count -eq 10 -and $preview[0].command -match 'path with space') 'Preview paths and count'
  if($cliBenchExe) {
    $cliBenchExe=(Resolve-Path -LiteralPath $cliBenchExe).Path
    $json=Join-Path $scratch 'result with space.json'
    & $cliBenchExe --device --vary-banks --batch-draws=32 --threads=8 --schedule=interleaved "--json=$json"
    Assert ($LASTEXITCODE -eq 0) 'Eight interleaved participants accepted'
    $value=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json
    Assert ($value.schema -eq 1 -and $value.samples.Count -eq 8 -and $value.rounds -eq 10 -and $value.warmup_rounds -eq 2) 'JSON measured rounds'
    Assert ($value.checksum -eq 31793585826180 -and $value.threads -eq 8 -and $value.scheduling -eq 'interleaved' -and $value.mode -eq 'prepared') 'Checksum and mode'
    & $cliBenchExe --threads=9;Assert ($LASTEXITCODE -eq 4) 'Reject ninth participant'
    & $cliBenchExe --schedule=invalid;Assert ($LASTEXITCODE -eq 4) 'Reject scheduling'
  }
  'PASS replay matrix planning, failures, environment, locale, paths and CLI'
} finally {
  [Threading.Thread]::CurrentThread.CurrentCulture=$oldCulture
  [Environment]::SetEnvironmentVariable('SR_MATRIX_PRESENT',$null,'Process')
  Remove-Item -LiteralPath $scratch -Recurse -Force
}
