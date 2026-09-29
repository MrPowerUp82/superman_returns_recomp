@echo off
setlocal
set "ROOT=%~dp0"
set "EXE=%ROOT%port\out\build\win-amd64-release\superman_returns.exe"
if not exist "%EXE%" (
  echo Executavel nao encontrado. Rode build.cmd primeiro.
  exit /b 1
)
if not exist "%ROOT%logs" mkdir "%ROOT%logs"
rem Os padroes de GPU (rtv, correcao do ceu, stencil Intel) ficam no app:
rem port/src/superman_returns_app.h. Qualquer --opcao passada aqui prevalece.
start "" /D "%ROOT%port\out\build\win-amd64-release" "%EXE%" --game_data_root="%ROOT%game" --log_file="%ROOT%logs\game.log" %*
