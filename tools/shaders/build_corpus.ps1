<#
Offline shader corpus for the native renderer (docs/native-port-plan.md
section 6). Windows port of crazyriddler/rexglue-native-kit @136bc6c4
tools/shaders/build_corpus.sh. Runs locally against the owner's game; every
output goes to .tools/ or artifacts/ (both ignored by Git). Never commit them.

  1. DXC release into .tools/dxc (if missing)
  2. patched reblue-XenosRecomp + fmt/xxHash into .tools/xenosrecomp
     (fetch_xenosrecomp.py) and the XenosRecompCorpus translator build
  3. extract_shaders.py: containers from game/, the decoded image
     (SR_DUMP_IMAGE) and the run-time dumps (sr_native_dump_shader_dir)
  4. join_runtime_cache.py: coverage against the Xenos shader storage (.xsh),
     when one exists under port/out
  5. build_catalog.py: HLSL, DXIL (+ SPIR-V), reflection ->
     artifacts/shaders/{catalog.json,dxil/,SHADER_CATALOG.md}
  6. make_preshaders.py: the pre-shader library (original containers + DXIL,
     as in nfsmw-nx) -> artifacts/shaders/superman_returns_shaders.srsl, also
     copied next to every superman_returns.exe under port/out/build
  7. Vulkan ABI emitter + make_vulkan_preshaders.py ->
     artifacts/shaders/superman_returns_vulkan.srvk, installed alongside the
     same executables. Skipped with -NoSpirv.

A SR_NATIVE=RENDERER build then embeds artifacts/shaders/dxil (port/CMakeLists.txt),
or run the game with --sr_native_shader_dir=<repo>\artifacts\shaders\dxil.

Usage: powershell -File tools\shaders\build_corpus.ps1 [-DumpDir <dir>[,<dir>...]] [-NoSpirv]
#>
param(
  [string[]]$DumpDir = @(),
  [switch]$NoSpirv
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$tools = Join-Path $root '.tools'
$dxcDir = Join-Path $tools 'dxc'
$dxcExe = Join-Path $dxcDir 'bin\x64\dxc.exe'
$dxcVersion = 'v1.9.2607'
$dxcZip = 'dxc_2026_07_29.zip'  # asset of that release (same as the kit)
$xrBuild = Join-Path $tools 'xenosrecomp\build'

function Run($exe, [string[]]$arguments) {
  & $exe @arguments
  if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE): $($arguments -join ' ')" }
}

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw 'Python 3 not found on PATH' }
& $python -c 'import xxhash' 2>$null
if ($LASTEXITCODE -ne 0) { Run $python @('-m', 'pip', 'install', 'xxhash') }

# 1. DXC
if (-not (Test-Path $dxcExe)) {
  Write-Output "[corpus] fetching DXC $dxcVersion"
  New-Item -ItemType Directory -Force $dxcDir | Out-Null
  $zip = Join-Path $dxcDir 'dxc.zip'
  Invoke-WebRequest -UseBasicParsing -OutFile $zip `
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/$dxcVersion/$dxcZip"
  Expand-Archive -Force $zip $dxcDir
  Remove-Item $zip
}

# 2. XenosRecomp (patched) + translator build, inside the VS developer
# environment like build.cmd (Windows SDK headers for Windows.h).
Run $python @((Join-Path $PSScriptRoot 'fetch_xenosrecomp.py'))
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -property installationPath
if (-not $vsRoot) { throw 'Visual Studio Build Tools not found' }
$llvm = Join-Path $root '.tools\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin'
$cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$source = Join-Path $PSScriptRoot 'xenosrecomp'
$cmd = "call `"$vsRoot\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && " +
       "set `"PATH=$llvm;$cmakeBin;$ninjaBin;%PATH%`" && " +
       "cmake -S `"$source`" -B `"$xrBuild`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ && " +
       "cmake --build `"$xrBuild`""
& cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'XenosRecompCorpus build failed' }

# 3-5. Corpus
$extract = @((Join-Path $PSScriptRoot 'extract_shaders.py'))
foreach ($d in $DumpDir) { $extract += @('--dump-dir', (Resolve-Path $d).Path) }
# Without -DumpDir every run-time dump folder under logs\ is used. The container
# extractor reads only the *.bin directly inside a folder, so subfolders such as
# logs\native_shaders\run_a count on their own. Skipping them drops the shaders the
# game creates at run time and the native renderer draws nothing for them.
if (-not $DumpDir) {
  $logs = Join-Path $root 'logs'
  if (Test-Path $logs) {
    Get-ChildItem $logs -Directory -Recurse |
      Where-Object { $_.FullName -match 'native_shaders|rt_corpus3|rt_shaders' -and
                     (Get-ChildItem $_.FullName -Filter '*.bin' -File -ErrorAction SilentlyContinue | Select-Object -First 1) } |
      ForEach-Object { $extract += @('--dump-dir', $_.FullName) }
  }
}
Run $python $extract

$xsh = Get-ChildItem (Join-Path $root 'port\out') -Recurse -Filter '454107ED.xsh' -ErrorAction SilentlyContinue
if ($xsh) {
  Run $python @((Join-Path $PSScriptRoot 'join_runtime_cache.py'))
} else {
  Write-Output '[corpus] no Xenos shader storage (454107ED.xsh) under port/out: coverage join skipped'
}

$catalog = @((Join-Path $PSScriptRoot 'build_catalog.py'))
if ($NoSpirv) { $catalog += '--no-spirv' }
Run $python $catalog

$pre = @((Join-Path $PSScriptRoot 'make_preshaders.py'))
Get-ChildItem (Join-Path $root 'port\out\build') -Recurse -Filter 'superman_returns.exe' -ErrorAction SilentlyContinue |
  ForEach-Object { $pre += @('--install', $_.DirectoryName) }
Run $python $pre
if (-not $NoSpirv) {
  & (Join-Path $root 'tools\build_vulkan_m2.ps1') -EmitterOnly
  $vulkanPre = @((Join-Path $PSScriptRoot 'make_vulkan_preshaders.py'))
  Get-ChildItem (Join-Path $root 'port\out\build') -Recurse -Filter 'superman_returns.exe' -ErrorAction SilentlyContinue |
    ForEach-Object { $vulkanPre += @('--install', $_.DirectoryName) }
  Run $python $vulkanPre
  Write-Output "[corpus] Vulkan pre-shaders: $root\artifacts\shaders\superman_returns_vulkan.srvk"
}
Write-Output "[corpus] done: $root\artifacts\shaders\catalog.json, $root\artifacts\shaders\SHADER_CATALOG.md"
