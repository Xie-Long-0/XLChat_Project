# XYChatOpenSSL.cmake
#
# 定位 3rdparty 内置的 OpenSSL，并按构建配置在 Release / Debug 版本之间自动切换。
#
# 目录约定（由 3rdparty 的实际布局决定）：
#   3rdparty/         Release 版：include/  lib/  bin/
#   3rdparty/debug/   Debug   版：lib/  bin/（头文件与 Release 共用 3rdparty/include）
#
# 对外接口：
#   xychat_find_openssl()              查找 OpenSSL 并绑定按配置的导入库
#                                      —— 必须在顶层 CMakeLists 中先于 add_subdirectory 调用，
#                                         以保证 OpenSSL::SSL / OpenSSL::Crypto 对所有子目录可见
#   xychat_copy_runtime_dlls(<target>) 为目标追加 POST_BUILD，拷贝与当前配置匹配的运行时 DLL

include_guard(GLOBAL)

set(XYCHAT_3RDPARTY_DIR "${CMAKE_SOURCE_DIR}/3rdparty" CACHE PATH "XYChat 第三方库根目录")

# ── 解析 Release / Debug 根目录 ────────────────────────────────────────────
set(_xy_root_release "${XYCHAT_3RDPARTY_DIR}")
set(_xy_root_debug "${XYCHAT_3RDPARTY_DIR}/debug")

if(NOT IS_DIRECTORY "${_xy_root_debug}/lib")
    message(STATUS "[OpenSSL] 未找到 Debug 库目录 ${_xy_root_debug}/lib，"
                   "Debug 构建将回退使用 Release 版 OpenSSL")
    set(_xy_root_debug "${_xy_root_release}")
endif()

set(XYCHAT_OPENSSL_ROOT_RELEASE "${_xy_root_release}" CACHE INTERNAL "OpenSSL Release 根目录")
set(XYCHAT_OPENSSL_ROOT_DEBUG "${_xy_root_debug}" CACHE INTERNAL
    "OpenSSL Debug 根目录（缺失时指向 Release）")

