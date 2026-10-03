param([string]$GpuUuid = '')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe=Join-Path $root 'build/tests-vulkan/vulkan/sr_vulkan_smoke.exe'
$logs=Join-Path $root 'logs/vulkan'
New-Item -ItemType Directory -Force $logs | Out-Null
$arguments=@('--frames=120','--self-test','--validation', ('--log-file="' + (Join-Path $logs 'm1-integration.log') + '"'))
if ($GpuUuid) { $arguments += "--gpu-uuid=$GpuUuid" }
$process=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logs 'integration.stdout.txt') -RedirectStandardError (Join-Path $logs 'integration.stderr.txt')
if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Vulkan self-test timed out' }
$process.Refresh()
if ($process.ExitCode -ne 0) { throw "Vulkan self-test failed ($($process.ExitCode)); see logs/vulkan" }
$text=Get-Content (Join-Path $logs 'm1-integration.log') -Raw
foreach ($phase in 'initial','resize-960','resize-640','minimize','restore/close') {
  if (-not $text.Contains("self-test phase $phase passed")) { throw "Missing phase: $phase" }
}
if (-not $text.Contains('presented=120') -or -not $text.Contains('validation_errors=0')) { throw 'Unexpected smoke-test completion' }
if ($GpuUuid -and -not $text.Contains("UUID=$GpuUuid")) { throw 'GPU selection mismatch' }
Write-Host 'GPU smoke test: 120 frames, resize/minimize/restore/close passed.'
if ($text.Contains('Validation not active')) { Write-Host 'Validation layers absent; no validation-layer claim.' }
