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

# ETL - fixed-capacity, no-heap containers (header-only, vendored as a submodule)
set(ETL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/etl/include)

# the wire contract (frame codec, message layouts, id catalog) - shared with the ground box
# firmware when it lands (phase 8); the flight software owns and evolves it until then
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# the flight logic - backend-neutral: each executable below adds its own platform backend
add_library(fsw STATIC
    src/comms/command_handler.cpp
    src/comms/telemetry_producer.cpp
    src/executive.cpp
    src/fault_manager.cpp
    src/mode_manager.cpp
    src/sensor_monitor.cpp
    ${COMMON_DIR}/protocol/frame.cpp
)
target_include_directories(fsw PUBLIC include ${COMMON_DIR} ${ETL_DIR})

# warnings are errors, always. this used to be an opt-in that only CI passed, which meant a local
# build could go green on something CI would reject - the one thing a local build must never do.
# scoped to this target rather than globally because the vendored doctest header warns under
# -Wextra and is not ours to fix
target_compile_options(fsw PRIVATE -Werror)

# unit tests (doctest, single header under third_party/)
enable_testing()
add_executable(fsw_tests
    test/test_comms.cpp
    test/test_fault_manager.cpp
    test/test_main.cpp
    test/test_mode_manager.cpp
    test/test_executive.cpp
    test/test_sensor_monitor.cpp
    platform/host/platform_host.cpp
)
target_include_directories(fsw_tests PRIVATE third_party)
target_link_libraries(fsw_tests PRIVATE fsw)

# register each doctest TEST_CASE as its own ctest test (so `ctest` lists them all)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
include(doctest)
doctest_discover_tests(fsw_tests)

# the sil shim
add_executable(sil_shim
    sil/sil_shim.cpp
)
target_link_libraries(sil_shim PRIVATE fsw)
