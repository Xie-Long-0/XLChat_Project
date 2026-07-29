import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "qrc:/theme"

Rectangle {
    id: chatView
    color: Theme.chatBackground

    property string peerUsername: ""
    property string chatTitle: ""
    property bool hasConversation: false

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
            Label {
                Layout.fillWidth: true
                text: chatTitle
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
                visible: hasConversation
            }

            Item {
                Layout.fillWidth: !hasConversation
            }
        }
    }

    // 消息列表
    ScrollView {
        id: messageScrollView
        anchors.top: chatHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: messageInput.top
        clip: true
        contentWidth: availableWidth

        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ListView {
            id: messageListView
            width: parent.width
            height: contentHeight
            spacing: Theme.spacingXSmall
            verticalLayoutDirection: ListView.TopToBottom
            model: ListModel { id: msgModel }

            delegate: MessageBubble {
                width: messageListView.width
                isMine: model.isMine
                senderName: model.senderUsername
                content: model.content
                time: model.createdAt
                status: model.status || ""
            }

            // 新消息动画
            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationNormal }
                NumberAnimation { property: "y"; from: y + 20; duration: Theme.animationNormal }
            }
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

    // 公共方法
    function setMessages(messages, myUserId) {
        msgModel.clear()
        for (var i = 0; i < messages.length; i++) {
            var msg = messages[i]
            msgModel.append({
                messageId: msg.messageId || 0,
                senderId: msg.senderId || 0,
                senderUsername: msg.senderUsername || "",
                content: msg.content || "",
                contentType: msg.contentType || "text",
                createdAt: msg.createdAt || "",
                status: msg.status || "",
                isMine: (msg.senderId == myUserId)
            })
        }
        scrollToBottom()
    }

    function appendMessage(msg, myUserId) {
        msgModel.append({
            messageId: msg.messageId || 0,
            senderId: msg.senderId || 0,
            senderUsername: msg.senderUsername || "",
            content: msg.content || "",
            contentType: msg.contentType || "text",
            createdAt: msg.createdAt || "",
            status: msg.status || "",
            isMine: (msg.senderId == myUserId)
        })
        scrollToBottom()
    }

    function scrollToBottom() {
        Qt.callLater(function() {
            messageScrollView.contentY = messageScrollView.contentHeight - messageScrollView.height
        })
    }

    function clearMessages() {
        msgModel.clear()
    }
}
