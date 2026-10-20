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
Run-Setup $fixture $true
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
$header = Join-Path $fixture 'src/system/write_tracking/include/rex_memory_tracking_abi.h'
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
Write-Output 'PASS setup idempotence, revision/path checks, partial patch and local edit preservation'
