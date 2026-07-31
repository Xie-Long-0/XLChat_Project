#include "TlsHelper.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/bn.h>

namespace XYChat::Security
{

QString TlsHelper::defaultCertDir()
{
    // 优先使用可执行文件同级的 certs 目录
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString exeCerts = exeDir + "/certs";
    if (QDir(exeCerts).exists()) {
        return exeCerts;
    }

    // 否则使用用户数据目录
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString certDir = dataDir + "/certs";
    QDir().mkpath(certDir);
    return certDir;
}

TlsHelper::TlsConfig TlsHelper::loadServerConfig(const QString &certPath,
                                                  const QString &keyPath,
                                                  const QString &caCertPath,
                                                  bool allowGenerate)
{
    TlsConfig config;

    // 检查证书文件是否存在
    if (!QFile::exists(certPath) || !QFile::exists(keyPath)) {
        if (!allowGenerate) {
            qWarning() << "[TLS] Server certificate not found:" << certPath;
            return config;
        }

        // 自动生成开发证书
        const QString certDir = QFileInfo(certPath).absolutePath();
        qInfo() << "[TLS] Generating development certificates in" << certDir;
        if (!generateDevCertificates(certDir)) {
            qCritical() << "[TLS] Failed to generate development certificates";
            return config;
        }
    }

    // 加载服务端证书
    QFile certFile(certPath);
    if (certFile.open(QIODevice::ReadOnly)) {
        config.certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
        certFile.close();
    }

    // 加载服务端私钥
    QFile keyFile(keyPath);
    if (keyFile.open(QIODevice::ReadOnly)) {
        config.privateKey = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
        keyFile.close();
    }

    // 加载 CA 证书
    QFile caFile(caCertPath);
    if (caFile.open(QIODevice::ReadOnly)) {
        config.caCertificate = QSslCertificate(caFile.readAll(), QSsl::Pem);
        caFile.close();
    }

    config.valid = !config.certificate.isNull() && !config.privateKey.isNull();
    if (!config.valid) {
        qCritical() << "[TLS] Failed to load server TLS configuration";
    } else {
        qInfo() << "[TLS] Server TLS configuration loaded successfully";
    }

    return config;
}

TlsHelper::TlsConfig TlsHelper::loadClientConfig(const QString &caCertPath)
{
    TlsConfig config;

    QFile caFile(caCertPath);
    if (caFile.open(QIODevice::ReadOnly)) {
        config.caCertificate = QSslCertificate(caFile.readAll(), QSsl::Pem);
        caFile.close();
    }

    if (config.caCertificate.isNull()) {
        qWarning() << "[TLS] CA certificate not found or invalid:" << caCertPath;
        return config;
    }

    config.valid = true;
    qInfo() << "[TLS] Client TLS configuration loaded (CA:" << caCertPath << ")";
    return config;
}

bool TlsHelper::generateDevCertificates(const QString &certDir)
{
    QDir().mkpath(certDir);
    return generateCaCert(certDir) && generateServerCert(certDir);
}

// ── 内部辅助：写入 EVP_PKEY 到 PEM 文件（使用内存 BIO 避免 Windows applink 问题）───
static bool writeKeyToFile(EVP_PKEY *pkey, const QString &path)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) return false;
    if (!PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        BIO_free(bio);
        return false;
    }
    char *data = nullptr;
    const long len = BIO_get_mem_data(bio, &data);
    QFile file(path);
    bool ok = false;
    if (file.open(QIODevice::WriteOnly)) {
        ok = file.write(data, len) == len;
        file.close();
    }
    BIO_free(bio);
    return ok;
}

// ── 内部辅助：写入 X509 到 PEM 文件 ─────────────────────────────────────────
static bool writeCertToFile(X509 *cert, const QString &path)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) return false;
    if (!PEM_write_bio_X509(bio, cert)) {
        BIO_free(bio);
        return false;
    }
    char *data = nullptr;
    const long len = BIO_get_mem_data(bio, &data);
    QFile file(path);
    bool ok = false;
    if (file.open(QIODevice::WriteOnly)) {
        ok = file.write(data, len) == len;
        file.close();
    }
    BIO_free(bio);
    return ok;
}

// ── 内部辅助：从 PEM 文件读取 EVP_PKEY ──────────────────────────────────────
static EVP_PKEY *readKeyFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return nullptr;
    const QByteArray data = file.readAll();
    file.close();
    BIO *bio = BIO_new_mem_buf(data.constData(), data.size());
    if (!bio) return nullptr;
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return pkey;
}

