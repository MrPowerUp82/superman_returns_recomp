$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$headers=Join-Path $root '.tools/vulkan-headers'
$revision='b379292b2ab6df5771ba9870d53cf8b2c9295daf'
if (-not (Test-Path -LiteralPath $headers)) {
  & git clone --depth 1 --branch v1.3.290 https://github.com/KhronosGroup/Vulkan-Headers.git $headers
  if ($LASTEXITCODE -ne 0) { throw 'Failed to fetch Vulkan-Headers' }
}
$actual=& git -C $headers rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $revision) { throw 'Unexpected Vulkan-Headers revision; existing checkout preserved' }
Write-Host 'Vulkan-Headers v1.3.290 ready.'
