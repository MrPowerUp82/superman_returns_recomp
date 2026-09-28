@echo off
setlocal
set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Build Tools not found.
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo Visual Studio installation not found.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
set "PATH=%ROOT%.tools\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "SDK=%ROOT%.tools\rexglue-sdk\win-amd64"
if not exist "%SDK%\bin\rexglue.exe" (
  echo ReXGlue SDK v0.10.0 not found in .tools.
  exit /b 1
)
if not exist "%ROOT%game\default.xex" (
  echo Extracted game files not found in game.
  exit /b 1
)
cmake -S "%ROOT%port" -B "%ROOT%port\out\build\win-amd64-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="%SDK%"
if errorlevel 1 exit /b 1
cmake --build "%ROOT%port\out\build\win-amd64-release" --parallel 4
exit /b %errorlevel%
