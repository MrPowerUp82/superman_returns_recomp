@echo off
setlocal
rem Base experimental de co-op local: uma instancia e um mundo compartilhado.
rem Por enquanto apenas prepara os controles em portas separadas.
rem Segundo Superman, cameras independentes e tela dividida ainda pendentes.
set "ROOT=%~dp0"
set "BIN=%ROOT%port\out\build\win-amd64-release"
set "EXE=%BIN%\superman_returns.exe"
if not exist "%EXE%" (
  echo Executavel nao encontrado. Rode build.cmd primeiro.
  exit /b 1
)
if not exist "%ROOT%logs" mkdir "%ROOT%logs"
echo Co-op experimental: abrindo uma unica instancia.
echo Segundo jogador e tela dividida ainda nao estao implementados.
start "" /D "%BIN%" "%EXE%" --game_data_root="%ROOT%game" --log_file="%ROOT%logs\coop.log" --sr_coop_input=true %*
