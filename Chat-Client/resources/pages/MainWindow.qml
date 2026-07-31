import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QWindowKit

import "../theme"
import "../components"

ApplicationWindow {
    id: mainWindow
    width: 1000
    height: 650
    minimumWidth: 700
    minimumHeight: 450
    visible: false
    color: "transparent"
    title: "XYChat"

    // 登出信号（由登录窗口处理窗口切换）
    signal logoutRequested()

    property int myUserId: 0
    property string myUsername: ""

    // QWindowKit WindowAgent
    WindowAgent {
        id: windowAgent
    }

    Component.onCompleted: {
        windowAgent.setup(mainWindow)
    }

    // 关闭主窗口即退出应用
    onClosing: Qt.quit()

    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 自定义标题栏
        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            window: mainWindow
            windowAgent: windowAgent
            title: "XYChat"
        }

        // 主页面
        MainPage {
            id: mainPage
            Layout.fillWidth: true
            Layout.fillHeight: true

            myUserId: mainWindow.myUserId
            myUsername: mainWindow.myUsername

            onSearchUsersRequested: function(query) {
                networkManager.searchUsers(query)
            }

            onAddContactRequested: function(userId) {
                networkManager.addContact(userId)
            }

            onLoadConversationsRequested: {
                networkManager.getConversations()
            }

            onLoadMessagesRequested: function(conversationId, afterId) {
                networkManager.syncMessages(conversationId, afterId)
            }

            onSendMessageRequested: function(peerUserId, content) {
                networkManager.sendMessage(peerUserId, content)
            }

            onLogoutRequested: {
                mainWindow.logoutRequested()
            }
        }
    }

    // ── NetworkManager 聊天信号连接 ──────────────────────────────
    Connections {
        target: networkManager

        function onConversationsResult(conversations) {
            // 将 QJsonArray 转为 JS 数组
            var convs = []
            for (var i = 0; i < conversations.length; i++) {
                convs.push(conversations[i])
            }
            mainPage.updateConversations(convs)
        }

        function onMessagesSynced(conversationId, messages, hasMore) {
            if (conversationId === mainPage.currentConversationId) {
                var msgs = []
                for (var i = 0; i < messages.length; i++) {
                    msgs.push(messages[i])
                }
                mainPage.updateMessages(msgs)
            }
        }

        function onNewMessageReceived(message) {
            var convId = message.conversationId
            if (convId === mainPage.currentConversationId) {
                mainPage.appendMessage(message)
            }
            // 刷新会话列表
            networkManager.getConversations()
        }

        function onMessageSent(messageId, conversationId) {
            if (conversationId === mainPage.currentConversationId) {
                networkManager.syncMessages(conversationId, 0)
            }
            networkManager.getConversations()
        }

        function onMessageSendFailed(error) {
            console.log("Send failed:", error)
        }

        function onSearchUsersResult(users) {
            var userList = []
            for (var i = 0; i < users.length; i++) {
                userList.push(users[i])
            }
            mainPage.showSearchResults(userList)
        }
    }

    // 公共方法
    function loadConversations() {
        networkManager.getConversations()
    }
}
