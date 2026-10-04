param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) {
  if ($env:SR_BUILD_DIR) {
    $OutDir = $env:SR_BUILD_DIR
  } else {
    $releaseDir = Join-Path $root 'port\out\build\win-amd64-release'
    $OutDir = if (Test-Path $releaseDir) { $releaseDir } else { Join-Path $root 'artifacts\launcher' }
  }
}
$localSdk = Join-Path $root '.tools\dotnet-launcher\dotnet.exe'
$dotnet = if (Test-Path $localSdk) { $localSdk } else { (Get-Command dotnet -ErrorAction Stop).Source }
& $dotnet publish (Join-Path $root 'launcher\SupermanReturnsLauncher.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true --source https://api.nuget.org/v3/index.json -o $OutDir
if ($LASTEXITCODE -ne 0) { throw 'Launcher publish failed. Install the .NET 8 SDK to build it.' }
# Notices from the SDK used to produce the bundled runtime.
$sdkRoot = Split-Path $dotnet -Parent
foreach ($name in 'LICENSE.txt', 'ThirdPartyNotices.txt') {
  $source = Join-Path $sdkRoot $name
  if (-not (Test-Path $source)) { throw "Missing .NET runtime notice: $source" }
  Copy-Item -LiteralPath $source -Destination (Join-Path $OutDir "DOTNET-$name")
}

$artifactLauncher = Join-Path $root 'artifacts\launcher'
$outResolved = (Resolve-Path $OutDir).Path
$artifactResolved = if (Test-Path $artifactLauncher) { (Resolve-Path $artifactLauncher).Path } else { '' }
if ($outResolved -ne $artifactResolved) {
  New-Item -ItemType Directory -Force $artifactLauncher | Out-Null
  Copy-Item (Join-Path $OutDir 'SupermanReturnsLauncher.exe') $artifactLauncher -Force
  if (Test-Path (Join-Path $OutDir 'SupermanReturnsLauncher.pdb')) {
    Copy-Item (Join-Path $OutDir 'SupermanReturnsLauncher.pdb') $artifactLauncher -Force
  }
  foreach ($name in 'LICENSE.txt', 'ThirdPartyNotices.txt') {
    Copy-Item (Join-Path $OutDir "DOTNET-$name") $artifactLauncher -Force
  }
}

