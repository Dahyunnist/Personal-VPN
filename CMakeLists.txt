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
option(PERSONAL_VPN_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(PERSONAL_VPN_BUILD_FUZZING "Build libFuzzer protocol targets" OFF)
option(PERSONAL_VPN_BUILD_BENCHMARKS "Build reproducible protocol benchmarks" OFF)

if(PERSONAL_VPN_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "Use the dedicated MSVC /fsanitize configuration on Windows")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

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
        src/server/metrics_exporter.cpp
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
    target_compile_definitions(personal-vpn-server
        PRIVATE PERSONAL_VPN_VERSION="${PROJECT_VERSION}")
    target_compile_options(personal-vpn-server PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    install(TARGETS personal-vpn-server RUNTIME DESTINATION bin)
    install(FILES deploy/systemd/personal-vpn-server.service
            DESTINATION lib/systemd/system)
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
if(WIN32)
    target_link_libraries(personal_vpn_core PUBLIC ws2_32)
endif()
target_compile_features(personal_vpn_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(personal_vpn_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(personal_vpn_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_library(personal_vpn_broker_protocol src/broker/broker_protocol.cpp)
if(WIN32)
    target_sources(personal_vpn_broker_protocol PRIVATE
        src/broker/windows_broker_pipe.cpp
        src/broker/windows_broker_profile.cpp
    )
    target_link_libraries(personal_vpn_broker_protocol PRIVATE advapi32 userenv)
    target_compile_definitions(personal_vpn_broker_protocol PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()
add_library(PersonalVPN::BrokerProtocol ALIAS personal_vpn_broker_protocol)
target_include_directories(personal_vpn_broker_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/broker/include>
)
target_compile_features(personal_vpn_broker_protocol PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(personal_vpn_broker_protocol PRIVATE /W4 /permissive-)
else()
    target_compile_options(personal_vpn_broker_protocol
        PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(WIN32)
    add_library(personal_vpn_windows_network
        src/client/windows_network_backend.cpp
        src/client/windows_kill_switch.cpp
    )
    add_library(PersonalVPN::WindowsNetwork ALIAS personal_vpn_windows_network)
    target_include_directories(personal_vpn_windows_network
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/client/include>
    )
    target_link_libraries(personal_vpn_windows_network
        PUBLIC PersonalVPN::Core
        PRIVATE fwpuclnt iphlpapi
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
        target_link_libraries(personal_vpn_client_transport
            PRIVATE advapi32 PersonalVPN::WindowsNetwork)
        target_link_libraries(personal_vpn_client_transport PUBLIC ws2_32)
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

if(PERSONAL_VPN_BUILD_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PERSONAL_VPN_BUILD_FUZZING requires Clang with libFuzzer")
    endif()
    # Instrument the parser implementation itself; sanitizing only the harness would
    # miss invalid accesses performed inside the protocol/core libraries.
    target_compile_options(personal_vpn_protocol PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(personal_vpn_core PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    add_executable(frame-decoder-fuzz fuzz/frame_decoder_fuzz.cpp)
    target_link_libraries(frame-decoder-fuzz PRIVATE
        PersonalVPN::Core
        PersonalVPN::BrokerProtocol
    )
    target_compile_features(frame-decoder-fuzz PRIVATE cxx_std_17)
    target_compile_options(frame-decoder-fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(frame-decoder-fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
endif()

if(PERSONAL_VPN_BUILD_BENCHMARKS)
    add_executable(protocol-benchmark benchmarks/protocol_benchmark.cpp)
    target_link_libraries(protocol-benchmark PRIVATE PersonalVPN::Core)
    target_compile_features(protocol-benchmark PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(protocol-benchmark PRIVATE /W4 /permissive-)
    else()
        target_compile_options(protocol-benchmark PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endif()

if(PERSONAL_VPN_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "personal-vpn")
set(CPACK_PACKAGE_VENDOR "Personal-VPN")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME
    "personal-vpn-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
install(FILES Readme.md
        DESTINATION share/doc/personal-vpn
        RENAME README.md)
install(DIRECTORY docs/
        DESTINATION share/doc/personal-vpn/docs
        FILES_MATCHING PATTERN "*.md")
install(FILES configs/client.example.json
        DESTINATION share/doc/personal-vpn/configs)
install(FILES deploy/systemd/personal-vpn-server.service
        DESTINATION share/doc/personal-vpn/deploy/systemd)
include(CPack)
