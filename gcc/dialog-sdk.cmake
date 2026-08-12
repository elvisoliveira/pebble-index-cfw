if("${DIALOG_SDK_PATH}" STREQUAL "")
    message(FATAL_ERROR "DIALOG_SDK_PATH not set")
endif()

if(NOT IS_ABSOLUTE ${DIALOG_SDK_PATH})
    message(STATUS "DIALOG_SDK_PATH is not absolute")
    set(DIALOG_SDK_PATH "${PROJECT_SOURCE_DIR}/${DIALOG_SDK_PATH}")
endif()

if(NOT EXISTS ${DIALOG_SDK_PATH})
    message(FATAL_ERROR "DIALOG_SDK_PATH does not exist (DIALOG_SDK_PATH: ${DIALOG_SDK_PATH})")
endif()

if(NOT IS_DIRECTORY ${DIALOG_SDK_PATH})
    message(FATAL_ERROR "DIALOG_SDK_PATH is not a directory (DIALOG_SDK_PATH: ${DIALOG_SDK_PATH})")
endif()

# Include Dialog SDK
set(DIALOG_SDK_INCLUDES
    ${DIALOG_SDK_PATH}/sdk/platform/include/CMSIS/5.6.0/Include
    ${DIALOG_SDK_PATH}/sdk/app_modules/api
    ${DIALOG_SDK_PATH}/sdk/ble_stack/controller/em
    ${DIALOG_SDK_PATH}/sdk/ble_stack/controller/llc
    ${DIALOG_SDK_PATH}/sdk/ble_stack/controller/lld
    ${DIALOG_SDK_PATH}/sdk/ble_stack/controller/llm
    ${DIALOG_SDK_PATH}/sdk/ble_stack/ea/api
    ${DIALOG_SDK_PATH}/sdk/ble_stack/em/api
    ${DIALOG_SDK_PATH}/sdk/ble_stack/hci/api
    ${DIALOG_SDK_PATH}/sdk/ble_stack/hci/src
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/att
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/att/attc
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/att/attm
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/att/atts
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/gap
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/gap/gapc
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/gap/gapm
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/gatt
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/gatt/gattc
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/gatt/gattm
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/l2c/l2cc
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/l2c/l2cm
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/smp
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/smp/smpc
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/smp/smpm
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/custom
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/custom/custs/api
    ${DIALOG_SDK_PATH}/sdk/ble_stack/rwble
    ${DIALOG_SDK_PATH}/sdk/ble_stack/rwble_hl
    ${DIALOG_SDK_PATH}/sdk/common_project_files
    ${DIALOG_SDK_PATH}/sdk/platform/arch
    ${DIALOG_SDK_PATH}/sdk/platform/arch/boot
    ${DIALOG_SDK_PATH}/sdk/platform/arch/boot/ARM
    ${DIALOG_SDK_PATH}/sdk/platform/arch/boot/GCC
    ${DIALOG_SDK_PATH}/sdk/platform/arch/compiler
    ${DIALOG_SDK_PATH}/sdk/platform/arch/compiler/ARM
    ${DIALOG_SDK_PATH}/sdk/platform/arch/compiler/GCC
    ${DIALOG_SDK_PATH}/sdk/platform/arch/ll
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/common/api
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/crypto
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/dbg/api
    # gtl/uart/dma headers are #included unconditionally by the SDK core
    # (rwip.c, arch_main.c, jump_table.c, spi_531.h); only the headers are
    # needed — the .c files are not built.
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/gtl/api
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/gtl/src
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/h4tl/api
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/arch_console
    ${DIALOG_SDK_PATH}/sdk/platform/driver/uart
    ${DIALOG_SDK_PATH}/sdk/platform/driver/dma
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/ke/api
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/ke/src
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/nvds/api
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/rf/api
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/rwip/api
    ${DIALOG_SDK_PATH}/sdk/platform/driver/adc
    ${DIALOG_SDK_PATH}/sdk/platform/driver/ble
    ${DIALOG_SDK_PATH}/sdk/platform/driver/gpio
    ${DIALOG_SDK_PATH}/sdk/platform/driver/hw_otpc
    ${DIALOG_SDK_PATH}/sdk/platform/driver/reg
    ${DIALOG_SDK_PATH}/sdk/platform/driver/spi
    ${DIALOG_SDK_PATH}/sdk/platform/driver/spi_flash
    ${DIALOG_SDK_PATH}/sdk/platform/driver/syscntl
    ${DIALOG_SDK_PATH}/sdk/platform/driver/trng
    ${DIALOG_SDK_PATH}/sdk/platform/driver/wkupct_quadec
    ${DIALOG_SDK_PATH}/sdk/platform/include
    ${DIALOG_SDK_PATH}/sdk/platform/system_library/include
    ${DIALOG_SDK_PATH}/third_party/hash
    ${DIALOG_SDK_PATH}/third_party/irng
    ${DIALOG_SDK_PATH}/third_party/rand
    ${DIALOG_SDK_PATH}/sdk/platform/utilities/otp_cs
    ${DIALOG_SDK_PATH}/sdk/platform/utilities/otp_hdr
)

set(DIALOG_SDK_SOURCES
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/hardfault_handler.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/nmi_handler.c
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/nvds/src/nvds.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/jump_table.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/arch_sleep.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/arch_system.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/arch_rom.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/main/arch_main.c
    ${DIALOG_SDK_PATH}/third_party/rand/chacha20.c
    ${DIALOG_SDK_PATH}/third_party/hash/hash.c
    ${DIALOG_SDK_PATH}/sdk/platform/utilities/otp_cs/otp_cs.c
    ${DIALOG_SDK_PATH}/sdk/platform/utilities/otp_hdr/otp_hdr.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/syscntl/syscntl.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/gpio/gpio.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/wkupct_quadec/wkupct_quadec.c
    # adc: referenced by arch_system.c (RF temperature calibration), not by the app
    ${DIALOG_SDK_PATH}/sdk/platform/driver/adc/adc_531.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/trng/trng.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/spi/spi_531.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/spi_flash/spi_flash.c
    ${DIALOG_SDK_PATH}/sdk/platform/driver/hw_otpc/hw_otpc_531.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/rwble/rwble.c
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/rwip/src/rwip.c
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/rf/src/ble_arp.c
    ${DIALOG_SDK_PATH}/sdk/platform/core_modules/rf/src/rf_531.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/host/att/attm/attm_db_128.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/custom/custom_common.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/custom/custs/src/custs1.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/custom/custs/src/custs1_task.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/prf.c
    ${DIALOG_SDK_PATH}/sdk/ble_stack/profiles/prf_utils.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_bond_db/app_bond_db.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_common/app.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_common/app_task.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_common/app_msg_utils.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_common/app_utils.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_custs/app_customs.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_custs/app_customs_task.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_custs/app_customs_common.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_default_hnd/app_default_handlers.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_easy/app_easy_msg_utils.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_easy/app_easy_timer.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_easy/app_easy_security.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_entry/app_entry_point.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_sec/app_security.c
    ${DIALOG_SDK_PATH}/sdk/app_modules/src/app_sec/app_security_task.c
    # SoC-specific boot — the ring is a DA14535 (the *_531.c drivers above are
    # family-wide and serve both; only the boot has its own file for the 535)
    ${DIALOG_SDK_PATH}/sdk/platform/arch/boot/system_DA14535.c
    ${DIALOG_SDK_PATH}/sdk/platform/arch/boot/GCC/ivtable_DA14535.S
    ${DIALOG_SDK_PATH}/sdk/platform/arch/boot/GCC/startup_DA14535.S
)
