import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "qrc:/theme"

Item {
    id: messageBubble
    width: parent ? parent.width : 400
    height: bubbleColumn.implicitHeight + Theme.spacingSmall * 2

    property bool isMine: false
    property string senderName: ""
    property string content: ""
    property string time: ""
    property string status: ""

    ColumnLayout {
        id: bubbleColumn
        anchors.left: isMine ? undefined : parent.left
        anchors.right: isMine ? parent.right : undefined
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        // 发送者名称（仅对方消息显示）
        Label {
            Layout.alignment: isMine ? Qt.AlignRight : Qt.AlignLeft
            Layout.leftMargin: isMine ? 0 : Theme.spacingXSmall
            Layout.rightMargin: isMine ? Theme.spacingXSmall : 0
            text: senderName
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
            color: Theme.primaryColor
            visible: !isMine && senderName !== ""
        }

        // 气泡主体
        Rectangle {
            id: bubbleRect
            Layout.maximumWidth: Theme.messageMaxWidth
            Layout.minimumWidth: 60
            Layout.preferredHeight: contentLayout.implicitHeight + Theme.spacingMedium * 2
            Layout.alignment: isMine ? Qt.AlignRight : Qt.AlignLeft
            Layout.leftMargin: isMine ? Theme.spacingXLarge : 0
            Layout.rightMargin: isMine ? 0 : Theme.spacingXLarge
            radius: Theme.radiusBubble
            color: isMine ? Theme.bubbleOutColor : Theme.bubbleInColor
            border.width: 1
            border.color: isMine ? Theme.bubbleOutBorderColor : Theme.bubbleInBorderColor

            ColumnLayout {
                id: contentLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingXSmall

                // 消息内容
                Label {
                    Layout.fillWidth: true
                    text: content
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeMedium
                    color: Theme.textPrimary
                    textFormat: Text.PlainText
                }

                // 时间和状态
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.spacingXSmall

                    Label {
                        text: time
                        font.pixelSize: Theme.fontSizeSmall - 1
                        color: Theme.textTertiary
                    }

                    // 消息状态图标（仅自己的消息）
                    Label {
                        visible: isMine && status !== ""
                        text: {
                            switch (status) {
                                case "sending": return "⏳"
                                case "sent": return "✓"
                                case "delivered": return "✓✓"
                                case "read": return "✓✓"
                                case "failed": return "⚠"
                                default: return ""
                            }
                        }
                        font.pixelSize: Theme.fontSizeSmall - 1
                        color: status === "read" ? Theme.primaryColor : Theme.textTertiary
                    }
                }
            }
        }
    }
}
