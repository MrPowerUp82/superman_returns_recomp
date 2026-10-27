<#
.SYNOPSIS
Bounded external RIP/RSP sampling of one existing Windows x64 thread.
.DESCRIPTION
Defaults: 6 seconds, 10 ms between attempts (100 Hz nominal). No target launch,
memory reads, hooks, symbols, or stack walking. Each sample briefly suspends the
selected thread: this is observational but intrusive; FPS is NOT a clean baseline.
Scheduling and API overhead can reduce the actual sampling frequency. The time
limit bounds the sampling window, not the latency of an individual Windows call.

Requires 64-bit PowerShell on x64 Windows and an x64 target. The process must
already be running. Access denied is fatal; this script does not elevate itself.
Writes Output as CSV and Output.modules.json as the pre-sampling module baseline.
The baseline does not track subsequent module loads/unloads. No outputs are
written until all suspension/resume attempts have completed and handles closed.
Finally blocks attempt resume after every successful suspend, but force-killing
the sampler/host or a failed ResumeThread can leave the target suspended.
.EXAMPLE
.\sample_thread_pc.ps1 -ProcessId 1234 -ThreadId 5678 -Output C:\Temp\pc.csv
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateRange(1, 2147483647)] [int]$ProcessId,
    [Parameter(Mandatory)] [ValidateRange(1, 4294967295)] [uint32]$ThreadId,
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string]$Output,
    [ValidateRange(1, 30)] [int]$Seconds = 6,
    [ValidateRange(5, 1000)] [int]$IntervalMs = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([IntPtr]::Size -ne 8 -or $env:OS -ne 'Windows_NT') {
    throw 'Run this script in 64-bit PowerShell on Windows.'
}
if ($ProcessId -eq $PID) { throw 'Self-sampling is unsafe; select an external process.' }
$csvPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$modulesPath = $csvPath + '.modules.json'
$outputDirectory = [IO.Path]::GetDirectoryName($csvPath)
if (-not [IO.Directory]::Exists($outputDirectory)) {
    throw "Output directory must already exist: $outputDirectory"
}
if ([IO.File]::Exists($csvPath) -or [IO.File]::Exists($modulesPath)) {
    throw 'Choose unused output paths; existing captures are not overwritten.'
}

if (-not ('BoundedThreadPcV1.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace BoundedThreadPcV1
{
    public struct Sample
    {
        public double Elapsed;
        public ulong Pc, Sp;
        public uint PreviousSuspendCount;
        public int SuspendError, GetContextError, ResumeError;
        public bool Suspended, ContextAttempted, ContextOk, ResumeFailed;
    }

    public sealed class Capture
    {
        // Allocate storage before any suspension. Count includes failed attempts.
        public readonly Sample[] Samples = new Sample[6000];
        public int Count;
    }

    public static class Native
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint GetProcessIdOfThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetProcessTimes(IntPtr process, out ulong creation,
            out ulong exit, out ulong kernel, out ulong user);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWow64Process2(IntPtr process,
            out ushort processMachine, out ushort nativeMachine);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint SuspendThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetThreadContext(IntPtr thread, IntPtr context);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);

        public static ulong CreationTime(IntPtr process)
        {
            ulong creation, exit, kernel, user;
            if (!GetProcessTimes(process, out creation, out exit, out kernel, out user))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "GetProcessTimes failed");
            return creation;
        }

        public static void RequireX64(IntPtr process)
        {
            ushort machine, nativeMachine;
            // Fail closed on older Windows without IsWow64Process2.
            if (!IsWow64Process2(process, out machine, out nativeMachine))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "IsWow64Process2 failed");
            if (nativeMachine != 0x8664 || machine != 0)
                throw new NotSupportedException("Requires native x64 Windows and an x64 target.");
        }

        public static Capture Collect(IntPtr thread, int seconds, int intervalMs)
        {
            if (seconds < 1 || seconds > 30 || intervalMs < 5 || intervalMs > 1000)
                throw new ArgumentOutOfRangeException("Sampling bounds");
            Capture capture = new Capture();
            // CONTEXT is 1280 bytes, aligned to 16. Allocate exactly once before
            // SuspendThread, retain the original pointer for FreeHGlobal.
            IntPtr allocation = Marshal.AllocHGlobal(1280 + 15);
            try
            {
                IntPtr context = new IntPtr((allocation.ToInt64() + 15L) & ~15L);
                for (int offset = 0; offset < 1280; offset += 4)
                    Marshal.WriteInt32(context, offset, 0);
                Stopwatch clock = Stopwatch.StartNew();
                while (capture.Count < 6000 && clock.Elapsed.TotalSeconds < seconds)
                {
                    Sample sample = new Sample();
                    sample.Elapsed = clock.Elapsed.TotalSeconds;
                    double nextMs = clock.Elapsed.TotalMilliseconds + intervalMs;
                    // AMD64 CONTEXT_CONTROL. Flags are reset before suspension.
                    Marshal.WriteInt32(context, 48, 0x00100001);
                    bool suspended = false;
                    try
                    {
                        uint previous = SuspendThread(thread);
                        if (previous == UInt32.MaxValue)
                            sample.SuspendError = Marshal.GetLastWin32Error();
                        else
                        {
                            suspended = true;
                            sample.Suspended = true;
                            sample.PreviousSuspendCount = previous;
                            sample.ContextAttempted = true;
                            sample.ContextOk = GetThreadContext(thread, context);
                            if (!sample.ContextOk)
                                sample.GetContextError = Marshal.GetLastWin32Error();
                        }
                    }
                    finally
                    {
                        // No PowerShell, output, symbol lookup, stack walking,
                        // allocation, or sleeping while our suspend is held.
                        if (suspended && ResumeThread(thread) == UInt32.MaxValue)
                        {
                            sample.ResumeError = Marshal.GetLastWin32Error();
                            sample.ResumeFailed = true;
                        }
                    }
                    // Read the local context buffer only after the resume attempt.
                    if (sample.ContextOk)
                    {
                        sample.Pc = unchecked((ulong)Marshal.ReadInt64(context, 248));
                        sample.Sp = unchecked((ulong)Marshal.ReadInt64(context, 152));
                    }
                    capture.Samples[capture.Count++] = sample;
                    // Do not keep sampling after a resume failure.
                    if (sample.ResumeFailed) break;
                    double waitMs = Math.Min(nextMs - clock.Elapsed.TotalMilliseconds,
                        seconds * 1000.0 - clock.Elapsed.TotalMilliseconds);
                    if (waitMs > 0) Thread.Sleep((int)Math.Ceiling(waitMs));
                }
                return capture;
            }
            finally { Marshal.FreeHGlobal(allocation); }
        }
    }
}
'@
}

