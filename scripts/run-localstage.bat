@echo off
rem Double-click to start LocalStage. Your browser opens the presenter page.
cd /d "%~dp0"
if not exist localstage.exe (
  echo localstage.exe not found next to this file.
  pause
  exit /b 1
)
rem PDFs work out of the box. PowerPoint files need LibreOffice: offer to install it once.
if exist "%ProgramFiles%\LibreOffice\program\soffice.exe" goto start
where soffice >nul 2>nul && goto start
if exist "%LOCALAPPDATA%\LocalStage\asked-libreoffice" goto start
mkdir "%LOCALAPPDATA%\LocalStage" 2>nul
echo.> "%LOCALAPPDATA%\LocalStage\asked-libreoffice"
where winget >nul 2>nul || goto start
choice /c YN /m "Install LibreOffice so you can present PowerPoint files (PDFs already work)"
if errorlevel 2 goto start
winget install --id TheDocumentFoundation.LibreOffice -e --accept-package-agreements --accept-source-agreements
:start
set OPEN_BROWSER=1
localstage.exe
if errorlevel 1 pause
