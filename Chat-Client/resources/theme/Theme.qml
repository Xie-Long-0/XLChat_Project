pragma Singleton
import QtQuick

QtObject {
    // ── 主题模式（M4.5：亮/暗切换） ──
    // 由 ThemeSettings 持久化驱动；所有颜色属性均为绑定表达式，
    // 切换 darkMode 后全界面自动刷新。
    property bool darkMode: false

    function toggleDarkMode() {
        darkMode = !darkMode
    }

    // ── 主色调（亮暗共用） ──
    readonly property color primaryColor: "#2AABEE"        // Telegram 蓝
    readonly property color primaryDarkColor: "#229ED9"    // 深蓝
    readonly property color primaryLightColor: darkMode ? "#1E3A4F" : "#E3F2FD"

    // ── 背景色 ──
    readonly property color windowBackground: darkMode ? "#212121" : "#FFFFFF"
    readonly property color sidebarBackground: darkMode ? "#212121" : "#FFFFFF"
    readonly property color chatBackground: darkMode ? "#0E1621" : "#E6ECEE"
    readonly property color inputBackground: darkMode ? "#212121" : "#FFFFFF"

    // ── 气泡颜色 ──
    readonly property color bubbleOutColor: darkMode ? "#2B5278" : "#D9FDD3"
    readonly property color bubbleInColor: darkMode ? "#212121" : "#FFFFFF"
    readonly property color bubbleOutBorderColor: darkMode ? "#3A6284" : "#C6E9BF"
    readonly property color bubbleInBorderColor: darkMode ? "#303030" : "#E0E0E0"

    // ── 文字颜色 ──
    readonly property color textPrimary: darkMode ? "#FFFFFF" : "#000000"
    readonly property color textSecondary: darkMode ? "#AAAAAA" : "#707579"
    readonly property color textTertiary: darkMode ? "#707579" : "#A0A4A9"
    readonly property color textOnPrimary: "#FFFFFF"
    readonly property color textLink: "#2AABEE"

    // ── 边框和分隔线 ──
    readonly property color borderColor: darkMode ? "#303030" : "#E0E0E0"
    readonly property color separatorColor: darkMode ? "#303030" : "#DADCE0"
    readonly property color dividerColor: darkMode ? "#2A2A2A" : "#F0F0F0"

    // ── 交互状态 ──
    readonly property color hoverColor: darkMode ? "#2C2C2C" : "#F4F4F5"
    readonly property color pressedColor: darkMode ? "#333333" : "#E8E8E9"
    readonly property color selectedColor: "#2AABEE"
    readonly property color selectedTextColor: "#FFFFFF"
    // 会话列表选中背景（Telegram：蓝色选中）
    readonly property color selectedConversationColor: "#3390EC"
    readonly property color selectedConversationTextColor: "#FFFFFF"
    readonly property color selectedConversationSecondaryColor: darkMode ? "#BBDEFB" : "#E1F0FF"

    // ── 未读标记 ──
    readonly property color unreadBadgeColor: "#2AABEE"
    readonly property color unreadBadgeTextColor: "#FFFFFF"
    readonly property color unreadBadgeMutedColor: "#A0A4A9"

    // ── 标题栏 ──
    readonly property color titleBarBackground: darkMode ? "#212121" : "#FFFFFF"
    readonly property int titleBarHeight: 32
    readonly property color titleBarButtonBackground: darkMode ? "#2C2C2C" : "#F0F0F0"
    readonly property color titleBarButtonHover: darkMode ? "#3A3A3A" : "#A0A0A0"
    readonly property color titleBarButtonCloseBackground: "#E81123"
    readonly property color titleBarButtonCloseHover: "#FA2233"

    // ── 输入框 ──
    readonly property color inputBorderColor: darkMode ? "#303030" : "#DADCE0"
    readonly property color inputFocusBorderColor: "#2AABEE"
    readonly property color inputPlaceholderColor: darkMode ? "#707579" : "#A0A4A9"

    // ── 登录页 ──
    readonly property color loginBackground: darkMode ? "#212121" : "#FFFFFF"
    readonly property color loginButtonColor: "#2AABEE"
    readonly property color loginButtonHover: "#229ED9"
    readonly property color loginButtonPressed: "#1B8FC4"
    readonly property color loginErrorColor: "#E53935"
    readonly property color successColor: "#4CAF50"

    // ── 日期分隔线 ──
    readonly property color dateDividerColor: darkMode ? "#AA2B5278" : "#B0BEC5"
    readonly property color dateDividerTextColor: "#FFFFFF"

    // ── 字体 ──
    readonly property string fontFamily: "Segoe UI"
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeMedium: 13
    readonly property int fontSizeLarge: 15
    readonly property int fontSizeXLarge: 18
    readonly property int fontSizeTitle: 22

    // ── 间距 ──
    readonly property int spacingXSmall: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 12
    readonly property int spacingLarge: 16
    readonly property int spacingXLarge: 24

    // ── 圆角 ──
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusBubble: 12

    // ── 尺寸 ──
    readonly property int sidebarWidth: 320
    readonly property int avatarSize: 48
    readonly property int avatarSizeSmall: 36
    readonly property int conversationItemHeight: 72
    readonly property int messageMaxWidth: 480
    readonly property int inputHeight: 44

    // ── 动画 ──
    readonly property int animationFast: 120
    readonly property int animationNormal: 200
    readonly property int animationSlow: 300
}
