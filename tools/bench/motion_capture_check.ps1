# Isolated opt-in motion diagnostic; does not enable DLSS or record performance.
param([string]$Name = ('motion_' + (Get-Date -Format 'yyyyMMdd_HHmmss')),
      [switch]$RayShadowChecks,
      [switch]$ForceCoverageFailure,
      [switch]$SceneJitter,
      [switch]$InstanceProbe,
      [switch]$ObservedSkinMotion)
$ErrorActionPreference='Stop'
if($ObservedSkinMotion -and -not $InstanceProbe) {throw 'ObservedSkinMotion requires InstanceProbe lifecycle tracking'}
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(Get-Process superman_returns -ErrorAction SilentlyContinue) {throw 'Game already running; leaving it untouched'}
$inputFile=Join-Path $root "logs/${Name}_input.txt"
$triggerFile=Join-Path $root "logs/${Name}_go.txt"
$dump=Join-Path $root "logs/$Name"
if((Test-Path -LiteralPath $triggerFile) -or (Test-Path -LiteralPath $dump)) {throw 'Use a new diagnostic name'}
New-Item -ItemType Directory -Path $dump | Out-Null
$env:SR_BENCH_GAMEPAD_ONLY='1'
$env:SR_BENCH_INPUT=$inputFile
$env:SR_BENCH_MOTION_CAPTURE_TRIGGER=$triggerFile
$argsTask=@(
 '--sr_renderer=native','--sr_preset=custom','--sr_native_api=d3d12','--sr_render_scale=100',
 '--window_width=1280','--window_height=720','--vsync=true','--d3d12_adapter=-1',
 '--sr_native_fps_limit=30','--sr_native_render_scale=1','--sr_native_anisotropic_filtering=-1',
 '--sr_native_fxaa=false','--sr_native_shadow_quality=1','--sr_native_msaa_samples=1',
 '--mnk_mode=false','--mnk_mouse=false','--sr_lsfg=false','--sr_rt_shadows=false','--sr_dlss=off',
 "--sr_native_debug_observed_skin_motion=$($ObservedSkinMotion.IsPresent.ToString().ToLowerInvariant())",
 "--sr_native_instance_probe=$($InstanceProbe.IsPresent.ToString().ToLowerInvariant())",
 "--sr_native_debug_motion_jitter=$($SceneJitter.IsPresent.ToString().ToLowerInvariant())",
 "--sr_native_debug_motion_coverage_pso_failure=$($ForceCoverageFailure.IsPresent.ToString().ToLowerInvariant())",
 "--sr_native_motion_capture_trigger_path=$triggerFile","--sr_native_dump_dir=$dump"
) -join ' '
try {
 & "$PSScriptRoot/bench.ps1" -Name $Name -ExtraArgs $argsTask -TitleTimeout 180 -WorldTimeout 200 -GraphicsMenuChecks -RequireNoRunningGame -RayShadowChecks:$RayShadowChecks
} finally {
 if(Test-Path -LiteralPath "$root/logs/game.log") {Copy-Item -LiteralPath "$root/logs/game.log" -Destination "$root/logs/bench_$Name.log"}
}
