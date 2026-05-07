# Emscripten toolchain file for CloudMine Shard Engine
# Update the path below to point to your Emscripten installation
# Example: /home/user/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake

set(EMSCRIPTEN TRUE CACHE BOOL "Emscripten build")
set(CMAKE_TOOLCHAIN_FILE ""
    CACHE PATH "Path to Emscripten toolchain (set by emsdk env)")
set(CMAKE_CXX_COMPILER "em++" CACHE PATH "Emscripten C++ compiler")
set(CMAKE_C_COMPILER "emcc" CACHE PATH "Emscripten C compiler")

# Emscripten flags for WASM output
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")

# OpenSSL support via Emscripten's built-in
# Compile with: emcmake cmake -B build
# Then: cmake --build build
