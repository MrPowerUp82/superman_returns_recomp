param([switch]$HiddenIndexOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$setup = Join-Path $root 'tools/setup_memory_tracking_sdk.ps1'
$source = Join-Path $root '.tools/rexglue-write-tracking-source'
$fixtures = Join-Path $root ('.tools/tracking-setup-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $fixtures | Out-Null
function Run-Setup([string]$Checkout, [bool]$Success, [switch]$VerifyOnly, [switch]$CheckDependencies, [string]$FailurePattern = '') {
  $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $setup, '-Checkout', $Checkout)
  if (-not $CheckDependencies) { $args += '-SkipDependencies' }
  if ($VerifyOnly) { $args += '-VerifyOnly' }
  $savedPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    $output = & powershell @args 2>&1
    $code = $LASTEXITCODE
  } finally { $ErrorActionPreference = $savedPreference }
  if (($code -eq 0) -ne $Success) { throw "unexpected setup result $code`: $output" }
  if ($FailurePattern -and ($output -join "`n") -notmatch $FailurePattern) { throw "wrong setup failure: $output" }
}
function Fixture-Git([string]$Directory, [string[]]$Arguments) {
  $output = & git.exe -c core.autocrlf=false -C $Directory @Arguments
  if ($LASTEXITCODE) { throw "fixture git failed: $Arguments" }
  return ($output -join "`n")
}
function New-Fixture([string]$Name) {
  $directory = Join-Path $fixtures $Name
  & git clone --quiet --shared --no-hardlinks $source $directory
  if ($LASTEXITCODE) { throw 'fixture clone failed' }
  return $directory
}
function Snapshot-Files([string[]]$Paths) {
  $snapshot = @{}
  foreach ($path in $Paths) {
    $snapshot[$path] = if (Test-Path -LiteralPath $path) {
      @{ hash=(Get-FileHash -LiteralPath $path).Hash; ticks=(Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks }
    } else { $null }
  }
  return $snapshot
}
function Assert-Unchanged($Snapshot) {
  foreach ($path in $Snapshot.Keys) {
    $before = $Snapshot[$path]
    if ($null -eq $before) {
      if (Test-Path -LiteralPath $path) { throw "rejected setup created input: $path" }
    } elseif (-not (Test-Path -LiteralPath $path) -or
              (Get-FileHash -LiteralPath $path).Hash -ne $before.hash -or
              (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $before.ticks) {
      throw "setup changed input: $path"
    }
  }
}
$patchNames = @(
  'tools/patches/rexglue/v0.10.0/0001-memory-write-tracking.patch',
  'tools/patches/rexglue/v0.10.0/0002-xma-drain-pending.patch'
)
$patches = @($patchNames | ForEach-Object { Join-Path $root $_ })
$patchPaths = @($patches | ForEach-Object {
  ,@([regex]::Matches([IO.File]::ReadAllText($_).Replace("`r`n", "`n"), '(?m)^\+\+\+ b/(.+)$') |
    ForEach-Object { $_.Groups[1].Value })
})
$allPatchedPaths = @($patchPaths[0]) + @($patchPaths[1])
function Assert-Manifest([string]$Directory) {
  $record = Get-Content -LiteralPath (Join-Path $Directory '.sr-memory-tracking.json') -Raw | ConvertFrom-Json
  if ($record.patch_sha256 -ne (Get-FileHash -LiteralPath $patches[0]).Hash) { throw 'legacy primary patch hash changed' }
  $entries = @($record.patches.PSObject.Properties)
  if ($entries.Count -ne 2) { throw 'manifest must list both patches' }
  for ($i = 0; $i -lt $patchNames.Count; $i++) {
    if ($entries[$i].Name -cne $patchNames[$i] -or $entries[$i].Value -ne (Get-FileHash -LiteralPath $patches[$i]).Hash) {
      throw 'manifest patch order or hash mismatch'
    }
  }
  foreach ($key in $allPatchedPaths) {
    if ($record.files.PSObject.Properties[$key].Value -ne (Get-FileHash -LiteralPath (Join-Path $Directory $key)).Hash) {
      throw "manifest omitted or misidentified patched file: $key"
    }
  }
}
function Assert-HiddenIndexGuard([string]$Mode) {
  $hidden = New-Fixture $Mode
  Run-Setup $hidden $true
  $key = 'src/audio/xma_context.cpp'
  Fixture-Git $hidden @('add','--',$key) | Out-Null
  Fixture-Git $hidden @('update-index',('--' + $Mode),'--',$key) | Out-Null
  [IO.File]::AppendAllText((Join-Path $hidden $key),"`n// hidden working-tree edit`n")
  $inputs = @($allPatchedPaths | ForEach-Object { Join-Path $hidden $_ }) +
    @((Join-Path $hidden '.sr-memory-tracking.json')) +
    @(Get-ChildItem -LiteralPath (Join-Path $hidden 'src/system/write_tracking') -File -Recurse | ForEach-Object { $_.FullName })
  $before = Snapshot-Files $inputs
  Run-Setup $hidden $false -FailurePattern 'Canonical SDK content mismatch'
  Run-Setup $hidden $false -VerifyOnly -FailurePattern 'Canonical SDK content mismatch'
  Assert-Unchanged $before
}
if ($HiddenIndexOnly) {
  foreach ($mode in @('assume-unchanged','skip-worktree')) { Assert-HiddenIndexGuard $mode }
  Write-Output 'PASS installed hidden-index fixtures reject the exact content mismatch without rewriting any source or manifest'
  exit 0
}
$previousXma = Join-Path $root 'tools/patches/rexglue/v0.10.0/previous/0002-xma-drain-pending.patch'
foreach ($hideEdit in @($false,$true)) {
  $prior = New-Fixture ('previous-xma-' + $hideEdit)
  Fixture-Git $prior @('apply',$patches[0]) | Out-Null
  Fixture-Git $prior @('apply',$previousXma) | Out-Null
  $xmaKey = 'src/audio/xma_context.cpp'
  if ($hideEdit) {
    Fixture-Git $prior @('add','--',$xmaKey) | Out-Null
    Fixture-Git $prior @('update-index','--assume-unchanged','--',$xmaKey) | Out-Null
    [IO.File]::AppendAllText((Join-Path $prior $xmaKey),"`n// unrecognized hidden edit`n")
  }
  $priorInputs = @($allPatchedPaths | ForEach-Object {Join-Path $prior $_}) +
    @((Join-Path $prior '.sr-memory-tracking.json'))
  $snapshot = Snapshot-Files $priorInputs
  if ($hideEdit) {
    Run-Setup $prior $false -FailurePattern 'Canonical SDK content mismatch'
    Assert-Unchanged $snapshot
  } else {
    Run-Setup $prior $false -VerifyOnly -FailurePattern 'canonical upgrade'
    Assert-Unchanged $snapshot
    Run-Setup $prior $true
    Run-Setup $prior $true -VerifyOnly
    Assert-Manifest $prior
  }
}
$fixture = Join-Path $fixtures 'sdk'
& git clone --quiet --shared --no-hardlinks $source $fixture
if ($LASTEXITCODE) { throw 'fixture clone failed' }
Run-Setup $fixture $true
Run-Setup $fixture $true -VerifyOnly
Assert-Manifest $fixture
$manifestPath = Join-Path $fixture '.sr-memory-tracking.json'
$installedFiles = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'src/system/write_tracking') -File -Recurse) +
  @(Get-Item -LiteralPath $manifestPath) + @($allPatchedPaths | ForEach-Object { Get-Item -LiteralPath (Join-Path $fixture $_) })
