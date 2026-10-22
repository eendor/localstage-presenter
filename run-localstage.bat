@echo off
rem Double-click to start LocalStage. Your browser opens the presenter page.
cd /d "%~dp0"
set OPEN_BROWSER=1
if not exist localstage.exe (
  echo localstage.exe not found next to this file.
  pause
  exit /b 1
)
localstage.exe
if errorlevel 1 pause
