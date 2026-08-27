@echo off
cd /d "%~dp0.."
set "ROOT=%CD%"

title SecureTunnel Server

echo.
echo ==========================================
echo   SecureTunnel Server
echo ==========================================
echo.
echo Keep this window OPEN.
echo Then run: scripts\run_client_wintun.bat
echo          or double-click: scripts\run_client_wintun.vbs
echo.

if not exist "%ROOT%\build-agent\tunnel_server.exe" (
    echo [ERROR] Missing tunnel_server.exe - build first.
    pause
    exit /b 1
)

"%ROOT%\build-agent\tunnel_server.exe" --quic --relay

echo.
echo Server stopped.
pause
