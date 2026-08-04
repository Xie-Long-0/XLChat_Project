import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "qrc:/theme"

Rectangle {
    id: conversationList
    color: Theme.sidebarBackground

    signal conversationClicked(int index)
    signal searchClicked()
    signal refreshClicked()

    // M4.5: 当前选中会话索引（修复原先错误的判断条件）
    property int selectedIndex: -1

    // 顶部工具栏
    Rectangle {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: Theme.sidebarBackground

        // 底部分隔线
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

            // 搜索按钮
            Rectangle {
                id: searchBtn
                width: 36; height: 36
                radius: 18
                color: searchMouse.containsMouse ? Theme.hoverColor : "transparent"

                MouseArea {
                    id: searchMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: conversationList.searchClicked()
                }

                // 搜索图标（放大镜）
                Canvas {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.textSecondary
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        ctx.arc(6, 6, 5, 0, Math.PI * 2)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(10, 10)
                        ctx.lineTo(15, 15)
                        ctx.stroke()
                    }
                }
            }

            // 标题
            Label {
                Layout.fillWidth: true
                text: "会话"
                font.pixelSize: Theme.fontSizeXLarge
                font.weight: Font.Bold
                color: Theme.textPrimary
            }

            // 刷新按钮
            Rectangle {
                id: refreshBtn
                width: 36; height: 36
                radius: 18
                color: refreshMouse.containsMouse ? Theme.hoverColor : "transparent"

                MouseArea {
                    id: refreshMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: conversationList.refreshClicked()
                }

                // 刷新图标
                Canvas {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.textSecondary
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        ctx.arc(8, 8, 6, 0.3, Math.PI * 1.7)
                        ctx.stroke()
                        // 箭头
                        ctx.beginPath()
                        ctx.moveTo(13, 3)
                        ctx.lineTo(14, 7)
                        ctx.lineTo(10, 5)
                        ctx.closePath()
                        ctx.fillStyle = Theme.textSecondary
                        ctx.fill()
                    }
                }
            }
        }
    }

    // 会话列表
    ListView {
        id: listView
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        model: ListModel { id: convModel }

        delegate: Rectangle {
            id: delegateItem
            width: listView.width
            height: Theme.conversationItemHeight
            property bool isSelected: index === conversationList.selectedIndex
            color: isSelected ? Theme.selectedConversationColor
                 : (delegateMouse.pressed ? Theme.pressedColor
                    : (delegateMouse.containsMouse ? Theme.hoverColor : "transparent"))

            Behavior on color { ColorAnimation { duration: Theme.animationFast } }

            MouseArea {
                id: delegateMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: conversationList.conversationClicked(index)
            }

            // 底部分隔线
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.leftMargin: Theme.avatarSize + Theme.spacingMedium * 2
                anchors.right: parent.right
                height: 1
                color: Theme.dividerColor
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSmall

                // 头像（按用户 ID 取色，同一用户颜色稳定；避免 delegate 移除时
                // index 为 undefined 导致 "Unable to assign [undefined] to QColor"）
                Rectangle {
                    width: Theme.avatarSize
                    height: Theme.avatarSize
                    radius: Theme.avatarSize / 2
                    color: {
                        var colors = ["#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEAA7", "#DDA0DD", "#98D8C8"]
                        var id = model.peerUserId || 0
                        return colors[id % colors.length]
                    }

                    Label {
                        anchors.centerIn: parent
                        text: model.peerUsername.length > 0 ? model.peerUsername[0].toUpperCase() : "?"
                        font.pixelSize: Theme.fontSizeXLarge
                        font.weight: Font.Bold
                        color: Theme.textOnPrimary
                    }
                }

                // 信息区域
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingXSmall

                    // 名称行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: model.peerUsername
                            font.pixelSize: Theme.fontSizeMedium
                            font.weight: Font.DemiBold
                            color: delegateItem.isSelected ? Theme.selectedConversationTextColor : Theme.textPrimary
                            elide: Text.ElideRight
                        }

                        Label {
                            text: model.lastMessageTime || ""
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: delegateItem.isSelected
                                 ? Theme.selectedConversationSecondaryColor
                                 : (model.unreadCount > 0 ? Theme.primaryColor : Theme.textTertiary)
                        }
                    }

                    // 消息预览行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: model.lastMessage || ""
                            font.pixelSize: Theme.fontSizeSmall
                            color: delegateItem.isSelected ? Theme.selectedConversationSecondaryColor : Theme.textSecondary
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        // 未读角标
                        Rectangle {
                            visible: model.unreadCount > 0
                            width: unreadLabel.implicitWidth + 10
                            height: 20
                            radius: 10
                            color: Theme.unreadBadgeColor

                            Label {
                                id: unreadLabel
                                anchors.centerIn: parent
                                text: model.unreadCount > 99 ? "99+" : model.unreadCount
                                font.pixelSize: Theme.fontSizeSmall - 1
                                font.weight: Font.DemiBold
                                color: Theme.unreadBadgeTextColor
                            }
                        }
                    }
                }
            }
        }

        // 空状态提示
        Label {
            anchors.centerIn: parent
            text: "暂无会话\n搜索用户开始聊天"
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeMedium
            color: Theme.textTertiary
            visible: convModel.count === 0
        }
    }

    // 公共方法
    // M4.5: 会话时间格式化（服务端字段为 ISO 时间 lastMessageAt）
    function formatConvTime(isoStr) {
        if (!isoStr || isoStr.length === 0) return ""
        var d = new Date(isoStr)
        if (isNaN(d.getTime())) return ""
        var now = new Date()
        if (d.toDateString() === now.toDateString()) {
            var hh = d.getHours()
            var mm = d.getMinutes()
            return (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm
        }
        return d.getFullYear() + "/" + (d.getMonth() + 1) + "/" + d.getDate()
    }

    function updateConversations(conversations) {
        convModel.clear()
        for (var i = 0; i < conversations.length; i++) {
            var conv = conversations[i]
            convModel.append({
                conversationId: conv.conversationId || 0,
                peerUserId: conv.peerUserId || 0,
                peerUsername: conv.peerUsername || "",
                lastMessage: conv.lastMessage || "",
                lastMessageTime: formatConvTime(conv.lastMessageAt || ""),
                unreadCount: conv.unreadCount || 0
            })
        }
    }

    function getConversation(index) {
        if (index >= 0 && index < convModel.count) {
            return convModel.get(index)
        }
        return null
    }

    // M4.5: 按会话 ID 选中（会话刷新后恢复高亮）
    function setSelectedByConversationId(conversationId) {
        selectedIndex = -1
        for (var i = 0; i < convModel.count; i++) {
            if (convModel.get(i).conversationId === conversationId) {
                selectedIndex = i
                return
            }
        }
    }

    // M4.5: 按对方用户 ID 查找既有会话（搜索发起对话时复用）
    function findConversationByPeerId(peerUserId) {
        for (var i = 0; i < convModel.count; i++) {
            if (convModel.get(i).peerUserId === peerUserId) {
                return convModel.get(i)
            }
        }
        return null
    }

    // M4.5: 新消息到达时本地更新预览与未读角标（当前打开的会话不计未读）
    function updateForNewMessage(conversationId, preview, timeStr, incrementUnread) {
        for (var i = 0; i < convModel.count; i++) {
            if (convModel.get(i).conversationId === conversationId) {
                convModel.setProperty(i, "lastMessage", preview)
                convModel.setProperty(i, "lastMessageTime", timeStr)
                if (incrementUnread) {
                    convModel.setProperty(i, "unreadCount", convModel.get(i).unreadCount + 1)
                }
                return
            }
        }
    }

    // M4.5: 清空选中与列表（登出时）
    function reset() {
        convModel.clear()
        selectedIndex = -1
    }
}
