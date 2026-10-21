<# Sequential replay experiments. Game uses SR_BENCH_INPUT and external Computer Use
   evidence via ready.json/end.json; it never sends Windows input or captures UI. #>
param(
  [ValidateSet('Run','Library')][string]$Action='Run',
  [ValidateSet('Isolated','Game')][string]$Phase='Isolated',
  [string]$Name='',[string]$Exe='',[string]$BenchExe='',
  [ValidateSet(10,20)][int]$Runs=10,[string[]]$Variants=@(),
  [switch]$StageTiming,[switch]$PlanOnly,
  [ValidateSet(32,256)][int]$BatchCommands=32,[ValidateSet(30,60)][int]$FpsLimit=30,
  [string]$OutputRoot='', [string]$ReferenceImage=''
)
$ErrorActionPreference='Stop'
function New-ReplayMatrixPlan([string[]]$Variants,[int]$Runs) {
  $valid=@('direct','contiguous-1','contiguous-2','contiguous-3','contiguous-4','contiguous-6','contiguous-8','interleaved-2','interleaved-3','interleaved-4','interleaved-6','interleaved-8')
  if($Runs -notin @(10,20) -or -not $Variants.Count -or @($Variants|Select-Object -Unique).Count -ne $Variants.Count){throw 'Unique variants and 10 or 20 blocks required'}
  foreach($variant in $Variants){if($variant -notin $valid){throw "Unknown replay variant: $variant"}}
  foreach($block in 1..$Runs) {
    $ordered=@($Variants);if($block%2 -eq 0){[array]::Reverse($ordered)}
    for($index=0;$index -lt $ordered.Count;$index++) {
      [pscustomobject]@{id=('b{0:d2}_{1:d2}_{2}' -f $block,($index+1),$ordered[$index]);block=$block;order=$index+1;variant=$ordered[$index];scenario_files=@{};profiled=$false;status='planned';exit_code=$null;error=$null}
    }
  }
}
function Invoke-ReplayEnvironment([hashtable]$Changes,[scriptblock]$Body) {
  $saved=@{}
  try {
    foreach($key in $Changes.Keys){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$Changes[$key],'Process')}
    & $Body
  } finally {foreach($key in $saved.Keys){if($null -eq $saved[$key]){Remove-Item -LiteralPath ("Env:"+$key) -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}}
}
function Invoke-ReplayMatrix([object[]]$Plan,[scriptblock]$Runner,[scriptblock]$Save={}) {
  foreach($run in $Plan) {
    $run.status='running';& $Save
    try {& $Runner $run|Out-Null;if($run.exit_code -ne 0){throw "Child exit $($run.exit_code)"};$run.status='complete'}
    catch {$run.status='failed';$run.error=$_.Exception.Message}
    finally {& $Save}
    $run
  }
}
function Assert-ReplayDestination([string]$Path,[scriptblock]$Processes={@(Get-Process superman_returns -ErrorAction SilentlyContinue)}) {
  if(Test-Path -LiteralPath $Path){throw 'Run label exists; use a new label'}
  if(@(& $Processes).Count){throw 'Existing game instance; close it before a matrix'}
}
function Get-ReplayArguments([string]$Variant,[int]$Batch) {
  $args=@('--device','--vary-banks',"--batch-draws=$Batch")
  if($Variant -ne 'direct'){$policy,$participants=$Variant.Split('-');$args+=@("--threads=$participants","--schedule=$policy")}
  return $args
}
function Assert-ReplayQuiet {
  if(Get-Process clang,clang++,lld-link,ninja,cmake,sr_native_tests,sr_memory_tracking_tests -ErrorAction SilentlyContinue){throw 'Build/test running; measurement rejected'}
}
if($Action -eq 'Library'){return}
if($Name -notmatch '^[a-zA-Z0-9_-]+$'){throw 'Name must be a nonempty file label'}
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $OutputRoot){$OutputRoot=Join-Path $root 'artifacts/native-renderer-performance'}
$destination=Join-Path $OutputRoot $Name
if(-not $Variants.Count) {
  if($Phase -eq 'Game'){throw 'Game requires explicit finalists/controls'}
  $Variants=@('direct','contiguous-1','contiguous-2','contiguous-3','contiguous-4','contiguous-6','contiguous-8','interleaved-2','interleaved-3','interleaved-4','interleaved-6','interleaved-8')
}
if(-not $BenchExe){$BenchExe=Join-Path $root 'build/tests-native/sr_replay_storage_bench.exe'}
if(-not $Exe){$Exe=Join-Path $root 'port/out/build/win-amd64-release/superman_returns.exe'}
$plan=@(New-ReplayMatrixPlan $Variants $Runs)
foreach($run in $plan){$run.profiled=[bool]$StageTiming}
if($PlanOnly) {
  foreach($run in $plan){[pscustomobject]@{id=$run.id;block=$run.block;variant=$run.variant;phase=$Phase;directory=(Join-Path $destination $run.id);command=$(if($Phase -eq 'Isolated'){'"'+$BenchExe+'" '+((Get-ReplayArguments $run.variant $BatchCommands) -join ' ')}else{'"'+$Exe+'"; game API + Computer Use ready/end evidence'});profiled=[bool]$StageTiming}}
  return
}
Assert-ReplayDestination $destination
Assert-ReplayQuiet
if($Phase -eq 'Isolated' -and $StageTiming){throw 'Isolated cycle measurements have no stage profiling mode'}
$selected=if($Phase -eq 'Isolated'){(Resolve-Path -LiteralPath $BenchExe).Path}else{(Resolve-Path -LiteralPath $Exe).Path}
New-Item -ItemType Directory -Path $destination|Out-Null
$manifestPath=Join-Path $destination 'manifest.json'
$patch=Join-Path $destination 'working-tree.patch'
git -C $root diff --binary HEAD --output=$patch
if($LASTEXITCODE){throw 'Cannot preserve source patch'}
$binaryFiles=@($selected)
if($Phase -eq 'Game'){$binaryFiles+=@(Get-ChildItem -LiteralPath (Split-Path $selected) -File|Where-Object Extension -in '.dll','.map','.srvk'|ForEach-Object FullName)}
$hashes=@($binaryFiles|ForEach-Object {[pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$inherited=@{};Get-ChildItem Env:|Where-Object Name -match '^SR_.*(PROFILE|TIMELINE|REPLAY|AUDIT|TRACKING)'|ForEach-Object {$inherited[$_.Name]=$_.Value}
$manifest=[pscustomobject]@{schema=1;phase=$Phase;name=$Name;source=@{revision=(git -C $root rev-parse HEAD);patch=$patch;patch_sha256=(Get-FileHash -LiteralPath $patch).Hash};executables=$hashes;settings=@{batch_commands=$BatchCommands;fps_limit=$FpsLimit;api='vulkan';pool=0;storage=1;global_resources=1;vsync=$true;lsfg=$false;msaa=1;scale=1;window='1280x720';warmup_seconds=25;scenario_seconds=20;inherited=$inherited};runs=$plan}
$save={ $manifest|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $manifestPath -Encoding UTF8 }
& $save
$runner={param($run)
  Assert-ReplayQuiet
  if(Get-Process superman_returns -ErrorAction SilentlyContinue){throw 'Existing game instance'}
  $directory=Join-Path $destination $run.id
  New-Item -ItemType Directory -Path $directory|Out-Null
  $run.scenario_files.log=Join-Path $directory 'process.log'
  if($Phase -eq 'Isolated') {
    $json=Join-Path $directory 'isolated.json';$run.scenario_files.isolated=$json
    $arguments=@(Get-ReplayArguments $run.variant $BatchCommands)+"--json=$json"
    & $selected @arguments *> $run.scenario_files.log
    $run.exit_code=$LASTEXITCODE
    if($run.exit_code -eq 0 -and -not(Test-Path -LiteralPath $json)){throw 'Missing isolated evidence'}
  } else {
    $run.scenario_files.windows=Join-Path $directory 'windows.json'
    $run.scenario_files.timeline=Join-Path $directory 'timeline.csv'
    $run.scenario_files.replay=Join-Path $directory 'timeline.csv.replay.csv'
    $run.scenario_files.start_image=Join-Path $directory 'start.png'
    $run.scenario_files.end_image=Join-Path $directory 'end.png'
    & "$PSScriptRoot/bench_replay.ps1" -Action Run -Exe $selected -Directory $directory -Variant $run.variant -BatchCommands $BatchCommands -FpsLimit $FpsLimit -StageTiming:$StageTiming -ReferenceImage $ReferenceImage
    $run.exit_code=0
  }
}
Invoke-ReplayMatrix $plan $runner $save|Select-Object id,status,exit_code,error
Write-Output "Manifest: $manifestPath"
