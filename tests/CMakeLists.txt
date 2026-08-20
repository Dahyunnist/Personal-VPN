add_executable(protocol_tests protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE PersonalVPN::Protocol)
target_compile_features(protocol_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME protocol_tests COMMAND protocol_tests)
