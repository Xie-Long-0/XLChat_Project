#!/bin/bash
# XYChat 开发环境自签名证书生成脚本
# 用途：生成 TLS 开发证书（CA + 服务端证书）
# 注意：仅用于开发/测试环境，生产环境请使用正式 CA 签发的证书

set -e

CERT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAYS=3650
KEY_SIZE=2048

echo "=== XYChat Dev Certificate Generator ==="
echo "Output directory: $CERT_DIR"
echo ""

# 1. 生成 CA 私钥和自签名 CA 证书
echo "[1/4] Generating CA key and certificate..."
openssl genrsa -out "$CERT_DIR/ca.key" $KEY_SIZE
openssl req -new -x509 -days $DAYS -key "$CERT_DIR/ca.key" \
    -out "$CERT_DIR/ca.crt" \
    -subj "/C=CN/ST=Dev/L=Dev/O=XYChat/OU=DevCA/CN=XYChat Dev Root CA"

# 2. 生成服务端私钥和 CSR
echo "[2/4] Generating server key and CSR..."
openssl genrsa -out "$CERT_DIR/server.key" $KEY_SIZE
openssl req -new -key "$CERT_DIR/server.key" \
    -out "$CERT_DIR/server.csr" \
    -subj "/C=CN/ST=Dev/L=Dev/O=XYChat/OU=Server/CN=localhost"

# 3. 使用 CA 签发服务端证书（含 SAN 扩展）
echo "[3/4] Signing server certificate with CA..."
cat > "$CERT_DIR/server_ext.cnf" <<EOF
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
EOF

openssl x509 -req -days $DAYS \
    -in "$CERT_DIR/server.csr" \
    -CA "$CERT_DIR/ca.crt" \
    -CAkey "$CERT_DIR/ca.key" \
    -CAcreateserial \
    -out "$CERT_DIR/server.crt" \
    -extfile "$CERT_DIR/server_ext.cnf"

# 4. 清理临时文件
echo "[4/4] Cleaning up..."
rm -f "$CERT_DIR/server.csr" "$CERT_DIR/server_ext.cnf" "$CERT_DIR/ca.srl"

echo ""
echo "=== Done ==="
echo "Generated files:"
echo "  ca.crt       - CA 根证书（客户端信任此证书）"
echo "  ca.key       - CA 私钥（妥善保管，勿泄露）"
echo "  server.crt   - 服务端证书"
echo "  server.key   - 服务端私钥"
echo ""
echo "使用方式："
echo "  服务端：加载 server.crt + server.key"
echo "  客户端：信任 ca.crt（开发模式）"
