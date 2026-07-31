# XYChat 开发环境自签名证书生成脚本 (Windows PowerShell)
# 用途：生成 TLS 开发证书（CA + 服务端证书）
# 注意：仅用于开发/测试环境，生产环境请使用正式 CA 签发的证书
# 前提：系统 PATH 中需要有 openssl 命令

$ErrorActionPreference = "Stop"
$CertDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Days = 3650
$KeySize = 2048

Write-Host "=== XYChat Dev Certificate Generator ===" -ForegroundColor Cyan
Write-Host "Output directory: $CertDir"
Write-Host ""

# 1. 生成 CA 私钥和自签名 CA 证书
Write-Host "[1/4] Generating CA key and certificate..." -ForegroundColor Yellow
openssl genrsa -out "$CertDir\ca.key" $KeySize
openssl req -new -x509 -days $Days -key "$CertDir\ca.key" `
    -out "$CertDir\ca.crt" `
    -subj "/C=CN/ST=Dev/L=Dev/O=XYChat/OU=DevCA/CN=XYChat Dev Root CA"

# 2. 生成服务端私钥和 CSR
Write-Host "[2/4] Generating server key and CSR..." -ForegroundColor Yellow
openssl genrsa -out "$CertDir\server.key" $KeySize
openssl req -new -key "$CertDir\server.key" `
    -out "$CertDir\server.csr" `
    -subj "/C=CN/ST=Dev/L=Dev/O=XYChat/OU=Server/CN=localhost"

# 3. 使用 CA 签发服务端证书（含 SAN 扩展）
Write-Host "[3/4] Signing server certificate with CA..." -ForegroundColor Yellow
$extContent = @"
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
"@
$extContent | Out-File -Encoding ascii "$CertDir\server_ext.cnf"

openssl x509 -req -days $Days `
    -in "$CertDir\server.csr" `
    -CA "$CertDir\ca.crt" `
    -CAkey "$CertDir\ca.key" `
    -CAcreateserial `
    -out "$CertDir\server.crt" `
    -extfile "$CertDir\server_ext.cnf"

# 4. 清理临时文件
Write-Host "[4/4] Cleaning up..." -ForegroundColor Yellow
Remove-Item -Force "$CertDir\server.csr" -ErrorAction SilentlyContinue
Remove-Item -Force "$CertDir\server_ext.cnf" -ErrorAction SilentlyContinue
Remove-Item -Force "$CertDir\ca.srl" -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Green
Write-Host "Generated files:"
Write-Host "  ca.crt       - CA 根证书（客户端信任此证书）"
Write-Host "  ca.key       - CA 私钥（妥善保管，勿泄露）"
Write-Host "  server.crt   - 服务端证书"
Write-Host "  server.key   - 服务端私钥"
Write-Host ""
Write-Host "使用方式："
Write-Host "  服务端：加载 server.crt + server.key"
Write-Host "  客户端：信任 ca.crt（开发模式）"
