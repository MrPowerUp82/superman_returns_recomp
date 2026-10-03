param([switch]$Corpus)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
$savedPath=$env:PATH
try {
 & python tools/shaders/prepare_vulkan_emitter.py
 if ($LASTEXITCODE -ne 0) { throw 'Emitter preparation failed' }
 $vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -property installationPath
 $cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
 $env:PATH="$root/.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin;$vs/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja;$env:PATH"
 & cmd.exe /d /c "call `"$vs/Common7/Tools/VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && `"$cmake`" -S tools/shaders/xenosrecomp -B build/vulkan-m2/emitter-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DXR_ROOT=`"$root/build/vulkan-m2/emitter-tree`" && `"$cmake`" --build build/vulkan-m2/emitter-build --parallel 4"
 if ($LASTEXITCODE -ne 0) { throw 'Emitter build failed' }
 & tools/build_vulkan.ps1
 if ($Corpus) {
  & python tools/shaders/validate_vulkan_corpus.py
  if ($LASTEXITCODE -ne 0) { throw 'Corpus has failed shaders; see artifacts/shaders/vulkan-m2/report.json' }
 }
} finally { $env:PATH=$savedPath; Pop-Location }
