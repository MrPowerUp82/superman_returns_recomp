param(
  [ValidateSet('Start', 'Input', 'Script', 'Status', 'Stop')]
  [string]$Action = 'Status',
  [string]$Script,
  [int]$Buttons = 0,
  [int]$Forward = 0,
  [ValidateRange(-32768, 32767)][int]$LookX = 0,
  [ValidateRange(-32768, 32767)][int]$LookY = 0,
  [ValidateSet(0, 1)][int]$Port = 0,
  [ValidateRange(1, 5000)][int]$Milliseconds = 250,
  [switch]$NoScript,
  [switch]$CameraProbe,
  [switch]$SecondCamera,
  [switch]$CameraTransport,
  [switch]$PreviewP2,
  [switch]$SplitPreview,
  [switch]$SharedIdle,
  [switch]$StatusProbe,
  [switch]$AptProbe,
  [switch]$StaminaPreview,
  [switch]$AutoJoin,
  [ValidateSet('native', 'xenos')][string]$Renderer = 'native'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$probe = Join-Path $root 'artifacts/coop-test'
$exe = Join-Path $root 'port/out/build/win-amd64-release/superman_returns.exe'
$pidFile = Join-Path $probe 'owned-pid.txt'
$inputFile = Join-Path $probe 'input.txt'
$p2InputFile = Join-Path $probe 'input-p2.txt'
$mailbox = Join-Path $probe 'command.lua'
$log = Join-Path $probe 'probe.log'

function Get-OwnedProbe {
  if (-not (Test-Path -LiteralPath $pidFile)) { return $null }
  $ownedId = [int](Get-Content -LiteralPath $pidFile -Raw)
  $owned = Get-CimInstance Win32_Process -Filter "ProcessId = $ownedId"
  if ($owned -and $owned.ExecutablePath -eq $exe -and
      $owned.CommandLine.Contains($probe)) { return $owned }
  return $null
}

function Set-Mailbox([string]$source) {
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Script missing: $source" }
  $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $source).Path)
  if (-not $bytes.Length -or $bytes.Length -ge 65536) { throw 'Script must contain 1..65535 bytes' }
  $temporary = Join-Path $probe 'command.tmp.lua'
  [IO.File]::WriteAllBytes($temporary, $bytes)
  Move-Item -LiteralPath $temporary -Destination $mailbox -Force
}

