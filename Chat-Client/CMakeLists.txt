cmake_minimum_required(VERSION 3.21)

project(Chat-Client VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 向编译器添加编译选项
if (MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# QML 模块：Qml + Quick
find_package(Qt6 COMPONENTS Widgets Quick QuickControls2 Network REQUIRED)
find_package(OpenSSL REQUIRED)

# QWindowKit
find_package(QWindowKit COMPONENTS Core Quick REQUIRED)

# 资源文件
set(RESOURCES
    resources/resources.qrc
)

# C++ 源文件
set(SOURCES
    main.cpp
    core/NetworkManager.cpp
)

# C++ 头文件
set(HEADERS
    core/NetworkManager.h
)

# 生成可执行文件
add_executable(Chat-Client ${SOURCES} ${HEADERS} ${RESOURCES})

# 包含目录
target_include_directories(Chat-Client PRIVATE
    core/
)

# 链接 Qt、OpenSSL、QWindowKit 和公共库
target_link_libraries(Chat-Client PUBLIC
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    OpenSSL::SSL
    QWindowKit::Core
    QWindowKit::Quick
    XYChatCommon
)

# 添加Widgets, Network 模块的头文件路径
target_include_directories(Chat-Client SYSTEM PUBLIC
    ${Qt6Widgets_INCLUDE_DIRS}
    ${Qt6Network_INCLUDE_DIRS}
    ${Qt6Quick_INCLUDE_DIRS}
)

# 构建后将 QWindowKit 运行时 DLL 拷贝到输出目录
# $<TARGET_FILE:...> 会自动根据当前构建配置选择对应的 DLL（Debug 为 QWKCored/QWKQuickd，Release 为 QWKCore/QWKQuick）
add_custom_command(TARGET Chat-Client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:QWindowKit::Core>
        $<TARGET_FILE:QWindowKit::Quick>
        $<TARGET_FILE_DIR:Chat-Client>
)