// ── 内部辅助：从 PEM 文件读取 X509 ─────────────────────────────────────────
static X509 *readCertFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return nullptr;
    const QByteArray data = file.readAll();
    file.close();
    BIO *bio = BIO_new_mem_buf(data.constData(), data.size());
    if (!bio) return nullptr;
    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return cert;
}

bool TlsHelper::generateCaCert(const QString &certDir)
{
    const QString caKeyPath = certDir + "/ca.key";
    const QString caCertPath = certDir + "/ca.crt";

    // 生成 RSA 密钥对
    EVP_PKEY *pkey = EVP_RSA_gen(2048);
    if (!pkey) {
        qWarning() << "[TLS] Failed to generate CA RSA key";
        return false;
    }

    // 创建 X509 证书
    X509 *cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // 设置版本为 v3
    X509_set_version(cert, 2);

    // 设置序列号
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    // 有效期 10 年
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3650L * 24 * 3600);

    // 设置公钥
    X509_set_pubkey(cert, pkey);

    // 设置 Subject 和 Issuer（自签名，两者相同）
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("CN"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("XYChat"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("DevCA"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("XYChat Dev Root CA"), -1, -1, 0);
    X509_set_issuer_name(cert, name);

    // 添加 CA 扩展
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:TRUE");
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "critical,keyCertSign,cRLSign");
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // 自签名
    if (!X509_sign(cert, pkey, EVP_sha256())) {
        qWarning() << "[TLS] Failed to sign CA certificate";
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    // 写入文件
    const bool keyOk = writeKeyToFile(pkey, caKeyPath);
    const bool certOk = writeCertToFile(cert, caCertPath);

    X509_free(cert);
    EVP_PKEY_free(pkey);

    if (keyOk && certOk) {
        qInfo() << "[TLS] CA certificate generated:" << caCertPath;
    }
    return keyOk && certOk;
}

bool TlsHelper::generateServerCert(const QString &certDir)
{
    const QString caKeyPath = certDir + "/ca.key";
    const QString caCertPath = certDir + "/ca.crt";
    const QString serverKeyPath = certDir + "/server.key";
    const QString serverCertPath = certDir + "/server.crt";

    // 加载 CA 证书和私钥
    EVP_PKEY *caKey = readKeyFromFile(caKeyPath);
    if (!caKey) {
        qWarning() << "[TLS] Cannot open CA key:" << caKeyPath;
        return false;
    }

    X509 *caCert = readCertFromFile(caCertPath);
    if (!caCert) {
        EVP_PKEY_free(caKey);
        return false;
    }

    // 生成服务端 RSA 密钥
    EVP_PKEY *serverKey = EVP_RSA_gen(2048);
    if (!serverKey) {
        X509_free(caCert);
        EVP_PKEY_free(caKey);
        return false;
    }

    // 创建服务端证书
    X509 *cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 2);

    // 有效期 10 年
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3650L * 24 * 3600);

    X509_set_pubkey(cert, serverKey);

    // Subject
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("CN"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("XYChat"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("Server"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("localhost"), -1, -1, 0);

    // Issuer = CA
    X509_set_issuer_name(cert, X509_get_subject_name(caCert));

    // 扩展
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, caCert, cert, nullptr, nullptr, 0);

    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "CA:FALSE");
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "digitalSignature,keyEncipherment");
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, "serverAuth");
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // SAN: localhost + 127.0.0.1
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name,
                              "DNS:localhost,IP:127.0.0.1");
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // 用 CA 私钥签名
    if (!X509_sign(cert, caKey, EVP_sha256())) {
        qWarning() << "[TLS] Failed to sign server certificate";
        X509_free(cert);
        EVP_PKEY_free(serverKey);
        X509_free(caCert);
        EVP_PKEY_free(caKey);
        return false;
    }

    // 写入文件
    const bool keyOk = writeKeyToFile(serverKey, serverKeyPath);
    const bool certOk = writeCertToFile(cert, serverCertPath);

    X509_free(cert);
    EVP_PKEY_free(serverKey);
    X509_free(caCert);
    EVP_PKEY_free(caKey);

    if (keyOk && certOk) {
        qInfo() << "[TLS] Server certificate generated:" << serverCertPath;
    }
    return keyOk && certOk;
}

} // namespace XYChat::Security
