if(NOT DEFINED PACKAGE_ARCHIVE OR NOT EXISTS "${PACKAGE_ARCHIVE}")
    message(FATAL_ERROR "PACKAGE_ARCHIVE must name an existing Windows package")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${PACKAGE_ARCHIVE}"
    RESULT_VARIABLE archive_result
    OUTPUT_VARIABLE archive_entries
    ERROR_VARIABLE archive_error
)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "Unable to list Windows package: ${archive_error}")
endif()

set(required_suffixes
    "/bin/personal-vpn-client.exe"
    "/bin/personal-vpn-broker-service.exe"
    "/bin/wintun.dll"
    "/config/client.example.json"
    "/licenses/imgui/LICENSE.txt"
    "/licenses/wintun/LICENSE.txt"
    "/share/doc/personal-vpn/README.md"
    "/share/doc/personal-vpn/docs/security/credential-handling.md"
)
foreach(required_suffix IN LISTS required_suffixes)
    string(FIND "${archive_entries}" "${required_suffix}" entry_position)
    if(entry_position EQUAL -1)
        message(FATAL_ERROR "Windows package is missing required file: ${required_suffix}")
    endif()
endforeach()

string(REPLACE "\r\n" "\n" archive_entries "${archive_entries}")
string(REPLACE "\n" ";" archive_entries_list "${archive_entries}")
foreach(entry IN LISTS archive_entries_list)
    string(TOLOWER "${entry}" entry_lower)
    if(entry_lower MATCHES "\\.(key|pem|p12|pfx)$" OR
       entry_lower MATCHES "(^|/)(credentials|secrets)(/|$)" OR
       entry_lower MATCHES "(^|/)config\\.json$")
        message(FATAL_ERROR "Windows package contains a credential-like path: ${entry}")
    endif()
endforeach()

message(STATUS "Windows package contents verified: ${PACKAGE_ARCHIVE}")
