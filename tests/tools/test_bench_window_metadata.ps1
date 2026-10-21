$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../tools/bench/bench_window_metadata.ps1"
$begin=[DateTimeOffset]::Parse('1970-01-01T00:00:00+00:00')
$end=[DateTimeOffset]::Parse('1970-01-01T00:00:20+00:00')
$value=New-BenchWindowMetadata 'fixture' 'idle' $begin $end
if($value.name -ne 'fixture' -or $value.scenario -ne 'idle'){throw 'Window identity lost'}
if($value.begin_utc_ns -ne 0 -or $value.end_utc_ns -ne 20000000000){throw 'UTC nanoseconds are incorrect'}
$same=New-BenchWindowMetadata 'fixture' 'idle' ([DateTimeOffset]::Parse('1969-12-31T21:00:00-03:00')) ([DateTimeOffset]::Parse('1969-12-31T21:00:20-03:00'))
if($same.begin_utc_ns -ne $value.begin_utc_ns -or $same.end_utc_ns -ne $value.end_utc_ns){throw 'UTC offset changed the instant'}
$rejected=$false
try{New-BenchWindowMetadata 'fixture' 'forward' $end $begin | Out-Null}catch{$rejected=$true}
if(-not $rejected){throw 'Reversed window accepted'}
Write-Output 'PASS benchmark metadata UTC, identity, offsets and reversed-window rejection'
