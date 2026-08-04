import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QWindowKit

import "../theme"
import "../components"

Window {
    id: mainWindow
    // M4.5 修复：作为独立根窗口由 main.cpp 加载，供其按 objectName 查找并注入登录窗口
    objectName: "mainWindow"
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
                // M4.5: 发送后以返回的幂等键跟踪乐观消息气泡
                var clientMessageId = networkManager.sendMessage(peerUserId, content)
                mainPage.trackOutgoingMessage(clientMessageId, content)
            }

            onLogoutRequested: {
                mainPage.resetUi()
                mainWindow.logoutRequested()
            }
        }
    }

    // ── NetworkManager 聊天信号连接 ──
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
                // M4.5: 打开会话时对最后一条对方消息发送已读回执
                sendReadAck()
            }
        }

        function onNewMessageReceived(message) {
            var convId = message.conversationId
            if (convId === mainPage.currentConversationId) {
                mainPage.appendMessage(message)
                // M4.5: 会话打开期间收到新消息，发送已读回执
                var msgId = message.messageId || 0
                if (msgId > 0) {
                    networkManager.ackMessage(msgId, "read")
                }
            } else {
                // M4.5: 未打开的会话本地更新预览与未读角标
                mainPage.updateConversationPreview(message)
            }
            // 以服务端为准刷新会话列表（含未读计数）
            networkManager.getConversations()
        }

        function onMessageSent(messageId, conversationId, clientMessageId) {
            // M4.5: 首条消息成功后绑定服务端会话 ID，并确认乐观消息
            mainPage.bindNewConversation(conversationId)
            mainPage.confirmMessage(clientMessageId, messageId)
            networkManager.getConversations()
        }

        function onMessageSendFailed(error) {
            console.log("Send failed:", error)
        }

        // M4.5: 消息状态推送（已送达/已读）实时更新气泡状态
        function onMessageStatusChanged(messageId, status) {
            mainPage.updateMessageStatus(messageId, status)
        }

        function onSearchUsersResult(users) {
            var userList = []
            for (var i = 0; i < users.length; i++) {
                userList.push(users[i])
            }
            mainPage.showSearchResults(userList)
        }
    }

    // M4.5: 对当前会话中最后一条对方消息发送已读回执
    function sendReadAck() {
        var lastIncomingId = mainPage.lastIncomingMessageId()
        if (lastIncomingId > 0) {
            networkManager.ackMessage(lastIncomingId, "read")
        }
    }

    // 公共方法
    function loadConversations() {
        networkManager.getConversations()
    }
}
