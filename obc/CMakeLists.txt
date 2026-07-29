# The OBC firmware build - the image that gets flashed, and the one CI compiles.
#
# This used to be a CI-only check next to CubeIDE's generated makefiles, and that split cost real
# bench time twice: those makefiles list every source file explicitly and are regenerated only by
# the IDE, so adding a source file broke `just obc-build` with undefined references until someone
# refreshed and cleaned inside CubeIDE. Worse, it meant two builds that could disagree about what
# was in the image. This is now the single build; CubeIDE stays as a debugger and can still build
# the project itself, but it is no longer the authority.
#
# Sources are globbed rather than listed, deliberately: configuring re-globs, so a new driver
# cannot silently fall out of the image the way it could out of a hand-listed makefile. `just
# obc-build` configures before it builds for exactly that reason.
#
# It links rather than only compiling. Twice this project has lost hours to code that compiled
# fine and was never handed to the linker - a weak symbol satisfied out of an archive on the ESC,
# and an object missing from CubeIDE's objects.list on the OBC. Only a link catches that class.
#
#   just obc-build    just obc-flash

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

# the kernel, listed rather than globbed - the submodule ships far more than this build uses, and
# only what the config turns on belongs in the image. no heap_*.c on purpose: static allocation
# only (configSUPPORT_DYNAMIC_ALLOCATION 0), and no timers.c because configUSE_TIMERS is 0. our own
# glue (rtos_hooks.c, the tasks) is a real directory under Src/ and comes in through the glob above
set(FREERTOS_DIR ${REPO_ROOT}/vendor/FreeRTOS-Kernel)
set(FREERTOS_SOURCES
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
)

add_executable(obc.elf
    ${OBC_SOURCES}
    ${FSW_SOURCES}
    ${FREERTOS_SOURCES}
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
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
)

target_compile_definitions(obc.elf PRIVATE STM32F446xx STM32 STM32F4 STM32F446RETx)

# hard-float cortex-m4, matching CubeIDE. a mismatch here links but faults at runtime, so these
# have to stay in step with the .cproject
set(CPU_FLAGS -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb)

# -O0 -g3 -DDEBUG match what CubeIDE's Debug configuration has been producing, so promoting this
# to the flashable build did not change the image's behaviour. Pinned rather than left to
# CMAKE_BUILD_TYPE: the optimisation level a firmware image is flashed at should not depend on
# whether the caller remembered a flag.
target_compile_options(obc.elf PRIVATE
    ${CPU_FLAGS}
    -O0 -g3
    -ffunction-sections -fdata-sections
    -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)

target_compile_definitions(obc.elf PRIVATE DEBUG)

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
