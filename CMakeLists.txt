cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(PersonalVPN VERSION 0.1.0 LANGUAGES CXX)

option(PERSONAL_VPN_BUILD_TESTS "Build unit tests" ON)
option(PERSONAL_VPN_BUILD_LINUX_TRANSPORT "Build the Linux TLS session transport" ON)
if(WIN32)
    set(PERSONAL_VPN_CLIENT_TRANSPORT_DEFAULT OFF)
else()
    set(PERSONAL_VPN_CLIENT_TRANSPORT_DEFAULT ON)
endif()
option(PERSONAL_VPN_BUILD_CLIENT_TRANSPORT
       "Build the portable TLS client runtime"
       ${PERSONAL_VPN_CLIENT_TRANSPORT_DEFAULT})
option(PERSONAL_VPN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PERSONAL_VPN_BUILD_WINDOWS_UI "Build the Windows desktop client" OFF)

if(PERSONAL_VPN_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
endif()

add_library(personal_vpn_protocol
    src/protocol/control_messages.cpp
    src/protocol/protocol.cpp
)

add_library(PersonalVPN::Protocol ALIAS personal_vpn_protocol)

target_include_directories(personal_vpn_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/include>
)

target_compile_features(personal_vpn_protocol PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(personal_vpn_protocol PRIVATE /W4 /permissive-)
else()
    target_compile_options(personal_vpn_protocol PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if((CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PERSONAL_VPN_BUILD_LINUX_TRANSPORT) OR
   PERSONAL_VPN_BUILD_CLIENT_TRANSPORT)
    find_package(Boost 1.74 REQUIRED)
    find_package(OpenSSL REQUIRED)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PERSONAL_VPN_BUILD_LINUX_TRANSPORT)

    add_library(personal_vpn_server_transport
        src/server/linux_tun_device.cpp
        src/server/server_config.cpp
        src/server/tls_tunnel_server.cpp
        src/server/tls_security.cpp
        src/server/tls_tunnel_session.cpp
    )

    add_library(PersonalVPN::ServerTransport ALIAS personal_vpn_server_transport)

    target_include_directories(personal_vpn_server_transport
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/server/include>
    )

    target_link_libraries(personal_vpn_server_transport
        PUBLIC
            PersonalVPN::Core
            Boost::headers
            OpenSSL::SSL
            OpenSSL::Crypto
    )

    target_compile_features(personal_vpn_server_transport PUBLIC cxx_std_17)
    target_compile_options(personal_vpn_server_transport PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(personal-vpn-server src/server/main.cpp)
    target_link_libraries(personal-vpn-server PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(personal-vpn-server PRIVATE cxx_std_17)
    target_compile_options(personal-vpn-server PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    install(TARGETS personal-vpn-server RUNTIME DESTINATION bin)
endif()

add_library(personal_vpn_core
    src/core/client_network_transaction.cpp
    src/core/client_session_controller.cpp
    src/core/lease_manager.cpp
    src/core/outbound_frame_queue.cpp
    src/core/session_controller.cpp
    src/core/tunnel_router.cpp
)

add_library(PersonalVPN::Core ALIAS personal_vpn_core)

target_include_directories(personal_vpn_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core/include>
)

target_link_libraries(personal_vpn_core PUBLIC PersonalVPN::Protocol)
target_compile_features(personal_vpn_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(personal_vpn_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(personal_vpn_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(WIN32)
    add_library(personal_vpn_windows_network src/client/windows_network_backend.cpp)
    add_library(PersonalVPN::WindowsNetwork ALIAS personal_vpn_windows_network)
    target_include_directories(personal_vpn_windows_network
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/client/include>
    )
    target_link_libraries(personal_vpn_windows_network
        PUBLIC PersonalVPN::Core
        PRIVATE iphlpapi
    )
    target_compile_definitions(personal_vpn_windows_network
        PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_compile_features(personal_vpn_windows_network PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(personal_vpn_windows_network PRIVATE /W4 /permissive-)
    else()
        target_compile_options(personal_vpn_windows_network PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endif()

if(PERSONAL_VPN_BUILD_CLIENT_TRANSPORT)
    add_library(personal_vpn_client_transport
        src/client/client_config.cpp
        src/client/client_runtime.cpp
        src/client/client_tls_security.cpp
        src/client/credential_security.cpp
        src/client/tls_tunnel_client.cpp
    )
    add_library(PersonalVPN::ClientTransport ALIAS personal_vpn_client_transport)
    target_include_directories(personal_vpn_client_transport
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/client/include>
    )
    target_link_libraries(personal_vpn_client_transport
        PUBLIC
            PersonalVPN::Core
            Boost::headers
            OpenSSL::SSL
            OpenSSL::Crypto
    )
    target_compile_features(personal_vpn_client_transport PUBLIC cxx_std_17)
    if(WIN32)
        target_link_libraries(personal_vpn_client_transport PRIVATE advapi32)
    endif()
    if(MSVC)
        target_compile_options(personal_vpn_client_transport PRIVATE /W4 /permissive-)
    else()
        target_compile_options(personal_vpn_client_transport
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endif()

if(PERSONAL_VPN_BUILD_WINDOWS_UI)
    if(NOT WIN32)
        message(FATAL_ERROR "PERSONAL_VPN_BUILD_WINDOWS_UI requires Windows")
    endif()
    add_subdirectory(vpn_client_ui)
endif()

if(PERSONAL_VPN_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
