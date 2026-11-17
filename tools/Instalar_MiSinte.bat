@echo off
rem Instala MiSinte.vst3 en la carpeta VST3 de Windows.
rem Haz clic derecho sobre este archivo y elige "Ejecutar como administrador".
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%~dp0MiSinte.vst3" (
  echo No encuentro MiSinte.vst3 junto a este archivo. Descomprime el zip completo primero.
  pause
  exit /b 1
)
xcopy /E /I /Y "%~dp0MiSinte.vst3" "%DEST%\MiSinte.vst3"
if errorlevel 1 (
  echo.
  echo Error al copiar. Asegurate de ejecutarlo como administrador.
) else (
  echo.
  echo Instalado en %DEST%\MiSinte.vst3
  echo Abre Ableton y pulsa "Rescan" en Preferencias - Plug-ins.
)
pause
