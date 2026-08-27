@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
set "ROOT=%CD%"
set "LOG=%TEMP%\sectunnel_client.log"

title SecureTunnel Client Wintun [Administrator]

echo ===== SecureTunnel Wintun Client ===== > "%LOG%"
echo Time: %date% %time% >> "%LOG%"
echo ROOT: %ROOT% >> "%LOG%"
echo.>> "%LOG%"

echo.
echo ==========================================
echo   SecureTunnel Wintun Client (Admin)
echo ==========================================
echo   Log file: %LOG%
echo.

net session >> "%LOG%" 2>&1
if errorlevel 1 goto :not_admin

echo [OK] Administrator confirmed >> "%LOG%"
echo [OK] Running as Administrator
echo.

if not exist "%ROOT%\build-agent\tunnel_client.exe" goto :missing_exe

echo [STEP 1] Start server in ANOTHER window first:
echo          scripts\run_server.bat
echo          OR: build-agent\tunnel_server.exe --quic --relay
echo.
echo [STEP 2] Starting client now (relay 30 seconds)...
echo.

"%ROOT%\build-agent\tunnel_client.exe" --quic --insecure --wintun --relay-seconds:30 >> "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"

echo Exit code: %RC% >> "%LOG%"

findstr /C:"[OK] Client state: established" "%LOG%" >nul
if not errorlevel 1 goto :ok_connected

if "%RC%"=="0" goto :ok_exit

goto :fail

:ok_connected
echo.
echo [OK] Client connected successfully - Wintun + QUIC.
echo      You can run verify_wintun.ps1 while this window waits.
set "RC=0"
goto :done

:ok_exit
echo.
echo [OK] Client finished with exit code 0.
goto :done

:fail
echo.
echo [FAIL] Client failed. Exit code: %RC%
echo.
echo Log file:
echo   %LOG%
echo.
type "%LOG%"
goto :done

:not_admin
echo [ERROR] Not running as Administrator! >> "%LOG%"
echo [ERROR] This window is NOT Administrator.
echo         Run scripts\run_client_wintun.vbs and click YES on UAC.
goto :done

:missing_exe
echo [ERROR] Missing build-agent\tunnel_client.exe >> "%LOG%"
echo [ERROR] Build the project first in Visual Studio.
goto :done

:done
echo.
echo ==========================================
echo   Press any key to close this window.
echo ==========================================
pause >nul
endlocal
exit /b 0
