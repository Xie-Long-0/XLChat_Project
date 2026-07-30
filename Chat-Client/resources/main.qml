import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QWindowKit

import "theme"
import "pages"
import "components"

ApplicationWindow {
    id: root
    width: 1000
    height: 650
    minimumWidth: 700
    minimumHeight: 450
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

        // 内容区域（StackView 页面切换）
        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true

            initialItem: loginPage

            // 页面切换动画
            pushEnter: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationSlow }
            }
            pushExit: Transition {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animationNormal }
            }
            popEnter: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationNormal }
            }
            popExit: Transition {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animationFast }
            }
        }
    }

    // ── 登录页面 ──────────────────────────────────────────────────
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

        onLoginSucceeded: {
            mainPage.myUserId = networkManager.userId
            mainPage.myUsername = networkManager.username
            stackView.push(mainPage)
            mainPage.loadConversationsRequested()
        }
    }

    // ── 主页面 ──────────────────────────────────────────────────
    MainPage {
        id: mainPage

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
            networkManager.logout()
            stackView.pop()
        }
    }

    // ── NetworkManager 信号连接 ──────────────────────────────────
    Connections {
        target: networkManager

        function onLoginSuccessful() {
            loginPage.onLoginSuccess()
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
}
