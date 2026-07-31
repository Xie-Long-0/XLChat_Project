import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QWindowKit

import "theme"
import "pages"
import "components"

ApplicationWindow {
    id: root
    width: 480
    height: 640
    minimumWidth: 420
    minimumHeight: 600
    visible: false
    color: "transparent"
    title: "XYChat"

    // QWindowKit WindowAgent
    WindowAgent {
        id: windowAgent
    }

    Component.onCompleted: {
        windowAgent.setup(root)
        root.visible = true
    }

    // 关闭登录窗口即退出应用
    onClosing: Qt.quit()

    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 自定义标题栏
        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            window: root
            windowAgent: windowAgent
            title: "XYChat"
        }

        // 登录页面
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            LoginPage {
                id: loginPage

                onLoginRequested: function(username, password) {
                    loginPage.setLoading(true)
                    networkManager.login(username, password)
                }

                onRegisterRequested: function(username, password, email, phone) {
                    loginPage.setLoading(true)
                    networkManager.registerAccount(username, password, email, phone)
                }
            }
        }
    }

    // ── 主窗口（登录成功后显示） ────────────────────────────────
    MainWindow {
        id: mainWindow

        onLogoutRequested: {
            networkManager.logout()
            mainWindow.hide()
            loginPage.setLoading(false)
            root.show()
        }
    }

    // ── NetworkManager 认证信号连接 ──────────────────────────────
    Connections {
        target: networkManager

        function onLoginSuccessful() {
            loginPage.onLoginSuccess()
            mainWindow.myUserId = networkManager.userId
            mainWindow.myUsername = networkManager.username
            root.hide()
            mainWindow.show()
            mainWindow.loadConversations()
        }

        function onLoginFailed(errorMessage) {
            loginPage.showError(errorMessage)
        }

        function onRegisterSuccessful() {
            loginPage.showSuccess("注册成功，请登录")
        }

        function onRegisterFailed(errorMessage) {
            loginPage.showError(errorMessage)
            loginPage.setLoading(false)
        }
    }
}
