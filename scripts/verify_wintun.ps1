#Requires -RunAsAdministrator
# Wintun 网卡验证脚本
# 用法：先在一个窗口运行 tunnel_client --quic --wintun --relay，再在本脚本窗口执行：
#   powershell -ExecutionPolicy Bypass -File scripts\verify_wintun.ps1

$ErrorActionPreference = "Continue"

Write-Host "=== SecureTunnel Wintun Verification ===" -ForegroundColor Cyan

$adapter = Get-NetAdapter -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "*SecureTunnel*" -and $_.Status -eq "Up" } |
    Select-Object -First 1

if (-not $adapter) {
    $adapter = Get-NetAdapter -ErrorAction SilentlyContinue |
        Where-Object { $_.InterfaceDescription -like "*Wintun*" -and $_.Status -eq "Up" } |
        ForEach-Object {
            $hasTunnelIp = Get-NetIPAddress -InterfaceIndex $_.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
                Where-Object { $_.IPAddress -eq "10.66.66.2" }
            if ($hasTunnelIp) { $_ }
        } | Select-Object -First 1
}

if (-not $adapter) {
    Write-Host "[FAIL] SecureTunnel / Wintun adapter not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Reason: Wintun is removed when tunnel_client.exe exits." -ForegroundColor Yellow
    Write-Host "        run_client_wintun.vbs only keeps client ~30s." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Easiest fix - one command (admin PowerShell):" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\verify_wintun_live.ps1" -ForegroundColor White
    Write-Host ""
    Write-Host "Manual checklist:" -ForegroundColor Yellow
    Write-Host "  1. Start server:  scripts\run_server.bat"
    Write-Host "  2. Start client (admin) and KEEP IT RUNNING:"
    Write-Host "     .\build-agent\tunnel_client.exe --quic --insecure --wintun --relay-seconds:60"
    Write-Host "  3. While client is still running, run this script again"
    exit 1
}

Write-Host "[OK] Adapter found: $($adapter.Name) ($($adapter.Status))" -ForegroundColor Green

$addresses = Get-NetIPAddress -InterfaceAlias $adapter.Name -AddressFamily IPv4 -ErrorAction SilentlyContinue
foreach ($addr in $addresses) {
    Write-Host "[OK] IPv4: $($addr.IPAddress)/$($addr.PrefixLength)" -ForegroundColor Green
    if ($addr.IPAddress -ne "10.66.66.2") {
        Write-Host "[WARN] Expected client IP 10.66.66.2" -ForegroundColor Yellow
    }
}

$dns = Get-DnsClientServerAddress -InterfaceAlias $adapter.Name -AddressFamily IPv4 -ErrorAction SilentlyContinue
if ($dns -and $dns.ServerAddresses) {
    Write-Host "[OK] DNS: $($dns.ServerAddresses -join ', ')" -ForegroundColor Green
} else {
    Write-Host "[WARN] No interface DNS configured" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "--- Default routes (0.0.0.0) ---" -ForegroundColor Cyan
Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue |
    Format-Table InterfaceAlias, NextHop, RouteMetric -AutoSize

$tunnelDefault = Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue |
    Where-Object { $_.InterfaceAlias -like "*SecureTunnel*" }

if ($tunnelDefault) {
    Write-Host "[OK] Global default route uses SecureTunnel (full-tunnel mode)" -ForegroundColor Green
} else {
    Write-Host "[INFO] No default route on SecureTunnel (normal for --wintun without --global-route)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "--- QUIC server reachability ---" -ForegroundColor Cyan
$udpTest = Test-NetConnection -ComputerName 127.0.0.1 -Port 44333 -WarningAction SilentlyContinue 2>$null
Write-Host "[INFO] Local server should be running: tunnel_server.exe --quic --relay" -ForegroundColor Gray

Write-Host ""
Write-Host "Done." -ForegroundColor Cyan