function Assert-ProcessIdentity([uint64]$ExpectedCreation) {
    # Reopen by PID: an original process handle alone does not identify the
    # process currently associated with that PID after an exit/reuse.
    $identityHandle = [BoundedThreadPcV1.Native]::OpenProcess(0x1000, $false, [uint32]$ProcessId)
    if ($identityHandle -eq [IntPtr]::Zero) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error(), 'Cannot recheck process identity')
    }
    try {
        if ([BoundedThreadPcV1.Native]::CreationTime($identityHandle) -ne $ExpectedCreation) {
            throw 'Process start identity changed (PID reuse); capture is invalid.'
        }
        $taskProcess.Refresh()
        if ($taskProcess.HasExited) { throw 'Selected process exited; capture is incomplete.' }
    } finally {
        [void][BoundedThreadPcV1.Native]::CloseHandle($identityHandle)
    }
}

$processHandle = [IntPtr]::Zero
$threadHandle = [IntPtr]::Zero
$taskProcess = $null
$identityError = $null
$capture = $null
$baseline = $null
try {
    # PROCESS_QUERY_LIMITED_INFORMATION only. No ReadProcessMemory import/use.
    $processHandle = [BoundedThreadPcV1.Native]::OpenProcess(0x1000, $false, [uint32]$ProcessId)
    if ($processHandle -eq [IntPtr]::Zero) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error(), 'OpenProcess failed')
    }
    [BoundedThreadPcV1.Native]::RequireX64($processHandle)
    [uint64]$creationBefore = [BoundedThreadPcV1.Native]::CreationTime($processHandle)
    $taskProcess = [Diagnostics.Process]::GetProcessById($ProcessId)
    if ([uint64]$taskProcess.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $creationBefore) {
        throw 'Process identity changed before module enumeration.'
    }
    $snapshotUtc = [DateTime]::UtcNow.ToString('o')
    # Process.Modules may require query/VM_READ rights internally for loader
    # enumeration. This script does not read target memory or decode instructions.
    $modules = @(
        foreach ($module in $taskProcess.Modules) {
            [uint64]$baseAddress = $module.BaseAddress.ToInt64()
            [pscustomobject]@{
                name = $module.ModuleName
                image_file = $module.FileName
                base_hex = ('0x{0:X16}' -f $baseAddress)
                size_bytes = [uint64]$module.ModuleMemorySize
            }
        }
    )
    if ($modules.Count -eq 0) { throw 'Module baseline is empty; refusing to sample.' }
    # THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION.
    $threadHandle = [BoundedThreadPcV1.Native]::OpenThread(0x004A, $false, $ThreadId)
    if ($threadHandle -eq [IntPtr]::Zero) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error(), 'OpenThread failed')
    }
    $owner = [BoundedThreadPcV1.Native]::GetProcessIdOfThread($threadHandle)
    if ($owner -eq 0) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error(), 'GetProcessIdOfThread failed')
    }
    if ($owner -ne [uint32]$ProcessId) { throw 'Selected thread does not belong to the selected process.' }
    Assert-ProcessIdentity $creationBefore
    $baseline = [ordered]@{
        process_id = $ProcessId
        thread_id = $ThreadId
        process_start_utc = [DateTime]::FromFileTimeUtc([long]$creationBefore).ToString('o')
        process_start_filetime_before = $creationBefore.ToString([Globalization.CultureInfo]::InvariantCulture)
        process_start_filetime_after = $null
        process_identity_verified_after = $false
        process_identity_error = $null
        module_snapshot_utc = $snapshotUtc
        seconds = $Seconds
        interval_ms = $IntervalMs
        sample_limit = 6000
        intrusive = $true
        fps_is_clean_baseline = $false
        modules = $modules
    }
    Write-Warning 'Intrusive PC sampling: selected thread will be briefly suspended. FPS is not a clean baseline. Do not force-kill this sampler.'
    $capture = [BoundedThreadPcV1.Native]::Collect($threadHandle, $Seconds, $IntervalMs)
    try {
        Assert-ProcessIdentity $creationBefore
        [uint64]$creationAfter = [BoundedThreadPcV1.Native]::CreationTime($processHandle)
        if ($creationAfter -ne $creationBefore) { throw 'Process start identity changed.' }
        $baseline.process_start_filetime_after = $creationAfter.ToString([Globalization.CultureInfo]::InvariantCulture)
        $baseline.process_identity_verified_after = $true
    } catch {
        $identityError = $_.Exception.Message
        $baseline.process_identity_error = $identityError
    }
} finally {
    if ($threadHandle -ne [IntPtr]::Zero) { [void][BoundedThreadPcV1.Native]::CloseHandle($threadHandle) }
    if ($processHandle -ne [IntPtr]::Zero) { [void][BoundedThreadPcV1.Native]::CloseHandle($processHandle) }
    if ($null -ne $taskProcess) { $taskProcess.Dispose() }
}

