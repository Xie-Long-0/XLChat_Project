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

    property var networkManager: null
    property int myUserId: 0
    property string myUsername: ""

    // 当前选中会话
    property int currentConversationId: 0
    property int currentPeerUserId: 0
    property string currentPeerUsername: ""

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.titleBarHeight
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
                anchors.fill: parent

                onConversationClicked: function(index) {
                    var conv = convList.getConversation(index)
                    if (conv) {
                        currentConversationId = conv.conversationId
                        currentPeerUserId = conv.peerUserId
                        currentPeerUsername = conv.peerUsername

                        chatView.peerUsername = conv.peerUsername
                        chatView.chatTitle = "与 " + conv.peerUsername + " 的对话"
                        chatView.hasConversation = true
                        chatView.clearMessages()

                        loadMessagesRequested(conv.conversationId, 0)
                    }
                }

                onSearchClicked: {
                    searchDialog.open()
                }

                onRefreshClicked: {
                    loadConversationsRequested()
                }
            }
        }

        // 右侧聊天区域
        ChatView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true

            onSendMessage: function(content) {
                if (currentPeerUserId > 0) {
                    sendMessageRequested(currentPeerUserId, content)
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
                            addContactRequested(model.userId)
                            searchDialog.close()
                            loadConversationsRequested()
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

    // 公共方法
    function updateConversations(conversations) {
        convList.updateConversations(conversations)
    }

    function updateMessages(messages) {
        chatView.setMessages(messages, myUserId)
    }

    function appendMessage(msg) {
        chatView.appendMessage(msg, myUserId)
    }

    function showSearchResults(users) {
        searchDialog.showResults(users)
    }
}