switch ($Action) {
  Start {
    if($AutoJoin){$StaminaPreview=$true}
    if($StaminaPreview){$AptProbe=$true}
    if($AptProbe){$SplitPreview=$true}
    if($SplitPreview -and $Renderer -ne 'native'){throw 'Split preview requires the native renderer'}
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the game with build.cmd first' }
    if (Get-CimInstance Win32_Process -Filter "Name = 'superman_returns.exe'") {
      throw 'Close the running game before starting the isolated probe'
    }
    New-Item -ItemType Directory -Path $probe -Force | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $probe 'game/default.xex'))) {
      Copy-Item -LiteralPath (Join-Path $root 'game') -Destination (Join-Path $probe 'game') -Recurse
    }
    $userdata = Join-Path $probe 'userdata'
    New-Item -ItemType Directory -Path $userdata -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $root 'save_to_skip_warworld_crash.zip'))
    try {
      $prefix = [IO.Path]::GetFullPath($userdata) + [IO.Path]::DirectorySeparatorChar
      foreach ($entry in $zip.Entries) {
        if ($entry.FullName -eq 'LEIAME.txt' -or $entry.FullName.EndsWith('/')) { continue }
        $destination = [IO.Path]::GetFullPath((Join-Path $userdata $entry.FullName))
        if (-not $destination.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
          throw 'Archive entry escapes isolated save directory'
        }
        if (-not (Test-Path -LiteralPath $destination)) {
          New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
          [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $false)
        }
      }
    } finally { $zip.Dispose() }
    if (-not $Script) { $Script = Join-Path $PSScriptRoot 'status.lua' }
    if (-not $NoScript) { Set-Mailbox $Script }
    '1 100 0 0' | Set-Content -LiteralPath $inputFile -Encoding ascii
    '1 100 0 0' | Set-Content -LiteralPath $p2InputFile -Encoding ascii
    $arguments = @(
      "--game_data_root=`"$(Join-Path $probe 'game')`"",
      "--user_data_root=`"$userdata`"", "--log_file=`"$log`"",
      '--sr_coop_input=true', '--sr_skip_intro=true', "--sr_renderer=$Renderer"
    )
    if (-not $NoScript) { $arguments += "--sr_coop_script=`"$mailbox`"" }
    if ($CameraProbe -or $CameraTransport -or $PreviewP2 -or $SplitPreview) { $arguments += '--sr_coop_camera_probe=true' }
    if ($SecondCamera -or $CameraTransport -or $PreviewP2 -or $SplitPreview -or $SharedIdle -or $StatusProbe) { $arguments += '--sr_coop_second_camera=true' }
    if ($SharedIdle -or $SplitPreview) { $arguments += '--sr_coop_idle=true' }
    if ($StatusProbe -or $SplitPreview) { $arguments += '--sr_coop_status_probe=true' }
    if ($AptProbe) { $arguments += '--sr_coop_apt_probe=true' }
    if ($StaminaPreview) { $arguments += '--sr_coop_stamina_preview=true' }
    if ($AutoJoin) { $arguments += '--sr_coop_autojoin=true' }
    if ($CameraTransport -or $PreviewP2 -or $SplitPreview) { $arguments += '--sr_coop_camera_transport=true' }
    if ($PreviewP2) { $arguments += '--sr_coop_camera_preview=true' }
    if ($SplitPreview) { $arguments += '--sr_coop_split_preview=true','--sr_native_api=d3d12','--sr_dlss=off','--sr_native_render_scale=1','--sr_render_scale=100' }
    $oldInput = $env:SR_BENCH_INPUT
    $oldP2Input = $env:SR_BENCH_INPUT_P2
    $oldDump = $env:SR_DUMP_IMAGE
    try {
      $env:SR_BENCH_INPUT = $inputFile
      $env:SR_BENCH_INPUT_P2 = $p2InputFile
      $env:SR_DUMP_IMAGE = Join-Path $probe 'guest-image.bin'
      $process = Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') `
        -WorkingDirectory (Split-Path $exe -Parent) -WindowStyle Hidden -PassThru
      $process.Id | Set-Content -LiteralPath $pidFile
      $process.PriorityClass = 'BelowNormal'
      "Started isolated probe PID $($process.Id). Use save slot 2 (15%). Log: $log"
    } finally {
      $env:SR_BENCH_INPUT = $oldInput
      $env:SR_BENCH_INPUT_P2 = $oldP2Input
      $env:SR_DUMP_IMAGE = $oldDump
    }
  }
  Input {
    if (-not (Get-OwnedProbe)) { throw 'No running owned probe' }
    if ($Port -eq 1) { $inputFile = $p2InputFile }
    $sequence = [long](([string](Get-Content -LiteralPath $inputFile -Raw)).Split(' ')[0]) + 1
    "$sequence $Milliseconds $Buttons $Forward $LookX $LookY" | Set-Content -LiteralPath $inputFile -Encoding ascii
  }
  Script {
    if (-not (Get-OwnedProbe)) { throw 'No running owned probe' }
    if (-not $Script) { throw 'Pass -Script with the Lua source path' }
    Set-Mailbox $Script
  }
  Status {
    Get-OwnedProbe | Select-Object ProcessId, ExecutablePath
    if (Test-Path -LiteralPath $log) {
      Select-String -LiteralPath $log -Pattern 'coop Lua:|benchmark input: accepted|Unhandled' |
        Select-Object -Last 12 -ExpandProperty Line
    }
  }
  Stop {
    $owned = Get-OwnedProbe
    if ($owned) { Stop-Process -Id $owned.ProcessId; "Stopped owned probe PID $($owned.ProcessId)" }
  }
}
