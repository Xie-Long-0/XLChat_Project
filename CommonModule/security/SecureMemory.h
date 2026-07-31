#pragma once

#include <QByteArray>
#include <QString>

namespace XYChat::Security
{

/**
 * 安全内存工具
 * - 提供安全清零函数，防止编译器优化掉 memset
 * - 密钥材料使用后应立即调用 secureWipe 清除
 */
class SecureMemory
{
public:
    /**
     * 安全清零内存（不会被编译器优化掉）
     */
    static void wipe(void *data, size_t length);

    /**
     * 安全清零 QByteArray
     */
    static void wipe(QByteArray &data);

    /**
     * 安全清零 QString（注意：QString 内部可能有共享引用）
     */
    static void wipe(QString &str);
};

} // namespace XYChat::Security
