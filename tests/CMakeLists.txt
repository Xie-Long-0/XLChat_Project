find_package(Qt6 COMPONENTS Test Sql REQUIRED)

set(CMAKE_AUTOMOC ON)

# ── TestEncryptionManager ────────────────────────────────────────────────────
add_executable(TestEncryptionManager
    unit/TestEncryptionManager.cpp
)

target_link_libraries(TestEncryptionManager PRIVATE
    Qt6::Test
    XYChatCommon
)

add_test(NAME TestEncryptionManager COMMAND TestEncryptionManager)

# ── TestPacketCodec ──────────────────────────────────────────────────────────
add_executable(TestPacketCodec
    unit/TestPacketCodec.cpp
)

target_link_libraries(TestPacketCodec PRIVATE
    Qt6::Test
    XYChatCommon
)

add_test(NAME TestPacketCodec COMMAND TestPacketCodec)

# ── TestDatabaseManager ──────────────────────────────────────────────────────
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
