@echo off
setlocal
rem Base experimental de co-op local: uma instancia e um mundo compartilhado.
rem Por enquanto apenas prepara os controles em portas separadas.
rem Para entrada automatica do P2 e tela dividida, use a opcao Cooperativo no launcher.
set "ROOT=%~dp0"
set "BIN=%ROOT%port\out\build\win-amd64-release"
set "EXE=%BIN%\superman_returns.exe"
if not exist "%EXE%" (
  echo Executavel nao encontrado. Rode build.cmd primeiro.
  exit /b 1
)
if not exist "%ROOT%logs" mkdir "%ROOT%logs"
echo Co-op experimental: abrindo uma unica instancia.
echo Este atalho prepara apenas os controles. Para P2 automatico e tela dividida, use o launcher.
start "" /D "%BIN%" "%EXE%" --game_data_root="%ROOT%game" --log_file="%ROOT%logs\coop.log" --sr_coop_input=true %*
