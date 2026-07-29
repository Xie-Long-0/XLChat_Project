import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QWindowKit

import "qrc:/theme"

Rectangle {
    id: titleBar
    height: Theme.titleBarHeight
    color: Theme.titleBarBackground

    property WindowAgent windowAgent
    property string title: ""
    property bool showCloseButton: true

    // 拖拽区域
    Item {
        id: dragRegion
        anchors.fill: parent
        anchors.rightMargin: showCloseButton ? 138 : 0

        Component.onCompleted: {
            if (windowAgent) {
                windowAgent.setHitTestVisible(dragRegion, false)
            }
        }
    }

    // 标题
    Label {
        anchors.centerIn: parent
        text: title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeMedium
        font.weight: Font.DemiBold
        color: Theme.textPrimary
        visible: title !== ""
    }

    // 窗口控制按钮
    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        visible: showCloseButton

        // 最小化
        Rectangle {
            id: minimizeBtn
            width: 46; height: Theme.titleBarHeight
            color: minimizeMouse.containsMouse ? Theme.titleBarButtonHover : "transparent"

            MouseArea {
                id: minimizeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: windowAgent ? windowAgent.minimizeWindow() : undefined
            }

            // 横线图标
            Rectangle {
                anchors.centerIn: parent
                width: 10; height: 1
                color: Theme.textSecondary
            }
        }

        // 最大化/还原
        Rectangle {
            id: maximizeBtn
            width: 46; height: Theme.titleBarHeight
            color: maximizeMouse.containsMouse ? Theme.titleBarButtonHover : "transparent"

            property bool isMaximized: windowAgent ? false : false

            MouseArea {
                id: maximizeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: windowAgent ? windowAgent.switchMaximized() : undefined
            }

            // 方块图标
            Rectangle {
                anchors.centerIn: parent
                width: 10; height: 10
                color: "transparent"
                border.width: 1
                border.color: Theme.textSecondary
            }
        }

        // 关闭
        Rectangle {
            id: closeBtn
            width: 46; height: Theme.titleBarHeight
            color: closeMouse.containsMouse ? Theme.titleBarButtonCloseHover : "transparent"

            MouseArea {
                id: closeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: Qt.quit()
            }

            // X 图标
            Canvas {
                anchors.centerIn: parent
                width: 10; height: 10
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = closeMouse.containsMouse ? Theme.titleBarButtonCloseHoverIcon : Theme.textSecondary
                    ctx.lineWidth = 1.2
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.lineTo(width, height)
                    ctx.moveTo(width, 0)
                    ctx.lineTo(0, height)
                    ctx.stroke()
                }

                Connections {
                    target: closeMouse
                    function onContainsMouseChanged() { closeBtn.requestPaint() }
                }
            }

            function requestPaint() { children[0].requestPaint() }
        }
    }
}
