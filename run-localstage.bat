@echo off
setlocal enabledelayedexpansion
title LocalStage - Local Network Presentation

cd /d "%~dp0"

echo ======================================================================
echo                 LOCALSTAGE - Presentation Server
echo ======================================================================
echo.

:: 1. Check if Node.js is installed
where node >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] Node.js is not found on your system!
    echo Please install Node.js (version 18 or newer) from https://nodejs.org/
    echo.
    pause
    exit /b 1
)

:: 2. Check if dependencies are installed
if not exist "node_modules\" (
    echo [INFO] First run detected. Installing dependencies...
    call npm install
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to install dependencies.
        pause
        exit /b 1
    )
)

:: 3. Check if build artifacts exist
if not exist "apps\server\dist\index.js" (
    echo [INFO] Building LocalStage...
    call npm run build
    if %errorlevel% neq 0 (
        echo [ERROR] Build failed.
        pause
        exit /b 1
    )
)
if not exist "apps\web\dist\index.html" (
    echo [INFO] Building frontend...
    call npm run build
    if %errorlevel% neq 0 (
        echo [ERROR] Build failed.
        pause
        exit /b 1
    )
)

echo [INFO] Starting LocalStage server...
echo [INFO] Your default web browser will open the presenter page automatically.
echo [INFO] Viewers on the same Wi-Fi can join using the LAN URL printed below.
echo.
echo Press Ctrl+C in this window to stop the server at any time.
echo ======================================================================
echo.

node scripts/launch.mjs

if %errorlevel% neq 0 (
    echo.
    echo [INFO] LocalStage has stopped.
    pause
)
