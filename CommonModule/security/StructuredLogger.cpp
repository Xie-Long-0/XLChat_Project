#include "StructuredLogger.h"
#include "LogSanitizer.h"

#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>

namespace XYChat::Security
{

namespace
{
// 级别名（写入 JSON 的 level 字段）
QString levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:    return "debug";
    case LogLevel::Info:     return "info";
    case LogLevel::Warning:  return "warning";
    case LogLevel::Critical: return "critical";
    }
    return "info";
}
} // namespace

StructuredLogger::StructuredLogger(LogLevel level, const QString &eventName)
    : m_level(level)
    , m_event(eventName)
{
}

StructuredLogger StructuredLogger::event(LogLevel level, const QString &eventName)
{
    return StructuredLogger(level, eventName);
}

StructuredLogger &StructuredLogger::requestId(quint64 id)
{
    m_fields.insert("requestId", static_cast<double>(id));
    return *this;
}

StructuredLogger &StructuredLogger::userId(qint64 id)
{
    m_fields.insert("userId", static_cast<double>(id));
    return *this;
}

StructuredLogger &StructuredLogger::deviceId(const QString &id)
{
    m_fields.insert("deviceId", id);
    return *this;
}

StructuredLogger &StructuredLogger::errorCode(int code)
{
    m_fields.insert("code", static_cast<double>(code));
    return *this;
}

StructuredLogger &StructuredLogger::durationMs(qint64 ms)
{
    m_fields.insert("durationMs", static_cast<double>(ms));
    return *this;
}

StructuredLogger &StructuredLogger::field(const QString &key, const QString &value)
{
    m_fields.insert(key, value);
    return *this;
}

StructuredLogger &StructuredLogger::field(const QString &key, qint64 value)
{
    m_fields.insert(key, static_cast<double>(value));
    return *this;
}

StructuredLogger &StructuredLogger::tokenField(const QString &key, const QString &token)
{
    m_fields.insert(key, LogSanitizer::maskToken(token));
    return *this;
}

StructuredLogger &StructuredLogger::ipField(const QString &key, const QString &ip)
{
    m_fields.insert(key, LogSanitizer::maskIpAddress(ip));
    return *this;
}

StructuredLogger &StructuredLogger::contentField(const QString &key, const QString &content)
{
    m_fields.insert(key, LogSanitizer::maskMessageContent(content));
    return *this;
}

QString StructuredLogger::toJson() const
{
    QJsonObject obj = m_fields;
    obj.insert("ts", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    obj.insert("level", levelName(m_level));
    obj.insert("event", m_event);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void StructuredLogger::write() const
{
    const QString line = toJson();
    switch (m_level) {
    case LogLevel::Debug:
        qDebug().noquote() << line;
        break;
    case LogLevel::Info:
        qInfo().noquote() << line;
        break;
    case LogLevel::Warning:
        qWarning().noquote() << line;
        break;
    case LogLevel::Critical:
        qCritical().noquote() << line;
        break;
    }
}

} // namespace XYChat::Security
