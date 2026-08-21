import QtQuick
import QtQuick.Controls

import "../theme"

Item {
    id: messageBubble
    // 宽度由外层 delegate 指定；高度由内容驱动
    height: bubbleColumn.height + Theme.spacingSmall * 2

    property bool isMine: false
    property string senderName: ""
    property string content: ""
    property string time: ""
    property string status: ""
    // M6: 端到端加密消息无法解密（无对应预密钥/新设备无历史密钥）
    property bool undecryptable: false

    // 气泡内容区可用宽度上限
    readonly property int maxContentWidth: Theme.messageMaxWidth - Theme.spacingMedium * 2

    Column {
        id: bubbleColumn
        // 自己的消息靠右，对方的消息靠左
        anchors.right: isMine ? parent.right : undefined
        anchors.rightMargin: isMine ? Theme.spacingMedium : 0
        anchors.left: isMine ? undefined : parent.left
        anchors.leftMargin: isMine ? 0 : Theme.spacingMedium
        spacing: 2

        // 发送者名称（仅对方消息显示）
        Label {
            text: senderName
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
            color: Theme.primaryColor
            visible: !isMine && senderName !== ""
        }

        // 气泡主体：宽度随内容自适应，超过上限自动换行
        Rectangle {
            id: bubbleRect
            width: Math.max(
                       Math.min(contentLabel.implicitWidth, messageBubble.maxContentWidth),
                       metaRow.width,
                       60 - Theme.spacingMedium * 2) + Theme.spacingMedium * 2
            height: contentColumn.implicitHeight + Theme.spacingMedium * 2
            radius: Theme.radiusBubble
            color: isMine ? Theme.bubbleOutColor : Theme.bubbleInColor
            border.width: 1
            border.color: isMine ? Theme.bubbleOutBorderColor : Theme.bubbleInBorderColor

            Column {
                id: contentColumn
                x: Theme.spacingMedium
                y: Theme.spacingMedium
                width: bubbleRect.width - Theme.spacingMedium * 2
                spacing: Theme.spacingXSmall

                // 消息内容：短消息单行自然宽度，长消息在最大宽度内自动换行
                Label {
                    id: contentLabel
                    width: Math.min(implicitWidth, messageBubble.maxContentWidth)
                    text: undecryptable ? "⚠ 无法解密此消息" : content
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeMedium
                    font.italic: undecryptable
                    color: undecryptable ? Theme.textTertiary : Theme.textPrimary
                    textFormat: Text.PlainText
                }

                // 时间和状态
                Row {
                    id: metaRow
                    spacing: Theme.spacingXSmall
                    // 右对齐：Row 不支持对齐，通过 x 偏移实现
                    x: contentColumn.width - width

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
