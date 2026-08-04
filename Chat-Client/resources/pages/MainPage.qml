import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"
import "../components"

Rectangle {
    id: mainPage
    color: Theme.windowBackground

    signal searchUsersRequested(string query)
    signal addContactRequested(int userId)
    signal loadConversationsRequested()
    signal loadMessagesRequested(int conversationId, int afterId)
    signal sendMessageRequested(int peerUserId, string content)
    signal logoutRequested()

    property int myUserId: 0
    property string myUsername: ""

    // 当前选中会话（currentConversationId=0 表示尚未在服务端创建的虚拟会话）
    property int currentConversationId: 0
    property int currentPeerUserId: 0
    property string currentPeerUsername: ""

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 左侧面板
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth
            color: Theme.sidebarBackground

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.separatorColor
            }

            ConversationList {
                id: convList
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: userBar.top

                onConversationClicked: function(index) {
                    var conv = convList.getConversation(index)
                    if (conv) {
                        mainPage.openConversation(conv)
                    }
                }

                onSearchClicked: {
                    searchDialog.open()
                }

                onRefreshClicked: {
                    loadConversationsRequested()
                }
            }

            // M4.5: 侧边栏底部用户信息栏（当前用户 + 登出入口）
            Rectangle {
                id: userBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 52
                color: Theme.sidebarBackground

                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separatorColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    // 当前用户头像
                    Rectangle {
                        width: Theme.avatarSizeSmall
                        height: Theme.avatarSizeSmall
                        radius: Theme.avatarSizeSmall / 2
                        color: Theme.primaryColor

                        Label {
                            anchors.centerIn: parent
                            text: mainPage.myUsername.length > 0 ? mainPage.myUsername[0].toUpperCase() : "?"
                            font.pixelSize: Theme.fontSizeMedium
                            font.weight: Font.Bold
                            color: Theme.textOnPrimary
                        }
                    }

                    // 用户名
                    Label {
                        Layout.fillWidth: true
                        text: mainPage.myUsername
                        font.pixelSize: Theme.fontSizeMedium
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    // 登出按钮
                    Rectangle {
                        id: logoutBtn
                        width: 36; height: 36
                        radius: 18
                        color: logoutMouse.containsMouse ? Theme.hoverColor : "transparent"

                        MouseArea {
                            id: logoutMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: mainPage.logoutRequested()
                        }

                        // 登出图标（门 + 箭头）
                        Canvas {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = Theme.textSecondary
                                ctx.lineWidth = 1.5
                                // 门框
                                ctx.beginPath()
                                ctx.moveTo(7, 2)
                                ctx.lineTo(2, 2)
                                ctx.lineTo(2, 14)
                                ctx.lineTo(7, 14)
                                ctx.stroke()
                                // 箭头
                                ctx.beginPath()
                                ctx.moveTo(6, 8)
                                ctx.lineTo(14, 8)
                                ctx.moveTo(11, 5)
                                ctx.lineTo(14, 8)
                                ctx.lineTo(11, 11)
                                ctx.stroke()
                            }
                        }
                    }
                }
            }
        }

        // 右侧聊天区域
        ChatView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true

            myUserId: mainPage.myUserId

            onSendMessage: function(content) {
                if (mainPage.currentPeerUserId > 0) {
                    mainPage.sendMessageRequested(mainPage.currentPeerUserId, content)
                }
            }
        }
    }

    // 搜索用户对话框
    Dialog {
        id: searchDialog
        title: "搜索用户"
        modal: true
        anchors.centerIn: parent
        width: 360
        padding: Theme.spacingLarge

        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.windowBackground
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: "输入用户名搜索"
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.textPrimary
            }

            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.inputHeight
                placeholderText: "用户名..."
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.textPrimary
                placeholderTextColor: Theme.inputPlaceholderColor
                selectByMouse: true
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.width: searchField.activeFocus ? 2 : 1
                    border.color: searchField.activeFocus ? Theme.inputFocusBorderColor : Theme.inputBorderColor
                }
                onAccepted: searchDialog.doSearch()
            }

            ListView {
                id: searchResults
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 200)
                clip: true
                visible: searchResultModel.count > 0
                model: ListModel { id: searchResultModel }

                delegate: Rectangle {
                    width: searchResults.width
                    height: 44
                    color: searchDelegateMouse.containsMouse ? Theme.hoverColor : "transparent"
                    radius: Theme.radiusSmall

                    MouseArea {
                        id: searchDelegateMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // M4.5: 点击搜索结果直接发起对话，同时后台添加联系人
                            mainPage.openChatWithUser(model.userId, model.username)
                            addContactRequested(model.userId)
                            searchDialog.close()
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium

                        Rectangle {
                            width: 32; height: 32; radius: 16
                            color: Theme.primaryColor
                            Label {
                                anchors.centerIn: parent
                                text: model.username.length > 0 ? model.username[0].toUpperCase() : "?"
                                font.pixelSize: Theme.fontSizeSmall
                                font.weight: Font.Bold
                                color: Theme.textOnPrimary
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: model.username + " (ID: " + model.userId + ")"
                            font.pixelSize: Theme.fontSizeMedium
                            color: Theme.textPrimary
                        }
                    }
                }
            }

            Label {
                id: searchStatus
                text: ""
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                visible: text !== ""
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    flat: true
                    font.pixelSize: Theme.fontSizeMedium
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: Theme.textSecondary
                    }
                    background: null
                    onClicked: searchDialog.close()
                }

                Button {
                    text: "搜索"
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: Font.DemiBold
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: searchDialogBtn.pressed ? Theme.loginButtonPressed
                             : (searchDialogBtn.hovered ? Theme.loginButtonHover : Theme.primaryColor)
                    }
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: Theme.textOnPrimary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    id: searchDialogBtn
                    onClicked: searchDialog.doSearch()
                }
            }
        }

        function doSearch() {
            var query = searchField.text.trim()
            if (query.length > 0) {
                searchStatus.text = "搜索中..."
                searchUsersRequested(query)
            }
        }

        function showResults(users) {
            searchResultModel.clear()
            if (users.length === 0) {
                searchStatus.text = "未找到匹配的用户"
                return
            }
            searchStatus.text = ""
            for (var i = 0; i < users.length; i++) {
                var u = users[i]
                searchResultModel.append({
                    userId: u.userId || 0,
                    username: u.username || ""
                })
            }
        }

        onClosed: {
            searchField.text = ""
            searchResultModel.clear()
            searchStatus.text = ""
        }
    }

    // ── 会话操作 ──
    // 打开一个既有会话
    function openConversation(conv) {
        currentConversationId = conv.conversationId
        currentPeerUserId = conv.peerUserId
        currentPeerUsername = conv.peerUsername

        chatView.peerUsername = conv.peerUsername
        chatView.chatTitle = conv.peerUsername
        chatView.hasConversation = true
        chatView.clearMessages()

        convList.setSelectedByConversationId(conv.conversationId)
        loadMessagesRequested(conv.conversationId, 0)
    }

    // M4.5: 与指定用户开始聊天；无既有会话时建立虚拟会话（conversationId=0），
    // 首条消息发送成功后由 bindNewConversation() 绑定服务端会话 ID
    function openChatWithUser(userId, username) {
        var existing = convList.findConversationByPeerId(userId)
        if (existing) {
            openConversation(existing)
            return
        }

        currentConversationId = 0
        currentPeerUserId = userId
        currentPeerUsername = username

        chatView.peerUsername = username
        chatView.chatTitle = username
        chatView.hasConversation = true
        chatView.clearMessages()

        convList.selectedIndex = -1
    }

    // M4.5: 首条消息发送成功后绑定服务端返回的会话 ID
    function bindNewConversation(conversationId) {
        if (currentConversationId === 0 && conversationId > 0) {
            currentConversationId = conversationId
            convList.setSelectedByConversationId(conversationId)
        }
    }

    // M4.5: 乐观插入“发送中”消息（由 MainWindow 在调用 networkManager.sendMessage 后回调）
    function trackOutgoingMessage(clientMessageId, content) {
        chatView.appendOptimisticMessage(clientMessageId, content)
        if (currentConversationId > 0) {
            var now = new Date()
            var hh = now.getHours()
            var mm = now.getMinutes()
            var timeStr = (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm
            convList.updateForNewMessage(currentConversationId, content, timeStr, false)
        }
    }

    // 公共方法
    function updateConversations(conversations) {
        convList.updateConversations(conversations)
        // 会话刷新后恢复当前选中高亮
        if (currentConversationId > 0) {
            convList.setSelectedByConversationId(currentConversationId)
        }
    }

    function updateMessages(messages) {
        chatView.setMessages(messages)
    }

    function appendMessage(msg) {
        chatView.appendMessage(msg)
    }

    // M4.5: 服务端确认后更新乐观消息
    function confirmMessage(clientMessageId, messageId) {
        chatView.confirmOptimisticMessage(clientMessageId, messageId)
    }

    // M4.5: 消息状态推送（已送达/已读）
    function updateMessageStatus(messageId, status) {
        chatView.updateMessageStatus(messageId, status)
    }

    // M4.5: 新消息到达但当前未打开该会话时，本地更新预览与未读角标
    function updateConversationPreview(message) {
        var now = new Date()
        var hh = now.getHours()
        var mm = now.getMinutes()
        var timeStr = (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm
        var preview = message.senderUsername ? message.senderUsername + ": " + message.content
                                             : message.content
        convList.updateForNewMessage(message.conversationId, preview, timeStr, true)
    }

    // M4.5: 当前会话中最后一条对方消息 ID（用于发送已读回执）
    function lastIncomingMessageId() {
        return chatView.lastIncomingMessageId()
    }

    function showSearchResults(users) {
        searchDialog.showResults(users)
    }

    // M4.5: 登出时重置界面状态
    function resetUi() {
        currentConversationId = 0
        currentPeerUserId = 0
        currentPeerUsername = ""
        chatView.hasConversation = false
        chatView.peerUsername = ""
        chatView.chatTitle = ""
        chatView.clearMessages()
        convList.reset()
    }
}
