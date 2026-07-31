#pragma once

#include <QString>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>

namespace XYChat::Security
{

/**
 * TLS 配置辅助类
 * - 服务端：加载或自动生成开发证书
 * - 客户端：加载 CA 证书进行服务端验证
 */
class TlsHelper
{
public:
    struct TlsConfig
    {
        QSslCertificate certificate;
        QSslKey privateKey;
        QSslCertificate caCertificate;
        bool valid = false;
    };

    /**
     * 加载服务端 TLS 配置
     * 如果证书文件不存在且 allowGenerate=true，则自动生成开发证书
     * @param certPath 服务端证书路径 (PEM)
     * @param keyPath  服务端私钥路径 (PEM)
     * @param caCertPath CA 证书路径 (PEM)
     * @param allowGenerate 是否允许自动生成开发证书
     */
    static TlsConfig loadServerConfig(const QString &certPath,
                                      const QString &keyPath,
                                      const QString &caCertPath,
                                      bool allowGenerate = true);

    /**
     * 加载客户端 TLS 配置
     * @param caCertPath CA 证书路径 (PEM)，用于验证服务端
     */
    static TlsConfig loadClientConfig(const QString &caCertPath);

    /**
     * 生成自签名开发证书（CA + 服务端证书）
     * @param certDir 证书输出目录
     * @return 是否成功
     */
    static bool generateDevCertificates(const QString &certDir);

    /**
     * 获取默认证书目录
     */
    static QString defaultCertDir();

private:
    static bool generateCaCert(const QString &certDir);
    static bool generateServerCert(const QString &certDir);
};

} // namespace XYChat::Security
