<#
Read accumulated CPU time without suspending game threads. Each row covers
one sampling interval, not one frame. CPU time has OS accounting granularity;
use averages over many intervals. cores=1 means one fully occupied CPU core.
Example: sample_thread_cpu.ps1 -ProcessId 1234 -Seconds 90 -Output logs/cpu.csv
#>
param(
  [Parameter(Mandatory)] [ValidateRange(1, 2147483647)] [int]$ProcessId,
  [ValidateRange(1, 600)] [int]$Seconds = 90,
  [Parameter(Mandatory)] [string]$Output
)
$ErrorActionPreference = 'Stop'
$taskProcess = [System.Diagnostics.Process]::GetProcessById($ProcessId)
$timer = [System.Diagnostics.Stopwatch]::StartNew()
$previous = @{}
$previousTime = 0.0
$rows = [System.Collections.Generic.List[object]]::new()
try {
  while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
    $taskProcess.Refresh()
    if ($taskProcess.HasExited) { break }
    $now = $timer.Elapsed.TotalSeconds
    $sampleTime = [DateTimeOffset]::Now.ToString('o')
    $interval = $now - $previousTime
    $next = @{}
    if ($taskProcess.Threads.Count -gt 512) { throw 'Thread count exceeds the diagnostic bound (512)' }
    foreach ($thread in $taskProcess.Threads) {
      try {
        $threadId = $thread.Id
        $threadStart = $thread.StartTime.ToUniversalTime().Ticks
        $cpu = $thread.TotalProcessorTime.TotalMilliseconds
        $userCpu = $thread.UserProcessorTime.TotalMilliseconds
        $kernelCpu = $thread.PrivilegedProcessorTime.TotalMilliseconds
        $next[$threadId] = @{ started = $threadStart; cpu = $cpu; user = $userCpu; kernel = $kernelCpu }
        if ($previous.ContainsKey($threadId) -and $threadStart -eq $previous[$threadId].started -and
            $cpu -ge $previous[$threadId].cpu -and $userCpu -ge $previous[$threadId].user -and
            $kernelCpu -ge $previous[$threadId].kernel -and $interval -gt 0) {
          $delta = $cpu - $previous[$threadId].cpu
          $rows.Add([pscustomobject]@{
            sample_local = $sampleTime
            elapsed_s = $now
            interval_s = $interval
            tid = $threadId
            cpu_ms = $delta
            user_ms = $userCpu - $previous[$threadId].user
            kernel_ms = $kernelCpu - $previous[$threadId].kernel
            cores = $delta / ($interval * 1000.0)
          })
        }
      } catch [System.ComponentModel.Win32Exception] {
        # A thread may exit between enumeration and the query. New or reused
        # IDs start a fresh interval; no negative CPU deltas are fabricated.
      } catch [System.InvalidOperationException] {
      } finally {
        $thread.Dispose()
      }
    }
    $previous = $next
    $previousTime = $now
    Start-Sleep -Milliseconds 1000
  }
} finally {
  $taskProcess.Dispose()
  $timer.Stop()
  $rows | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8
}
if ($rows.Count -eq 0) { throw 'No thread CPU intervals acquired; no CPU utilization conclusion is available' }
Write-Output "$($rows.Count) thread CPU intervals saved to $Output"
