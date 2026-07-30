pragma Singleton
import QtQuick

QtObject {
    // ── Telegram 风格配色（亮色主题） ──────────────────────────────
    readonly property color primaryColor: "#2AABEE"        // Telegram 蓝
    readonly property color primaryDarkColor: "#229ED9"    // 深蓝
    readonly property color primaryLightColor: "#E3F2FD"   // 浅蓝背景

    // 背景色
    readonly property color windowBackground: "#FFFFFF"
    readonly property color sidebarBackground: "#FFFFFF"
    readonly property color chatBackground: "#E6ECEE"     // 聊天区域浅灰蓝
    readonly property color inputBackground: "#FFFFFF"

    // 气泡颜色
    readonly property color bubbleOutColor: "#D9FDD3"      // 自己的消息 - 浅绿
    readonly property color bubbleInColor: "#FFFFFF"       // 对方的消息 - 白色
    readonly property color bubbleOutBorderColor: "#C6E9BF"
    readonly property color bubbleInBorderColor: "#E0E0E0"

    // 文字颜色
    readonly property color textPrimary: "#000000"
    readonly property color textSecondary: "#707579"
    readonly property color textTertiary: "#A0A4A9"
    readonly property color textOnPrimary: "#FFFFFF"
    readonly property color textLink: "#2AABEE"

    // 边框和分隔线
    readonly property color borderColor: "#E0E0E0"
    readonly property color separatorColor: "#DADCE0"
    readonly property color dividerColor: "#F0F0F0"

    // 交互状态
    readonly property color hoverColor: "#F4F4F5"
    readonly property color pressedColor: "#E8E8E9"
    readonly property color selectedColor: "#2AABEE"
    readonly property color selectedTextColor: "#FFFFFF"

    // 未读标记
    readonly property color unreadBadgeColor: "#2AABEE"
    readonly property color unreadBadgeTextColor: "#FFFFFF"
    readonly property color unreadBadgeMutedColor: "#A0A4A9"

    // 标题栏
    readonly property color titleBarBackground: "#FFFFFF"
    readonly property int titleBarHeight: 36
    readonly property color titleBarButtonBackground: "#F0F0F0"
    readonly property color titleBarButtonHover: "#A0A0A0"
    readonly property color titleBarButtonCloseBackground: "#E81123"
    readonly property color titleBarButtonCloseHover: "#FA2233"

    // 输入框
    readonly property color inputBorderColor: "#DADCE0"
    readonly property color inputFocusBorderColor: "#2AABEE"
    readonly property color inputPlaceholderColor: "#A0A4A9"

    // 登录页
    readonly property color loginBackground: "#FFFFFF"
    readonly property color loginButtonColor: "#2AABEE"
    readonly property color loginButtonHover: "#229ED9"
    readonly property color loginButtonPressed: "#1B8FC4"
    readonly property color loginErrorColor: "#E53935"

    // ── 字体 ──────────────────────────────────────────────────────
    readonly property string fontFamily: "Segoe UI"
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeMedium: 13
    readonly property int fontSizeLarge: 15
    readonly property int fontSizeXLarge: 18
    readonly property int fontSizeTitle: 22

    // ── 间距 ──────────────────────────────────────────────────────
    readonly property int spacingXSmall: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 12
    readonly property int spacingLarge: 16
    readonly property int spacingXLarge: 24

    // ── 圆角 ──────────────────────────────────────────────────────
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusBubble: 12

    // ── 尺寸 ──────────────────────────────────────────────────────
    readonly property int sidebarWidth: 320
    readonly property int avatarSize: 48
    readonly property int avatarSizeSmall: 36
    readonly property int conversationItemHeight: 72
    readonly property int messageMaxWidth: 480
    readonly property int inputHeight: 44

    // ── 动画 ──────────────────────────────────────────────────────
    readonly property int animationFast: 120
    readonly property int animationNormal: 200
    readonly property int animationSlow: 300
}
