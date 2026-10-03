param([switch]$ConfigureOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -property installationPath
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$clang=Join-Path $root '.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin'
$env:PATH="$clang;$ninja;$(Split-Path $cmake);$env:PATH"
$configure="`"$cmake`" -S `"$root/tests/vulkan`" -B `"$root/build/tests-vulkan`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DSR_VULKAN_DXC=`"$root/.tools/dxc/bin/x64/dxc.exe`""
& cmd.exe /d /c "call `"$vs/Common7/Tools/VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && $configure"
if ($LASTEXITCODE -ne 0) { throw 'Vulkan configure failed' }
if (-not $ConfigureOnly) {
  & cmd.exe /d /c "call `"$vs/Common7/Tools/VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && `"$cmake`" --build `"$root/build/tests-vulkan`" --parallel 4"
  if ($LASTEXITCODE -ne 0) { throw 'Vulkan build failed' }
}
