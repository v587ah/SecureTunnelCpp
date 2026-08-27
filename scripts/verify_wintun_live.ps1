#Requires -RunAsAdministrator
# 一键验证 Wintun（自动启动 client 并多次检测网卡）

$ErrorActionPreference = "Continue"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ClientExe = Join-Path $Root "build-agent\tunnel_client.exe"
$RelaySeconds = 90

function Find-TunnelAdapter {
    # 1) SecureTunnel adapter (our client), must be Up
    $secure = Get-NetAdapter -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*SecureTunnel*" -and $_.Status -eq "Up" }
    if ($secure) {
        return $secure | Select-Object -First 1 Name, Status, InterfaceDescription, ifIndex
    }

    # 2) Wintun adapter with expected tunnel IP (ignore other WireGuard VPNs)
    $candidates = Get-NetAdapter -ErrorAction SilentlyContinue |
        Where-Object { $_.InterfaceDescription -like "*Wintun*" -and $_.Status -eq "Up" }

    foreach ($adapter in $candidates) {
        $hasTunnelIp = Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -eq "10.66.66.2" }
        if ($hasTunnelIp) {
            return $adapter | Select-Object Name, Status, InterfaceDescription, ifIndex
        }
    }

    return $null
}

function Show-AdapterInfo($adapter) {
    Write-Host "[OK] Adapter: $($adapter.Name)" -ForegroundColor Green
    Write-Host "     Status: $($adapter.Status)" -ForegroundColor Green
    Write-Host "     Description: $($adapter.InterfaceDescription)" -ForegroundColor Green

    Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        ForEach-Object {
            Write-Host "[OK] IPv4: $($_.IPAddress)/$($_.PrefixLength)" -ForegroundColor Green
        }

    Get-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        ForEach-Object {
            if ($_.ServerAddresses) {
                Write-Host "[OK] DNS: $($_.ServerAddresses -join ', ')" -ForegroundColor Green
            }
        }
}

Write-Host "=== SecureTunnel Wintun Live Verification ===" -ForegroundColor Cyan
Write-Host "Project: $Root"
Write-Host ""

if (-not (Test-Path $ClientExe)) {
    Write-Host "[FAIL] Not found: $ClientExe" -ForegroundColor Red
    exit 1
}

$server = Get-Process -Name "tunnel_server" -ErrorAction SilentlyContinue
if ($server) {
    Write-Host "[OK] Server running (PID $($server.Id))" -ForegroundColor Green
} else {
    Write-Host "[FAIL] tunnel_server.exe is NOT running!" -ForegroundColor Red
    Write-Host "       Run scripts\run_server.bat first, or use scripts\verify_wintun.bat" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "[INFO] Starting client (relay ${RelaySeconds}s)..." -ForegroundColor Cyan
Write-Host "       $ClientExe --quic --insecure --wintun --relay-seconds:$RelaySeconds"
Write-Host ""

$clientProc = Start-Process `
    -FilePath $ClientExe `
    -ArgumentList "--quic", "--insecure", "--wintun", "--relay-seconds:$RelaySeconds" `
    -WorkingDirectory $Root `
    -PassThru `
    -WindowStyle Normal

Write-Host "[INFO] Client PID: $($clientProc.Id)" -ForegroundColor Cyan
Write-Host "[INFO] Polling for Wintun adapter (up to 30 seconds)..." -ForegroundColor Cyan
Write-Host ""

$found = $false
$foundAdapter = $null

for ($i = 1; $i -le 15; $i++) {
    Start-Sleep -Seconds 2

    if ($clientProc.HasExited) {
        Write-Host "[WARN] Client exited early at check $i (exit code: $($clientProc.ExitCode))" -ForegroundColor Yellow
        break
    }

    $adapters = Find-TunnelAdapter
    if ($adapters) {
        $found = $true
        $foundAdapter = $adapters | Select-Object -First 1
        Write-Host "[OK] Found on check $i ($($i * 2)s):" -ForegroundColor Green
        Show-AdapterInfo $foundAdapter
        break
    }

    Write-Host "  check $i/15 ... not yet" -ForegroundColor DarkGray
}

Write-Host ""

if (-not $found) {
    Write-Host "[FAIL] SecureTunnel Wintun adapter not found while client was running." -ForegroundColor Red
    Write-Host "       Expected: name *SecureTunnel*, status Up, IP 10.66.66.2/24" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "All network adapters on this PC:" -ForegroundColor Yellow
    Get-NetAdapter | Format-Table Name, Status, InterfaceDescription -AutoSize
    Write-Host ""
    Write-Host "Check the client window for errors." -ForegroundColor Yellow
    Write-Host "Log: $env:TEMP\sectunnel_client.log" -ForegroundColor Yellow
} else {
    Write-Host "[OK] Wintun verification PASSED" -ForegroundColor Green
}

Write-Host ""
Write-Host "[INFO] Waiting for client to finish..." -ForegroundColor Cyan
try {
    if (-not $clientProc.HasExited) {
        Wait-Process -Id $clientProc.Id -Timeout ($RelaySeconds + 15) -ErrorAction Stop
    }
} catch {
    if (-not $clientProc.HasExited) {
        Stop-Process -Id $clientProc.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Done." -ForegroundColor Cyan
if ($found) { exit 0 } else { exit 1 }
