# CMakeLists.txt — CloudMine Shard Engine
# C++23 — adheres to CppCoreGuidelines
# Builds: static library (native), WASM binary (Emscripten), and test executable

cmake_minimum_required(VERSION 3.25)
project(shard-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies ---

# Catch2 for testing (header-only, fetched at configure time)
include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(catch2)

# OpenSSL for native builds (AES-256-GCM, SHA-256)
if (NOT EMSCRIPTEN)
    find_package(OpenSSL REQUIRED)
endif()

# --- Static library (shared by native and WASM targets) ---
add_library(shard-engine-lib STATIC
    src/shard.cpp
)
target_include_directories(shard-engine-lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
if (EMSCRIPTEN)
    target_compile_options(shard-engine-lib PRIVATE -sUSE_OPENSSL=1)
else()
    target_link_libraries(shard-engine-lib PUBLIC OpenSSL::Crypto)
endif()

# --- WASM executable (for Emscripten build) ---
if (EMSCRIPTEN)
    add_executable(shard-engine-wasm src/bindings.cpp)
    target_link_libraries(shard-engine-wasm PRIVATE shard-engine-lib)
    set_target_properties(shard-engine-wasm PROPERTIES
        OUTPUT_NAME "shard-engine"
        SUFFIX ".wasm"
    )
    target_compile_options(shard-engine-wasm PRIVATE
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
        -sEXPORT_ES6=1
        -sMODULARIZE=1
    )
    target_link_options(shard-engine-wasm PRIVATE
        -sEXPORTED_FUNCTIONS=['_shard_split','_shard_get_manifest_json','_shard_reconstruct','_shard_free','_shard_free_string']
        -sALLOW_MEMORY_GROWTH=1
    )
else()
    # --- Native test executable ---
    add_executable(shard-engine-tests
        tests/test_main.cpp
        tests/test_reed_solomon.cpp
        tests/test_crypto.cpp
        tests/test_merkle.cpp
        tests/test_shard.cpp
    )
    target_link_libraries(shard-engine-tests
        PRIVATE shard-engine-lib Catch2::Catch2WithMain)
    target_include_directories(shard-engine-tests PRIVATE tests)

    # Enable testing
    enable_testing()
    add_test(NAME shard-engine-tests COMMAND shard-engine-tests)
endif()
