# portable C++ flight software - builds and tests on the host, no hardware
cmake_minimum_required(VERSION 3.16)
project(fsw CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# emit compile_commands.json so vscode intellisense uses the real build's include paths
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# CI passes -DFSW_WERROR=ON; scoped to the fsw library below (not the vendored doctest tests)
option(FSW_WERROR "treat warnings as errors" OFF)

# the C frame/msg code is shared with the firmware and compiled from here too
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ETL - fixed-capacity, no-heap containers (header-only, vendored as a submodule)
set(ETL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/etl/include)

# the flight logic itself - mode + fault managers, plus the host backend of the
# platform layer. pure decisions; reaches the outside world only through platform.hpp
add_library(fsw STATIC
    src/mode_manager.cpp
    src/fault_manager.cpp
    platform/host/platform_host.cpp
    ${COMMON_DIR}/protocol/frame.c
)
target_include_directories(fsw PUBLIC include ${COMMON_DIR} ${ETL_DIR})

if(FSW_WERROR)
    target_compile_options(fsw PRIVATE -Werror)
endif()

# unit tests (doctest, single header under third_party/)
enable_testing()
add_executable(fsw_tests
    test/test_main.cpp
    test/test_mode_manager.cpp
    test/test_fault_manager.cpp
    test/test_protocol.cpp
)
target_include_directories(fsw_tests PRIVATE third_party)
target_link_libraries(fsw_tests PRIVATE fsw)

# register each doctest TEST_CASE as its own ctest test (so `ctest` lists them all)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
include(doctest)
doctest_discover_tests(fsw_tests)
