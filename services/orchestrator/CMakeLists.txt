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

# PostgreSQL client library (libpq)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQ REQUIRED libpq)

# Main library (static)
add_library(orchestrator-lib STATIC
    src/server.cpp
    src/postgres_store.cpp
)
target_include_directories(orchestrator-lib PUBLIC include ${PQ_INCLUDE_DIRS})
target_link_libraries(orchestrator-lib PUBLIC ${PQ_LIBRARIES})
target_compile_options(orchestrator-lib PRIVATE ${PQ_CFLAGS_OTHER})

# Server executable
add_executable(orchestrator-server
    src/server.cpp
    src/postgres_store.cpp
)
target_include_directories(orchestrator-server PRIVATE include ${PQ_INCLUDE_DIRS})
target_link_libraries(orchestrator-server orchestrator-lib ${PQ_LIBRARIES})
target_compile_options(orchestrator-server PRIVATE ${PQ_CFLAGS_OTHER})

# Test executable
add_executable(orchestrator-tests
    tests/test_main.cpp
    tests/test_orchestrator.cpp
)
target_link_libraries(orchestrator-tests orchestrator-lib Catch2::Catch2WithMain ${PQ_LIBRARIES})

enable_testing()
add_test(NAME orchestrator-tests COMMAND orchestrator-tests)
