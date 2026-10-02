@echo off
rem Joga com teclado e mouse: o runtime emula um controle de Xbox 360.
rem O mouse controla o analogico direito (camera). Veja os controles no LEIAME.txt.
call "%~dp0run.cmd" --mnk_mode --mnk_mouse %*
