import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "qrc:/theme"

Rectangle {
    id: messageInput
    height: inputRow.implicitHeight + Theme.spacingMedium * 2
    color: Theme.inputBackground

    signal messageSent(string text)

    // 顶部分隔线
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.separatorColor
    }

    RowLayout {
        id: inputRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingSmall

        // 输入框
        TextField {
            id: inputField
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.inputHeight
            placeholderText: "输入消息..."
            font.pixelSize: Theme.fontSizeMedium
            wrapMode: TextEdit.Wrap
            leftPadding: Theme.spacingMedium

            placeholderTextColor: Theme.inputPlaceholderColor
            color: Theme.textPrimary
            selectByMouse: true

            background: Rectangle {
                radius: Theme.radiusMedium
                color: Theme.chatBackground
                border.width: inputField.activeFocus ? 2 : 1
                border.color: inputField.activeFocus ? Theme.inputFocusBorderColor : Theme.inputBorderColor

                Behavior on border.color { ColorAnimation { duration: Theme.animationFast } }
            }

            Keys.onReturnPressed: {
                if (text.trim().length > 0) {
                    sendMessage()
                }
            }

            Keys.onEnterPressed: {
                if (text.trim().length > 0) {
                    sendMessage()
                }
            }
        }

        // 发送按钮
        Rectangle {
            id: sendButton
            width: Theme.inputHeight
            height: Theme.inputHeight
            radius: Theme.inputHeight / 2
            color: sendMouse.pressed ? Theme.loginButtonPressed
                 : (sendMouse.containsMouse ? Theme.loginButtonHover : Theme.primaryColor)

            Behavior on color { ColorAnimation { duration: Theme.animationFast } }

            MouseArea {
                id: sendMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sendMessage()
            }

            // 发送箭头图标
            Canvas {
                anchors.centerIn: parent
                width: 18; height: 18
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = Theme.textOnPrimary
                    ctx.beginPath()
                    ctx.moveTo(2, width / 2)
                    ctx.lineTo(width - 2, width / 2)
                    ctx.lineTo(width - 6, 3)
                    ctx.closePath()
                    ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo(2, width / 2)
                    ctx.lineTo(width - 2, width / 2)
                    ctx.lineTo(width - 6, width - 3)
                    ctx.closePath()
                    ctx.fill()
                }
            }
        }
    }

    function sendMessage() {
        var text = inputField.text.trim()
        if (text.length > 0) {
            messageSent(text)
            inputField.text = ""
        }
    }

    function clearInput() {
        inputField.text = ""
    }

    function setFocus() {
        inputField.forceActiveFocus()
    }
}
