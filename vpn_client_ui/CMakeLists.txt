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
        # Dear ImGui v1.91.9b commit; a commit ID prevents tag retargeting.
        GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06
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
    PersonalVPN::BrokerRuntime
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
option(PERSONAL_VPN_FETCH_WINTUN
       "Download the pinned, signed Wintun release during configuration"
       OFF)

set(PERSONAL_VPN_WINTUN_LICENSE "" CACHE FILEPATH
    "Optional path to the license accompanying a local Wintun DLL")
if(PERSONAL_VPN_FETCH_WINTUN AND NOT PERSONAL_VPN_WINTUN_DLL)
    set(PERSONAL_VPN_WINTUN_VERSION "0.14.1")
    set(PERSONAL_VPN_WINTUN_SHA256
        "07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51")
    set(PERSONAL_VPN_WINTUN_ARCHIVE
        "${CMAKE_BINARY_DIR}/_deps/wintun-${PERSONAL_VPN_WINTUN_VERSION}.zip")
    set(PERSONAL_VPN_WINTUN_ROOT
        "${CMAKE_BINARY_DIR}/_deps/wintun-${PERSONAL_VPN_WINTUN_VERSION}")

    if(CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM64|arm64)$" OR
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(PERSONAL_VPN_WINTUN_ARCH "arm64")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PERSONAL_VPN_WINTUN_ARCH "amd64")
    else()
        set(PERSONAL_VPN_WINTUN_ARCH "x86")
    endif()

    if(EXISTS "${PERSONAL_VPN_WINTUN_ARCHIVE}")
        file(SHA256 "${PERSONAL_VPN_WINTUN_ARCHIVE}" PERSONAL_VPN_WINTUN_CACHED_SHA256)
        if(NOT "${PERSONAL_VPN_WINTUN_CACHED_SHA256}" STREQUAL
               "${PERSONAL_VPN_WINTUN_SHA256}")
            file(REMOVE "${PERSONAL_VPN_WINTUN_ARCHIVE}")
        endif()
    endif()
    if(NOT EXISTS "${PERSONAL_VPN_WINTUN_ARCHIVE}")
        file(DOWNLOAD
            "https://www.wintun.net/builds/wintun-${PERSONAL_VPN_WINTUN_VERSION}.zip"
            "${PERSONAL_VPN_WINTUN_ARCHIVE}"
            EXPECTED_HASH "SHA256=${PERSONAL_VPN_WINTUN_SHA256}"
            TLS_VERIFY ON
            STATUS PERSONAL_VPN_WINTUN_DOWNLOAD_STATUS
        )
        list(GET PERSONAL_VPN_WINTUN_DOWNLOAD_STATUS 0 PERSONAL_VPN_WINTUN_STATUS_CODE)
        list(GET PERSONAL_VPN_WINTUN_DOWNLOAD_STATUS 1 PERSONAL_VPN_WINTUN_STATUS_MESSAGE)
        if(NOT PERSONAL_VPN_WINTUN_STATUS_CODE EQUAL 0)
            file(REMOVE "${PERSONAL_VPN_WINTUN_ARCHIVE}")
            message(FATAL_ERROR
                "Unable to download Wintun: ${PERSONAL_VPN_WINTUN_STATUS_MESSAGE}")
        endif()
    endif()
    if(NOT EXISTS "${PERSONAL_VPN_WINTUN_ROOT}/wintun/bin/${PERSONAL_VPN_WINTUN_ARCH}/wintun.dll")
        file(REMOVE_RECURSE "${PERSONAL_VPN_WINTUN_ROOT}")
        file(MAKE_DIRECTORY "${PERSONAL_VPN_WINTUN_ROOT}")
        file(ARCHIVE_EXTRACT
            INPUT "${PERSONAL_VPN_WINTUN_ARCHIVE}"
            DESTINATION "${PERSONAL_VPN_WINTUN_ROOT}"
        )
    endif()
    set(PERSONAL_VPN_WINTUN_DLL
        "${PERSONAL_VPN_WINTUN_ROOT}/wintun/bin/${PERSONAL_VPN_WINTUN_ARCH}/wintun.dll")
    set(PERSONAL_VPN_WINTUN_LICENSE
        "${PERSONAL_VPN_WINTUN_ROOT}/wintun/LICENSE.txt")
endif()

if(PERSONAL_VPN_WINTUN_DLL)
    if(NOT EXISTS "${PERSONAL_VPN_WINTUN_DLL}")
        message(FATAL_ERROR "Wintun DLL does not exist: ${PERSONAL_VPN_WINTUN_DLL}")
    endif()
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
install(FILES "${CMAKE_SOURCE_DIR}/configs/client.example.json"
        DESTINATION config)
install(FILES "${imgui_SOURCE_DIR}/LICENSE.txt"
        DESTINATION licenses/imgui)
if(PERSONAL_VPN_WINTUN_DLL)
    install(FILES "${PERSONAL_VPN_WINTUN_DLL}" DESTINATION bin RENAME wintun.dll)
endif()
if(PERSONAL_VPN_WINTUN_LICENSE AND EXISTS "${PERSONAL_VPN_WINTUN_LICENSE}")
    install(FILES "${PERSONAL_VPN_WINTUN_LICENSE}"
            DESTINATION licenses/wintun
            RENAME LICENSE.txt)
elseif(PERSONAL_VPN_WINTUN_DLL)
    message(WARNING
        "A local Wintun DLL was supplied without PERSONAL_VPN_WINTUN_LICENSE; "
        "do not distribute this package until the matching license is included")
endif()
