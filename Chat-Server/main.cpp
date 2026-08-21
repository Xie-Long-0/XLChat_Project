#include <QCoreApplication>
#include <QCommandLineParser>
#include "core/Server.h"
#include "TlsHelper.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("XYChat");
    app.setApplicationName("XYChat-Server");

    // M5.5: 开发明文模式必须显式开启（默认关闭，生产禁用）
    QCommandLineParser parser;
    parser.setApplicationDescription("XYChat Server (TLS fail-closed by default)");
    parser.addHelpOption();
    QCommandLineOption plaintextOption(
        QStringList() << "allow-plaintext",
        "Development only: allow plaintext TCP when TLS is unavailable. "
        "Never use in production.");
    parser.addOption(plaintextOption);
    parser.process(app);

    Server server;

    // M5: 初始化 TLS（自动生成开发证书）
    const QString certDir = XYChat::Security::TlsHelper::defaultCertDir();
    if (!server.initTls(certDir)) {
        qWarning() << "[Main] TLS init failed.";
    }

    // M5.5: fail-closed：TLS 不可用且未显式允许明文时拒绝启动
    if (!server.start(12345, parser.isSet(plaintextOption)))
    {
        qCritical() << "Failed to start server";
        return -1;
    }
    return app.exec();
}
