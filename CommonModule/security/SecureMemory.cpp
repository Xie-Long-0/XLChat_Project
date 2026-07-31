#include "SecureMemory.h"

#include <openssl/crypto.h>

namespace XYChat::Security
{

void SecureMemory::wipe(void *data, size_t length)
{
    if (data && length > 0) {
        // OPENSSL_cleanse 使用平台安全清零实现
        // Windows: SecureZeroMemory / RtlSecureZeroMemory
        // Linux: explicit_bzero 或 volatile 技巧
        OPENSSL_cleanse(data, length);
    }
}

void SecureMemory::wipe(QByteArray &data)
{
    if (!data.isEmpty()) {
        wipe(data.data(), static_cast<size_t>(data.size()));
        data.clear();
    }
}

void SecureMemory::wipe(QString &str)
{
    if (!str.isEmpty()) {
        // QString 使用 UTF-16 内部存储
        // 先 detach 确保不共享，然后清零
        str.detach();
        wipe(str.data(), static_cast<size_t>(str.size()) * sizeof(QChar));
        str.clear();
    }
}

} // namespace XYChat::Security
