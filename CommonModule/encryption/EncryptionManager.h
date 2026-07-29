#pragma once

#include <QString>
#include <QByteArray>

class EncryptionManager
{
public:
    // M1 兼容：客户端 SHA-256 摘要（用于传输层）
    static QString encryptPassword(const QString &password);

    // M2：服务端 PBKDF2 慢哈希
    // 返回格式: "v1:<iterations>:<salt-hex>:<hash-hex>"
    static QString hashPasswordWithSalt(const QString &password, int iterations = 100000);

    // M2：验证 PBKDF2 密码
    static bool verifyPassword(const QString &password, const QString &storedHash);

    // M2：生成安全随机 token（hex 编码）
    static QString generateToken(int byteLength = 32);

    // M2：对 token 做 SHA-256 摘要（服务端存储 token 哈希）
    static QString hashToken(const QString &token);

    // 当前哈希参数版本
    static constexpr int currentHashVersion() { return 1; }
    static constexpr int defaultIterations() { return 100000; }
};
