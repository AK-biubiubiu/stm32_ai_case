cmake_minimum_required(VERSION 3.20)

project(stm32_mnist_ai C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "No toolchain selected. Build with:\n"
        "    cmake --preset debug\n"
        "    cmake --build --preset debug")
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(TARGET stm32_mnist_ai)

# ---------------------------------------------------------------------------
# sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
    User/main.c
    App/ai_mlp.c
    App/ai_preprocess.c
    App/draw_canvas.c
    App/app_ui.c
    App/board_port.c
)

file(GLOB_RECURSE BSP_SOURCES CONFIGURE_DEPENDS ${ROOT}/BSP/*.c)
file(GLOB_RECURSE BSP_HEADERS CONFIGURE_DEPENDS ${ROOT}/BSP/*.h)
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS ${ROOT}/Libraries/*.c)
file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS ${ROOT}/Libraries/*.h)

if(NOT BSP_SOURCES)
    message(FATAL_ERROR
        "No BSP sources found in ${ROOT}/BSP.\n"
        "Copy the Wildfire standard-library template into the project:\n"
        "  ${ROOT}/BSP        <- bsp_*.c / bsp_*.h\n"
        "  ${ROOT}/Libraries  <- CMSIS + STM32F10x_StdPeriph_Driver\n"
        "See .trae/documents/stm32f103-mnist-ai-plan.md, section 3.")
endif()

set(INC_DIRS
    ${ROOT}/User
    ${ROOT}/App
    ${ROOT}/Model
)

# Add every directory that actually contains a header, whatever the Wildfire
# folder layout looks like.
foreach(hdr ${BSP_HEADERS} ${LIB_HEADERS})
    get_filename_component(_dir ${hdr} DIRECTORY)
    list(APPEND INC_DIRS ${_dir})
endforeach()
list(REMOVE_DUPLICATES INC_DIRS)

add_executable(${TARGET}
    startup/startup_stm32f10x_hd.S
    ${APP_SOURCES}
    ${BSP_SOURCES}
    ${LIB_SOURCES}
)

target_include_directories(${TARGET} PRIVATE ${INC_DIRS})

target_compile_definitions(${TARGET} PRIVATE
    USE_STDPERIPH_DRIVER
    STM32F10X_HD
)

target_compile_options(${TARGET} PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>
)

target_link_options(${TARGET} PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -T${ROOT}/ld/STM32F103VETx_FLASH.ld
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET}.map
    -specs=nano.specs
    -specs=nosys.specs
)

set_target_properties(${TARGET} PROPERTIES
    OUTPUT_NAME ${TARGET}
    SUFFIX ".elf"
    LINK_DEPENDS ${ROOT}/ld/STM32F103VETx_FLASH.ld
)

# ---------------------------------------------------------------------------
# artefacts
# ---------------------------------------------------------------------------
find_program(ARM_OBJCOPY NAMES arm-none-eabi-objcopy HINTS ${ARM_TOOLCHAIN_DIR})

if(ARM_OBJCOPY)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${ARM_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_BINARY_DIR}/${TARGET}.bin
        COMMAND ${ARM_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET}> ${CMAKE_BINARY_DIR}/${TARGET}.hex
        COMMENT "Generating .bin / .hex")

    if(CMAKE_SIZE)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
            COMMENT "Firmware size")
    endif()
endif()
