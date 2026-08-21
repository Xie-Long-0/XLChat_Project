find_package(Qt6 COMPONENTS Test Sql Network REQUIRED)
find_package(OpenSSL REQUIRED)

set(CMAKE_AUTOMOC ON)

# 公共 OpenSSL 运行时 DLL 列表
set(OPENSSL_RUNTIME_DLLS
    ${CMAKE_SOURCE_DIR}/3rdparty/bin/libcrypto-3-x64.dll
    ${CMAKE_SOURCE_DIR}/3rdparty/bin/libssl-3-x64.dll
    ${CMAKE_SOURCE_DIR}/3rdparty/bin/legacy.dll
    ${CMAKE_SOURCE_DIR}/3rdparty/bin/zlib1.dll
)

# ── TestEncryptionManager ──
add_executable(TestEncryptionManager
    unit/TestEncryptionManager.cpp
)

target_link_libraries(TestEncryptionManager PRIVATE
    Qt6::Test
    XYChatCommon
)

add_test(NAME TestEncryptionManager COMMAND TestEncryptionManager)

add_custom_command(TARGET TestEncryptionManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENSSL_RUNTIME_DLLS}
        $<TARGET_FILE_DIR:TestEncryptionManager>
)

# ── TestPacketCodec ──
add_executable(TestPacketCodec
    unit/TestPacketCodec.cpp
)

target_link_libraries(TestPacketCodec PRIVATE
    Qt6::Test
    XYChatCommon
)

add_test(NAME TestPacketCodec COMMAND TestPacketCodec)

# ── TestDatabaseManager ──
add_executable(TestDatabaseManager
    unit/TestDatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/Chat-Server/database/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/Chat-Server/database/DatabaseManager.h
)

target_include_directories(TestDatabaseManager PRIVATE
    ${CMAKE_SOURCE_DIR}/Chat-Server
    ${CMAKE_SOURCE_DIR}/Chat-Server/database
)

target_link_libraries(TestDatabaseManager PRIVATE
    Qt6::Test
    Qt6::Sql
    XYChatCommon
)

add_test(NAME TestDatabaseManager COMMAND TestDatabaseManager)

add_custom_command(TARGET TestDatabaseManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENSSL_RUNTIME_DLLS}
        $<TARGET_FILE_DIR:TestDatabaseManager>
)

# ── TestSecurity (M5) ──
add_executable(TestSecurity
    unit/TestSecurity.cpp
    # M5.5: NonceCache 重放保护组件（服务端 core）
    ${CMAKE_SOURCE_DIR}/Chat-Server/core/NonceCache.cpp
    ${CMAKE_SOURCE_DIR}/Chat-Server/core/NonceCache.h
)

target_include_directories(TestSecurity PRIVATE
    ${CMAKE_SOURCE_DIR}/Chat-Server/core
)

target_link_libraries(TestSecurity PRIVATE
    Qt6::Test
    Qt6::Network
    XYChatCommon
)

add_test(NAME TestSecurity COMMAND TestSecurity)

add_custom_command(TARGET TestSecurity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENSSL_RUNTIME_DLLS}
        $<TARGET_FILE_DIR:TestSecurity>
)

# ── TestLocalStore (M6.5 客户端本地加密持久化缓存) ──
add_executable(TestLocalStore
    unit/TestLocalStore.cpp
    ${CMAKE_SOURCE_DIR}/Chat-Client/core/LocalStore.cpp
    ${CMAKE_SOURCE_DIR}/Chat-Client/core/LocalStore.h
    ${CMAKE_SOURCE_DIR}/Chat-Client/core/KeyStorage.cpp
    ${CMAKE_SOURCE_DIR}/Chat-Client/core/KeyStorage.h
)

target_include_directories(TestLocalStore PRIVATE
    ${CMAKE_SOURCE_DIR}/Chat-Client/core
)

target_link_libraries(TestLocalStore PRIVATE
    Qt6::Test
    Qt6::Sql
    XYChatCommon
)

# KeyStorage 使用 DPAPI 保护本地密钥与存储密钥
if (WIN32)
    target_link_libraries(TestLocalStore PRIVATE crypt32)
endif()

add_test(NAME TestLocalStore COMMAND TestLocalStore)

add_custom_command(TARGET TestLocalStore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENSSL_RUNTIME_DLLS}
        $<TARGET_FILE_DIR:TestLocalStore>
)
