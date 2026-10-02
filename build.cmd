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
rem Native renderer build level (docs/native-port-plan.md): RENDERER by default
rem (sr_renderer=native); set SR_NATIVE=OFF or CAPTURE to change it.
if not defined SR_NATIVE set "SR_NATIVE=RENDERER"
rem The native renderer needs the in-process GPU sources; fetch them once.
if /i not "%SR_NATIVE%"=="OFF" if not exist "%ROOT%.tools\rexglue-sdk-source\src\graphics\d3d12\command_processor.cpp" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\setup_gpu_source.ps1"
  if errorlevel 1 echo Warning: GPU sources unavailable; the game will use the xenos backend.
)
if not defined SR_LSFG set "SR_LSFG=OFF"
rem Optional separate executable folder; an empty value restores normal output.
rem tools\package_release.ps1 builds a separate folder with SR_EMBED_SHADERS=OFF: a
rem release carries nothing translated from the game.
if not defined SR_BUILD_DIR set "SR_BUILD_DIR=%ROOT%port\out\build\win-amd64-release"
if not defined SR_EMBED_SHADERS set "SR_EMBED_SHADERS=ON"
cmake -S "%ROOT%port" -B "%SR_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="%SDK%" -DSR_NATIVE=%SR_NATIVE% -DSR_LSFG=%SR_LSFG% -DSR_NATIVE_EMBED_SHADERS=%SR_EMBED_SHADERS% -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%SR_RUNTIME_OUTPUT_DIR%"
if errorlevel 1 exit /b 1
cmake --build "%SR_BUILD_DIR%" --parallel 4
exit /b %errorlevel%
