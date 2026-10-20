@echo off
setlocal
set "BUILD=%~dp0_Build\UFC-Dev124-Standalone"
if not exist "%BUILD%\launcher.exe" (
    echo Original Dev 124 launcher not found: "%BUILD%\launcher.exe"
    pause
    exit /b 1
)
start "" /D "%BUILD%" "%BUILD%\launcher.exe"
