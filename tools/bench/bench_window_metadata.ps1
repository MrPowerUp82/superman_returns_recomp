function New-BenchWindowMetadata {
  param([string]$Name,[ValidateSet('idle','forward')][string]$Scenario,[DateTimeOffset]$Begin,[DateTimeOffset]$End)
  if($End -le $Begin){throw 'Benchmark window must have a positive duration'}
  [pscustomobject]@{
    name=$Name;scenario=$Scenario
    begin_utc_ns=[int64](($Begin.UtcDateTime.Ticks-[int64]621355968000000000)*100)
    end_utc_ns=[int64](($End.UtcDateTime.Ticks-[int64]621355968000000000)*100)
  }
}
