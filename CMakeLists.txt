cmake_minimum_required(VERSION 3.16)

project(${DEVICE_NAME} LANGUAGES C ASM)

# Include Dialog SDK
include(${CMAKE_SOURCE_DIR}/gcc/dialog-sdk.cmake)

add_executable(${PROJECT_NAME}
    ${DIALOG_SDK_SOURCES}
    # DA14535 system library (SDK 6.0.22 ships this as source, not in da14535.lib):
    # provides patch_func, my_custom_msg_handlers, lld_force_timeout_refresh, etc.
    ${DIALOG_SDK_PATH}/sdk/platform/system_library/src/DA14535/system_library_535.c
    # Source
    src/user_custs_config.c
    src/user_custs1_def.c
    src/user_periph_setup.c
    src/interrupts.c
    src/user_app.c
    src/ble_handlers.c
    src/cfw_ctrl.c
    src/led.c
    src/clip_tx.c
    src/adpcm.c
    src/mic.c
    src/chacha.c
    src/secret.c
)

set(GLOBAL_DEBUG_OPTIONS -mthumb -mcpu=cortex-m0plus -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -flto -Wall -Werror -g3)

# The ring's SoC is a DA14535. In the SDK the 535 is a VARIANT of the 531: the
# `#if defined(__DA14535__)` blocks are nested inside `#if defined(__DA14531__)`
# (e.g. arch.h:119-120), so both defines go together — do not swap one for the
# other.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C,ASM>:__DA14531__>
    $<$<COMPILE_LANGUAGE:C,ASM>:__DA14535__>
    $<$<COMPILE_LANGUAGE:C>:GPIO_DRV_PIN_ALLOC_MON_DISABLED>
)

# Board/feature defines, injected from the command line. Empty = the shipping RING build.
# For the DA14535 devkit bench build:  -DKIT_DEFS=TARGET_KIT
# (button P0_11 = J4 "INT" = SW2's pin, kit flash pins, RGB LED on P0_9/P0_8/P0_6).
# board_config.h is the authority on the pinout; keep this line in step with it.
# See board_config.h (TARGET_KIT) and da1458x_config_basic.h (WITH_CTRL_POINT, default on).
if(DEFINED KIT_DEFS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${KIT_DEFS})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
# C, Assembly
    $<$<COMPILE_LANGUAGE:C,ASM>:${GLOBAL_DEBUG_OPTIONS}>
# C only
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/components/config/inc/da1458x_config_basic.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/components/config/inc/da1458x_config_advanced.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/components/config/inc/user_config.h>
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
    $<$<COMPILE_LANGUAGE:C>:-Wno-int-conversion>
    $<$<COMPILE_LANGUAGE:C>:-Wno-unused-variable>
    $<$<COMPILE_LANGUAGE:C>:-Wno-address>
# Assembly
    $<$<COMPILE_LANGUAGE:ASM>:-xassembler-with-cpp>
)

set(USER_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/components/config/inc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${DIALOG_SDK_INCLUDES}>
    $<$<COMPILE_LANGUAGE:C>:${USER_INCLUDES}>
)

# Linker file needs to be preprocessed
set(LINKER_SCRIPT_IN "${PROJECT_SOURCE_DIR}/gcc/ldscript_DA14531.lds.S")
set(LINKER_SCRIPT_OUT "ldscript.lds")

# Linker script includes
set(LINKER_SCRIPT_INCLUDES
    -I ${CMAKE_SOURCE_DIR}/components/config/inc
    -I ${PROJECT_SOURCE_DIR}/gcc/
    -I ${DIALOG_SDK_PATH}/sdk/common_project_files
)

function(generate_linker_script target_name linker_script_includes linker_script_defs linker_script_input linker_script_output)
    if(NOT EXISTS ${linker_script_input})
        message(FATAL_ERROR "Linker input file not found.")
    endif()

    add_custom_command(OUTPUT ${linker_script_output}
                    COMMAND ${CMAKE_C_COMPILER} ${linker_script_includes} ${linker_script_defs} -E -P -c ${linker_script_input} -o ${linker_script_output}
                    DEPENDS ${linker_script_input}
                    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                    COMMENT "Generating linker script: ${linker_script_output}"
                    VERBATIM)

    add_custom_target(linkerscript DEPENDS ${linker_script_output} VERBATIM)

    # Add linker preprocessed file as dependency
    add_dependencies(${target_name} linkerscript)
    # Add dependency on linker input file
    set_target_properties(${target_name} PROPERTIES LINK_DEPENDS ${linker_script_input})
endfunction(generate_linker_script)

# mem_DA14531.lds serves both: the regions come from da1458x_scatter_config.h,
# which already branches on __DA14535__ (e.g. __SCT_OLD_BLE_BASE 0x07FCDC00 on the
# 535 vs 0x07FC9C00 on the 531) — that's where the 64 KB come from.
generate_linker_script(${PROJECT_NAME} "${LINKER_SCRIPT_INCLUDES}" "-D__DA14531__;-D__DA14535__" "${LINKER_SCRIPT_IN}" "${LINKER_SCRIPT_OUT}")

target_link_directories(${PROJECT_NAME} PRIVATE
    ${DIALOG_SDK_PATH}/sdk/platform/system_library/output/Keil_5/
    # ld resolves the linker script's `INCLUDE da14535_symbols.lds` via -L
    ${DIALOG_SDK_PATH}/sdk/common_project_files/misc/
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    :da14535.lib
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${GLOBAL_DEBUG_OPTIONS}
    "-T${LINKER_SCRIPT_OUT}"
    "-Xlinker"
    "--gc-sections"
    "--specs=nano.specs"
    "--specs=nosys.specs"
    "-Wl,--no-wchar-size-warning" # Suppress the warning from linking Dialog's system library
    "-Wl,-Map,${PROJECT_NAME}.map" # Produce map file
)

# Post build actions

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Print output application size"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Oihex "$<TARGET_FILE:${PROJECT_NAME}>" ${PROJECT_NAME}.hex
    COMMENT "Convert output to hex"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Obinary "$<TARGET_FILE:${PROJECT_NAME}>" ${PROJECT_NAME}.bin
    COMMENT "Convert output to binary"
)
