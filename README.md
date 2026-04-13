# IDM2040-7A 開発環境構築ガイド（Windows + VS Code）

対象ハードウェア：Bridgetek **IDM2040-7A**（RP2040 + BT817、800×480 WVGA）  
ビルドツール：**pico-sdk + CMake + ARM GCC**  
エディタ：**Visual Studio Code**

---

## 必要なものの全体像

| ツール | 役割 |
|--------|------|
| pico-sdk Windows インストーラー | ARM GCC / CMake / Ninja / pico-sdk を一括導入 |
| EveApps（Bridgetek） | BT817 を動かす EVE HAL ライブラリ |
| Visual Studio Code | エディタ + ビルド環境（インストーラーが自動設定） |
| Git | ライブラリの取得 |

---

## Step 1 — pico-sdk Windows インストーラーを導入する

1. ブラウザで以下を開く  
   `https://github.com/raspberrypi/pico-setup-windows/releases`

2. 最新版の **`pico-setup-windows-x64-standalone.exe`** をダウンロード

3. ダブルクリックして実行 → 「はい」→ デフォルトのまま **Install**

4. インストール後、新しいコマンドプロンプトを開いて確認

   ```
   cmake --version
   arm-none-eabi-gcc --version
   ```

   バージョン番号が表示されれば OK。

> **注意：** このインストーラーは VS Code と CMake Tools 拡張機能も自動でインストールします。

---

## Step 2 — EveApps（EVE ライブラリ）を取得する

1. **保存場所を決める**（例：`C:\EVE\EveApps`）

2. コマンドプロンプトまたは Git Bash で実行

   ```
   git clone https://github.com/Bridgetek/EveApps.git C:\EVE\EveApps
   ```

3. 完了後、以下のファイルが存在することを確認

   ```
   C:\EVE\EveApps\common\eve_hal\EVE_Hal.c
   C:\EVE\EveApps\common\eve_hal\EVE_HalImpl_RP2040.c
   ```

   > ファイル名は `EVE_HalImpl_RP2040.c` です（`RPIPICO` ではありません）。

---

## Step 3 — 環境変数を設定する

1. スタートメニューで「**環境変数**」と検索 → 「**システム環境変数の編集**」をクリック

2. 「**環境変数(N)...**」→「ユーザー環境変数」の「**新規(N)**」

3. 以下の2つを追加

   | 変数名 | 値 |
   |--------|----|
   | `EVEAPPS_PATH` | `C:\EVE\EveApps`（EveApps を置いた場所） |
   | `PICO_SDK_PATH` | `C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\pico-sdk` |

   > `PICO_SDK_PATH` はインストーラーが自動設定している場合があります。  
   > コマンドプロンプトで `echo %PICO_SDK_PATH%` を実行して確認してください。

4. **OK** → **OK** で閉じる

---

## Step 4 — プロジェクトファイルを配置する

プロジェクトフォルダ `C:\Users\<ユーザー名>\panel-demo` に以下のファイルを用意します。

```
panel-demo/
├── main.c
├── panel_demo.c
├── panel_demo.h
├── EVE.h          ← 必須：自作ラッパーファイル（後述）
└── CMakeLists.txt
```

### EVE.h（新規作成）

EveApps には `EVE.h` が存在しないため、プロジェクトフォルダに以下の内容で作成します。

```c
/**
 * EVE.h — EveApps ヘッダ集約ラッパー
 */
#ifndef EVE_H
#define EVE_H

#include "EVE_Platform.h"
#include "EVE_Hal.h"
#include "EVE_CoCmd.h"
#include "EVE_CoDl.h"
#include "EVE_Util.h"
#include "EVE_GpuDefs.h"

/* 互換定義 */
#define EVE_RECTS      RECTS
#define EVE_LINES      LINES
#define EVE_LINE_STRIP LINE_STRIP
#define EVE_POINTS     POINTS

#define EVE_OPT_CENTER  OPT_CENTER
#define EVE_OPT_CENTERY OPT_CENTERY
#define EVE_OPT_CENTERX OPT_CENTERX
#define EVE_OPT_RIGHTX  OPT_RIGHTX

#define EVE_TMODE_CONTINUOUS TOUCHMODE_CONTINUOUS
#define EVE_CoCmd_bgcolor    EVE_CoCmd_bgColor

#endif /* EVE_H */
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.13)

file(TO_CMAKE_PATH "$ENV{PICO_SDK_PATH}" PICO_SDK_PATH_FIXED)
include(${PICO_SDK_PATH_FIXED}/external/pico_sdk_import.cmake)

project(panel_demo C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT DEFINED ENV{EVEAPPS_PATH})
    message(FATAL_ERROR "環境変数 EVEAPPS_PATH が設定されていません。Step 3 を参照してください。")
endif()

file(TO_CMAKE_PATH "$ENV{EVEAPPS_PATH}" EVEAPPS)
set(EVE_HAL_DIR ${EVEAPPS}/common/eve_hal)

set(EVE_HAL_SOURCES
    ${EVE_HAL_DIR}/EVE_Hal.c
    ${EVE_HAL_DIR}/EVE_Cmd.c
    ${EVE_HAL_DIR}/EVE_CoDl.c
    ${EVE_HAL_DIR}/EVE_CoCmd.c
    ${EVE_HAL_DIR}/EVE_CoCmd_IO.c
    ${EVE_HAL_DIR}/EVE_CoCmd_State.c
    ${EVE_HAL_DIR}/EVE_CoCmd_Widgets.c
    ${EVE_HAL_DIR}/EVE_Util.c
    ${EVE_HAL_DIR}/EVE_LoadFile_FATFS.c
    ${EVE_HAL_DIR}/EVE_MediaFifo.c
    ${EVE_HAL_DIR}/EVE_GpuDefs_BT81X.c
    ${EVE_HAL_DIR}/EVE_HalImpl_RP2040.c
    ${EVEAPPS}/common/application/fatfs/source/ff.c
    ${EVEAPPS}/common/application/fatfs/source/ffsystem.c
    ${EVEAPPS}/common/application/fatfs/source/diskio_rp2040.c
)

add_executable(panel_demo
    main.c
    panel_demo.c
    ${EVE_HAL_SOURCES}
)

target_include_directories(panel_demo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${EVE_HAL_DIR}
    ${EVEAPPS}/common/application/fatfs/source
)

target_compile_definitions(panel_demo PRIVATE
    EVE_GRAPHICS_IDM2040
    EVE_PLATFORM_RP2040
    EVE_DISPLAY_WVGA
)

target_link_libraries(panel_demo
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_gpio
    hardware_timer
)

pico_add_extra_outputs(panel_demo)
pico_enable_stdio_usb(panel_demo  1)
pico_enable_stdio_uart(panel_demo 0)
```

