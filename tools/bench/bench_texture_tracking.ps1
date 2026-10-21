<# Phased benchmark: Launch, Input (game API), Measure idle/forward, Validate, Stop.
   Capture StartImage before idle and EndImage immediately after forward with the
   computer-use skill. This script reads PNG evidence; it never drives Windows UI.
   Use the same EXE/DLL for OFF/ON/ON/OFF. StageTiming runs are diagnostic only. #>
param(
  [ValidateSet('Library','Launch','Input','Measure','Validate','Stop')][string]$Action='Launch',
  [string]$Exe='', [string]$Name='',
  [ValidateSet('off','compare','on')][string]$Mode='off', [switch]$StageTiming,
  [ValidateRange(1,4)][int]$Order=1,
  [ValidateSet('idle','forward')][string]$Scenario='idle',
  [ValidateRange(20,20)][int]$Seconds=20,
  [ValidateRange(0,65535)][int]$Buttons=0,
  [ValidateRange(-32768,32767)][int]$Forward=0,
  [ValidateRange(1,30000)][int]$Milliseconds=700,
  [string]$StartImage='', [string]$EndImage='',
  [ValidateSet('aligned','different_camera','unknown')][string]$Alignment='unknown'
)
$ErrorActionPreference='Stop'
function Invoke-TrackingEnvironment($Changes,[scriptblock]$Body) {
  $saved=@{}
  try {
    foreach($key in $Changes.Keys){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$Changes[$key],'Process')}
    & $Body
  } finally {foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}
}
function Invoke-TrackingLifetime([scriptblock]$Body,[scriptblock]$Neutral,[scriptblock]$Stop,[int]$OwnedProcess) {
  try { & $Body } finally {try {& $Neutral} finally {if($OwnedProcess){& $Stop $OwnedProcess}}}
}
function Convert-TrackingUtc($Value) {
  if($Value -is [DateTime]){return $Value.ToUniversalTime()}
  return [DateTime]::Parse($Value,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}
function Test-TrackingProcessIdentity($Expected,[DateTime]$Actual,[string]$ExpectedPath,[string]$ActualPath) {
  return (Convert-TrackingUtc $Expected).Ticks -eq $Actual.ToUniversalTime().Ticks -and [string]::Equals($ExpectedPath,$ActualPath,[StringComparison]::OrdinalIgnoreCase)
}
function Test-TrackingEvidenceTime([DateTime]$Launch,[DateTime]$Begin,[DateTime]$End,[DateTime]$StartImageTime,[DateTime]$EndImageTime) {
  $delta=($EndImageTime-$End).TotalSeconds
  return $StartImageTime -ge $Launch -and [math]::Abs(($StartImageTime-$Begin).TotalSeconds) -le 10 -and $delta -ge 0 -and $delta -le 10
}
if($Action -eq 'Library'){return}
if($Name -notmatch '^[a-zA-Z0-9_-]+$'){throw 'Name must be a nonempty file label'}
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskState=Join-Path $taskRoot "logs/${Name}_session.json"
function Assert-NoTrackingWork {
  if(Get-Process clang,clang++,lld-link,ninja,cmake,sr_native_tests,sr_memory_tracking_tests -ErrorAction SilentlyContinue){throw 'Build/test running; reject measurement'}
}
function Save-TrackingState($Value){$Value|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $taskState -Encoding UTF8}
function Send-TrackingInput($Value,[int]$PadButtons,[int]$Axis,[int]$Hold) {
  $id=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  Set-Content -LiteralPath $Value.input -Value "$id $Hold $PadButtons $Axis" -Encoding ascii -NoNewline
}
function Get-TrackingProcess($Value) {
  $process=Get-Process -Id $Value.pid -ErrorAction Stop
  if(-not (Test-TrackingProcessIdentity $Value.process_start_utc $process.StartTime $Value.exe $process.MainModule.FileName)){throw 'Process identity changed; refusing access'}
  return $process
}
function Stop-TrackingProcess($Value) {
  Invoke-TrackingLifetime {} {Send-TrackingInput $Value 0 0 1} {
    param($owned)
    $process=Get-TrackingProcess $Value
    if($process.Id -ne $owned){throw 'Process identity changed'}
    Stop-Process -Id $process.Id
    $process.WaitForExit(10000)|Out-Null
  } $Value.pid
}
if($Action -eq 'Launch') {
  Assert-NoTrackingWork
  if(Get-Process superman_returns -ErrorAction SilentlyContinue){throw 'Existing game instance; close it before launching a benchmark'}
  if(Test-Path -LiteralPath $taskState){throw 'Run label exists; use a new label'}
  if(-not $Exe){$Exe=Join-Path $taskRoot 'port/out/build/win-amd64-write-tracking/superman_returns.exe'}
  $Exe=(Resolve-Path -LiteralPath $Exe).Path
  $inputFile=Join-Path $taskRoot "logs/${Name}_input.txt"
  Set-Content -LiteralPath $inputFile -Value '0 1 0 0' -Encoding ascii -NoNewline
  $gameArgs=@("--game_data_root=$taskRoot/game","--log_file=$taskRoot/logs/${Name}_game.log",'--sr_renderer=native','--sr_native_api=d3d12','--sr_preset=custom','--sr_native_fps_limit=60','--sr_native_render_scale=1','--sr_render_scale=100','--sr_native_msaa_samples=1','--sr_lsfg=false','--vsync=true','--window_width=1280','--window_height=720','--fullscreen=false','--sr_skip_intro=true','--mnk_mode=false','--d3d12_adapter=-1','--sr_native_worker=true')
  $changes=@{SR_D3D12_TEXTURE_WRITE_TRACKING=$Mode;SR_D3D12_TEXTURE_TRACKING_TIMING=$(if($StageTiming){'1'}else{'0'});SR_BENCH_INPUT=$inputFile;SR_LOG_FPS='1';SR_VULKAN_PROFILE='0';SR_NATIVE_CPU_PROFILE='0';SR_NATIVE_TEXTURE_AUDIT='0';SR_FRAME_TIMELINE='';SR_FRAME_TIMELINE_DETAIL='0';SR_NATIVE_GUEST_CPU_PROFILE='0'}
  $script:trackingProcess=$null
  try {
    Invoke-TrackingEnvironment $changes {$script:trackingProcess=Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru}
    $process=$script:trackingProcess
    $value=[pscustomobject]@{name=$Name;mode=$Mode;order=$Order;exe=$Exe;exe_sha256=(Get-FileHash -LiteralPath $Exe).Hash;runtime_sha256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $Exe) 'rexruntime.dll')).Hash;args=$gameArgs;pid=$process.Id;process_start_utc=$process.StartTime.ToUniversalTime().ToString('o');input=$inputFile;scene='new-game-midtown';profile=[bool]$StageTiming;concurrent_work=$false;hud_start=$false;hud_end=$false;image_alignment='unknown';image_gate_pass=$null;scenarios=@()}
    Save-TrackingState $value
    $value|ConvertTo-Json -Depth 8
  } catch {if($script:trackingProcess){Stop-Process -Id $script:trackingProcess.Id -ErrorAction SilentlyContinue};throw}
  return
}
$session=Get-Content -LiteralPath $taskState -Raw|ConvertFrom-Json
if($Action -eq 'Stop'){Stop-TrackingProcess $session;return}
$process=Get-TrackingProcess $session
if($Action -eq 'Input'){Send-TrackingInput $session $Buttons $Forward $Milliseconds;return}
if($Action -eq 'Measure') {
  Assert-NoTrackingWork
  if(@($session.scenarios|Where-Object name -eq $Scenario).Count){throw 'Scenario already measured'}
  $log=Join-Path $taskRoot "logs/${Name}_game.log"
  $lineCount=@(Get-Content -LiteralPath $log).Count
  $cpu=$process.TotalProcessorTime.TotalSeconds
  $begin=[DateTime]::UtcNow
  $timer=[Diagnostics.Stopwatch]::StartNew()
  try {
    if($Scenario -eq 'forward'){Send-TrackingInput $session 0 32767 30000}
    Start-Sleep -Seconds $Seconds
    $timer.Stop();$process.Refresh();if($process.HasExited){throw 'Game exited during measurement'}
    Assert-NoTrackingWork
    $lines=@(Get-Content -LiteralPath $log|Select-Object -Skip $lineCount)
    $lines|Set-Content (Join-Path $taskRoot "logs/${Name}_${Scenario}_window.log") -Encoding UTF8
    $fps=@($lines|Select-String 'guest fps: ([\d.]+)'|ForEach-Object {[double]::Parse($_.Matches[0].Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)})
    if($fps.Count -lt 8){throw 'Too few FPS samples'}
    $row=[pscustomobject]@{name=$Scenario;duration_s=$timer.Elapsed.TotalSeconds;begin_utc=$begin.ToString('o');end_utc=[DateTime]::UtcNow.ToString('o');fps=$fps;avg_fps=($fps|Measure-Object -Average).Average;min_fps=($fps|Measure-Object -Minimum).Minimum;cpu_cores=($process.TotalProcessorTime.TotalSeconds-$cpu)/$timer.Elapsed.TotalSeconds}
    $session.scenarios=@($session.scenarios)+$row;Save-TrackingState $session
    $row|ConvertTo-Json -Depth 5
  } catch {try{Stop-TrackingProcess $session}catch{};throw}
  finally {Send-TrackingInput $session 0 0 1}
  return
}
if($Action -eq 'Validate') {
  . "$PSScriptRoot/bench_hud.ps1"
  Add-Type -AssemblyName System.Drawing
  if(@($session.scenarios).Count -ne 2){throw 'Both measurement windows required'}
  foreach($path in @($StartImage,$EndImage)){if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw 'PNG evidence missing'}}
  $idle=$session.scenarios|Where-Object name -eq 'idle';$forwardWindow=$session.scenarios|Where-Object name -eq 'forward'
  $startTime=(Get-Item -LiteralPath $StartImage).LastWriteTimeUtc
  $endTime=(Get-Item -LiteralPath $EndImage).LastWriteTimeUtc
  if(-not (Test-TrackingEvidenceTime (Convert-TrackingUtc $session.process_start_utc) (Convert-TrackingUtc $idle.begin_utc) (Convert-TrackingUtc $forwardWindow.end_utc) $startTime $endTime)){throw 'Image evidence is late or belongs to a different window'}
  $a=[Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $StartImage).Path);$b=[Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $EndImage).Path)
  try {$session.hud_start=Test-SupermanHud $a;$session.hud_end=Test-SupermanHud $b}finally{$a.Dispose();$b.Dispose()}
  $session.image_alignment=$Alignment
  $session|Add-Member -Force -NotePropertyName images -NotePropertyValue @($StartImage,$EndImage)
  Save-TrackingState $session
  if(-not $session.hud_start -or -not $session.hud_end){throw 'Gameplay HUD not confirmed'}
  $session|ConvertTo-Json -Depth 8
}
