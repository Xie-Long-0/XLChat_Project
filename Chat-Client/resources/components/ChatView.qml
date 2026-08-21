import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

Rectangle {
    id: chatView
    color: Theme.chatBackground

    property string peerUsername: ""
    property string chatTitle: ""
    property bool hasConversation: false
    // M4.5: 当前登录用户 ID，用于判断消息归属
    property int myUserId: 0

    signal sendMessage(string content)
    signal backClicked()

    // 顶部标题栏
    Rectangle {
        id: chatHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: Theme.windowBackground

        Rectangle {
            anchors.bottom: parent.bottom
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

            // 返回按钮（窄屏时使用）
            Rectangle {
                id: backBtn
                width: 36; height: 36
                radius: 18
                visible: chatView.width < 600
                color: backMouse.containsMouse ? Theme.hoverColor : "transparent"

                MouseArea {
                    id: backMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chatView.backClicked()
                }

                Canvas {
                    anchors.centerIn: parent
                    width: 12; height: 12
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.textSecondary
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(width, 0)
                        ctx.lineTo(0, height / 2)
                        ctx.lineTo(width, height)
                        ctx.stroke()
                    }
                }
            }

            // 头像
            Rectangle {
                width: Theme.avatarSizeSmall
                height: Theme.avatarSizeSmall
                radius: Theme.avatarSizeSmall / 2
                color: Theme.primaryColor
                visible: hasConversation

                Label {
                    anchors.centerIn: parent
                    text: peerUsername.length > 0 ? peerUsername[0].toUpperCase() : "?"
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: Font.Bold
                    color: Theme.textOnPrimary
                }
            }

            // 标题
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                visible: hasConversation

                Label {
                    text: chatTitle
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }

            Item {
                Layout.fillWidth: !hasConversation
            }
        }
    }

    // 消息列表（直接用 ListView 作为滚动容器，ScrollView 不暴露 contentY）
    ListView {
        id: messageListView
        anchors.top: chatHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: messageInput.top
        anchors.topMargin: Theme.spacingSmall
        anchors.bottomMargin: Theme.spacingSmall
        clip: true
        spacing: Theme.spacingXSmall
        verticalLayoutDirection: ListView.TopToBottom
        model: ListModel { id: msgModel }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // M4.5: delegate 内直接条件实例化（不用 Loader，避免 model 角色
        // 在 Loader 加载组件内绑定失效导致气泡空白）
        delegate: Column {
            width: messageListView.width

            // 日期分隔线
            Item {
                width: parent.width
                height: 32
                visible: model.isDivider

                Rectangle {
                    anchors.centerIn: parent
                    height: 22
                    width: dividerLabel.implicitWidth + Theme.spacingLarge * 2
                    radius: 11
                    color: Theme.dateDividerColor

                    Label {
                        id: dividerLabel
                        anchors.centerIn: parent
                        text: model.dividerText
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.DemiBold
                        color: Theme.dateDividerTextColor
                    }
                }
            }

            // 消息气泡
            MessageBubble {
                visible: !model.isDivider
                width: parent.width
                isMine: model.isMine
                senderName: model.senderUsername
                content: model.content
                time: model.displayTime
                status: model.status || ""
                undecryptable: model.undecryptable === true
            }
        }

        // 新消息动画
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationNormal }
            NumberAnimation { property: "y"; from: y + 20; duration: Theme.animationNormal }
        }

        // 内容高度变化时若已贴近底部则自动跟随（新消息到达场景）
        onContentHeightChanged: {
            if (stayAtBottom && contentHeight > height) {
                programmaticScroll = true
                contentY = contentHeight - height - Theme.spacingSmall * 2
                Qt.callLater(function() { chatView.programmaticScroll = false })
            }
        }

        // 用户手动滚动时暂停自动贴底，避免新消息把视图拽回去
        onMovementStarted: {
            if (!chatView.programmaticScroll) {
                chatView.stayAtBottom = false
            }
        }
    }

    // 是否保持贴底（打开会话/发送或接收消息后置 true）
    property bool stayAtBottom: false
    // 程序化滚动标志（区分用户手动滚动）
    property bool programmaticScroll: false

    // 滚动到底部定时器：等待新 delegate 完成布局后再滚动
    Timer {
        id: scrollTimer
        interval: 50
        repeat: false
        onTriggered: {
            chatView.stayAtBottom = true
            chatView.programmaticScroll = true
            if (messageListView.contentHeight > messageListView.height) {
                messageListView.contentY = messageListView.contentHeight - messageListView.height - Theme.spacingSmall * 2
            } else {
                messageListView.contentY = 0
            }
            Qt.callLater(function() { chatView.programmaticScroll = false })
        }
    }

    // 消息输入框
    MessageInput {
        id: messageInput
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: hasConversation
        onMessageSent: function(text) {
            chatView.sendMessage(text)
        }
    }

    // 空状态
    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingMedium
        visible: !hasConversation

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 80; height: 80
            radius: 40
            color: Theme.primaryLightColor

            Label {
                anchors.centerIn: parent
                text: "💬"
                font.pixelSize: 32
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "选择一个会话开始聊天"
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.textSecondary
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "或搜索用户发起新对话"
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textTertiary
        }
    }

    // ── 时间格式化辅助 ──
    function parseDate(createdAt) {
        var d = new Date(createdAt)
        if (isNaN(d.getTime())) {
            d = new Date()
        }
        return d
    }

    function formatTime(createdAt) {
        var d = parseDate(createdAt)
        var hh = d.getHours()
        var mm = d.getMinutes()
        return (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm
    }

    function dateKey(createdAt) {
        var d = parseDate(createdAt)
        return d.getFullYear() + "-" + d.getMonth() + "-" + d.getDate()
    }

    function formatDividerText(createdAt) {
        var d = parseDate(createdAt)
        var now = new Date()
        var today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        var that = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        var diffDays = Math.round((today.getTime() - that.getTime()) / 86400000)
        if (diffDays === 0) return "今天"
        if (diffDays === 1) return "昨天"
        return d.getFullYear() + "年" + (d.getMonth() + 1) + "月" + d.getDate() + "日"
    }

    // 若与上一条消息不在同一天，先插入日期分隔线
    function ensureDivider(createdAt) {
        var key = dateKey(createdAt)
        var lastKey = ""
        for (var i = msgModel.count - 1; i >= 0; i--) {
            var item = msgModel.get(i)
            if (!item.isDivider) {
                lastKey = item.dateKeyStr || ""
                break
            }
        }
        if (lastKey !== key) {
            msgModel.append({
                isDivider: true,
                dividerText: formatDividerText(createdAt),
                dateKeyStr: key,
                messageId: 0, clientMessageId: "", senderId: 0,
                senderUsername: "", content: "", contentType: "text",
                createdAt: "", displayTime: "", status: "", isMine: false,
                undecryptable: false
            })
        }
    }

    function makeMessageEntry(msg) {
        return {
            isDivider: false,
            dividerText: "",
            dateKeyStr: dateKey(msg.createdAt || ""),
            messageId: msg.messageId || 0,
            clientMessageId: msg.clientMessageId || "",
            senderId: msg.senderId || 0,
            senderUsername: msg.senderUsername || "",
            content: msg.content || "",
            contentType: msg.contentType || "text",
            createdAt: msg.createdAt || "",
            displayTime: formatTime(msg.createdAt || ""),
            status: msg.status || "",
            isMine: (msg.senderId == chatView.myUserId),
            // M6: 无法解密的端到端加密消息显示占位样式
            undecryptable: msg.undecryptable === true
        }
    }

    // ── 公共方法 ──
    function setMessages(messages) {
        msgModel.clear()
        for (var i = 0; i < messages.length; i++) {
            var msg = messages[i]
            ensureDivider(msg.createdAt || "")
            msgModel.append(makeMessageEntry(msg))
        }
        scrollToBottom()
    }

    function appendMessage(msg) {
        ensureDivider(msg.createdAt || "")
        msgModel.append(makeMessageEntry(msg))
        scrollToBottom()
    }

    // M4.5: 乐观插入“发送中”消息（本地立即展示）
    function appendOptimisticMessage(clientMessageId, content) {
        var now = new Date().toISOString()
        ensureDivider(now)
        msgModel.append({
            isDivider: false,
            dividerText: "",
            dateKeyStr: dateKey(now),
            messageId: 0,
            clientMessageId: clientMessageId,
            senderId: chatView.myUserId,
            senderUsername: "",
            content: content,
            contentType: "text",
            createdAt: now,
            displayTime: formatTime(now),
            status: "sending",
            isMine: true,
            undecryptable: false
        })
        scrollToBottom()
    }

    // M4.5: 服务端确认后，以幂等键定位乐观消息并更新为已发送
    function confirmOptimisticMessage(clientMessageId, messageId) {
        for (var i = 0; i < msgModel.count; i++) {
            var item = msgModel.get(i)
            if (!item.isDivider && item.clientMessageId === clientMessageId) {
                msgModel.setProperty(i, "messageId", messageId)
                msgModel.setProperty(i, "status", "sent")
                return
            }
        }
    }

    // M4.5: 收到 MessageStatusUpdate 推送后更新气泡状态
    function updateMessageStatus(messageId, status) {
        for (var i = 0; i < msgModel.count; i++) {
            var item = msgModel.get(i)
            if (!item.isDivider && item.messageId === messageId) {
                msgModel.setProperty(i, "status", status)
                return
            }
        }
    }

    // M4.5: 返回当前列表中最后一条对方消息的 ID（用于已读回执）
    function lastIncomingMessageId() {
        for (var i = msgModel.count - 1; i >= 0; i--) {
            var item = msgModel.get(i)
            if (!item.isDivider && !item.isMine && item.messageId > 0) {
                return item.messageId
            }
        }
        return 0
    }

    function scrollToBottom() {
        scrollTimer.restart()
    }

    function clearMessages() {
        chatView.stayAtBottom = false
        msgModel.clear()
    }
}
