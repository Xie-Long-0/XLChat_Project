#pragma once

#include <QString>

namespace XYChat::Security
{

/**
 * 日志脱敏工具
 * 用于在日志输出前对敏感信息进行掩码处理
 * 禁止在日志中输出：密码、token、私钥、完整消息正文
 */
class LogSanitizer
{
public:
    /**
     * 对密码进行脱敏（只显示前2字符 + ***）
     */
    static QString maskPassword(const QString &password);

    /**
     * 对 token 进行脱敏（只显示前8字符 + ...）
     */
    static QString maskToken(const QString &token);

    /**
     * 对消息正文进行脱敏（只显示前20字符 + ...）
     */
    static QString maskMessageContent(const QString &content);

    /**
     * 对 IP 地址进行部分脱敏（保留前两段）
     */
    static QString maskIpAddress(const QString &ip);

    /**
     * 对邮箱进行脱敏
     */
    static QString maskEmail(const QString &email);

    /**
     * 通用脱敏：对任意敏感字符串只保留前 n 个字符
     */
    static QString mask(const QString &sensitive, int keepChars = 4);
};

} // namespace XYChat::Security
