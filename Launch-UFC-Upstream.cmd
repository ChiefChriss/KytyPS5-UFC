@echo off
setlocal
set "BUILD=%~dp0_Build\UFC-Upstream-099de8d"
if not exist "%BUILD%\launcher.exe" (
    echo Packaged launcher not found: "%BUILD%\launcher.exe"
    pause
    exit /b 1
)
start "" /D "%BUILD%" "%BUILD%\launcher.exe"
