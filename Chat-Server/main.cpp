#include <QCoreApplication>
#include "core/Server.h"
#include "TlsHelper.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("XYChat");
    app.setApplicationName("XYChat-Server");

    Server server;

    // M5: 初始化 TLS（自动生成开发证书）
    const QString certDir = XYChat::Security::TlsHelper::defaultCertDir();
    if (!server.initTls(certDir)) {
        qWarning() << "[Main] TLS init failed, falling back to plain TCP";
    }

    if (!server.start(12345))
    {
        qCritical() << "Failed to start server";
        return -1;
    }
    return app.exec();
}
