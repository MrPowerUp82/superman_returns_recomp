$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../tools/bench/bench_texture_tracking.ps1" -Action Library
$env:SR_D3D12_TEXTURE_WRITE_TRACKING='original'
$script:neutral=0;$script:stopped=@()
try {
  Invoke-TrackingEnvironment @{SR_D3D12_TEXTURE_WRITE_TRACKING='on'} {
    if($env:SR_D3D12_TEXTURE_WRITE_TRACKING -ne 'on'){throw 'environment was not set'}
    Invoke-TrackingLifetime {throw 'injected failure'} {$script:neutral++} {param($id) $script:stopped+= $id} 44
  }
  throw 'failure was swallowed'
} catch {if($_.Exception.Message -ne 'injected failure'){throw}}
if($env:SR_D3D12_TEXTURE_WRITE_TRACKING -ne 'original'){throw 'environment leaked'}
if($script:neutral -ne 1 -or $script:stopped.Count -ne 1 -or $script:stopped[0] -ne 44){throw 'wrong input/process cleanup'}
Remove-Item Env:SR_D3D12_TEXTURE_WRITE_TRACKING
Write-Output 'PASS environment restoration, neutral input and exact owned process cleanup'

$t=[DateTime]::UtcNow
if(-not (Test-TrackingEvidenceTime $t.AddSeconds(-5) $t $t.AddSeconds(20) $t.AddSeconds(-.2) $t.AddSeconds(20.5))){throw 'fractional image timestamps rejected'}
if(Test-TrackingEvidenceTime $t.AddSeconds(-5) $t $t.AddSeconds(20) $t $t.AddSeconds(40)){throw 'late image accepted'}
if(Test-TrackingEvidenceTime $t.AddSeconds(-5) $t $t.AddSeconds(20) $t $t.AddSeconds(19)){throw 'image before window end accepted'}
Write-Output 'PASS image evidence timestamp boundaries'

if(-not (Test-TrackingProcessIdentity $t $t 'C:\game.exe' 'c:\GAME.exe')){throw 'DateTime process identity rejected'}
if(-not (Test-TrackingProcessIdentity $t.ToString('o') $t 'C:\game.exe' 'C:\game.exe')){throw 'ISO process identity rejected'}
if(Test-TrackingProcessIdentity $t $t.AddTicks(1) 'C:\game.exe' 'C:\game.exe'){throw 'reused PID accepted'}
Write-Output 'PASS PowerShell 5/7 timestamp identity and PID reuse'
