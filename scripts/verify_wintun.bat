@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
set "ROOT=%CD%"

title SecureTunnel - Verify Wintun

net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo ================================================
    echo   Wintun verify needs Administrator
    echo ================================================
    echo.
    echo   Click YES on UAC. A NEW window will open.
    echo   Or double-click: scripts\verify_wintun.vbs
    echo.
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WorkingDirectory '%ROOT%'"
    exit /b 0
)

echo.
echo ==========================================
echo   SecureTunnel Wintun Verify (All-in-One)
echo ==========================================
echo.
echo Step 1: Checking server...
echo.

tasklist /FI "IMAGENAME eq tunnel_server.exe" 2>nul | find /I "tunnel_server.exe" >nul
if errorlevel 1 (
    echo [WARN] tunnel_server.exe is NOT running.
    echo.
    echo Starting server in a NEW window...
    start "SecureTunnel Server" /D "%ROOT%" cmd /k "scripts\run_server.bat"
    echo Waiting 3 seconds for server to start...
    timeout /t 3 /nobreak >nul
) else (
    echo [OK] Server is running.
)

echo.
echo Step 2: Running live Wintun test (starts client 90s, checks adapter)...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\verify_wintun_live.ps1"
set RC=%ERRORLEVEL%

echo.
if "%RC%"=="0" (
    echo ==========================================
    echo   RESULT: Wintun verification PASSED
    echo ==========================================
) else (
    echo ==========================================
    echo   RESULT: Wintun verification FAILED
    echo   See messages above.
    echo ==========================================
)

echo.
pause
exit /b %RC%
