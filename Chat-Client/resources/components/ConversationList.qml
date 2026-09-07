import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

Rectangle {
    id: conversationList
    color: Theme.sidebarBackground

    signal conversationClicked(int index)
    signal searchClicked()
    signal refreshClicked()
    // M7a: 打开建群对话框
    signal createGroupClicked()
    // M9 特性栈：右键菜单设置会话偏好（置顶/免打扰）
    signal conversationPrefsRequested(int conversationId, bool pinned, bool muted)

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

            // M7a: 建群按钮
            Rectangle {
                id: createGroupBtn
                width: 36; height: 36
                radius: 18
                color: createGroupMouse.containsMouse ? Theme.hoverColor : "transparent"

                MouseArea {
                    id: createGroupMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: conversationList.createGroupClicked()
                }

                // 加号图标
                Canvas {
                    anchors.centerIn: parent
                    width: 14; height: 14
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.textSecondary
                        ctx.lineWidth = 1.8
                        ctx.beginPath()
                        ctx.moveTo(7, 1)
                        ctx.lineTo(7, 13)
                        ctx.moveTo(1, 7)
                        ctx.lineTo(13, 7)
                        ctx.stroke()
                    }
                }
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
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        convContextMenu.popup()
                    } else {
                        conversationList.conversationClicked(index)
                    }
                }
            }

            // M9 特性栈：会话右键菜单（置顶/免打扰）
            Menu {
                id: convContextMenu
                MenuItem {
                    text: model.pinned === true ? "取消置顶" : "置顶会话"
                    onTriggered: conversationList.conversationPrefsRequested(
                        model.conversationId, model.pinned !== true, model.muted === true)
                }
                MenuItem {
                    text: model.muted === true ? "取消免打扰" : "开启免打扰"
                    onTriggered: conversationList.conversationPrefsRequested(
                        model.conversationId, model.pinned === true, model.muted !== true)
                }
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

                // 头像（按用户/会话 ID 取色，同一会话颜色稳定；避免 delegate
                // 移除时 index 为 undefined 导致 "Unable to assign [undefined] to QColor"）
                Rectangle {
                    width: Theme.avatarSize
                    height: Theme.avatarSize
                    // M7a: 群会话用圆角方形头像区分
                    radius: model.type === "group" ? Theme.radiusMedium : Theme.avatarSize / 2
                    color: {
                        var colors = ["#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEAA7", "#DDA0DD", "#98D8C8"]
                        var id = model.type === "group" ? (model.conversationId || 0)
                                                        : (model.peerUserId || 0)
                        return colors[id % colors.length]
                    }

                    Label {
                        anchors.centerIn: parent
                        text: model.displayName.length > 0 ? model.displayName[0].toUpperCase() : "?"
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
                            text: model.displayName
                            font.pixelSize: Theme.fontSizeMedium
                            font.weight: Font.DemiBold
                            color: delegateItem.isSelected ? Theme.selectedConversationTextColor : Theme.textPrimary
                            elide: Text.ElideRight
                        }

                        // M7a: 群成员数标识
                        Label {
                            visible: model.type === "group"
                            text: model.memberCount + "人"
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: delegateItem.isSelected
                                 ? Theme.selectedConversationSecondaryColor
                                 : Theme.textTertiary
                        }

                        // M9 特性栈：免打扰标识
                        Label {
                            visible: model.muted === true
                            text: "🔕"
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: delegateItem.isSelected
                                 ? Theme.selectedConversationSecondaryColor
                                 : Theme.textTertiary
                        }

                        Label {
                            text: model.lastMessageTime || ""
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: delegateItem.isSelected
                                 ? Theme.selectedConversationSecondaryColor
                                 : (model.unreadCount > 0 ? Theme.primaryColor : Theme.textTertiary)
                        }

                        // M9 特性栈：置顶标识
                        Label {
                            visible: model.pinned === true
                            text: "📌"
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: delegateItem.isSelected
                                 ? Theme.selectedConversationSecondaryColor
                                 : Theme.primaryColor
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

    // 将 conversationId 统一为 Number，避免 QJsonArray/QML 模型中 number/string 混用导致 === 匹配失败
    function normalizeConversationId(id) {
        var n = Number(id)
        return isNaN(n) ? 0 : n
    }

    function findIndexByConversationId(conversationId) {
        var target = normalizeConversationId(conversationId)
        for (var i = 0; i < convModel.count; i++) {
            if (normalizeConversationId(convModel.get(i).conversationId) === target) {
                return i
            }
        }
        return -1
    }

    function updateConversations(conversations) {
        if (!conversations || conversations.length === undefined) {
            return
        }
        var seen = {}
        for (var i = 0; i < conversations.length; i++) {
            var conv = conversations[i]
            if (!conv || conv.conversationId === undefined) {
                continue
            }
            var convId = normalizeConversationId(conv.conversationId)
            seen[convId] = true
            var type = conv.type || "private"
            var entry = {
                conversationId: convId,
                type: type,
                peerUserId: normalizeConversationId(conv.peerUserId),
                peerUsername: conv.peerUsername || "",
                // M7a: 群会话显示群名与成员数
                name: conv.name || "",
                memberCount: conv.memberCount || 0,
                displayName: type === "group" ? (conv.name || "未命名群组")
                                              : (conv.peerUsername || ""),
                lastMessage: conv.lastMessage || "",
                lastMessageTime: formatConvTime(conv.lastMessageAt || ""),
                unreadCount: conv.unreadCount || 0,
                // M9 特性栈：会话偏好（置顶/免打扰）
                pinned: conv.pinned === true,
                muted: conv.muted === true
            }
            var pos = findIndexByConversationId(convId)
            if (pos >= 0) {
                convModel.set(pos, entry)
            } else {
                convModel.append(entry)
            }
        }
        // 移除服务端已不存在的会话（退群/被移出后列表同步消失）
        for (var j = convModel.count - 1; j >= 0; j--) {
            if (!(normalizeConversationId(convModel.get(j).conversationId) in seen)) {
                // Qt 部分版本 ListModel.remove 要求显式 count
                convModel.remove(j, 1)
            }
        }
        // 按服务端顺序重排当前列表（插入排序式单步移动，k 递增且 move 目标 <= k 可保证正确性）
        for (var k = 0; k < conversations.length; k++) {
            var convK = conversations[k]
            if (!convK || convK.conversationId === undefined) {
                continue
            }
            var idx = findIndexByConversationId(normalizeConversationId(convK.conversationId))
            if (idx >= 0 && idx !== k) {
                // Qt 部分版本 ListModel.move 要求三个参数，显式传入 count=1
                convModel.move(idx, k, 1)
            }
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
        var target = normalizeConversationId(conversationId)
        selectedIndex = -1
        for (var i = 0; i < convModel.count; i++) {
            if (normalizeConversationId(convModel.get(i).conversationId) === target) {
                selectedIndex = i
                return
            }
        }
    }

    // M4.5: 按对方用户 ID 查找既有会话（搜索发起对话时复用）
    function findConversationByPeerId(peerUserId) {
        var target = normalizeConversationId(peerUserId)
        for (var i = 0; i < convModel.count; i++) {
            if (normalizeConversationId(convModel.get(i).peerUserId) === target) {
                return convModel.get(i)
            }
        }
        return null
    }

    // M4.5: 新消息到达时本地更新预览与未读角标（当前打开的会话不计未读）
    function updateForNewMessage(conversationId, preview, timeStr, incrementUnread) {
        var target = normalizeConversationId(conversationId)
        for (var i = 0; i < convModel.count; i++) {
            if (normalizeConversationId(convModel.get(i).conversationId) === target) {
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

    // M9 特性栈：本地应用会话偏好（服务端推送 conversation_prefs 后回填），
    // 置顶变更时按 pinned DESC 稳定重排（置顶在前，保持各自相对顺序）
    function applyPrefs(conversationId, pinned, muted) {
        var idx = findIndexByConversationId(conversationId)
        if (idx < 0) {
            return
        }
        convModel.setProperty(idx, "pinned", pinned === true)
        convModel.setProperty(idx, "muted", muted === true)
        reorderByPinned()
    }

    // 稳定分区：置顶会话在前、未置顶在后，各自保持原有相对顺序（单步 move 升序移动）
    function reorderByPinned() {
        var order = []
        // 先收集置顶
        for (var i = 0; i < convModel.count; i++) {
            if (convModel.get(i).pinned === true) {
                order.push(normalizeConversationId(convModel.get(i).conversationId))
            }
        }
        // 再收集未置顶
        for (var j = 0; j < convModel.count; j++) {
            if (convModel.get(j).pinned !== true) {
                order.push(normalizeConversationId(convModel.get(j).conversationId))
            }
        }
        for (var k = 0; k < order.length; k++) {
            var cur = normalizeConversationId(convModel.get(k).conversationId)
            if (cur !== order[k]) {
                var target = findIndexByConversationId(order[k])
                if (target > k) {
                    convModel.move(target, k, 1)
                }
            }
        }
    }
}