# Everything below runs with no suspend held and no target handles open.
$contextCount = 0
$resumeFailures = 0
$rows = @(
    for ($index = 0; $index -lt $capture.Count; $index++) {
        $sample = $capture.Samples[$index]
        if ($sample.ContextOk) { $contextCount++ }
        if ($sample.ResumeFailed) { $resumeFailures++ }
        [pscustomobject]@{
            elapsed_s = $sample.Elapsed.ToString('F6', [Globalization.CultureInfo]::InvariantCulture)
            tid = $ThreadId
            pc_hex = $(if ($sample.ContextOk) { '0x{0:X16}' -f [uint64]$sample.Pc } else { '' })
            sp_hex = $(if ($sample.ContextOk) { '0x{0:X16}' -f [uint64]$sample.Sp } else { '' })
            suspend_failed = -not $sample.Suspended
            getcontext_failed = $(if ($sample.ContextAttempted) { -not $sample.ContextOk } else { '' })
            resume_failed = $sample.ResumeFailed
            suspend_error = $sample.SuspendError
            getcontext_error = $(if ($sample.ContextAttempted) { $sample.GetContextError } else { '' })
            resume_error = $(if ($sample.Suspended) { $sample.ResumeError } else { '' })
            previous_suspend_count = $(if ($sample.Suspended) { $sample.PreviousSuspendCount } else { '' })
        }
    }
)
$baseline['sample_count'] = $capture.Count
$baseline['context_count'] = $contextCount
$baseline['resume_failures'] = $resumeFailures
$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$baseline | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $modulesPath -Encoding UTF8
if ($resumeFailures -gt 0) {
    throw "ResumeThread failed; sampling stopped. Target may remain suspended. Inspect resume_error in $csvPath"
}
if ($null -ne $identityError) { throw "Saved capture is invalid/incomplete: $identityError" }
if ($contextCount -eq 0) { throw "No contexts captured; inspect failure columns in $csvPath" }
Write-Output "$contextCount contexts / $($capture.Count) attempts saved to $csvPath; module baseline: $modulesPath"
