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
$fixture = Join-Path $fixtures 'sdk'
& git clone --quiet --shared --no-hardlinks $source $fixture
if ($LASTEXITCODE) { throw 'fixture clone failed' }
Run-Setup $fixture $true
Run-Setup $fixture $true -VerifyOnly
$manifestPath = Join-Path $fixture '.sr-memory-tracking.json'
$installedFiles = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'src/system/write_tracking') -File -Recurse) + @(Get-Item -LiteralPath $manifestPath)
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
Write-Output 'PASS setup timestamp idempotence, authorized canonical updates, preflight guards, revision/path checks, partial patch and local edit preservation'
