# Cross-build of the OBC firmware, for CI.
#
# STM32CubeIDE owns the image that actually gets flashed - its generated makefiles under
# obc/Debug/ are the authority, and they are gitignored, which is why CI cannot use them. This
# exists so a firmware break is caught by CI rather than only on the bench, and it is a
# compile-and-link check, not a second way to produce a deliverable. Do not flash what it emits.
#
# It links rather than only compiling on purpose. Twice now this project has lost hours to code
# that compiled fine and was never handed to the linker - a weak symbol satisfied out of an
# archive on the ESC, and an object missing from CubeIDE's objects.list on the OBC. Only a link
# catches that class of bug.
#
# Sources are globbed rather than listed. That is normally poor CMake practice, but here the
# whole point is that a new driver cannot silently fall out of coverage: CubeIDE discovers files
# by scanning the tree, so this has to as well or the two disagree the moment a file is added.
#
#   just obc-check

cmake_minimum_required(VERSION 3.20)

# there is exactly one valid toolchain for this project, so it selects its own rather than making
# every caller pass -DCMAKE_TOOLCHAIN_FILE. that also removes a way to get it wrong: without this,
# forgetting the flag silently configures a cortex-m4 image with the host compiler. must come
# before project(), which is where cmake reads it
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(obc_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB_RECURSE OBC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Startup/*.s
)

# the portable flight software, cross-compiled into the image alongside its stm32 backend - the
# same sources the host unit tests build, which is the claim the whole fsw/obc split rests on
file(GLOB_RECURSE FSW_SOURCES
    ${REPO_ROOT}/fsw/src/*.cpp
    ${REPO_ROOT}/fsw/platform/stm32/*.cpp
)

# CMSIS's clock plumbing - SystemCoreClock and APBPrescTable, which the clock and systick drivers
# read. listed rather than globbed because it is not in this tree: CubeIDE pulls it out of the
# submodule as a linked resource, so it looks like it lives in obc/Src there and does not here.
# every other file CubeIDE links from outside obc/ is covered by the globs above
set(CMSIS_SYSTEM ${REPO_ROOT}/vendor/cmsis-device-f4/Source/Templates/system_stm32f4xx.c)

add_executable(obc.elf
    ${OBC_SOURCES}
    ${FSW_SOURCES}
    ${CMSIS_SYSTEM}
    ${REPO_ROOT}/common/protocol/frame.cpp
)

target_include_directories(obc.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${REPO_ROOT}/common
    ${REPO_ROOT}/fsw/include
    ${REPO_ROOT}/vendor/cmsis-core/CMSIS/Core/Include
    ${REPO_ROOT}/vendor/cmsis-device-f4/Include
    ${REPO_ROOT}/vendor/etl/include
)

# no FreeRTOS include path here, unlike the CubeIDE project: nothing includes a kernel header yet
# (the RTOS lands in phase 6), so requiring the submodule would make CI fetch what it cannot use.
# add it here in the same change that adds the first #include of it

target_compile_definitions(obc.elf PRIVATE STM32F446xx STM32 STM32F4 STM32F446RETx)

# hard-float cortex-m4, matching CubeIDE. a mismatch here links but faults at runtime, so these
# have to stay in step with the .cproject
set(CPU_FLAGS -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb)

target_compile_options(obc.elf PRIVATE
    ${CPU_FLAGS}
    -ffunction-sections -fdata-sections
    -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)

target_link_options(obc.elf PRIVATE
    ${CPU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F446RETX_FLASH.ld
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=obc.map
    -static
)

# report the section sizes, so CI output shows flash/ram growth the same way the bench does
add_custom_command(TARGET obc.elf POST_BUILD COMMAND arm-none-eabi-size $<TARGET_FILE:obc.elf>)
