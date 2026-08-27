@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
set "ROOT=%CD%"

title SecureTunnel Client (Smart VPN)

net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo ================================================
    echo   Smart VPN needs Administrator
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
echo [Admin OK] SecureTunnel - Smart VPN mode
echo Project: %ROOT%
echo.
echo [INFO] Domestic sites use your normal network.
echo [INFO] Listed foreign/blocked sites use the tunnel.
echo [INFO] Edit lists: config\smart_route_domains.txt
echo.
echo [INFO] Server must run in another window:
echo        scripts\run_server.bat
echo.
echo [INFO] Ctrl+C to stop and restore routes.
echo.

"%ROOT%\build-agent\tunnel_client.exe" --quic --insecure --wintun --smart-route
set "RC=%ERRORLEVEL%"

echo.
echo Client stopped. Exit code: %RC%
echo.
pause
exit /b %RC%
