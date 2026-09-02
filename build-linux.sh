#!/bin/sh
set -e

if [ "$#" -ne 2 ]; then
    echo "Call this script providing: $0 GCC_TOOLCHAIN_PATH DIALOG_SDK_PATH"
    exit 1
fi

rm -rf build
cmake -DDEVICE_NAME=DA14531_App -DCMAKE_BUILD_TYPE=DEBUG -DCMAKE_TOOLCHAIN_FILE=gcc/arm-none-eabi.cmake -DGCC_TOOLCHAIN_PATH="$1" -DDIALOG_SDK_PATH="$2" -S . -B build
cmake --build build -j
