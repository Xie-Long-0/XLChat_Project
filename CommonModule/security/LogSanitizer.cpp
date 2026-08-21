#include "LogSanitizer.h"

#include <QStringList>

namespace XYChat::Security
{

QString LogSanitizer::maskPassword(const QString &password)
{
    if (password.isEmpty()) return "[empty]";
    if (password.size() <= 2) return "***";
    return password.left(2) + "***";
}

QString LogSanitizer::maskToken(const QString &token)
{
    if (token.isEmpty()) return "[empty]";
    if (token.size() <= 8) return "[token]";
    return token.left(8) + "...";
}

QString LogSanitizer::maskMessageContent(const QString &content)
{
    if (content.isEmpty()) return "[empty]";
    if (content.size() <= 20) return content.left(10) + "...";
    return content.left(20) + "...";
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
        return ip.left(8) + "...";
    }
    return ip;
}

QString LogSanitizer::maskEmail(const QString &email)
{
    const int atPos = email.indexOf(QLatin1Char('@'));
    if (atPos <= 1) return "[email]";
    return email.left(2) + "***" + email.mid(atPos);
}

QString LogSanitizer::mask(const QString &sensitive, int keepChars)
{
    if (sensitive.isEmpty()) return "[empty]";
    if (sensitive.size() <= keepChars) return "[masked]";
    return sensitive.left(keepChars) + "...";
}

} // namespace XYChat::Security
