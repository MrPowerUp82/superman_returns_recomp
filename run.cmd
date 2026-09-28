@echo off
setlocal
set "ROOT=%~dp0"
set "EXE=%ROOT%port\out\build\win-amd64-release\superman_returns.exe"
if not exist "%EXE%" (
  echo Executavel nao encontrado. Rode build.cmd primeiro.
  exit /b 1
)
if not exist "%ROOT%logs" mkdir "%ROOT%logs"
rem render_target_path_d3d12=rtv: o modo padrao (rov) deixa as cenas 3D a ~2 FPS
rem em GPUs integradas Intel; rtv chega a ~10 FPS. Com GPU dedicada, teste
rem --render_target_path_d3d12=rov, que e mais preciso.
rem depth_float24_convert_in_pixel_shader: no modo rtv, sem isso o ceu e o
rem fundo distante somem (ficam pretos) por imprecisao do depth float24.
start "" /D "%ROOT%port\out\build\win-amd64-release" "%EXE%" --game_data_root="%ROOT%game" --log_file="%ROOT%logs\game.log" --render_target_path_d3d12=rtv --depth_float24_convert_in_pixel_shader=true %*
