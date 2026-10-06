<#
Builds the distribution package: the port's executable built WITHOUT the
shader corpus (SR_EMBED_SHADERS=OFF) and with the native Vulkan renderer, the
shader translation tools (plus the Vulkan runtime scripts and a minimal Python
built from the local installation), the Visual C++ runtime, launch scripts and
notices, as one zip. Nothing derived from the
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
  $env:SR_VULKAN = 'ON'
  try {
    & cmd.exe /c (Join-Path $root 'build.cmd')
    if ($LASTEXITCODE -ne 0) { throw "build.cmd failed ($LASTEXITCODE)" }
  } finally {
    $env:SR_BUILD_DIR = $null
    $env:SR_EMBED_SHADERS = $null
    $env:SR_VULKAN = $null
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
foreach ($leftover in 'superman_returns_shaders.srsl', 'superman_returns_shaders.pak', 'superman_returns_vulkan.srvk') {
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

# Native Vulkan translates shaders at runtime through these scripts (standard
# library only), run by a minimal Python in the embeddable layout: exe + DLLs,
# the standard library zipped, and a ._pth that also exposes ..\vulkan.
$vulkanTools = Join-Path $stage 'shader_tools\vulkan'
New-Item -ItemType Directory -Force $vulkanTools | Out-Null
# Keep the Vulkan ABI emitter separate from the D3D12 translator/header.
# The browser pre-shaders use this exact patched tree too.
& (Join-Path $root 'tools/build_vulkan_m2.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Vulkan shader emitter build failed' }
Copy-Item (Need (Join-Path $root 'build/vulkan-m2/emitter-build/XenosRecompCorpus.exe') '') (Join-Path $vulkanTools 'sr_xenosrecomp.exe')
Copy-Item (Need (Join-Path $root 'build/vulkan-m2/emitter-tree/src/XenosRecomp/shader_common.h') '') $vulkanTools
foreach ($script in 'runtime_vulkan_shader.py', 'compile_vulkan.py', 'spirv_metadata.py', 'validate_vulkan_corpus.py', 'vulkan_contract.py') {
  Copy-Item (Need (Join-Path $root "tools\shaders\$script") '') $vulkanTools
}
$python = (Get-Command python -ErrorAction Stop).Source
$pyHome = Split-Path $python
$pyVersion = (& $python -c "import sys;print(f'{sys.version_info[0]}{sys.version_info[1]}')").Trim()
$pyOut = Join-Path $stage 'shader_tools\python'
New-Item -ItemType Directory -Force $pyOut | Out-Null
foreach ($f in 'python.exe', 'python3.dll', "python$pyVersion.dll", 'vcruntime140.dll', 'vcruntime140_1.dll') {
  $src = Join-Path $pyHome $f
  if (Test-Path $src) { Copy-Item $src $pyOut }
}
Get-ChildItem (Join-Path $pyHome 'DLLs') -File | Where-Object {
  ($_.Extension -in '.pyd', '.dll') -and $_.Name -notmatch '^(_test|_tkinter|tcl|tk|_ctypes_test|_sqlite3|sqlite3)'
} | Copy-Item -Destination $pyOut
& $python (Join-Path $root 'tools\release\make_python_zip.py') (Join-Path $pyHome 'Lib') (Join-Path $pyOut "python$pyVersion.zip")
if ($LASTEXITCODE -ne 0) { throw 'Python standard library zip failed' }
Set-Content (Join-Path $pyOut "python$pyVersion._pth") "python$pyVersion.zip`r`n.`r`n..\vulkan`r`n" -Encoding ascii
& (Join-Path $pyOut 'python.exe') -c "import runtime_vulkan_shader, hashlib, subprocess, json, concurrent.futures; hashlib.sha256(b'x')"
if ($LASTEXITCODE -ne 0) { throw 'The bundled Python cannot import the Vulkan shader scripts.' }

$licenses = Join-Path $stage 'licenses'
Copy-Item (Join-Path $pyHome 'LICENSE.txt') (Join-Path $licenses 'LICENSE-Python.txt')
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
