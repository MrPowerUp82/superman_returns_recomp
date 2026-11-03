# Requires a running isolated probe, gameplay on foot, and an already spawned P2.
# Enemy FSMs remain paused in this disposable probe; normal saves are untouched.
param([ValidateRange(0.01, 2)][double]$IdleTolerance = 0.25)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$probe = Join-Path $root 'artifacts/coop-test'
$log = Join-Path $probe 'probe.log'
$launcher = Join-Path $PSScriptRoot 'probe.ps1'
$scriptFile = Join-Path $probe 'motion-check.lua'
$reportFile = Join-Path $probe 'motion-check.json'
$runId = [Guid]::NewGuid().ToString('N')
$script:stage = 0
$checks = [Collections.Generic.List[object]]::new()
$report = [ordered]@{ passed = $false; idle_tolerance = $IdleTolerance; checks = $checks }

function Invoke-ProbeLua([string]$code) {
  $script:stage++
  $token = "COOP_TEST_${runId}_$($script:stage):"
  $offset = (Get-Item -LiteralPath $log).Length
  $source = "local token=`"$token`"`n" + $code
  [IO.File]::WriteAllText($scriptFile, $source, [Text.UTF8Encoding]::new($false))
  & $launcher -Action Script -Script $scriptFile
  $deadline = (Get-Date).AddSeconds(15)
  do {
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try {
      $null = $stream.Seek($offset, 'Begin')
      $reader = [IO.StreamReader]::new($stream)
      $fresh = $reader.ReadToEnd()
      $reader.Dispose()
    } finally { $stream.Dispose() }
    $pattern = 'coop Lua: status=0 .* result=' + [regex]::Escape($token) + '([^\r\n]*)'
    if ($fresh -match $pattern) { return $Matches[1] }
    if ($fresh -match 'Unhandled guest access violation|coop Lua: status=[1-9]') {
      throw 'Guest/script failure; inspect the probe log'
    }
    Start-Sleep -Milliseconds 100
  } until ((Get-Date) -gt $deadline)
  throw 'Timed out waiting for this probe script; check gameplay/pause state'
}

function Read-Sample {
  $text = Invoke-ProbeLua @'
if not SR_COOP_P2 then return token.."ERROR no P2" end
local p1=GetIDFromBinIndex("SUPERMAN",0)
local x1,y1,z1=GetEntityPosition(p1)
local x2,y2,z2=GetEntityPosition(SR_COOP_P2)
return token..tostring(x1).." "..tostring(y1).." "..tostring(z1).." "..
    tostring(x2).." "..tostring(y2).." "..tostring(z2)
'@
  $values = $text.Trim().Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
  if ($values.Count -ne 6) { throw "Invalid sample: $text" }
  $numbers = @($values | ForEach-Object {
    $number = [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture)
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) { throw 'Non-finite actor position' }
    $number
  })
  return ,$numbers
}

function Get-Displacement($before, $after, [int]$player) {
  $sum = 0.0
  for ($i = $player * 3; $i -lt $player * 3 + 3; $i++) {
    $delta = $after[$i] - $before[$i]
    $sum += $delta * $delta
  }
  return [Math]::Sqrt($sum)
}

function Check-Motion([string]$name, $before, $after, [bool]$p1Moves, [bool]$p2Moves) {
  $distances = @((Get-Displacement $before $after 0), (Get-Displacement $before $after 1))
  $expected = @($p1Moves, $p2Moves)
  $passed = $true
  for ($i = 0; $i -lt 2; $i++) {
    if ($expected[$i]) { $passed = $passed -and $distances[$i] -ge 2.0 }
    else { $passed = $passed -and $distances[$i] -le $IdleTolerance }
  }
  $checks.Add([ordered]@{ name = $name; passed = $passed; p1_distance = $distances[0];
      p2_distance = $distances[1]; before = $before; after = $after })
  if (-not $passed) { throw "Motion check failed: $name (P1=$($distances[0]), P2=$($distances[1]))" }
  "PASS $name (P1=$($distances[0]), P2=$($distances[1]))" | Write-Host
}

try {
  & $launcher -Action Input -Port 0 -Forward 0
  & $launcher -Action Input -Port 1 -Forward 0
  $isolate = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'isolate_movement.lua'))
  # Capture the known isolation script's return so it can carry this run's token.
  $result = Invoke-ProbeLua ("local function isolate()`n" + $isolate + "`nend`nreturn token..isolate()")
  if (-not $result.StartsWith('isolated P2=')) { throw "Isolation failed: $result" }
  $report.isolation = $result
  Start-Sleep -Seconds 1
  $before = Read-Sample
  foreach ($port in @(1, 0)) {
    & $launcher -Action Input -Port $port -Forward 32767 -Milliseconds 2000
    Start-Sleep -Milliseconds 2500
    $after = Read-Sample
    Check-Motion "port $port only" $before $after ($port -eq 0) ($port -eq 1)
    $before = $after
    Start-Sleep -Seconds 2
    $after = Read-Sample
    Check-Motion "port $port release" $before $after $false $false
    $before = $after
  }
  & $launcher -Action Input -Port 0 -Forward 32767 -Milliseconds 2000
  & $launcher -Action Input -Port 1 -Forward -32767 -Milliseconds 2000
  Start-Sleep -Milliseconds 2500
  $after = Read-Sample
  Check-Motion 'both ports' $before $after $true $true
  $before = $after
  Start-Sleep -Seconds 2
  $after = Read-Sample
  Check-Motion 'both released' $before $after $false $false
  $report.passed = $true
} catch {
  $report.error = $_.Exception.Message
  throw
} finally {
  $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportFile -Encoding utf8
  Write-Host "Evidence: $reportFile"
}
