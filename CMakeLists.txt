cmake_minimum_required(VERSION 3.13)

# ─────────────────────────────────────────────────
# pico-sdk のインポート（PICO_SDK_PATH 環境変数を使用）
# ─────────────────────────────────────────────────
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(panel_demo C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ─────────────────────────────────────────────────
# EveApps ライブラリのパス（EVEAPPS_PATH 環境変数）
# ─────────────────────────────────────────────────
if(NOT DEFINED ENV{EVEAPPS_PATH})
    message(FATAL_ERROR
        "環境変数 EVEAPPS_PATH が設定されていません。\n"
        "README.md の Step 4 を参照して設定してください。\n"
        "例: C:/EVE/EveApps"
    )
endif()

set(EVEAPPS ${CMAKE_SOURCE_DIR}/../EveApps)
if(DEFINED ENV{EVEAPPS_PATH})
    set(EVEAPPS $ENV{EVEAPPS_PATH})
endif()

set(EVE_HAL_DIR ${EVEAPPS}/common/eve_hal)

# ─────────────────────────────────────────────────
# EVE HAL ソースファイル（RP2040 向け）
# ─────────────────────────────────────────────────
set(EVE_HAL_SOURCES
    ${EVE_HAL_DIR}/EVE_Hal.c
    ${EVE_HAL_DIR}/EVE_CoDl.c
    ${EVE_HAL_DIR}/EVE_CoCmd.c
    ${EVE_HAL_DIR}/EVE_Util.c
    ${EVE_HAL_DIR}/EVE_HalImpl_RPIPICO.c
)

# ─────────────────────────────────────────────────
# 実行ファイル定義
# ─────────────────────────────────────────────────
add_executable(panel_demo
    main.c
    panel_demo.c
    ${EVE_HAL_SOURCES}
)

# ─────────────────────────────────────────────────
# インクルードパス
# ─────────────────────────────────────────────────
target_include_directories(panel_demo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${EVE_HAL_DIR}
    ${EVEAPPS}/common/eve_hal
)

# ─────────────────────────────────────────────────
# コンパイルオプション：BT817 / RP2040 向け定義
# ─────────────────────────────────────────────────
target_compile_definitions(panel_demo PRIVATE
    EVE_SUPPORT_CHIPID=EVE_BT817
    DISPLAY_RESOLUTION_WVGA
    EVE_SUPPORT_DISPLAY=WVGA
    EVE_PLATFORM_RP2040
    PICO_BOARD_HEADER_FILES="boards/pico.h"
)

# ─────────────────────────────────────────────────
# pico-sdk ライブラリリンク
# ─────────────────────────────────────────────────
target_link_libraries(panel_demo
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_gpio
    hardware_timer
)

# ─────────────────────────────────────────────────
# UF2 / map / bin ファイルも生成する
# ─────────────────────────────────────────────────
pico_add_extra_outputs(panel_demo)

# デバッグ用 USB シリアル出力（printf → USB）
pico_enable_stdio_usb(panel_demo    1)
pico_enable_stdio_uart(panel_demo   0)
