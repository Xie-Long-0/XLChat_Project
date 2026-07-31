#include "LogSanitizer.h"

#include <QStringList>

namespace XYChat::Security
{

QString LogSanitizer::maskPassword(const QString &password)
{
    if (password.isEmpty()) return QStringLiteral("[empty]");
    if (password.size() <= 2) return QStringLiteral("***");
    return password.left(2) + QStringLiteral("***");
}

QString LogSanitizer::maskToken(const QString &token)
{
    if (token.isEmpty()) return QStringLiteral("[empty]");
    if (token.size() <= 8) return QStringLiteral("[token]");
    return token.left(8) + QStringLiteral("...");
}

QString LogSanitizer::maskMessageContent(const QString &content)
{
    if (content.isEmpty()) return QStringLiteral("[empty]");
    if (content.size() <= 20) return content.left(10) + QStringLiteral("...");
    return content.left(20) + QStringLiteral("...");
}

QString LogSanitizer::maskIpAddress(const QString &ip)
{
    // IPv4: 保留前两段，如 "192.168.*.*"
    const QStringList parts = ip.split(QLatin1Char('.'));
    if (parts.size() == 4) {
        return parts[0] + "." + parts[1] + ".*.*";
    }
    // IPv6 或其他：只保留前 8 字符
    if (ip.size() > 8) {
        return ip.left(8) + QStringLiteral("...");
    }
    return ip;
}

QString LogSanitizer::maskEmail(const QString &email)
{
    const int atPos = email.indexOf(QLatin1Char('@'));
    if (atPos <= 1) return QStringLiteral("[email]");
    return email.left(2) + QStringLiteral("***") + email.mid(atPos);
}

QString LogSanitizer::mask(const QString &sensitive, int keepChars)
{
    if (sensitive.isEmpty()) return QStringLiteral("[empty]");
    if (sensitive.size() <= keepChars) return QStringLiteral("[masked]");
    return sensitive.left(keepChars) + QStringLiteral("...");
}

} // namespace XYChat::Security
