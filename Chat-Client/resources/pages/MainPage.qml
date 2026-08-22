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
    // M7a: 群组操作信号
    signal loadContactsRequested()
    signal createGroupRequested(string name, var memberIds)
    signal inviteGroupMembersRequested(int conversationId, var userIds)
    signal leaveGroupRequested(int conversationId)
    signal kickGroupMemberRequested(int conversationId, int userId)
    signal getGroupInfoRequested(int conversationId)
    signal sendGroupMessageRequested(int conversationId, string content)

    property int myUserId: 0
    property string myUsername: ""

    // 当前选中会话（currentConversationId=0 表示尚未在服务端创建的虚拟会话）
    property int currentConversationId: 0
    property int currentPeerUserId: 0
    property string currentPeerUsername: ""
    // M7a: 当前会话类型（"private"/"group"）
    property string currentConversationType: "private"
    // M7a: 当前群成员数（群会话头部副标题）
    property int currentGroupMemberCount: 0
    // M7a: 用户搜索用途路由（"chat" 发起对话 / "invite" 群邀请）
    property string searchMode: "chat"

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
                    searchMode = "chat"
                    searchDialog.open()
                }

                onRefreshClicked: {
                    loadConversationsRequested()
                }

                // M7a: 建群入口
                onCreateGroupClicked: {
                    loadContactsRequested()
                    createGroupDialog.open()
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
            // M7a: 群会话状态绑定
            isGroup: mainPage.currentConversationType === "group"
            groupMemberCount: mainPage.currentGroupMemberCount

            onSendMessage: function(content) {
                // M7a: 群聊与私聊发送分流
                if (mainPage.currentConversationType === "group") {
                    if (mainPage.currentConversationId > 0) {
                        mainPage.sendGroupMessageRequested(mainPage.currentConversationId, content)
                    }
                } else if (mainPage.currentPeerUserId > 0) {
                    mainPage.sendMessageRequested(mainPage.currentPeerUserId, content)
                }
            }

            // M7a: 打开群信息对话框
            onGroupInfoRequested: {
                if (mainPage.currentConversationId > 0) {
                    mainPage.getGroupInfoRequested(mainPage.currentConversationId)
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

    // M7a: 建群对话框（群名 + 联系人多选）
    Dialog {
        id: createGroupDialog
        title: "新建群组"
        modal: true
        anchors.centerIn: parent
        width: 380
        padding: Theme.spacingLarge

        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.windowBackground
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            TextField {
                id: groupNameField
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.inputHeight
                placeholderText: "群名称（1-64 字符）"
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.textPrimary
                placeholderTextColor: Theme.inputPlaceholderColor
                selectByMouse: true
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.width: groupNameField.activeFocus ? 2 : 1
                    border.color: groupNameField.activeFocus ? Theme.inputFocusBorderColor : Theme.inputBorderColor
                }
            }

            Label {
                text: "选择初始成员（联系人）"
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                visible: contactModel.count > 0
            }

            ListView {
                id: contactList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 220)
                clip: true
                visible: contactModel.count > 0
                model: ListModel { id: contactModel }

                delegate: Rectangle {
                    width: contactList.width
                    height: 40
                    color: contactDelegateMouse.containsMouse ? Theme.hoverColor : "transparent"
                    radius: Theme.radiusSmall

                    MouseArea {
                        id: contactDelegateMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: contactModel.setProperty(index, "selected", !model.selected)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium

                        Label {
                            text: model.selected ? "☑" : "☐"
                            font.pixelSize: Theme.fontSizeMedium
                            color: model.selected ? Theme.primaryColor : Theme.textTertiary
                        }

                        Label {
                            Layout.fillWidth: true
                            text: model.username + " (ID: " + model.userId + ")"
                            font.pixelSize: Theme.fontSizeMedium
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Label {
                text: "暂无联系人，可先搜索添加联系人"
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textTertiary
                visible: contactModel.count === 0
            }

            Label {
                id: createGroupStatus
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
                    onClicked: createGroupDialog.close()
                }

                Button {
                    id: createGroupBtn
                    text: "创建"
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: Font.DemiBold
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: createGroupBtn.pressed ? Theme.loginButtonPressed
                             : (createGroupBtn.hovered ? Theme.loginButtonHover : Theme.primaryColor)
                    }
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: Theme.textOnPrimary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    onClicked: createGroupDialog.doCreate()
                }
            }
        }

        function showContacts(contacts) {
            contactModel.clear()
            for (var i = 0; i < contacts.length; i++) {
                contactModel.append({
                    userId: contacts[i].userId || 0,
                    username: contacts[i].username || "",
                    selected: false
                })
            }
        }

        function doCreate() {
            var name = groupNameField.text.trim()
            if (name.length === 0 || name.length > 64) {
                createGroupStatus.text = "群名称需为 1-64 字符"
                return
            }
            var ids = []
            for (var i = 0; i < contactModel.count; i++) {
                var c = contactModel.get(i)
                if (c.selected) {
                    ids.push(c.userId)
                }
            }
            createGroupRequested(name, ids)
            close()
        }

        onClosed: {
            groupNameField.text = ""
            contactModel.clear()
            createGroupStatus.text = ""
        }
    }

    // M7a: 群信息对话框（成员列表/邀请/踢人/退群）
    Dialog {
        id: groupInfoDialog
        title: "群信息"
        modal: true
        anchors.centerIn: parent
        width: 400
        padding: Theme.spacingLarge

        property int convId: 0
        property string myRole: "member"

        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.windowBackground
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                id: groupInfoTitleText
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                id: groupInfoSubtitle
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }

            ListView {
                id: memberList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 260)
                clip: true
                model: ListModel { id: memberModel }

                delegate: Rectangle {
                    width: memberList.width
                    height: 44
                    color: memberDelegateMouse.containsMouse ? Theme.hoverColor : "transparent"
                    radius: Theme.radiusSmall

                    MouseArea {
                        id: memberDelegateMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium

                        Rectangle {
                            width: 28; height: 28; radius: 14
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
                            // 使用 == 兼容 C++ qint64 经 JSON 传递到 QML 后可能为 string/number 的情况
                            text: model.username + (model.userId == mainPage.myUserId ? "（我）" : "")
                            font.pixelSize: Theme.fontSizeMedium
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }

                        Label {
                            text: groupInfoDialog.roleDisplay(model.role)
                            font.pixelSize: Theme.fontSizeSmall
                            color: model.role === "owner" ? Theme.primaryColor : Theme.textTertiary
                        }

                        // 踢人按钮（层级保护：owner 可移除 admin/member，admin 仅可移除 member）
                        Button {
                            visible: groupInfoDialog.canKick(model.role)
                                     && model.userId !== mainPage.myUserId
                            text: "移除"
                            flat: true
                            font.pixelSize: Theme.fontSizeSmall
                            contentItem: Label {
                                text: parent.text
                                font: parent.font
                                color: Theme.unreadBadgeColor
                            }
                            background: null
                            onClicked: {
                                kickGroupMemberRequested(groupInfoDialog.convId, model.userId)
                                // 刷新群信息（服务端推送 group_changed 亦会刷新会话列表）
                                getGroupInfoRequested(groupInfoDialog.convId)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Button {
                    text: "邀请成员"
                    flat: true
                    font.pixelSize: Theme.fontSizeMedium
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: Theme.primaryColor
                    }
                    background: null
                    onClicked: inviteDialog.open()
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "退出群聊"
                    flat: true
                    font.pixelSize: Theme.fontSizeMedium
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: Theme.unreadBadgeColor
                    }
                    background: null
                    onClicked: {
                        leaveGroupRequested(groupInfoDialog.convId)
                        groupInfoDialog.close()
                    }
                }
            }
        }

        function showInfo(info) {
            convId = info.conversationId || 0
            myRole = info.myRole || "member"
            groupInfoTitleText.text = info.name || "未命名群组"
            groupInfoSubtitle.text = (info.memberCount || 0) + " 位成员 · 你是" + roleDisplay(myRole)
            memberModel.clear()
            var members = info.members || []
            for (var i = 0; i < members.length; i++) {
                memberModel.append({
                    userId: members[i].userId || 0,
                    username: members[i].username || "",
                    role: members[i].role || "member"
                })
            }
            if (!visible) {
                open()
            }
        }

        function roleDisplay(role) {
            if (role === "owner") return "群主"
            if (role === "admin") return "管理员"
            return "成员"
        }

        function canKick(memberRole) {
            if (myRole === "owner") return memberRole !== "owner"
            if (myRole === "admin") return memberRole === "member"
            return false
        }
    }

    // M7a: 邀请成员对话框（搜索用户后多选邀请）
    Dialog {
        id: inviteDialog
        title: "邀请成员"
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

            TextField {
                id: inviteSearchField
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.inputHeight
                placeholderText: "搜索用户名..."
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.textPrimary
                placeholderTextColor: Theme.inputPlaceholderColor
                selectByMouse: true
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.width: inviteSearchField.activeFocus ? 2 : 1
                    border.color: inviteSearchField.activeFocus ? Theme.inputFocusBorderColor : Theme.inputBorderColor
                }
                onAccepted: inviteDialog.doSearch()
            }

            ListView {
                id: inviteResultList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 200)
                clip: true
                visible: inviteResultModel.count > 0
                model: ListModel { id: inviteResultModel }

                delegate: Rectangle {
                    width: inviteResultList.width
                    height: 40
                    color: inviteDelegateMouse.containsMouse ? Theme.hoverColor : "transparent"
                    radius: Theme.radiusSmall

                    MouseArea {
                        id: inviteDelegateMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: inviteResultModel.setProperty(index, "selected", !model.selected)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium

                        Label {
                            text: model.selected ? "☑" : "☐"
                            font.pixelSize: Theme.fontSizeMedium
                            color: model.selected ? Theme.primaryColor : Theme.textTertiary
                        }

                        Label {
                            Layout.fillWidth: true
                            text: model.username + " (ID: " + model.userId + ")"
                            font.pixelSize: Theme.fontSizeMedium
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Label {
                id: inviteStatus
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
                    onClicked: inviteDialog.close()
                }

                Button {
                    id: inviteConfirmBtn
                    text: "邀请"
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: Font.DemiBold
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: inviteConfirmBtn.pressed ? Theme.loginButtonPressed
                             : (inviteConfirmBtn.hovered ? Theme.loginButtonHover : Theme.primaryColor)
                    }
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: Theme.textOnPrimary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    onClicked: inviteDialog.doInvite()
                }
            }
        }

        function doSearch() {
            var query = inviteSearchField.text.trim()
            if (query.length > 0) {
                inviteStatus.text = "搜索中..."
                searchMode = "invite"
                searchUsersRequested(query)
            }
        }

        function showResults(users) {
            inviteResultModel.clear()
            if (users.length === 0) {
                inviteStatus.text = "未找到匹配的用户"
                return
            }
            inviteStatus.text = ""
            for (var i = 0; i < users.length; i++) {
                inviteResultModel.append({
                    userId: users[i].userId || 0,
                    username: users[i].username || "",
                    selected: false
                })
            }
        }

        function doInvite() {
            var ids = []
            for (var i = 0; i < inviteResultModel.count; i++) {
                var u = inviteResultModel.get(i)
                if (u.selected) {
                    ids.push(u.userId)
                }
            }
            if (ids.length === 0) {
                inviteStatus.text = "请先选择要邀请的用户"
                return
            }
            inviteGroupMembersRequested(mainPage.currentConversationId, ids)
            close()
        }

        onClosed: {
            inviteSearchField.text = ""
            inviteResultModel.clear()
            inviteStatus.text = ""
            searchMode = "chat"
        }
    }

    // ── 会话操作 ──
    // 打开一个既有会话
    function openConversation(conv) {
        currentConversationId = conv.conversationId
        currentConversationType = conv.type || "private"

        if (currentConversationType === "group") {
            // M7a: 群会话：标题为群名，私聊字段不适用
            currentPeerUserId = 0
            currentPeerUsername = ""
            currentGroupMemberCount = conv.memberCount || 0
            chatView.peerUsername = conv.name || "未命名群组"
            chatView.chatTitle = conv.name || "未命名群组"
        } else {
            currentPeerUserId = conv.peerUserId
            currentPeerUsername = conv.peerUsername
            currentGroupMemberCount = 0
            chatView.peerUsername = conv.peerUsername
            chatView.chatTitle = conv.peerUsername
        }

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
        currentConversationType = "private"
        currentPeerUserId = userId
        currentPeerUsername = username
        currentGroupMemberCount = 0

        chatView.peerUsername = username
        chatView.chatTitle = username
        chatView.hasConversation = true
        chatView.clearMessages()

        convList.selectedIndex = -1
    }

    // M4.5: 首条消息发送成功后绑定服务端返回的会话 ID
    function bindNewConversation(conversationId) {
        // 使用 == 兼容 C++ qint64 经 JSON 传递到 QML 后可能为 string/number 的情况
        if (currentConversationId == 0 && conversationId > 0) {
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
            // M7a: 当前群会话的成员数/群名变化同步到聊天区
            for (var i = 0; i < conversations.length; i++) {
                var conv = conversations[i]
                // 使用 == 兼容 C++ qint64 经 JSON 传递到 QML 后可能为 string/number 的情况
                if ((conv.conversationId || 0) == currentConversationId) {
                    if ((conv.type || "private") === "group") {
                        currentGroupMemberCount = conv.memberCount || 0
                        chatView.chatTitle = conv.name || "未命名群组"
                    }
                    break
                }
            }
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
        // M7a: 群系统消息预览用可读摘要
        var contentText = message.contentType === "system"
            ? chatView.systemMessageText(message.content || "")
            : message.content
        var preview = message.senderUsername ? message.senderUsername + ": " + contentText
                                             : contentText
        convList.updateForNewMessage(message.conversationId, preview, timeStr, true)
    }

    // M4.5: 当前会话中最后一条对方消息 ID（用于发送已读回执）
    function lastIncomingMessageId() {
        return chatView.lastIncomingMessageId()
    }

    function showSearchResults(users) {
        // M7a: 搜索结果按用途路由（发起对话 / 群邀请）
        if (searchMode === "invite") {
            inviteDialog.showResults(users)
        } else {
            searchDialog.showResults(users)
        }
    }

    // M7a: 联系人列表（建群对话框成员选择）
    function showContacts(contacts) {
        createGroupDialog.showContacts(contacts)
    }

    // M7a: 群信息响应（打开/刷新群信息对话框）
    function showGroupInfo(info) {
        groupInfoDialog.showInfo(info)
    }

    // M4.5: 登出时重置界面状态
    function resetUi() {
        currentConversationId = 0
        currentPeerUserId = 0
        currentPeerUsername = ""
        currentConversationType = "private"
        currentGroupMemberCount = 0
        searchMode = "chat"
        chatView.hasConversation = false
        chatView.peerUsername = ""
        chatView.chatTitle = ""
        chatView.clearMessages()
        convList.reset()
    }

    // M7a: 建群成功后打开新群会话
    function openCreatedGroup(conversationId, name) {
        loadConversationsRequested()
        currentConversationId = conversationId
        currentConversationType = "group"
        currentPeerUserId = 0
        currentPeerUsername = ""
        chatView.peerUsername = name
        chatView.chatTitle = name
        chatView.hasConversation = true
        chatView.clearMessages()
        loadMessagesRequested(conversationId, 0)
    }

    // M7a: 退群/被移出后若当前正在该群，关闭聊天区
    function closeGroupIfCurrent(conversationId) {
        // 使用 == 兼容 C++ qint64 经 JSON 传递到 QML 后可能为 string/number 的情况
        if (currentConversationId == conversationId) {
            currentConversationId = 0
            currentConversationType = "private"
            currentGroupMemberCount = 0
            chatView.hasConversation = false
            chatView.peerUsername = ""
            chatView.chatTitle = ""
            chatView.clearMessages()
            convList.selectedIndex = -1
        }
        loadConversationsRequested()
    }
}
