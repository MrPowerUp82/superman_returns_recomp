@echo off
setlocal
set "ROOT=%~dp0"
if not exist "%ROOT%superman_returns.exe" (
  echo superman_returns.exe nao encontrado nesta pasta.
  exit /b 1
)
if not exist "%ROOT%game\default.xex" (
  echo Os arquivos do jogo nao estao em game\. Use o instalador para copia-los: ver LEIAME.txt.
  pause
  exit /b 1
)
if not exist "%ROOT%logs" mkdir "%ROOT%logs"
rem Qualquer --opcao passada aqui prevalece sobre os padroes do jogo.
start "" /D "%ROOT%" "%ROOT%superman_returns.exe" --game_data_root="%ROOT%game" --log_file="%ROOT%logs\game.log" %*
