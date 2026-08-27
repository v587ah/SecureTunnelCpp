# 生成本地开发用自签名证书（Windows Schannel / MsQuic）。
# 输出：certs/dev/thumbprint.txt（40 位十六进制 SHA1，无分隔符）

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot "certs\dev"
$thumbprintFile = Join-Path $outputDir "thumbprint.txt"
$friendlyName = "SecureTunnel-Dev"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$existing = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.FriendlyName -eq $friendlyName } |
    Select-Object -First 1

if ($null -eq $existing) {
    $existing = New-SelfSignedCertificate `
        -DnsName @("localhost", "127.0.0.1") `
        -FriendlyName $friendlyName `
        -KeyUsageProperty Sign `
        -KeyUsage DigitalSignature `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -HashAlgorithm SHA256 `
        -Provider "Microsoft Software Key Storage Provider" `
        -KeyExportPolicy Exportable
}

$thumbprint = ($existing.Thumbprint -replace ":", "").ToLowerInvariant()
Set-Content -Path $thumbprintFile -Value $thumbprint -NoNewline -Encoding ascii

Write-Host "开发证书已就绪"
Write-Host "FriendlyName : $friendlyName"
Write-Host "Thumbprint   : $thumbprint"
Write-Host "Saved to     : $thumbprintFile"
