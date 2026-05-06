# Emscripten toolchain file for CloudMine Shard Engine
# Update the path below to point to your Emscripten installation
# e.g., /path/to/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake

set(CMAKE_TOOLCHAIN_FILE "" CACHE PATH "Path to Emscripten toolchain")
set(CMAKE_CXX_COMPILER "em++" CACHE PATH "Emscripten C++ compiler")
set(CMAKE_C_COMPILER "emcc" CACHE PATH "Emscripten C compiler")

# Emscripten-specific flags for WASM output
set(CMAKE_EXECUTBLE_SUFFIX ".wasm")
