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
