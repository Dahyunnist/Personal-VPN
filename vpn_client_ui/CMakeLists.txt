include(FetchContent)

if(NOT WIN32)
    message(FATAL_ERROR "The Personal-VPN desktop client is Windows-only")
endif()
if(NOT TARGET PersonalVPN::ClientTransport OR NOT TARGET PersonalVPN::WindowsNetwork)
    message(FATAL_ERROR
        "Build from the repository root with PERSONAL_VPN_BUILD_CLIENT_TRANSPORT=ON")
endif()

set(PERSONAL_VPN_IMGUI_DIR "" CACHE PATH "Optional path to a Dear ImGui checkout")
if(PERSONAL_VPN_IMGUI_DIR)
    set(imgui_SOURCE_DIR "${PERSONAL_VPN_IMGUI_DIR}")
else()
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)
endif()

add_library(personal_vpn_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(personal_vpn_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_compile_features(personal_vpn_imgui PUBLIC cxx_std_17)
target_link_libraries(personal_vpn_imgui PRIVATE d3d11 dxgi)

add_executable(personal-vpn-client WIN32
    main.cpp
    ui_main.cpp
    ui_main.h
    vpn_client_core.cpp
    vpn_client_core.h
)
target_compile_features(personal-vpn-client PRIVATE cxx_std_17)
target_compile_definitions(personal-vpn-client PRIVATE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(personal-vpn-client PRIVATE /W4 /permissive-)
else()
    target_compile_options(personal-vpn-client PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
target_link_libraries(personal-vpn-client PRIVATE
    PersonalVPN::ClientTransport
    PersonalVPN::WindowsNetwork
    personal_vpn_imgui
    user32
    gdi32
    d3d11
    dxgi
    dwmapi
    d3dcompiler
    ws2_32
    shell32
    advapi32
)

set(PERSONAL_VPN_WINTUN_DLL "" CACHE FILEPATH "Path to a pinned Wintun DLL")
if(PERSONAL_VPN_WINTUN_DLL)
    add_custom_command(TARGET personal-vpn-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PERSONAL_VPN_WINTUN_DLL}"
                "$<TARGET_FILE_DIR:personal-vpn-client>/wintun.dll"
        VERBATIM
    )
else()
    message(WARNING "PERSONAL_VPN_WINTUN_DLL is unset; the client package will not be runnable")
endif()

install(TARGETS personal-vpn-client RUNTIME DESTINATION bin)
