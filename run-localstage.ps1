# LocalStage Quick Launcher for Windows PowerShell
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "                LOCALSTAGE - Presentation Server" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] Node.js is not installed or not in PATH!" -ForegroundColor Red
    Write-Host "Please download and install Node.js (version 18+) from https://nodejs.org/"
    Read-Host "Press Enter to exit"
    exit 1
}

if (-not (Test-Path "node_modules")) {
    Write-Host "[INFO] Installing dependencies..." -ForegroundColor Yellow
    npm install
}

if (-not (Test-Path "apps/server/dist/index.js") -or -not (Test-Path "apps/web/dist/index.html")) {
    Write-Host "[INFO] Building LocalStage..." -ForegroundColor Yellow
    npm run build
}

Write-Host "[INFO] Starting LocalStage server..." -ForegroundColor Green
Write-Host "[INFO] Your default browser will open the presenter page automatically." -ForegroundColor Green
Write-Host "Press Ctrl+C to stop." -ForegroundColor Gray
Write-Host ""

node scripts/launch.mjs
