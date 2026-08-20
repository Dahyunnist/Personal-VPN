add_executable(protocol_tests protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE PersonalVPN::Protocol)
target_compile_features(protocol_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME protocol_tests COMMAND protocol_tests)

add_executable(control_message_tests control_message_tests.cpp)
target_link_libraries(control_message_tests PRIVATE PersonalVPN::Protocol)
target_compile_features(control_message_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(control_message_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(control_message_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME control_message_tests COMMAND control_message_tests)

add_executable(lease_manager_tests lease_manager_tests.cpp)
target_link_libraries(lease_manager_tests PRIVATE PersonalVPN::Core)
target_compile_features(lease_manager_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lease_manager_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(lease_manager_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME lease_manager_tests COMMAND lease_manager_tests)

add_executable(session_controller_tests session_controller_tests.cpp)
target_link_libraries(session_controller_tests PRIVATE PersonalVPN::Core)
target_compile_features(session_controller_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(session_controller_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(session_controller_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME session_controller_tests COMMAND session_controller_tests)

add_executable(outbound_frame_queue_tests outbound_frame_queue_tests.cpp)
target_link_libraries(outbound_frame_queue_tests PRIVATE PersonalVPN::Core)
target_compile_features(outbound_frame_queue_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(outbound_frame_queue_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(outbound_frame_queue_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME outbound_frame_queue_tests COMMAND outbound_frame_queue_tests)

add_executable(tunnel_router_tests tunnel_router_tests.cpp)
target_link_libraries(tunnel_router_tests PRIVATE PersonalVPN::Core)
target_compile_features(tunnel_router_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(tunnel_router_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(tunnel_router_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME tunnel_router_tests COMMAND tunnel_router_tests)

add_executable(client_session_controller_tests client_session_controller_tests.cpp)
target_link_libraries(client_session_controller_tests PRIVATE PersonalVPN::Core)
target_compile_features(client_session_controller_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(client_session_controller_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(client_session_controller_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME client_session_controller_tests COMMAND client_session_controller_tests)

add_executable(client_network_transaction_tests client_network_transaction_tests.cpp)
target_link_libraries(client_network_transaction_tests PRIVATE PersonalVPN::Core)
target_compile_features(client_network_transaction_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(client_network_transaction_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(client_network_transaction_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME client_network_transaction_tests COMMAND client_network_transaction_tests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PERSONAL_VPN_BUILD_LINUX_TRANSPORT)
    find_program(BASH_EXECUTABLE bash REQUIRED)
    set(TEST_PKI_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-pki")

    add_executable(tls_security_tests tls_security_tests.cpp)
    target_link_libraries(tls_security_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(tls_security_tests PRIVATE cxx_std_17)
    target_compile_options(tls_security_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(tls_tunnel_session_tests tls_tunnel_session_tests.cpp)
    target_link_libraries(tls_tunnel_session_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(tls_tunnel_session_tests PRIVATE cxx_std_17)
    target_compile_options(tls_tunnel_session_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(linux_tun_device_tests linux_tun_device_tests.cpp)
    target_link_libraries(linux_tun_device_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(linux_tun_device_tests PRIVATE cxx_std_17)
    target_compile_options(linux_tun_device_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_test(NAME linux_tun_device_tests COMMAND linux_tun_device_tests)
    set_tests_properties(linux_tun_device_tests PROPERTIES TIMEOUT 10)

    add_executable(tls_tunnel_server_tests tls_tunnel_server_tests.cpp)
    target_link_libraries(tls_tunnel_server_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(tls_tunnel_server_tests PRIVATE cxx_std_17)
    target_compile_options(tls_tunnel_server_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(tls_tunnel_client_tests tls_tunnel_client_tests.cpp)
    target_link_libraries(tls_tunnel_client_tests
        PRIVATE PersonalVPN::ClientTransport PersonalVPN::ServerTransport)
    target_compile_features(tls_tunnel_client_tests PRIVATE cxx_std_17)
    target_compile_options(tls_tunnel_client_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(client_config_tests client_config_tests.cpp)
    target_link_libraries(client_config_tests PRIVATE PersonalVPN::ClientTransport)
    target_compile_features(client_config_tests PRIVATE cxx_std_17)
    target_compile_options(client_config_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(server_config_tests server_config_tests.cpp)
    target_link_libraries(server_config_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(server_config_tests PRIVATE cxx_std_17)
    target_compile_options(server_config_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_test(NAME server_config_tests COMMAND server_config_tests)

    add_test(
        NAME generate_test_pki
        COMMAND "${BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate-dev-pki.sh"
                "${TEST_PKI_DIRECTORY}"
    )
    set_tests_properties(generate_test_pki PROPERTIES FIXTURES_SETUP test_pki)

    add_test(
        NAME tls_security_tests
        COMMAND tls_security_tests
                "${TEST_PKI_DIRECTORY}/ca.crt"
                "${TEST_PKI_DIRECTORY}/server.crt"
                "${TEST_PKI_DIRECTORY}/server.key"
                "${TEST_PKI_DIRECTORY}/client.crt"
                "${TEST_PKI_DIRECTORY}/client.key"
    )
    set_tests_properties(tls_security_tests PROPERTIES FIXTURES_REQUIRED test_pki)

    add_test(
        NAME tls_tunnel_session_tests
        COMMAND tls_tunnel_session_tests
                "${TEST_PKI_DIRECTORY}/ca.crt"
                "${TEST_PKI_DIRECTORY}/server.crt"
                "${TEST_PKI_DIRECTORY}/server.key"
                "${TEST_PKI_DIRECTORY}/client.crt"
                "${TEST_PKI_DIRECTORY}/client.key"
    )
    set_tests_properties(tls_tunnel_session_tests PROPERTIES
        FIXTURES_REQUIRED test_pki
        TIMEOUT 15
    )

    add_test(
        NAME tls_tunnel_server_tests
        COMMAND tls_tunnel_server_tests
                "${TEST_PKI_DIRECTORY}/ca.crt"
                "${TEST_PKI_DIRECTORY}/server.crt"
                "${TEST_PKI_DIRECTORY}/server.key"
                "${TEST_PKI_DIRECTORY}/client.crt"
                "${TEST_PKI_DIRECTORY}/client.key"
                "${TEST_PKI_DIRECTORY}/client-2.crt"
                "${TEST_PKI_DIRECTORY}/client-2.key"
    )
    set_tests_properties(tls_tunnel_server_tests PROPERTIES
        FIXTURES_REQUIRED test_pki
        TIMEOUT 20
    )

    add_test(
        NAME tls_tunnel_client_tests
        COMMAND tls_tunnel_client_tests
                "${TEST_PKI_DIRECTORY}/ca.crt"
                "${TEST_PKI_DIRECTORY}/server.crt"
                "${TEST_PKI_DIRECTORY}/server.key"
                "${TEST_PKI_DIRECTORY}/client.crt"
                "${TEST_PKI_DIRECTORY}/client.key"
    )
    set_tests_properties(tls_tunnel_client_tests PROPERTIES
        FIXTURES_REQUIRED test_pki
        TIMEOUT 20
    )

    add_test(
        NAME client_config_tests
        COMMAND client_config_tests "${TEST_PKI_DIRECTORY}"
    )
    set_tests_properties(client_config_tests PROPERTIES
        FIXTURES_REQUIRED test_pki
        TIMEOUT 10
    )
endif()
