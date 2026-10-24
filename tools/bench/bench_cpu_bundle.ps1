<#
Compare the assembled D3D12 CPU candidate, never individual tweaks.
Run with the write-tracking SDK build. Normal renderer defaults stay unchanged.
  .\tools\bench\bench_cpu_bundle.ps1 -Name cpu_bundle -Mode candidate
  .\tools\bench\bench_cpu_bundle.ps1 -Name cpu_reference -Mode reference
Profile adds diagnostic overhead; leave it off for FPS acceptance.
#>
param(
  [Parameter(Mandatory)][string]$Name,
  [ValidateSet('candidate','reference')][string]$Mode='candidate',
  [string]$Exe='',
  [ValidateRange(0,1000)][int]$FpsLimit=60,
  [switch]$Profile,
  [switch]$Timeline,
  [switch]$ColdPipelineCache
)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(!$Exe){$Exe=Join-Path $root 'port/out/build/win-amd64-write-tracking/superman_returns.exe'}
$Exe=(Resolve-Path -LiteralPath $Exe).Path
if(Get-Process clang,clang++,lld-link,ninja,cmake,ctest,sr_native_tests,sr_memory_tracking_tests -ErrorAction SilentlyContinue){throw 'Build/test running; reject measurement'}
$bundle=if($Mode -eq 'candidate'){'true'}else{'false'}
$timelinePath=Join-Path $root "logs/timeline_$Name.csv"
$saved=@{}
$settings=@{
 SR_D3D12_TEXTURE_WRITE_TRACKING='off';SR_NATIVE_SHARED_CONSTANTS_CACHE='0'
 SR_NATIVE_CPU_PROFILE='0';SR_NATIVE_CONSTANTS_PROFILE='0';SR_NATIVE_STREAMS_PROFILE='0'
 SR_NATIVE_PM4_READ_PROFILE='0';SR_FRAME_TIMELINE='';SR_FRAME_TIMELINE_DETAIL='0';SR_BENCH_INPUT=''
 SR_VULKAN_PROFILE='0';SR_NATIVE_TEXTURE_AUDIT='0';SR_D3D12_TEXTURE_TRACKING_TIMING='0';SR_NATIVE_GUEST_CPU_PROFILE='0'
 SR_NATIVE_TEXTURE_REPLAY='0';SR_NATIVE_TEXTURE_SOURCE_PROBE='0';SR_NATIVE_RESOURCE_UNLOCK_AUDIT='0'
}
if($Profile){foreach($key in @('SR_NATIVE_CPU_PROFILE','SR_NATIVE_CONSTANTS_PROFILE','SR_NATIVE_STREAMS_PROFILE','SR_NATIVE_PM4_READ_PROFILE')){$settings[$key]='1'}}
if($Timeline){$settings.SR_FRAME_TIMELINE=$timelinePath}
$gameArgs="--sr_renderer=native --sr_preset=custom --sr_native_api=d3d12 --sr_native_cpu_bundle=$bundle --sr_native_pm4_bounded_reads=false --sr_native_shared_staging=false --sr_native_resolve_identity_cache=false --sr_native_buffer_srv_cache=0 --sr_native_stacked_resolves=false --sr_native_fps_limit=$FpsLimit --sr_native_render_scale=1 --sr_render_scale=100 --window_width=1280 --window_height=720 --vsync=true --sr_native_msaa_samples=1 --sr_native_shadow_quality=1 --sr_native_fxaa=false --sr_native_anisotropic_filtering=-1 --sr_lsfg=false --mnk_mode=true --mnk_mouse=false --sr_native_pipeline_cache=true"
$cache=Join-Path (Split-Path $Exe) 'superman_returns_pipelines.bin'
$cacheBackup=''
$learnedCache=Join-Path $root "logs/${Name}_new_local_pipelines_$([Guid]::NewGuid().ToString('N')).bin"
try {
 foreach($key in $settings.Keys){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$settings[$key],'Process')}
 if($ColdPipelineCache -and (Test-Path -LiteralPath $cache)) {
  $cacheBackup="$cache.bundle_$([Guid]::NewGuid().ToString('N')).bak"
  Move-Item -LiteralPath $cache -Destination $cacheBackup
 }
 $manifest=[ordered]@{name=$Name;mode=$Mode;exe=$Exe;exe_sha256=(Get-FileHash -LiteralPath $Exe).Hash;runtime_sha256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $Exe) 'rexruntime.dll')).Hash;args=$gameArgs;environment=$settings;cold_local_pipeline_cache=[bool]$ColdPipelineCache;pipeline_backup=$cacheBackup;learned_pipeline_cache=$learnedCache;start_utc=[DateTimeOffset]::UtcNow.ToString('o')}
 $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root "logs/${Name}_launch.json") -Encoding utf8
 & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'bench.ps1') -Name $Name -Exe $Exe -ExtraArgs $gameArgs -TitleTimeout 180 -WorldTimeout 200 -WindowMetadata (Join-Path $root "logs/${Name}_windows.json")
 $runExit=$LASTEXITCODE
} finally {
 try {
  foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
  $gameLog=Join-Path $root 'logs/game.log'
  if(Test-Path -LiteralPath $gameLog){Copy-Item -LiteralPath $gameLog -Destination (Join-Path $root "logs/bench_$Name.log") -Force}
 } finally {
  if($cacheBackup -and (Test-Path -LiteralPath $cacheBackup)) {
   # Preserve newly learned records separately and restore the user's cache,
   # including when copying diagnostic logs failed.
   # First park new records beside the cache (same-volume rename), restore
   # the original, and only then attempt optional archival in logs.
   $parkedCache="$cache.learned_$([Guid]::NewGuid().ToString('N')).bin"
   $hasLearned=Test-Path -LiteralPath $cache
   if($hasLearned){Move-Item -LiteralPath $cache -Destination $parkedCache}
   Move-Item -LiteralPath $cacheBackup -Destination $cache
   if($hasLearned) {
    try {Move-Item -LiteralPath $parkedCache -Destination $learnedCache}
    catch {Write-Warning "Original pipeline cache restored; learned records preserved at $parkedCache. $($_.Exception.Message)"}
   }
  }
 }
}
if(Get-Process clang,clang++,lld-link,ninja,cmake,ctest,sr_native_tests,sr_memory_tracking_tests -ErrorAction SilentlyContinue){throw 'Build/test active after measurement; reject result'}
if($Mode -eq 'candidate' -and $runExit -eq 0) {
 $runLog=Get-Content -LiteralPath (Join-Path $root "logs/bench_$Name.log") -Raw
 if($runLog -notmatch 'native CPU bundle: requested=true selected=true tracking=true bounded_reads=true shared_staging=true resolve_identity=true') {throw 'CPU bundle was not fully selected'}
 if($runLog -notmatch 'native texture tracking: frame=.*watches=[1-9]\d*.*skipped_checks=[1-9]\d*') {throw 'No effective tracked-texture reuse; reject full-bundle result'}
}
exit $runExit
