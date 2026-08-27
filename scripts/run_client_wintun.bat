@echo off
cd /d "%~dp0.."
set "ROOT=%CD%"

echo. > "%TEMP%\sectunnel_launch.log"
echo [%date% %time%] run_client_wintun.bat started >> "%TEMP%\sectunnel_launch.log"
echo ROOT=%ROOT% >> "%TEMP%\sectunnel_launch.log"

net session >> "%TEMP%\sectunnel_launch.log" 2>&1
if errorlevel 1 (
    echo [%date% %time%] Not admin, requesting UAC >> "%TEMP%\sectunnel_launch.log"
    echo.
    echo ==========================================
    echo   Click YES on the UAC security popup
    echo   A new Administrator window will open
    echo ==========================================
    echo.
    echo Log: %TEMP%\sectunnel_launch.log
    echo.
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~dp0_client_wintun_worker.bat' -Verb RunAs -WorkingDirectory '%ROOT%'"
    echo.
    echo If you clicked YES, check the NEW window (Administrator).
    echo.
    pause
    exit /b 0
)

echo [%date% %time%] Already admin, running worker >> "%TEMP%\sectunnel_launch.log"
call "%~dp0_client_wintun_worker.bat"
