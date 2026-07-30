#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <QWKQuick/qwkquickglobal.h>

#include "core/NetworkManager.h"

int main(int argc, char *argv[])
{
    QGuiApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    //QQuickWindow::setDefaultAlphaBuffer(true);

    QGuiApplication app(argc, argv);
    app.setOrganizationName("XYChat");
    app.setApplicationName("XYChat");

    // 设置默认样式
    QQuickStyle::setStyle("Basic");

    // 创建 NetworkManager
    NetworkManager networkManager;

    // 创建 QML 引擎
    QQmlApplicationEngine engine;

    // 暴露 C++ 对象到 QML
    engine.rootContext()->setContextProperty("networkManager", &networkManager);

    // 注册 QWindowKit QML 类型
    QWK::registerTypes(&engine);

    // 加载 QML
    engine.addImportPath(":/");
    engine.load(QUrl("qrc:/main.qml"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
