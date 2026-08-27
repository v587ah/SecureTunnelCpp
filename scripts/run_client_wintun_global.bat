@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
set "ROOT=%CD%"

title SecureTunnel Client (Wintun + Global Route)

net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo ================================================
    echo   Wintun + global route needs Administrator
    echo ================================================
    echo.
    echo   Click YES on UAC. A NEW window will open.
    echo.
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WorkingDirectory '%ROOT%'"
    echo.
    pause
    exit /b 1
)

echo.
echo [Admin OK] SecureTunnel - full tunnel mode
echo Project: %ROOT%
echo.
echo [INFO] Server must run in another window:
echo        tunnel_server.exe --quic --relay
echo.
echo [INFO] Ctrl+C to stop and restore routes.
echo.

"%ROOT%\build-agent\tunnel_client.exe" --quic --insecure --wintun --global-route
set "RC=%ERRORLEVEL%"

echo.
echo Client stopped. Exit code: %RC%
echo.
pause
exit /b %RC%