### main.c のポイント（EVE HAL API の正しい使い方）

EveApps の RP2040 向け HAL は一般的な EVE サンプルコードと名称が異なります。

```c
/* 型名は EVE_HalParameters（_t なし） */
static EVE_HalParameters s_hal_params;

/* 初期化順序 */
EVE_Hal_initialize();
EVE_Hal_defaults(&s_hal_params);          /* デフォルト値を先に取得 */
s_hal_params.SpiCsPin     = PIN_CS;
s_hal_params.PowerDownPin = PIN_PD;       /* .SpiPdPin ではない */
/* SpiClockrateKHz は RP2040 版に存在しない（25MHz にハードコード済み） */

EVE_Hal_open(phost, &s_hal_params);
EVE_Util_bootupConfig(phost);             /* 引数は phost のみ */
```

---

## Step 5 — VS Code を開く（重要）

> **必ずスタートメニューの「Pico - Visual Studio Code」から起動してください。**  
> 通常の VS Code アイコンから起動すると CMake / Ninja の PATH が通らずビルドできません。

1. スタートメニュー → 「**Pico - Visual Studio Code**」をクリック

2. **ファイル → フォルダーを開く** → `C:\Users\<ユーザー名>\panel-demo` を選択

3. 「このフォルダーの作成者を信頼しますか？」→ **はい、作成者を信頼します**

---

## Step 6 — CMake の設定とビルド

1. **Ctrl+Shift+P** → `CMake: Configure` を実行

2. コンパイラの選択画面が出たら **`GCC for arm-none-eabi`** を選択  
   （Visual Studio 系のコンパイラが表示されても選ばない）

3. ターミナルに `-- Configuring done` と表示されれば設定完了

4. **Ctrl+Shift+P** → `CMake: Build` を実行

5. `Build finished with exit code 0` が表示されれば成功

   > `ffsystem.c` で `malloc` の警告が出ますが、エラーではないので無視して OK。

6. 生成ファイル：`build\panel_demo.uf2`

---

## Step 7 — IDM2040-7A に書き込む

1. IDM2040-7A の **BOOTSEL ボタンを押しながら** USB ケーブルを PC に接続

2. PC に **RPI-RP2** というドライブが表示される

3. `build\panel_demo.uf2` を RPI-RP2 ドライブにドラッグ＆ドロップ

4. 自動的に再起動し、パネルデモが起動する

---

## トラブルシューティング

| 症状 | 原因 | 対処 |
|------|------|------|
| `cmake --version` が動かない | PATH が通っていない | PC 再起動後に再試行 |
| `Cannot find EVE_Hal.c` | EVEAPPS_PATH が未設定 | Step 3 を再確認、VS Code 再起動 |
| `PICO_SDK_PATH` が空 | インストーラーで設定されなかった | Step 3 で手動設定 |
| ビルドが通らない（ninja エラー） | 通常の VS Code から起動している | 「Pico - Visual Studio Code」から起動し直す |
| `GCC for arm-none-eabi` が出ない | CMake: Configure を実行していない | Ctrl+Shift+P → CMake: Configure を先に実行 |
| `Cannot find EVE_HalImpl_RP2040.c` | ファイル名を RPIPICO にしている | RP2040 が正しいファイル名 |
| ビルドは通るが画面が映らない | SPI 配線ミス | `main.c` の GPIO 番号を確認 |
| RPI-RP2 ドライブが出ない | BOOTSEL を押し忘れた | BOOTSEL を押しながら再接続 |
