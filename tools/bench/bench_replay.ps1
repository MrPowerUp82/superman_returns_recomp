<# Game API driver. Run waits for Computer Use to save start.png/end.png and
   submit Ready/End. Input is the game's SR_BENCH_INPUT file, never Windows UI.
   A missing/late image, HUD, golden gate or trace prevents a successful run. #>
param(
  [ValidateSet('Library','Run','Status','Input','Ready','End')][string]$Action='Status',
  [string]$Directory='', [string]$Exe='', [string]$Variant='direct',
  [ValidateSet(32,256)][int]$BatchCommands=32,[ValidateSet(30,60)][int]$FpsLimit=30,
  [ValidateSet('vulkan','d3d12')][string]$Api='vulkan',[switch]$StageTiming,[string]$ReferenceImage='',
  [ValidateRange(0,65535)][int]$Buttons=0,[ValidateRange(-32768,32767)][int]$Forward=0,
  [ValidateRange(1,30000)][int]$Milliseconds=700
)
$ErrorActionPreference='Stop'
function Test-ReplayImageTime([DateTime]$Image,[DateTime]$Event){$delta=($Event.ToUniversalTime()-$Image.ToUniversalTime()).TotalSeconds;return $delta -ge 0 -and $delta -le 10}
function Assert-ReplayTrace([bool]$Profiled,[string]$Trace,[string[]]$Log) {
  if($Log -match 'replay_trace_invalid=1'){throw 'Replay trace marked invalid'}
  if($Profiled -and (-not(Test-Path -LiteralPath $Trace -PathType Leaf) -or (Get-Item -LiteralPath $Trace).Length -eq 0)){throw 'Replay trace absent/empty'}
}
function Send-ReplayInput($Session,[int]$PadButtons,[int]$Axis,[int]$Hold) {
  $process=Get-ReplayProcess $Session
  $id=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  Set-Content -LiteralPath $Session.input -Value "$id $Hold $PadButtons $Axis" -Encoding ascii -NoNewline
}
function Get-ReplayProcess($Session) {
  $process=Get-Process -Id $Session.pid -ErrorAction Stop
  $expected=[DateTime]::Parse($Session.process_start_utc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
  if($process.StartTime.ToUniversalTime().Ticks -ne $expected.Ticks -or -not [string]::Equals($process.MainModule.FileName,$Session.exe,[StringComparison]::OrdinalIgnoreCase)){throw 'Owned process identity changed'}
  return $process
}
function Assert-ReplayHud([string]$Image) {
  . "$PSScriptRoot/bench_hud.ps1"
  Add-Type -AssemblyName System.Drawing
  $bitmap=[Drawing.Bitmap]::FromFile($Image)
  try {if(-not(Test-SupermanHud $bitmap)){throw 'Gameplay HUD not confirmed'}}finally{$bitmap.Dispose()}
}
if($Action -eq 'Library'){return}
if(-not $Directory){throw 'Directory required'}
$Directory=[IO.Path]::GetFullPath($Directory)
$statePath=Join-Path $Directory 'session.json'
$requestPath=Join-Path $Directory 'request.json'
function Save-ReplayRequest([string]$State) {
  @{state=$State;directory=$Directory;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $requestPath -Encoding UTF8
}
if($Action -eq 'Status'){Get-Content -LiteralPath $requestPath -Raw;return}
if($Action -ne 'Run') {
  $session=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
  if($Action -eq 'Input'){Send-ReplayInput $session $Buttons $Forward $Milliseconds;return}
  $state=(Get-Content -LiteralPath $requestPath -Raw|ConvertFrom-Json).state
  $expected=if($Action -eq 'Ready'){'await-ready'}else{'await-end'}
  if($state -ne $expected){throw "Expected $expected, got $state"}
  $file=if($Action -eq 'Ready'){'start.png'}else{'end.png'}
  $image=Join-Path $Directory $file
  if(-not(Test-ReplayImageTime (Get-Item -LiteralPath $image).LastWriteTimeUtc ([DateTime]::UtcNow))){throw 'Image evidence older than ten seconds'}
  Assert-ReplayHud $image
  @{utc=[DateTime]::UtcNow.ToString('o');image=$image}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Directory ($Action.ToLower()+'.json')) -Encoding UTF8
  return
}
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(Test-Path -LiteralPath $statePath){throw 'Session already exists'}
if(Get-Process superman_returns -ErrorAction SilentlyContinue){throw 'Existing game instance'}
if(-not $ReferenceImage){$ReferenceImage=Join-Path $root 'artifacts/golden/start.png'}
if(-not(Test-Path -LiteralPath $ReferenceImage -PathType Leaf)){throw 'Existing known-good golden image required'}
$Exe=(Resolve-Path -LiteralPath $Exe).Path
$policy='contiguous';$participants=''
if($Variant -ne 'direct') {
  if($Variant -notmatch '^(contiguous|interleaved)-([1-8])$'){throw 'Invalid variant'}
  $policy=$Matches[1];$participants=$Matches[2]
}
$log=Join-Path $Directory 'process.log'
$timeline=Join-Path $Directory 'timeline.csv'
$inputFile=Join-Path $Directory 'input.txt'
Set-Content -LiteralPath $inputFile -Value '0 1 0 0' -Encoding ascii -NoNewline
$changes=@{SR_NATIVE_REPLAY_THREADS=$participants;SR_NATIVE_REPLAY_SCHEDULING=$policy;SR_NATIVE_BATCH_COMMANDS="$BatchCommands";SR_NATIVE_REPLAY_POOL='0';SR_NATIVE_REPLAY_STORAGE='1';SR_VULKAN_GLOBAL_RESOURCES='1';SR_VULKAN_PROFILE=$(if($StageTiming){'1'}else{'0'});SR_FRAME_TIMELINE=$(if($StageTiming){$timeline}else{''});SR_FRAME_TIMELINE_DETAIL=$(if($StageTiming){'1'}else{'0'});SR_BENCH_INPUT=$inputFile;SR_LOG_FPS='1';SR_NATIVE_CPU_PROFILE='0';SR_NATIVE_GUEST_CPU_PROFILE='0';SR_NATIVE_TEXTURE_AUDIT='0';SR_D3D12_TEXTURE_TRACKING_TIMING='0';SR_NATIVE_TEXTURE_REPLAY='0'}
$saved=@{};$owned=$null;$session=$null
try {
  foreach($key in $changes.Keys){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$changes[$key],'Process')}
  # Quote complete path arguments for Windows PowerShell's Start-Process join.
  $arguments=@(('"--game_data_root='+$root+'/game"'),('"--log_file='+$log+'"'),'--sr_renderer=native',"--sr_native_api=$Api",'--sr_preset=custom',"--sr_native_fps_limit=$FpsLimit",'--sr_native_render_scale=1','--sr_render_scale=100','--sr_native_msaa_samples=1','--sr_native_fxaa=false','--sr_native_shadow_quality=1','--sr_native_anisotropic_filtering=-1','--sr_lsfg=false','--vsync=true','--window_width=1280','--window_height=720','--fullscreen=false','--sr_skip_intro=true','--mnk_mode=false','--d3d12_adapter=-1','--sr_native_worker=true')
  $owned=Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru
  foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')};$saved=@{}
  $session=[pscustomobject]@{pid=$owned.Id;process_start_utc=$owned.StartTime.ToUniversalTime().ToString('o');exe=$Exe;input=$inputFile;variant=$Variant;api=$Api;profiled=[bool]$StageTiming;args=$arguments;reference_image=$ReferenceImage;reference_sha256=(Get-FileHash -LiteralPath $ReferenceImage).Hash}
  $session|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $statePath -Encoding UTF8
  Save-ReplayRequest 'launch'
  function Wait-ReplayControl([string]$File,[string]$State) {
    Save-ReplayRequest $State
    $deadline=[DateTime]::UtcNow.AddMinutes(5)
    while(-not(Test-Path -LiteralPath (Join-Path $Directory $File))) {
      if([DateTime]::UtcNow -gt $deadline){throw "Timed out waiting for $File Computer Use evidence"}
      $null=Get-ReplayProcess $session
      Start-Sleep -Milliseconds 500
    }
  }
  # Computer Use activates the observed window, enters New Game, then waits for
  # gameplay. Ready must follow at least 25 s of process warmup and HUD evidence.
  Start-Sleep -Seconds 25
  Wait-ReplayControl 'ready.json' 'await-ready'
  $start=Get-Item -LiteralPath (Join-Path $Directory 'start.png')
  if(-not(Test-ReplayImageTime $start.LastWriteTimeUtc ([DateTime]::UtcNow))){throw 'Start screenshot is stale'}
  . "$PSScriptRoot/image_gate_lib.ps1"
  $gate=Compare-GateImages -Reference $ReferenceImage -Candidate $start.FullName
  $gate|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Directory 'gate.json') -Encoding UTF8
  if(-not $gate.Pass){throw "Image gate failed: $($gate.Reason)"}
  . "$PSScriptRoot/bench_window_metadata.ps1"
  $windows=@()
  foreach($scenario in @('idle','forward')) {
    if(Get-Process clang,clang++,lld-link,ninja,cmake,sr_native_tests,sr_memory_tracking_tests -ErrorAction SilentlyContinue){throw 'Concurrent build/test'}
    Save-ReplayRequest "measure-$scenario"
    $lineCount=@(Get-Content -LiteralPath $log).Count
    if($scenario -eq 'forward'){Send-ReplayInput $session 0 32767 30000}
    $begin=[DateTimeOffset]::UtcNow
    try {Start-Sleep -Seconds 20;$end=[DateTimeOffset]::UtcNow;$null=Get-ReplayProcess $session}
    finally {Send-ReplayInput $session 0 0 1}
    $lines=@(Get-Content -LiteralPath $log|Select-Object -Skip $lineCount)
    $lines|Set-Content -LiteralPath (Join-Path $Directory "${scenario}_window.log") -Encoding UTF8
    $fps=@($lines|Select-String 'guest fps: ([\d.]+)'|ForEach-Object {[double]::Parse($_.Matches[0].Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)})
    if($fps.Count -lt 8){throw 'Too few periodic FPS samples'}
    $row=New-BenchWindowMetadata (Split-Path $Directory -Leaf) $scenario $begin $end
    $row|Add-Member -NotePropertyName fps_samples -NotePropertyValue $fps
    $windows+=@($row)
    @{schema=1;windows=$windows}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Directory 'windows.json') -Encoding UTF8
  }
  Wait-ReplayControl 'end.json' 'await-end'
  if(-not(Test-ReplayImageTime $end.UtcDateTime (Get-Item -LiteralPath (Join-Path $Directory 'end.png')).LastWriteTimeUtc)){throw 'End screenshot outside ten-second evidence window'}
  Assert-ReplayTrace ([bool]$StageTiming -and $Api -eq 'vulkan') ($timeline+'.replay.csv') @(Get-Content -LiteralPath $log)
  Save-ReplayRequest 'complete'
} catch {Save-ReplayRequest ('failed: '+$_.Exception.Message);throw}
finally {
  foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
  if($session){try{Send-ReplayInput $session 0 0 1}finally{try{$process=Get-ReplayProcess $session;Stop-Process -Id $process.Id;$process.WaitForExit(10000)|Out-Null}catch{Write-Warning $_.Exception.Message}}}
  elseif($owned){Stop-Process -Id $owned.Id -ErrorAction SilentlyContinue}
}
