cmake_minimum_required(VERSION 3.28)
project(shard-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Stub target for scaffold - full implementation in Phase 1
add_executable(shard-engine src/shard.cpp)

# For Emscripten WASM output
if (EMSCRIPTEN)
  set_target_properties(shard-engine
    PROPERTIES
    OUTPUT_NAME "shard-engine"
    SUFFIX ".wasm"
  )
endif()
