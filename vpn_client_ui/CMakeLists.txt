cmake_minimum_required(VERSION 3.21)

project(VPNClientUI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui路径
set(IMGUI_DIR "D:/imgui/imgui" CACHE PATH "ImGui目录")
set(IMGUI_BACKENDS_DIR "${IMGUI_DIR}/backends")

# 检查ImGui是否存在
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "未找到ImGui！请确保ImGui已下载到 D:/imgui/imgui/")
endif()

# client路径（已复制到vpn_client_ui目录下）
set(CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/client")

# ImGui核心源文件
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# ImGui后端（Win32 + DirectX11）
if(WIN32)
    set(IMGUI_BACKENDS
        ${IMGUI_BACKENDS_DIR}/imgui_impl_win32.cpp
        ${IMGUI_BACKENDS_DIR}/imgui_impl_dx11.cpp
    )
    
    # 检查后端文件
    foreach(BACKEND ${IMGUI_BACKENDS})
        if(NOT EXISTS "${BACKEND}")
            message(FATAL_ERROR "未找到ImGui后端文件: ${BACKEND}")
        endif()
    endforeach()
endif()

# VPN客户端核心逻辑
set(VPN_CLIENT_SOURCES
    vpn_client_core.cpp
    vpn_client_core.h
)

# 检查client目录是否存在
if(NOT EXISTS "${CLIENT_DIR}/client.h")
    message(FATAL_ERROR "未找到client目录！请确保client目录存在于: ${CLIENT_DIR}")
endif()

# 项目源文件
set(PROJECT_SOURCES
    main.cpp
    ui_main.cpp
    ui_main.h
    ${VPN_CLIENT_SOURCES}
)

# 添加可执行文件
add_executable(VPNClientUI
    ${PROJECT_SOURCES}
    ${IMGUI_SOURCES}
    ${IMGUI_BACKENDS}
)

# 包含目录
target_include_directories(VPNClientUI PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_BACKENDS_DIR}
    ${CLIENT_DIR}
)

# 编译vpn_base/client的client.cpp，但排除main函数
# 由于不能修改原文件，我们创建一个包装文件
set(CLIENT_CORE_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/client_core_impl.cpp")
if(NOT EXISTS "${CLIENT_CORE_IMPL}")
    message(FATAL_ERROR "client_core_impl.cpp not found. Please create it by copying vpn_base/client/client.cpp and excluding main function.")
endif()

# 添加client_core_impl.cpp到编译
target_sources(VPNClientUI PRIVATE "${CLIENT_CORE_IMPL}")
target_include_directories(VPNClientUI PRIVATE ${CLIENT_DIR})
message(STATUS "Using client_core_impl.cpp (copied from client/client.cpp)")

# Windows特定设置
if(WIN32)
    set_target_properties(VPNClientUI PROPERTIES
        WIN32_EXECUTABLE ON
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()

# 链接库
target_link_libraries(VPNClientUI PRIVATE
    # Windows系统库（ImGui需要）
    user32
    gdi32
    d3d11
    dxgi
    dwmapi
    d3dcompiler
    # VPN客户端需要的库
    ws2_32
    iphlpapi
    ole32
    crypt32
    advapi32
    shell32
    # Boost和OpenSSL
    boost_thread-mt
    ssl
    crypto
)

# 复制wintun.dll至可执行文件同级目录
set(WINTUN_DLL_PATH "${CLIENT_DIR}/wintun.dll")
set(DEST_DLL_PATH "${CMAKE_CURRENT_BINARY_DIR}/wintun.dll")
if(EXISTS "${WINTUN_DLL_PATH}")
    add_custom_command(
        TARGET VPNClientUI
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINTUN_DLL_PATH}" "${DEST_DLL_PATH}"
        COMMENT "Copying wintun.dll to build directory..."
    )
else()
    message(WARNING "未找到wintun.dll，请确保文件存在于: ${WINTUN_DLL_PATH}")
endif()

