param([string]$NamePrefix = 'native_c10_ranges')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
function Read-Fields([string]$Line) {
  $fields=[ordered]@{}
  foreach($m in [regex]::Matches($Line,'(\w+)=([^\s]+)')) {
    $key=$m.Groups[1].Value; $value=$m.Groups[2].Value
    if($key -in @('caller','api','object','vtable','target','next_target')) { $fields[$key]=$value }
    elseif($value -in @('true','false')) { $fields[$key]=$value -eq 'true' }
    else { $fields[$key]=[double]::Parse($value,[cultureinfo]::InvariantCulture) }
  }
  return $fields
}
$results=@()
foreach($api in @('d3d12','vulkan')) {
  $lines=Get-Content -LiteralPath "$root/logs/bench_${NamePrefix}_${api}.log"
  $last=@($lines | Select-String 'native resource unlock audit: ' | Select-Object -Last 1)
  if($last.Count -ne 1) { throw "Missing unlock summary for $api" }
  $fields=Read-Fields $last[0].Line
  if($fields.calls -ne ($fields.candidates+$fields.other+$fields.invalid_fetch+$fields.unreadable) -or
     $fields.matched_range_checks -gt $fields.validation_checks -or
     $fields.matched_range_sets -gt $fields.range_sets -or $fields.matched_range_sets -le 0) {
    throw "Inconsistent or uncorrelated audit for $api"
  }
  $callers=@($lines | Select-String "native resource unlock caller: frame=$($fields.frame) " |
    ForEach-Object { [pscustomobject](Read-Fields $_.Line) })
  $callerSum=($callers | Measure-Object -Property calls -Sum).Sum
  if($callerSum+$fields.dropped_callers -ne $fields.calls) { throw "Caller totals disagree for $api" }
  $producers=@($lines | Select-String "native texture producer audit: frame=$($fields.frame) " |
    ForEach-Object { [pscustomobject](Read-Fields $_.Line) })
  if($producers.Count -gt 0) {
    $planeCalls=($callers | Where-Object { $_.caller -in @('8235CBD4','8235CBE0','8235CBEC') } |
      Measure-Object -Property calls -Sum).Sum
    $samples=($producers | Measure-Object -Property samples -Sum).Sum
    # unreadable/dropped are global cumulative counters repeated per tuple.
    if($samples+$producers[0].unreadable+$producers[0].dropped -ne $planeCalls) {
      throw "Producer samples disagree with successful plane unlocks for $api"
    }
  }
  $handoff=$null
  $handoffLines=@($lines | Select-String "native frame handoff audit: frame=$($fields.frame) ")
  if($handoffLines.Count -gt 0) {
    $handoff=Read-Fields $handoffLines[-1].Line
    $planeCalls=($callers | Where-Object { $_.caller -in @('8235CBD4','8235CBE0','8235CBEC') } |
      Measure-Object -Property calls -Sum).Sum
    if($handoff.returns -gt $handoff.calls -or
       $handoff.nonnegative+$handoff.negative -ne $handoff.returns -or
       $handoff.unreadable+$handoff.unsupported+$handoff.nested -gt $handoff.returns -or
       $handoff.groups -le 0 -or $handoff.groups -gt $handoff.returns -or
       3*$handoff.groups -gt $handoff.plane_matches -or
       $handoff.plane_matches+$handoff.plane_misses+$handoff.unattributed -ne $planeCalls) {
      throw "Inconsistent or uncorrelated handoff journal for $api"
    }
  }
  $textureChecks=0; $changes=0; $unnotified=0
  foreach($line in @($lines | Select-String "native texture audit interval: api=$api ")) {
    $a=Read-Fields $line.Line
    $textureChecks+=$a.checks; $changes+=$a.changes; $unnotified+=$a.unnotified_changes
  }
  $results += [pscustomobject]@{api=$api; unlock=$fields; callers=$callers; producers=$producers; handoff=$handoff;
    texture_audit=[ordered]@{checks=$textureChecks; changes=$changes; unnotified_changes=$unnotified}}
}
$results | ConvertTo-Json -Depth 7 | Set-Content "$root/logs/${NamePrefix}_summary.json"
$results | ForEach-Object { '{0}: unlocks={1} candidates={2} matched_ranges={3} matched_checks={4} exact_fetch_matches={5} changes={6}' -f
  $_.api,$_.unlock.calls,$_.unlock.candidates,$_.unlock.matched_range_sets,$_.unlock.matched_range_checks,$_.unlock.matched_fetches,$_.texture_audit.changes }
