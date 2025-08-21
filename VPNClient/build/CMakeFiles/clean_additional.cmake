# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles\\VPNClient_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\VPNClient_autogen.dir\\ParseCache.txt"
  "VPNClient_autogen"
  )
endif()
