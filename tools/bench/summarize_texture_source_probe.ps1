<# Summarize completed source probes; times are totals over stable sampled checks, not frame times. #>
param([string]$NamePrefix = 'native_c9_source')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$results = @()
foreach ($api in @('d3d12', 'vulkan')) {
  $lines = @(Get-Content -LiteralPath "$root/logs/bench_${NamePrefix}_${api}.log" |
    Select-String 'native texture source probe: ')
  if ($lines.Count -ne 1) { throw "Expected one completed source probe for $api; found $($lines.Count)" }
  $fields = [ordered]@{}
  foreach ($match in [regex]::Matches($lines[0].Line, '(\w+)=([^\s]+)')) {
    $key = $match.Groups[1].Value
    $value = $match.Groups[2].Value
    if ($key -eq 'api' -or $key -like '*protect_or') { $fields[$key] = $value }
    else { $fields[$key] = [double]::Parse($value, [cultureinfo]::InvariantCulture) }
  }
  if ($fields.api -ne $api -or $fields.stable -le 0 -or $fields.owned_ms -le 0 -or $fields.original_ms -le 0 -or
      $fields.selected -ne ($fields.stable + $fields.changed + $fields.unreadable)) {
    throw "Invalid or incomplete source probe for $api"
  }
  $fields.original_us_per_check = $fields.original_ms * 1000 / $fields.stable
  $fields.guest_us_per_check = $fields.guest_ms * 1000 / $fields.stable
  $fields.owned_us_per_check = $fields.owned_ms * 1000 / $fields.stable
  $fields.copy_us_per_check = $fields.copy_ms * 1000 / $fields.stable
  $fields.original_to_owned_ratio = $fields.original_ms / $fields.owned_ms
  $fields.guest_to_owned_ratio = $fields.guest_ms / $fields.owned_ms
  $fields.copy_plus_owned_to_original_ratio = ($fields.copy_ms + $fields.owned_ms) / $fields.original_ms
  $results += [pscustomobject]$fields
}
$results | ConvertTo-Json -Depth 5 | Set-Content "$root/logs/${NamePrefix}_summary.json"
$results | ConvertTo-Json -Depth 5
