@echo off
setlocal
title BrewSphere Mockup Server

cd /d "%~dp0"

where py >nul 2>&1
if %errorlevel%==0 (
    py -3 server.py
) else (
    python server.py
)

if not %errorlevel%==0 (
    echo.
    echo Der BrewSphere Mockup Server konnte nicht gestartet werden.
    echo Bitte pruefen, ob Python installiert und im PATH verfuegbar ist.
    pause
)