# ── 查找 OpenSSL 并按配置绑定导入库 ────────────────────────────────────────
function(xychat_find_openssl)
    # 注意：此处不能用缓存变量做「已配置」短路 —— 缓存会跨 configure 保留，
    # 而导入目标不会，跳过 find_package 会导致 OpenSSL::SSL 目标缺失。
    # find_package 的结果本身是缓存的，重复调用开销可忽略。

    # 头文件两个配置共用 Release 根下的 include/
    set(OPENSSL_ROOT_DIR "${XYCHAT_OPENSSL_ROOT_RELEASE}")
    find_package(OpenSSL REQUIRED)

    if(NOT TARGET OpenSSL::SSL OR NOT TARGET OpenSSL::Crypto)
        message(FATAL_ERROR
            "[OpenSSL] find_package 未生成 OpenSSL::SSL / OpenSSL::Crypto 导入目标")
    endif()

    # 以 Release 库名为准，在 Debug 根下定位同名库（兼容 libssl.lib / ssleay32.lib 等命名）
    get_filename_component(_ssl_name "${OPENSSL_SSL_LIBRARY}" NAME)
    get_filename_component(_crypto_name "${OPENSSL_CRYPTO_LIBRARY}" NAME)

    set(_ssl_release "${OPENSSL_SSL_LIBRARY}")
    set(_ssl_debug "${XYCHAT_OPENSSL_ROOT_DEBUG}/lib/${_ssl_name}")
    set(_crypto_release "${OPENSSL_CRYPTO_LIBRARY}")
    set(_crypto_debug "${XYCHAT_OPENSSL_ROOT_DEBUG}/lib/${_crypto_name}")

    if(NOT EXISTS "${_ssl_debug}")
        message(STATUS "[OpenSSL] Debug 构建缺失 ${_ssl_debug}，回退使用 Release 库")
        set(_ssl_debug "${_ssl_release}")
    endif()
    if(NOT EXISTS "${_crypto_debug}")
        message(STATUS "[OpenSSL] Debug 构建缺失 ${_crypto_debug}，回退使用 Release 库")
        set(_crypto_debug "${_crypto_release}")
    endif()

    # 按配置绑定：DEBUG -> Debug 库，其余配置 -> Release 库。
    # 覆盖 MSVC/Ninja 的四种标准配置，单配置与多配置生成器均可正确解析。
    set(_configs RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    foreach(_spec "OpenSSL::SSL;${_ssl_release};${_ssl_debug}"
                  "OpenSSL::Crypto;${_crypto_release};${_crypto_debug}")
        list(GET _spec 0 _tgt)
        list(GET _spec 1 _rel)
        list(GET _spec 2 _dbg)

        set_property(TARGET ${_tgt} PROPERTY IMPORTED_CONFIGURATIONS ${_configs})
        foreach(_cfg IN LISTS _configs)
            if(_cfg STREQUAL "DEBUG")
                set(_loc "${_dbg}")
            else()
                set(_loc "${_rel}")
            endif()
            set_target_properties(${_tgt} PROPERTIES
                IMPORTED_LOCATION_${_cfg} "${_loc}"
                IMPORTED_LINK_INTERFACE_LANGUAGES_${_cfg} "C")
        endforeach()
    endforeach()

    # 单配置生成器（Ninja / Make）：无后缀 IMPORTED_LOCATION 也指向当前配置，
    # 以便自定义配置名（非上述四种）时仍能取到正确库
    if(CMAKE_BUILD_TYPE)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt)
        if(_bt STREQUAL "DEBUG")
            set_target_properties(OpenSSL::SSL PROPERTIES IMPORTED_LOCATION "${_ssl_debug}")
            set_target_properties(OpenSSL::Crypto PROPERTIES IMPORTED_LOCATION "${_crypto_debug}")
        else()
            set_target_properties(OpenSSL::SSL PROPERTIES IMPORTED_LOCATION "${_ssl_release}")
            set_target_properties(OpenSSL::Crypto PROPERTIES IMPORTED_LOCATION "${_crypto_release}")
        endif()
    endif()

    # 状态信息只在首次绑定时打印（全局属性不跨 configure 保留，仅用于抑制重复输出）
    get_property(_xy_printed GLOBAL PROPERTY XYCHAT_OPENSSL_STATUS_PRINTED)
    if(NOT _xy_printed)
        set_property(GLOBAL PROPERTY XYCHAT_OPENSSL_STATUS_PRINTED TRUE)
        message(STATUS "[OpenSSL] ${OPENSSL_VERSION}")
        message(STATUS "[OpenSSL]   Release : ${_ssl_release}")
        message(STATUS "[OpenSSL]           : ${_crypto_release}")
        message(STATUS "[OpenSSL]   Debug   : ${_ssl_debug}")
        message(STATUS "[OpenSSL]           : ${_crypto_debug}")
    endif()
endfunction()

# ── 运行时 DLL 拷贝 ────────────────────────────────────────────────────────
# Debug 配置拷 3rdparty/debug/bin，其余配置拷 3rdparty/bin。
# 条件生成器表达式拼接在同一个参数内，保证参数永不为空白（避免 copy_if_different 收到空参数）。
function(xychat_copy_runtime_dlls target)
    set(_dlls)

    foreach(_name IN ITEMS libcrypto-3-x64.dll libssl-3-x64.dll legacy.dll)
        set(_dbg "${XYCHAT_OPENSSL_ROOT_DEBUG}/bin/${_name}")
        set(_rel "${XYCHAT_OPENSSL_ROOT_RELEASE}/bin/${_name}")
        if(NOT EXISTS "${_dbg}")
            set(_dbg "${_rel}")
        endif()
        if(EXISTS "${_rel}")
            list(APPEND _dlls
                "$<$<CONFIG:Debug>:${_dbg}>$<$<NOT:$<CONFIG:Debug>>:${_rel}>")
        endif()
    endforeach()

    # zlib 仅提供 Release 变体，两个配置都用它
    if(EXISTS "${XYCHAT_OPENSSL_ROOT_RELEASE}/bin/zlib1.dll")
        list(APPEND _dlls "${XYCHAT_OPENSSL_ROOT_RELEASE}/bin/zlib1.dll")
    endif()

    if(_dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_dlls}
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying runtime DLLs to output"
            VERBATIM
        )
    endif()
endfunction()
