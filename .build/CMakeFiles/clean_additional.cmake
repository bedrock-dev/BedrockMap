# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "BedrockMap_autogen"
  "CMakeFiles\\BedrockMap_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\BedrockMap_autogen.dir\\ParseCache.txt"
  )
endif()
