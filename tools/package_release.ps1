<#
Builds the distribution package: the port's executable built WITHOUT the
shader corpus (SR_EMBED_SHADERS=OFF), the shader translation tools, the Visual
C++ runtime, launch scripts and notices, as one zip. Nothing derived from the
game's shaders or data goes in: the shaders are translated on the user's
machine (port/src/native_renderer/shader_translator.h).

  powershell -File tools\package_release.ps1            # builds, then packages
  powershell -File tools\package_release.ps1 -NoBuild   # package an existing build

Output: artifacts/release/superman_returns_win64.zip and version.json
#>
param(
  [switch]$NoBuild,
  [string]$BuildDir = '',
  [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $root 'port\out\build\win-amd64-dist' }
if (-not $OutDir) { $OutDir = Join-Path $root 'artifacts\release' }

if (-not $NoBuild) {
  $env:SR_BUILD_DIR = $BuildDir
  $env:SR_EMBED_SHADERS = 'OFF'
  try {
    & cmd.exe /c (Join-Path $root 'build.cmd')
    if ($LASTEXITCODE -ne 0) { throw "build.cmd failed ($LASTEXITCODE)" }
  } finally {
    $env:SR_BUILD_DIR = $null
    $env:SR_EMBED_SHADERS = $null
  }
  & (Join-Path $root 'tools\build_launcher.ps1')
}

function Need($path, $hint) {
  if (-not (Test-Path $path)) { throw "$path is missing. $hint" }
  return (Resolve-Path $path).Path
}

$exe = Need (Join-Path $BuildDir 'superman_returns.exe') 'Run without -NoBuild.'
$launcherDir = if (Test-Path (Join-Path $BuildDir 'SupermanReturnsLauncher.exe')) { $BuildDir } else { Join-Path $root 'artifacts\launcher' }
$launcher = Need (Join-Path $launcherDir 'SupermanReturnsLauncher.exe') 'Run tools\build_launcher.ps1 first.'
foreach ($leftover in 'superman_returns_shaders.srsl', 'superman_returns_shaders.pak') {
  if (Test-Path (Join-Path $BuildDir $leftover)) {
    throw "$leftover is in ${BuildDir}: that build embeds game-derived shaders. Use a SR_EMBED_SHADERS=OFF build folder."
  }
}
$translator = Need (Join-Path $root '.tools\xenosrecomp\build\XenosRecompCorpus.exe') 'Run tools\shaders\build_corpus.ps1 once to build the translator.'
$dxcDir = Need (Join-Path $root '.tools\dxc\bin\x64') 'Run tools\shaders\build_corpus.ps1 once to fetch DXC.'
$common = Need (Join-Path $root '.tools\xenosrecomp\src\XenosRecomp\shader_common.h') 'Run tools\shaders\fetch_xenosrecomp.py.'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory | Select-Object -Last 1
if (-not $crt) { throw 'The Visual C++ redistributable folder was not found.' }

$OutDir = [IO.Path]::GetFullPath($OutDir)
$stage = [IO.Path]::GetFullPath((Join-Path $OutDir 'stage'))
if (-not $stage.StartsWith($OutDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid staging directory.' }
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage, (Join-Path $stage 'shader_tools'), (Join-Path $stage 'licenses') | Out-Null

Copy-Item $exe $stage
Copy-Item $launcher $stage
foreach ($notice in 'DOTNET-LICENSE.txt', 'DOTNET-ThirdPartyNotices.txt') {
  Copy-Item (Need (Join-Path $launcherDir $notice) 'Rebuild the launcher.') (Join-Path $stage 'licenses')
}
foreach ($dll in 'rexruntime.dll', 'rexgpu-xenos.dll') { Copy-Item (Need (Join-Path $BuildDir $dll) '') $stage }
foreach ($name in 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
  Copy-Item (Need (Join-Path $crt.FullName $name) '') $stage
}
Copy-Item $translator (Join-Path $stage 'shader_tools\sr_xenosrecomp.exe')
foreach ($f in 'dxc.exe', 'dxcompiler.dll', 'dxil.dll') { Copy-Item (Join-Path $dxcDir $f) (Join-Path $stage 'shader_tools') }
Copy-Item $common (Join-Path $stage 'shader_tools')

$licenses = Join-Path $stage 'licenses'
Copy-Item (Join-Path $root '.tools\xenosrecomp\src\LICENSE.md') (Join-Path $licenses 'LICENSE-XenosRecomp.md')
foreach ($pair in @(@('LICENCE-MIT.txt', 'MIT'), @('LICENSE-LLVM.txt', 'LLVM'), @('LICENSE-MS.txt', 'MS'))) {
  $src = Join-Path $root (".tools\dxc\" + $pair[0])
  if (Test-Path $src) { Copy-Item $src (Join-Path $licenses ("LICENSE-DXC-" + $pair[1] + ".txt")) }
}
$sdkLicenses = Join-Path $root '.tools\rexglue-sdk\win-amd64\licenses'
if (Test-Path $sdkLicenses) { Copy-Item (Join-Path $sdkLicenses '*') $licenses -Recurse }
foreach ($candidate in '.tools\rexglue-sdk-source\LICENSE', '.tools\rexglue-sdk\win-amd64\LICENSE') {
  $p = Join-Path $root $candidate
  if (Test-Path $p) { Copy-Item $p (Join-Path $licenses 'ReXGlue-LICENSE.txt'); break }
}

foreach ($f in 'run.cmd', 'run_keyboard.cmd', 'LEIAME.txt', 'THIRD_PARTY_NOTICES.txt') {
  Copy-Item (Join-Path $root "tools\release\$f") $stage
}
# Batch files and notes ship with Windows line endings whatever the checkout used.
foreach ($f in 'run.cmd', 'run_keyboard.cmd', 'LEIAME.txt', 'THIRD_PARTY_NOTICES.txt') {
  $path = Join-Path $stage $f
  $text = [IO.File]::ReadAllText($path) -replace "`r?`n", "`r`n"
  [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false)))
}

$commit = (git -C $root rev-parse --short HEAD).Trim()
$dirty = if ((git -C $root status --porcelain -- port tools launcher build.cmd) ) { '-dirty' } else { '' }
$version = "$(Get-Date -Format 'yyyy.MM.dd')-$commit$dirty"
$info = [ordered]@{ version = $version; commit = $commit; built = (Get-Date).ToUniversalTime().ToString('o'); xex_sha256 = 'c8f243acd99de9a91f5ae4f409721c0e954e3d5eb96861419d3da07b8106db2b' }
$info | ConvertTo-Json | Set-Content (Join-Path $stage 'version.json') -Encoding utf8
$info | ConvertTo-Json | Set-Content (Join-Path $OutDir 'version.json') -Encoding utf8

$zip = Join-Path $OutDir 'superman_returns_win64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
$python = (Get-Command python).Source
& $python (Join-Path $root 'tools\release\make_zip.py') $stage $zip
if ($LASTEXITCODE -ne 0) { throw 'zip failed' }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"{0}  {1}" -f $hash, (Split-Path $zip -Leaf) | Set-Content (Join-Path $OutDir 'superman_returns_win64.zip.sha256')
"{0} MB, version {1}" -f [math]::Round((Get-Item $zip).Length / 1MB, 1), $version
$zip
