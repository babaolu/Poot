cmake_minimum_required(VERSION 3.28)
project(orchestrator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
include(FetchContent)
FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4)
FetchContent_MakeAvailable(catch2)

# Main library (static)
add_library(orchestrator-lib STATIC
    src/server.cpp
)
target_include_directories(orchestrator-lib PUBLIC include)

# Server executable
add_executable(orchestrator-server
    src/server.cpp
)
target_link_libraries(orchestrator-server orchestrator-lib)

# Test executable
add_executable(orchestrator-tests
    tests/test_main.cpp
    tests/test_orchestrator.cpp
)
target_link_libraries(orchestrator-tests orchestrator-lib Catch2::Catch2WithMain)

enable_testing()
add_test(NAME orchestrator-tests COMMAND orchestrator-tests)
