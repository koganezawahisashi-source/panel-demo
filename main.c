/**
 * main.c
 * BT817 操作パネルデモ - RP2040 エントリポイント
 * Target: RP2040 + BT817, 800x480
 *
 * 依存ライブラリ:
 *   - Bridgetek EVE_HAL_Library (EveApps)
 *   - pico-sdk
 *
 * SPI 接続例（GPIO番号は基板レイアウトに合わせて変更）:
 *   SCK  → GP2
 *   MOSI → GP3
 *   MISO → GP4
 *   CS   → GP5
 *   PD   → GP6  (Power Down / Reset)
 *   INT  → GP7  (割り込み、未使用時は省略可)
 */

#include "panel_demo.h"
#include "EVE_HAL.h"
#include "EVE_CoCmd.h"
#include "pico/stdlib.h"
#include <stdio.h>

/* ─────────────────────────────────────────────
   GPIO / SPI ピン定義
───────────────────────────────────────────── */
#define PIN_SCK     2
#define PIN_MOSI    3
#define PIN_MISO    4
#define PIN_CS      5
#define PIN_PD      6

/* SPI クロック（BT817 最大 30MHz、安全側で 20MHz） */
#define SPI_FREQ_HZ (20 * 1000 * 1000)

/* ─────────────────────────────────────────────
   BT817 ディスプレイタイミング（800×480 WVGA）
───────────────────────────────────────────── */
static const EVE_HalParameters_t s_hal_params = {
    /* SPI */
    .SpiCsPin   = PIN_CS,
    .SpiPdPin   = PIN_PD,
    .SpiClockrateKHz = (SPI_FREQ_HZ / 1000),
};

static const EVE_BootupParameters_t s_bootup = {
    .Display = {
        .Width      = 800,
        .Height     = 480,
        .HCycle     = 928,
        .HOffset    = 88,
        .HSync0     = 0,
        .HSync1     = 48,
        .VCycle     = 525,
        .VOffset    = 32,
        .VSync0     = 0,
        .VSync1     = 3,
        .PCLK       = 2,
        .Swizzle    = 0,
        .PCLKPol    = 1,
        .CSpread    = 0,
        .Dither     = 1,
    },
    .ExternalOsc = true,
};

/* ─────────────────────────────────────────────
   メイン
───────────────────────────────────────────── */
int main(void)
{
    /* pico-sdk 標準初期化 */
    stdio_init_all();

    /* EVE HAL 初期化 */
    EVE_HalContext host;
    EVE_HalContext *phost = &host;

    EVE_Hal_initialize();

    if (!EVE_Hal_open(phost, &s_hal_params)) {
        /* ハードウェア接続失敗 */
        printf("EVE_Hal_open failed\n");
        for (;;) tight_loop_contents();
    }

    if (!EVE_Util_bootupConfig(phost, &s_bootup)) {
        printf("EVE bootup failed\n");
        EVE_Hal_close(phost);
        EVE_Hal_release();
        for (;;) tight_loop_contents();
    }

    /* タッチ感度設定（BT817 容量タッチコントローラ） */
    EVE_Hal_wr8(phost, REG_TOUCH_MODE, EVE_TMODE_CONTINUOUS);
    EVE_Hal_wr16(phost, REG_TOUCH_RZTHRESH, 1200);

    /* アプリケーション状態初期化 */
    AppState_t app;
    panel_demo_init(&app);

    printf("Panel demo started\n");

    /* ─── メインループ ─── */
    uint32_t last_tick = to_ms_since_boot(get_absolute_time());

    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        /* 状態更新（RTC・アニメーション・認証タイマー） */
        panel_demo_update(&app, now);

        /* タッチ処理 */
        panel_demo_touch(phost, &app, now);

        /* 描画 */
        panel_demo_render(phost, &app, now);

        /* フレームレート制御：約 30fps（33ms/frame） */
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - now;
        if (elapsed < 33) {
            sleep_ms(33 - elapsed);
        }

        last_tick = now;
        (void)last_tick;
    }

    /* 到達しない */
    EVE_Hal_close(phost);
    EVE_Hal_release();
    return 0;
}
