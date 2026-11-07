# Cross-compilation toolchain for STM32F10x (arm-none-eabi-gcc).
#
# The compiler is looked up on PATH first, then in ARM_TOOLCHAIN_DIR, which you
# can set in CMakePresets.json or on the command line:
#     cmake --preset debug -DARM_TOOLCHAIN_DIR="C:/ST/STM32CubeIDE/bin"
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_arm_names
    arm-none-eabi-gcc
    arm-none-eabi-gcc.exe)

find_program(ARM_C_COMPILER NAMES ${_arm_names}
             HINTS ${ARM_TOOLCHAIN_DIR} ENV ARM_TOOLCHAIN_DIR)
find_program(ARM_ASM_COMPILER NAMES ${_arm_names}
             HINTS ${ARM_TOOLCHAIN_DIR} ENV ARM_TOOLCHAIN_DIR)

if(NOT ARM_C_COMPILER)
    message(FATAL_ERROR
        "arm-none-eabi-gcc not found.\n"
        "Install it (STM32CubeIDE toolchain, xpack, or Arm GNU Toolchain) and "
        "either put it on PATH or pass -DARM_TOOLCHAIN_DIR=<dir with the exe>.")
endif()

set(CMAKE_C_COMPILER   ${ARM_C_COMPILER})
set(CMAKE_ASM_COMPILER ${ARM_ASM_COMPILER})
set(CMAKE_CXX_COMPILER ${ARM_C_COMPILER})

execute_process(
    COMMAND ${ARM_C_COMPILER} -print-prog-name=objcopy
    OUTPUT_VARIABLE ARM_OBJCOPY_HINT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(ARM_OBJCOPY_HINT AND EXISTS "${ARM_OBJCOPY_HINT}")
    get_filename_component(_arm_bin ${ARM_OBJCOPY_HINT} DIRECTORY)
    list(APPEND CMAKE_PROGRAM_PATH ${_arm_bin})
    find_program(CMAKE_SIZE NAMES arm-none-eabi-size HINTS ${_arm_bin})
    find_program(ARM_OBJCOPY NAMES arm-none-eabi-objcopy HINTS ${_arm_bin})
    set(ARM_TOOLCHAIN_DIR ${_arm_bin} CACHE PATH "arm-none-eabi toolchain bin" FORCE)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The CMake compiler check needs a *.elf aware link rule.
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
