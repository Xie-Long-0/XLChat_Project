#pragma once

#include <QString>
#include <QJsonObject>
#include <QtGlobal>

namespace XYChat::Security
{

// 结构化日志级别
enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Critical
};

// 结构化日志记录器
// 以统一字段（时间戳/级别/事件/请求 ID/用户/设备/错误码/耗时/来源 IP）输出单行紧凑
// JSON，便于日志聚合与检索；敏感字段（token/IP/消息正文）写入前经 LogSanitizer 脱敏，
// 避免在日志中泄漏凭据或完整正文。
// 用法（链式，临时对象在整条表达式结束前保持存活）：
//   StructuredLogger::event(LogLevel::Info, "response")
//       .requestId(id).userId(uid).deviceId(did)
//       .field("type", type).errorCode(code).durationMs(ms)
//       .write();
class StructuredLogger
{
public:
    StructuredLogger(LogLevel level, const QString &eventName);

    // 统一字段
    StructuredLogger &requestId(quint64 id);
    StructuredLogger &userId(qint64 id);
    StructuredLogger &deviceId(const QString &id);
    StructuredLogger &errorCode(int code);
    StructuredLogger &durationMs(qint64 ms);

    // 自定义字段（非敏感值）
    StructuredLogger &field(const QString &key, const QString &value);
    StructuredLogger &field(const QString &key, qint64 value);

    // 敏感字段（写入前经 LogSanitizer 脱敏）
    StructuredLogger &tokenField(const QString &key, const QString &token);
    StructuredLogger &ipField(const QString &key, const QString &ip);
    StructuredLogger &contentField(const QString &key, const QString &content);

    // 序列化为单行紧凑 JSON（不含末尾换行）
    QString toJson() const;
    // 按级别写入 Qt 日志系统（qDebug/qInfo/qWarning/qCritical）
    void write() const;

    // 工厂：构造一条指定级别与事件名的记录
    static StructuredLogger event(LogLevel level, const QString &eventName);

private:
    LogLevel m_level;
    QString m_event;
    QJsonObject m_fields;
};

} // namespace XYChat::Security
