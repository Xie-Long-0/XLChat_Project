cmake_minimum_required(VERSION 3.21)

project(Chat-Server VERSION 1.0 LANGUAGES CXX)

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

find_package(Qt6 COMPONENTS Core Network Sql REQUIRED)
find_package(OpenSSL REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

# 源文件列表
set(SOURCES
    main.cpp
    core/Server.cpp
    core/RequestHandler.cpp
    core/NonceCache.cpp
    database/DatabaseManager.cpp
)

# 头文件列表
set(HEADERS
    core/Server.h
    core/RequestHandler.h
    core/NonceCache.h
    database/DatabaseManager.h
)

# 生成可执行文件
qt_add_executable(Chat-Server ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(Chat-Server PRIVATE
    core/
    database/
)

# 链接 Qt 和 OpenSSL 库
target_link_libraries(Chat-Server PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    OpenSSL::SSL
    XYChatCommon
)

# 添加 Network/Sql 模块的头文件路径
target_include_directories(Chat-Server SYSTEM PUBLIC
    ${Qt6Network_INCLUDE_DIRS}
    ${Qt6Sql_INCLUDE_DIRS}
)

# 构建后将 OpenSSL 运行时 DLL 拷贝到输出目录
add_custom_command(TARGET Chat-Server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/3rdparty/bin/libcrypto-3-x64.dll
        ${CMAKE_SOURCE_DIR}/3rdparty/bin/libssl-3-x64.dll
        ${CMAKE_SOURCE_DIR}/3rdparty/bin/legacy.dll
        ${CMAKE_SOURCE_DIR}/3rdparty/bin/zlib1.dll
        $<TARGET_FILE_DIR:Chat-Server>
    COMMENT "Copying OpenSSL runtime DLLs to output"
)

# M5: 构建后拷贝证书生成脚本到输出目录
add_custom_command(TARGET Chat-Server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:Chat-Server>/certs
    COMMENT "Creating certs directory for Chat-Server"
)
