add_executable(protocol_tests protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE PersonalVPN::Protocol)
target_compile_features(protocol_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(WIN32 AND PERSONAL_VPN_BUILD_CLIENT_TRANSPORT)
    add_executable(client_credential_security_tests client_credential_security_tests.cpp)
    target_link_libraries(client_credential_security_tests PRIVATE
        PersonalVPN::ClientTransport PersonalVPN::BrokerRuntime)
    target_compile_features(client_credential_security_tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(client_credential_security_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(client_credential_security_tests
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    add_test(NAME client_credential_security_tests COMMAND client_credential_security_tests)
    set_tests_properties(client_credential_security_tests PROPERTIES TIMEOUT 10)
endif()

add_test(NAME protocol_tests COMMAND protocol_tests)

add_executable(broker_protocol_tests broker_protocol_tests.cpp)
target_link_libraries(broker_protocol_tests PRIVATE PersonalVPN::BrokerProtocol)
if(TARGET PersonalVPN::BrokerRuntime)
    target_link_libraries(broker_protocol_tests PRIVATE PersonalVPN::BrokerRuntime)

    add_executable(broker_runtime_registry_tests broker_runtime_registry_tests.cpp)
    target_link_libraries(broker_runtime_registry_tests PRIVATE PersonalVPN::BrokerRuntime)
    target_compile_features(broker_runtime_registry_tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(broker_runtime_registry_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(broker_runtime_registry_tests
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    add_test(NAME broker_runtime_registry_tests COMMAND broker_runtime_registry_tests)

    add_executable(broker_service_policy_tests broker_service_policy_tests.cpp)
    target_link_libraries(broker_service_policy_tests PRIVATE PersonalVPN::BrokerRuntime)
    target_compile_features(broker_service_policy_tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(broker_service_policy_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(broker_service_policy_tests
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    add_test(NAME broker_service_policy_tests COMMAND broker_service_policy_tests)
endif()
target_compile_features(broker_protocol_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(broker_protocol_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(broker_protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
add_test(NAME broker_protocol_tests COMMAND broker_protocol_tests)

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

    # DrvFS does not preserve chmod permissions unless metadata is enabled.
    # Keep WSL test keys on the Linux filesystem so private-key checks stay meaningful.
    if(EXISTS "/proc/sys/kernel/osrelease")
        file(READ "/proc/sys/kernel/osrelease" KERNEL_RELEASE)
        if(KERNEL_RELEASE MATCHES "[Mm]icrosoft")
            string(SHA256 TEST_PKI_BUILD_HASH "${CMAKE_CURRENT_BINARY_DIR}")
            string(SUBSTRING "${TEST_PKI_BUILD_HASH}" 0 16 TEST_PKI_BUILD_ID)
            set(TEST_PKI_DIRECTORY "/tmp/personal-vpn-test-pki-${TEST_PKI_BUILD_ID}")
        endif()
    endif()

    add_executable(tls_security_tests tls_security_tests.cpp)
    target_link_libraries(tls_security_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(tls_security_tests PRIVATE cxx_std_17)
    target_compile_options(tls_security_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(tls_tunnel_session_tests tls_tunnel_session_tests.cpp)
    target_link_libraries(tls_tunnel_session_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(tls_tunnel_session_tests PRIVATE cxx_std_17)
    target_compile_options(tls_tunnel_session_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_executable(session_timeout_tests session_timeout_tests.cpp)
    target_link_libraries(session_timeout_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(session_timeout_tests PRIVATE cxx_std_17)
    target_compile_options(session_timeout_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

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

    if(PERSONAL_VPN_BUILD_CLIENT_TRANSPORT)
        add_executable(tls_tunnel_client_tests tls_tunnel_client_tests.cpp)
        target_link_libraries(tls_tunnel_client_tests
            PRIVATE PersonalVPN::ClientTransport PersonalVPN::ServerTransport)
        target_compile_features(tls_tunnel_client_tests PRIVATE cxx_std_17)
        target_compile_options(tls_tunnel_client_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

        add_executable(client_config_tests client_config_tests.cpp)
        target_link_libraries(client_config_tests PRIVATE PersonalVPN::ClientTransport)
        target_compile_features(client_config_tests PRIVATE cxx_std_17)
        target_compile_options(client_config_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

        add_executable(client_runtime_tests client_runtime_tests.cpp)
        target_link_libraries(client_runtime_tests
            PRIVATE PersonalVPN::ClientTransport PersonalVPN::ServerTransport)
        target_compile_features(client_runtime_tests PRIVATE cxx_std_17)
        target_compile_options(client_runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()

    add_executable(server_config_tests server_config_tests.cpp)
    target_link_libraries(server_config_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(server_config_tests PRIVATE cxx_std_17)
    target_compile_options(server_config_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    add_test(NAME server_config_tests COMMAND server_config_tests)

    add_executable(metrics_exporter_tests metrics_exporter_tests.cpp)
    target_link_libraries(metrics_exporter_tests PRIVATE PersonalVPN::ServerTransport)
    target_compile_features(metrics_exporter_tests PRIVATE cxx_std_17)
    target_compile_options(metrics_exporter_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME metrics_exporter_tests COMMAND metrics_exporter_tests)

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
                "${TEST_PKI_DIRECTORY}/client.crl"
                "${TEST_PKI_DIRECTORY}/client-2.crt"
                "${TEST_PKI_DIRECTORY}/client-2.key"
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
        NAME session_timeout_tests
        COMMAND session_timeout_tests
                "${TEST_PKI_DIRECTORY}/ca.crt"
                "${TEST_PKI_DIRECTORY}/server.crt"
                "${TEST_PKI_DIRECTORY}/server.key"
                "${TEST_PKI_DIRECTORY}/client.crt"
                "${TEST_PKI_DIRECTORY}/client.key"
    )
    set_tests_properties(session_timeout_tests PROPERTIES
        FIXTURES_REQUIRED test_pki
        TIMEOUT 10
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

    if(PERSONAL_VPN_BUILD_CLIENT_TRANSPORT)
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

        add_test(
            NAME client_runtime_tests
            COMMAND client_runtime_tests
                    "${TEST_PKI_DIRECTORY}/ca.crt"
                    "${TEST_PKI_DIRECTORY}/server.crt"
                    "${TEST_PKI_DIRECTORY}/server.key"
                    "${TEST_PKI_DIRECTORY}/client.crt"
                    "${TEST_PKI_DIRECTORY}/client.key"
        )
        set_tests_properties(client_runtime_tests PROPERTIES
            FIXTURES_REQUIRED test_pki
            TIMEOUT 20
        )
    endif()
endif()
