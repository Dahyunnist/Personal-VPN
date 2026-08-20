cmake_minimum_required(VERSION 3.21)

project(PersonalVPN VERSION 0.1.0 LANGUAGES CXX)

option(PERSONAL_VPN_BUILD_TESTS "Build unit tests" ON)

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

if(PERSONAL_VPN_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
