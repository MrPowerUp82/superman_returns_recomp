<#
Fetch the GPU source matching the prebuilt ReXGlue SDK v0.10.0.
The checkout lives under .tools/ and is not committed.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$checkout = Join-Path $root '.tools\rexglue-sdk-source'
$expected = 'f5337cdc947ff6d4c4196737e2c807a48f2a1fc2'

if (-not (Test-Path -LiteralPath $checkout)) {
  New-Item -ItemType Directory -Force (Split-Path $checkout -Parent) | Out-Null
  & git clone --depth 1 --branch v0.10.0 --filter=blob:none --sparse `
    'https://github.com/rexglue/rexglue-sdk.git' $checkout
  if ($LASTEXITCODE -ne 0) { throw 'Could not clone ReXGlue v0.10.0' }
}

$revision = (& git -C $checkout rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $revision -ne $expected) {
  throw "GPU source revision $revision does not match ReXGlue v0.10.0 ($expected)"
}

& git -C $checkout sparse-checkout set src/graphics thirdparty/dxbc thirdparty/renderdoc
if ($LASTEXITCODE -ne 0) { throw 'Could not check out ReXGlue GPU sources' }
Write-Output "ReXGlue GPU sources ready at $checkout ($revision)"