$unchanged = @{}
foreach ($file in $installedFiles) {
  # Distinct historical timestamps catch writes without sleeps or filesystem
  # clock-resolution assumptions, even when Copy-Item preserves source times.
  $stamp = [datetime]::SpecifyKind([datetime]'2001-02-03', [DateTimeKind]::Utc).AddSeconds($unchanged.Count)
  [IO.File]::SetLastWriteTimeUtc($file.FullName, $stamp)
  $unchanged[$file.FullName] = @{ ticks=$stamp.Ticks; hash=(Get-FileHash -LiteralPath $file.FullName).Hash }
}
Run-Setup $fixture $true
Run-Setup $fixture $true -VerifyOnly
$rewritten = @()
foreach ($path in $unchanged.Keys) {
  if ((Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $unchanged[$path].ticks -or
      (Get-FileHash -LiteralPath $path).Hash -ne $unchanged[$path].hash) { $rewritten += $path }
}
if ($rewritten.Count) { throw "unchanged setup inputs were rewritten: $($rewritten -join ', ')" }

# User diff preferences must not change the canonical patch representation.
foreach ($config in @('diff.suppressBlankEmpty', 'diff.interHunkContext')) {
  $value = if ($config -eq 'diff.suppressBlankEmpty') { 'true' } else { '100' }
  Fixture-Git $fixture @('config',$config,$value) | Out-Null
  try {
    Run-Setup $fixture $true -VerifyOnly
    Run-Setup $fixture $true
    Assert-Unchanged $unchanged
  } finally { Fixture-Git $fixture @('config','--unset',$config) | Out-Null }
}

# A real legacy manifest lists only 0001 and its shared sources. VerifyOnly
# must leave it untouched; normal setup installs 0002 and upgrades the record.
$legacy = New-Fixture 'legacy'
Run-Setup $legacy $true
Fixture-Git $legacy (@('restore','--source=HEAD','--') + $patchPaths[1]) | Out-Null
$legacyManifestPath = Join-Path $legacy '.sr-memory-tracking.json'
$legacyManifest = Get-Content -LiteralPath $legacyManifestPath -Raw | ConvertFrom-Json
$legacyManifest.PSObject.Properties.Remove('patches')
foreach ($key in $patchPaths[1]) { $legacyManifest.files.PSObject.Properties.Remove($key) }
$legacyManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $legacyManifestPath -Encoding UTF8
$legacyInputs = @($allPatchedPaths | ForEach-Object { Join-Path $legacy $_ }) + @($legacyManifestPath) +
  @(Get-ChildItem -LiteralPath (Join-Path $legacy 'src/system/write_tracking') -File -Recurse | ForEach-Object { $_.FullName })
$beforeLegacy = Snapshot-Files $legacyInputs
Run-Setup $legacy $false -VerifyOnly -FailurePattern 'SDK patch missing.*0002'
Assert-Unchanged $beforeLegacy
# Unknown shared edits must stop a pending 0002 before it touches either file.
$legacyHeader = Join-Path $legacy 'src/system/write_tracking/include/rex_memory_tracking_abi.h'
$savedLegacyHeader = [IO.File]::ReadAllBytes($legacyHeader)
[IO.File]::AppendAllText($legacyHeader, "`n// unknown legacy edit`n")
$beforeLegacyEdit = Snapshot-Files $legacyInputs
Run-Setup $legacy $false -FailurePattern 'Unrecognized source edit'
Assert-Unchanged $beforeLegacyEdit
[IO.File]::WriteAllBytes($legacyHeader, $savedLegacyHeader)
$legacyStablePaths = @($patchPaths[0] | ForEach-Object { Join-Path $legacy $_ }) +
  @(Get-ChildItem -LiteralPath (Join-Path $legacy 'src/system/write_tracking') -File -Recurse | ForEach-Object { $_.FullName })
$legacyStable = Snapshot-Files $legacyStablePaths
Run-Setup $legacy $true
Run-Setup $legacy $true -VerifyOnly
Assert-Manifest $legacy
Assert-Unchanged $legacyStable
$upgraded = Snapshot-Files $legacyInputs
Run-Setup $legacy $true
Run-Setup $legacy $true -VerifyOnly
Assert-Unchanged $upgraded
# Even when both patches are installed, VerifyOnly requires the new map.
$savedLegacyManifest = [IO.File]::ReadAllBytes($legacyManifestPath)
$oldSchema = Get-Content -LiteralPath $legacyManifestPath -Raw | ConvertFrom-Json
$oldSchema.PSObject.Properties.Remove('patches')
$oldSchema | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $legacyManifestPath -Encoding UTF8
$oldSchemaSnapshot = Snapshot-Files $legacyInputs
Run-Setup $legacy $false -VerifyOnly -FailurePattern 'SDK patch manifest missing or outdated'
Assert-Unchanged $oldSchemaSnapshot
[IO.File]::WriteAllBytes($legacyManifestPath, $savedLegacyManifest)

# Reject every bad state before installing a still-pristine 0001.
foreach ($scenario in @('unrelated', 'partial-xma', 'modified-xma', 'case-xma', 'unknown-shared', 'unknown-untracked')) {
  $rejected = New-Fixture $scenario
  $extraPaths = @()
  switch ($scenario) {
    'unrelated' {
      $extraPaths += Join-Path $rejected 'README.md'
      [IO.File]::AppendAllText($extraPaths[0], "`n<!-- unrelated fixture edit -->`n")
      $pattern = 'unknown local changes'
    }
    'partial-xma' {
      Fixture-Git $rejected @('apply', ('--include=' + $patchPaths[1][0]), $patches[1]) | Out-Null
      $pattern = 'partial patch'
    }
    'modified-xma' {
      Fixture-Git $rejected @('apply', $patches[1]) | Out-Null
      [IO.File]::AppendAllText((Join-Path $rejected $patchPaths[1][0]), "`n// unknown xma edit`n")
      $pattern = 'partial patch'
    }
    'case-xma' {
      Fixture-Git $rejected @('apply', $patches[1]) | Out-Null
      $xmaPath = Join-Path $rejected $patchPaths[1][0]
      $contents = [IO.File]::ReadAllText($xmaPath)
      $caseMatch = [regex]::Match($contents, '[a-zA-Z]')
      $letter = $caseMatch.Value
      $replacement = if ($letter -ceq $letter.ToUpperInvariant()) { $letter.ToLowerInvariant() } else { $letter.ToUpperInvariant() }
      $contents = $contents.Substring(0, $caseMatch.Index) + $replacement + $contents.Substring($caseMatch.Index + 1)
      [IO.File]::WriteAllText($xmaPath, $contents)
      $pattern = 'partial patch'
    }
    'unknown-shared' {
      $extraPaths += Join-Path $rejected 'src/system/write_tracking/include/rex_memory_tracking_abi.h'
      New-Item -ItemType Directory -Force (Split-Path $extraPaths[0] -Parent) | Out-Null
      [IO.File]::WriteAllText($extraPaths[0], '// unknown shared edit')
      $pattern = 'Unrecognized source edit'
    }
    'unknown-untracked' {
      $extraPaths += Join-Path $rejected 'src/system/tracking_unknown.cpp'
      [IO.File]::WriteAllText($extraPaths[0], '// unknown build input')
      $pattern = 'Unrecognized SDK build input'
    }
  }
  $rejectedPaths = @($allPatchedPaths | ForEach-Object { Join-Path $rejected $_ }) + $extraPaths +
    @((Join-Path $rejected '.sr-memory-tracking.json'), (Join-Path $rejected 'src/system/write_tracking/src/runtime_memory_tracker.cpp'))
  $beforeRejected = Snapshot-Files $rejectedPaths
  Run-Setup $rejected $false -FailurePattern $pattern
  Assert-Unchanged $beforeRejected
  Run-Setup $rejected $false -VerifyOnly
  Assert-Unchanged $beforeRejected
}
foreach ($mode in @('assume-unchanged','skip-worktree')) { Assert-HiddenIndexGuard $mode }
# The inverse mixed state is also safe: retain canonical 0002 and install 0001.
$xmaOnly = New-Fixture 'xma-only'
Fixture-Git $xmaOnly @('apply', $patches[1]) | Out-Null
$xmaSnapshot = Snapshot-Files @($patchPaths[1] | ForEach-Object { Join-Path $xmaOnly $_ })
Run-Setup $xmaOnly $true
Run-Setup $xmaOnly $true -VerifyOnly
Assert-Manifest $xmaOnly
Assert-Unchanged $xmaSnapshot

# Model an older installed canonical revision, authorized by its manifest.
# Only the fixture changes; the workspace's canonical sources stay untouched.
$header = Join-Path $fixture 'src/system/write_tracking/include/rex_memory_tracking_abi.h'
[IO.File]::AppendAllText($header, "`n// previous canonical revision`n")
$previousHeaderHash = (Get-FileHash -LiteralPath $header).Hash
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$headerKey = @($manifest.files.PSObject.Properties.Name | Where-Object { $_ -match 'rex_memory_tracking_abi\.h$' })[0]
$manifest.files.PSObject.Properties[$headerKey].Value = $previousHeaderHash
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Run-Setup $fixture $false -VerifyOnly -FailurePattern 'SDK source manifest mismatch'

# An unknown edit later in preflight must prevent even an authorized update.
$peer = Join-Path $fixture 'src/system/write_tracking/src/write_tracking_state.h'
$savedPeer = [IO.File]::ReadAllBytes($peer)
[IO.File]::AppendAllText($peer, "`n// unrecognized edit during canonical update`n")
$beforeRejectedManifest = (Get-FileHash -LiteralPath $manifestPath).Hash
$beforeRejectedTime = (Get-Item -LiteralPath $manifestPath).LastWriteTimeUtc.Ticks
Run-Setup $fixture $false -FailurePattern 'Unrecognized source edit'
if ((Get-FileHash -LiteralPath $header).Hash -ne $previousHeaderHash) { throw 'preflight partially updated an authorized source' }
if ((Get-FileHash -LiteralPath $manifestPath).Hash -ne $beforeRejectedManifest -or
    (Get-Item -LiteralPath $manifestPath).LastWriteTimeUtc.Ticks -ne $beforeRejectedTime) { throw 'failed preflight rewrote manifest' }
[IO.File]::WriteAllBytes($peer, $savedPeer)
[IO.File]::SetLastWriteTimeUtc($peer, [datetime]::new($unchanged[$peer].ticks, [DateTimeKind]::Utc))
Run-Setup $fixture $true
Run-Setup $fixture $true -VerifyOnly
if ((Get-FileHash -LiteralPath $header).Hash -ne $unchanged[$header].hash) { throw 'authorized canonical update was not installed' }
$updatedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($updatedManifest.files.PSObject.Properties[$headerKey].Value -ne $unchanged[$header].hash) { throw 'manifest did not record canonical update' }
if ((Get-Item -LiteralPath $peer).LastWriteTimeUtc.Ticks -ne $unchanged[$peer].ticks) { throw 'canonical update rewrote an unchanged peer' }
$unknown = Join-Path $fixture 'src/system/tracking_unexpected.cpp'
[IO.File]::WriteAllText($unknown, '// unrecognized build input')
Run-Setup $fixture $false
if (-not (Test-Path -LiteralPath $unknown)) { throw 'setup removed unknown source' }
Remove-Item -LiteralPath $unknown
$dependency = Join-Path $fixture 'thirdparty/cli11'
New-Item -ItemType Directory -Force $dependency | Out-Null
& git -C $dependency init --quiet
& git -C $dependency -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet --allow-empty -m fixture
Run-Setup $fixture $false -VerifyOnly -CheckDependencies -FailurePattern 'dependency revision'
$saved = [IO.File]::ReadAllBytes($header)
[IO.File]::AppendAllText($header, "`n// unexpected local edit`n")
Run-Setup $fixture $false -VerifyOnly
Run-Setup $fixture $false
if (-not ([IO.File]::ReadAllText($header).Contains('unexpected local edit'))) { throw 'setup overwrote user edit' }
[IO.File]::WriteAllBytes($header, $saved)
$target = Join-Path $fixture 'src/system/xmemory.cpp'
[IO.File]::AppendAllText($target, "`n// unrelated change`n")
Run-Setup $fixture $false
if (-not ([IO.File]::ReadAllText($target).Contains('unrelated change'))) { throw 'setup overwrote patch edit' }
Run-Setup $root $false
$wrong = Join-Path $fixtures 'wrong'
New-Item -ItemType Directory $wrong | Out-Null
& git -C $wrong init --quiet
& git -C $wrong -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet --allow-empty -m fixture
Run-Setup $wrong $false
$partial = Join-Path $fixtures 'partial'
& git clone --quiet --shared --no-hardlinks $source $partial
if ($LASTEXITCODE) { throw 'partial fixture clone failed' }
[IO.File]::AppendAllText((Join-Path $partial 'src/system/CMakeLists.txt'), "`n# partial edit`n")
Run-Setup $partial $false
Write-Output 'PASS two-patch setup, legacy migration, VerifyOnly, timestamp idempotence, Git diff preferences, hidden-index edit rejection, authorized canonical updates, preflight guards, revision/path checks, partial patch and local edit preservation'
