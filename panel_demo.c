/**
 * panel_demo.c
 * BT817 操作パネルデモ - 描画・タッチ・状態機械
 * Target: RP2040 + BT817, 800x480
 */

#include "panel_demo.h"
#include "EVE_CoCmd.h"
#include <stdio.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════
   内部ヘルパー
═══════════════════════════════════════════════════════════ */

/* 塗りつぶし矩形（タグなし） */
static void draw_rect(EVE_HalContext *phost,
                      int16_t x, int16_t y, int16_t w, int16_t h,
                      uint32_t color)
{
    SET_COLOR(phost, color);
    EVE_CoDl_begin(phost, EVE_RECTS);
    EVE_CoDl_vertex2f(phost, x * 16, y * 16);
    EVE_CoDl_vertex2f(phost, (x + w) * 16, (y + h) * 16);
    EVE_CoDl_end(phost);
}

/* 水平線 */
static void draw_hline(EVE_HalContext *phost,
                       int16_t x, int16_t y, int16_t w,
                       uint32_t color, uint8_t thickness)
{
    SET_COLOR(phost, color);
    EVE_CoDl_lineWidth(phost, thickness * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, x * 16, y * 16);
    EVE_CoDl_vertex2f(phost, (x + w) * 16, y * 16);
    EVE_CoDl_end(phost);
}

/* プログレスバー（背景＋前景） */
static void draw_bar(EVE_HalContext *phost,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     uint8_t pct, uint32_t fg_color)
{
    draw_rect(phost, x, y, w, h, COL_BG);
    if (pct > 100) pct = 100;
    draw_rect(phost, x, y, (int16_t)(w * pct / 100), h, fg_color);
}

/* ハンバーガーアイコン描画（左 L1 MENU） */
static void draw_icon_menu(EVE_HalContext *phost, int16_t bx, int16_t by)
{
    /* 3本横線 */
    int16_t lx0 = bx + 22, lx1 = bx + 68;
    SET_COLOR(phost, COL_TXT2);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, lx0 * 16, (by + 30) * 16);
    EVE_CoDl_vertex2f(phost, lx1 * 16, (by + 30) * 16);
    EVE_CoDl_vertex2f(phost, lx0 * 16, (by + 45) * 16);
    EVE_CoDl_vertex2f(phost, lx1 * 16, (by + 45) * 16);
    EVE_CoDl_vertex2f(phost, lx0 * 16, (by + 60) * 16);
    EVE_CoDl_vertex2f(phost, lx1 * 16, (by + 60) * 16);
    EVE_CoDl_end(phost);
}

/* 上矢印アイコン（左 L2 UP） */
static void draw_icon_up(EVE_HalContext *phost, int16_t bx, int16_t by)
{
    int16_t cx = bx + 45;
    SET_COLOR(phost, COL_TXT2);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    /* 左辺 */
    EVE_CoDl_vertex2f(phost, cx * 16,        (by + 28) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 22) * 16, (by + 60) * 16);
    /* 右辺 */
    EVE_CoDl_vertex2f(phost, cx * 16,        (by + 28) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 68) * 16, (by + 60) * 16);
    EVE_CoDl_end(phost);
}

/* OK アイコン（左 L3） */
static void draw_icon_ok(EVE_HalContext *phost, int16_t bx, int16_t by)
{
    SET_COLOR(phost, COL_GREEN);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    /* チェックマーク: ← 下り → 上り */
    EVE_CoDl_vertex2f(phost, (bx + 22) * 16, (by + 48) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 38) * 16, (by + 62) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 38) * 16, (by + 62) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 68) * 16, (by + 28) * 16);
    EVE_CoDl_end(phost);
}

/* 下矢印アイコン（左 L4 DOWN） */
static void draw_icon_down(EVE_HalContext *phost, int16_t bx, int16_t by)
{
    int16_t cx = bx + 45;
    SET_COLOR(phost, COL_TXT2);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, (bx + 22) * 16, (by + 28) * 16);
    EVE_CoDl_vertex2f(phost, cx * 16,         (by + 62) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 68) * 16,  (by + 28) * 16);
    EVE_CoDl_vertex2f(phost, cx * 16,          (by + 62) * 16);
    EVE_CoDl_end(phost);
}

/* 左固定ボタン 4 個を描画 */
static void draw_left_buttons(EVE_HalContext *phost)
{
    static const int16_t ys[4] = { BTN_Y0, BTN_Y1, BTN_Y2, BTN_Y3 };
    static const uint8_t tags[4] = { TAG_L1, TAG_L2, TAG_L3, TAG_L4 };

    for (int i = 0; i < 4; i++) {
        int16_t y = ys[i];

        /* ボタン背景 */
        EVE_CoDl_tag(phost, tags[i]);
        draw_rect(phost, 0, y, BTN_W, BTN_H, COL_SURF);

        /* 下境界線 */
        draw_hline(phost, 0, y + BTN_H - 1, BTN_W, COL_BORDER, 1);

        /* 右境界線 */
        SET_COLOR(phost, COL_BORDER);
        EVE_CoDl_lineWidth(phost, 1 * 16);
        EVE_CoDl_begin(phost, EVE_LINES);
        EVE_CoDl_vertex2f(phost, (BTN_W - 1) * 16, y * 16);
        EVE_CoDl_vertex2f(phost, (BTN_W - 1) * 16, (y + BTN_H) * 16);
        EVE_CoDl_end(phost);
    }

    /* タグをリセットしてからアイコン描画（タッチ領域と重なっても問題なし） */
    EVE_CoDl_tag(phost, TAG_NONE);
    draw_icon_menu(phost, 0,      BTN_Y0);
    draw_icon_up  (phost, 0,      BTN_Y1);
    draw_icon_ok  (phost, 0,      BTN_Y2);
    draw_icon_down(phost, 0,      BTN_Y3);
}

/* 右可変ボタン 1 個を描画 */
static void draw_right_btn(EVE_HalContext *phost,
                           uint8_t tag, int16_t y,
                           const char *label, uint32_t bg_color,
                           bool enabled)
{
    uint32_t actual_bg  = enabled ? bg_color : COL_SURF;
    uint32_t label_col  = enabled ? COL_TXT  : COL_TXT3;

    EVE_CoDl_tag(phost, enabled ? tag : TAG_NONE);
    draw_rect(phost, RIGHT_X, y, BTN_W, BTN_H, actual_bg);

    /* 左・下境界線 */
    SET_COLOR(phost, COL_BORDER);
    EVE_CoDl_lineWidth(phost, 1 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, RIGHT_X * 16,           y * 16);
    EVE_CoDl_vertex2f(phost, RIGHT_X * 16,           (y + BTN_H) * 16);
    EVE_CoDl_vertex2f(phost, RIGHT_X * 16,           (y + BTN_H - 1) * 16);
    EVE_CoDl_vertex2f(phost, (RIGHT_X + BTN_W) * 16, (y + BTN_H - 1) * 16);
    EVE_CoDl_end(phost);

    if (label && label[0]) {
        SET_COLOR(phost, label_col);
        EVE_CoCmd_text(phost,
                       RIGHT_X + BTN_W / 2,
                       y + BTN_H / 2,
                       FONT_MD, EVE_OPT_CENTER, label);
    }
    EVE_CoDl_tag(phost, TAG_NONE);
}

/* コンテンツ領域背景をクリア */
static void clear_content(EVE_HalContext *phost)
{
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
}

/* カード（角丸なし近似：背景＋枠線） */
static void draw_card(EVE_HalContext *phost,
                      int16_t x, int16_t y, int16_t w, int16_t h)
{
    draw_rect(phost, x, y, w, h, COL_CARD);
    /* 枠線（4辺） */
    SET_COLOR(phost, COL_BORDER);
    EVE_CoDl_lineWidth(phost, 1 * 16);
    EVE_CoDl_begin(phost, EVE_LINE_STRIP);
    EVE_CoDl_vertex2f(phost,  x        * 16,  y        * 16);
    EVE_CoDl_vertex2f(phost, (x+w)     * 16,  y        * 16);
    EVE_CoDl_vertex2f(phost, (x+w)     * 16, (y+h)     * 16);
    EVE_CoDl_vertex2f(phost,  x        * 16, (y+h)     * 16);
    EVE_CoDl_vertex2f(phost,  x        * 16,  y        * 16);
    EVE_CoDl_end(phost);
}

/* ステータスチップ（小さな色付きバッジ） */
static void draw_chip(EVE_HalContext *phost,
                      int16_t x, int16_t y,
                      const char *label, uint32_t color)
{
    int16_t cw = (int16_t)(strlen(label) * 8 + 16);
    draw_rect(phost, x, y, cw, 20, color & 0x1A1A1AUL);  /* 暗い背景 */
    SET_COLOR(phost, color);
    EVE_CoCmd_text(phost, x + cw / 2, y + 10, FONT_SM,
                   EVE_OPT_CENTER, label);
}

/* ═══════════════════════════════════════════════════════════
   初期化
═══════════════════════════════════════════════════════════ */

void panel_demo_init(AppState_t *app)
{
    memset(app, 0, sizeof(*app));

    app->screen           = SCREEN_LOCK;
    app->panel            = PANEL_HOME;
    app->role             = ROLE_NONE;
    app->user_name        = "";
    app->user_role_str    = "";
    app->lock_cursor      = 0;
    app->menu_open        = false;
    app->menu_cursor      = 0;

    /* 機械初期値 */
    app->machine          = MACHINE_RUNNING;
    app->set_rpm          = 1200;
    app->actual_rpm       = 1200;
    app->feed_idx         = 0;    /* 25% */
    app->spindle_load     = 68;
    app->spindle_temp     = 72;
    app->coolant_temp     = 48;
    app->env_temp         = 24;
    app->parts_total      = 1247;
    app->parts_today      = 247;
    app->op_seconds       = 30735; /* 08:32:15 */

    /* アラーム初期データ */
    app->alarms[0] = (AlarmRecord_t){
        .level      = ALARM_LVL_WARN,
        .title      = "Coolant Temp High",
        .detail     = "Coolant temp exceeded 45C limit. Now 48C.",
        .time_str   = "2026-03-31 08:15",
        .active     = true,
        .admin_only = true,
    };
    app->alarms[1] = (AlarmRecord_t){
        .level      = ALARM_LVL_INFO,
        .title      = "Maintenance Due",
        .detail     = "Spindle oil change due 2026-04-01.",
        .time_str   = "2026-03-31 10:00",
        .active     = true,
        .admin_only = false,
    };
    app->alarm_count  = 2;
    app->alarm_filter = ALARM_FILTER_ALL;
    app->alarm_cursor = 0;

    /* 認証演出時間 */
    app->auth_duration_ms = 1500;

    /* RTC 初期値（実機は RTC ペリフェラルから取得） */
    app->rtc_h = 10;
    app->rtc_m = 30;
    app->rtc_s = 0;
}

/* ═══════════════════════════════════════════════════════════
   状態更新（毎フレーム呼び出し）
═══════════════════════════════════════════════════════════ */

void panel_demo_update(AppState_t *app, uint32_t tick_ms)
{
    /* 簡易 RTC：1秒ごとにインクリメント */
    if (tick_ms - app->rtc_last_tick >= 1000) {
        app->rtc_last_tick = tick_ms;
        app->rtc_s++;
        if (app->rtc_s >= 60) { app->rtc_s = 0; app->rtc_m++; }
        if (app->rtc_m >= 60) { app->rtc_m = 0; app->rtc_h++; }
        if (app->rtc_h >= 24) { app->rtc_h = 0; }

        /* 稼働中なら積算秒をインクリメント */
        if (app->machine == MACHINE_RUNNING) {
            app->op_seconds++;
        }
    }

    /* 認証演出完了判定 */
    if (app->screen == SCREEN_AUTH) {
        if (tick_ms - app->auth_tick_start >= app->auth_duration_ms) {
            app->role          = app->auth_pending;
            app->user_name     = (app->role == ROLE_ADMIN)
                                 ? "Y.Yamada" : "I.Tanaka";
            app->user_role_str = (app->role == ROLE_ADMIN)
                                 ? "Admin" : "Operator";
            app->screen        = SCREEN_MAIN;
            app->panel         = PANEL_HOME;
            app->menu_open     = false;
        }
    }

    /* アニメーション tick */
    app->anim_tick = tick_ms;

    /* トースト タイムアウト（3秒） */
    if (app->toast_visible &&
        tick_ms - app->toast_show_tick >= 3000) {
        app->toast_visible = false;
    }
}

/* ─── トースト表示ヘルパー ─── */
static void show_toast(AppState_t *app, const char *msg, uint32_t tick_ms)
{
    strncpy(app->toast_msg, msg, sizeof(app->toast_msg) - 1);
    app->toast_msg[sizeof(app->toast_msg) - 1] = '\0';
    app->toast_show_tick = tick_ms;
    app->toast_visible   = true;
}

/* ═══════════════════════════════════════════════════════════
   ロック画面描画
═══════════════════════════════════════════════════════════ */

static void render_lock(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    /* 背景 */
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);

    /* ─ RFID アニメーション リング ─ */
    uint32_t t      = tick_ms % 2400;
    int16_t  cx     = CONT_X + CONT_W / 2;
    int16_t  cy     = SCR_H / 2 - 40;

    /* 3リング（delay 0, 600, 1200 ms） */
    static const uint16_t delays[3]  = { 0, 600, 1200 };
    static const uint16_t radii[3]   = { 30, 50, 70 };

    for (int i = 0; i < 3; i++) {
        uint32_t phase = (t + 2400 - delays[i]) % 2400;
        /* 0→1 でフェードイン→アウト */
        uint8_t alpha;
        if (phase < 1200) {
            alpha = (uint8_t)(179 - phase * 179 / 1200);
        } else {
            alpha = 0;
        }
        if (alpha == 0) continue;

        EVE_CoDl_colorA(phost, alpha);
        SET_COLOR(phost, COL_ACCENT);
        EVE_CoDl_lineWidth(phost, 2 * 16);

        /* 円をポリライン近似（32 頂点） */
        EVE_CoDl_begin(phost, EVE_LINE_STRIP);
        for (int j = 0; j <= 32; j++) {
            /* 固定小数点三角関数（簡易 Look-Up 不使用、ビルド環境に math.h を想定） */
            float angle = (float)j * 6.2832f / 32.0f;
            int16_t px = (int16_t)(cx + radii[i] * __builtin_cosf(angle));
            int16_t py = (int16_t)(cy + radii[i] * __builtin_sinf(angle));
            EVE_CoDl_vertex2f(phost, px * 16, py * 16);
        }
        EVE_CoDl_end(phost);
    }
    EVE_CoDl_colorA(phost, 255);

    /* ─ 中央アイコン円 ─ */
    draw_rect(phost, cx - 26, cy - 26, 52, 52, COL_CARD);
    SET_COLOR(phost, COL_ACCENT);
    EVE_CoCmd_text(phost, cx, cy, FONT_LG, EVE_OPT_CENTER, "ID");

    /* ─ ロゴ ─ */
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx, 60, FONT_SM, EVE_OPT_CENTER,
                   "NEXUS CONTROL SYSTEMS");

    /* ─ ガイドテキスト ─ */
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, cy + 90, FONT_MD, EVE_OPT_CENTER,
                   "Present RFID card or select role");

    /* ─ 時計 ─ */
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             app->rtc_h, app->rtc_m, app->rtc_s);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx, SCR_H - 60, FONT_XL, EVE_OPT_CENTER, time_str);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx, SCR_H - 24, FONT_SM, EVE_OPT_CENTER,
                   "2026/04/10");

    /* ─ 右ボタン：OPR / ADM ─ */
    bool opr_sel = (app->lock_cursor == 0);
    bool adm_sel = (app->lock_cursor == 1);
    draw_right_btn(phost, TAG_R1, BTN_Y0, "OPR",
                   opr_sel ? COL_ACCENT : COL_CARD, true);
    draw_right_btn(phost, TAG_R2, BTN_Y1, "ADM",
                   adm_sel ? COL_ACCENT : COL_CARD, true);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "",   COL_SURF, false);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "",   COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   認証オーバーレイ描画
═══════════════════════════════════════════════════════════ */

static void render_auth(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    /* 半透明オーバーレイ */
    EVE_CoDl_colorA(phost, 180);
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
    EVE_CoDl_colorA(phost, 255);

    int16_t cx = CONT_X + CONT_W / 2;
    int16_t cy = SCR_H / 2;

    /* ダイアログ枠 */
    draw_card(phost, cx - 140, cy - 80, 280, 160);

    /* スピナー（円弧は EVE_CoCmd_spinner 使用） */
    EVE_CoCmd_bgcolor(phost, COL_CARD);
    EVE_CoCmd_spinner(phost, cx, cy - 20, 0, 1);

    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, cy + 30, FONT_MD, EVE_OPT_CENTER,
                   "Authenticating...");
}

/* ═══════════════════════════════════════════════════════════
   メイン画面：ヘッダバー
═══════════════════════════════════════════════════════════ */

#define HDR_H   44

static void render_header(EVE_HalContext *phost, AppState_t *app)
{
    draw_rect(phost, CONT_X, 0, CONT_W, HDR_H, COL_SURF);
    draw_hline(phost, CONT_X, HDR_H - 1, CONT_W, COL_BORDER, 1);

    /* ロゴ */
    SET_COLOR(phost, COL_ACCENT);
    EVE_CoCmd_text(phost, CONT_X + 10, HDR_H / 2, FONT_MD,
                   EVE_OPT_CENTERY, "NCS");

    /* 区切り */
    SET_COLOR(phost, COL_BORDER);
    EVE_CoDl_lineWidth(phost, 1 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, (CONT_X + 48) * 16, 10 * 16);
    EVE_CoDl_vertex2f(phost, (CONT_X + 48) * 16, (HDR_H - 10) * 16);
    EVE_CoDl_end(phost);

    /* 機械名 */
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, CONT_X + 58, HDR_H / 2, FONT_MD,
                   EVE_OPT_CENTERY, "CNC-7800");

    /* ステータスチップ */
    const char *st_label = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
    uint32_t    st_col   = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
    int16_t     st_x     = CONT_X + 160;
    draw_rect(phost, st_x, 12, 76, 20, (st_col >> 1) & 0x0F0F0FUL);
    SET_COLOR(phost, st_col);
    EVE_CoCmd_text(phost, st_x + 38, 22, FONT_SM, EVE_OPT_CENTER, st_label);

    /* 右端：ユーザー名・時計 */
    char time_str[10];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             app->rtc_h, app->rtc_m, app->rtc_s);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, RIGHT_X - 10, HDR_H / 2, FONT_SM,
                   EVE_OPT_CENTERY | EVE_OPT_RIGHTX, time_str);

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, RIGHT_X - 90, HDR_H / 2 - 7, FONT_SM,
                   EVE_OPT_CENTERY, app->user_name);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, RIGHT_X - 90, HDR_H / 2 + 7, FONT_SM,
                   EVE_OPT_CENTERY, app->user_role_str);
}

/* ═══════════════════════════════════════════════════════════
   ホームパネル
═══════════════════════════════════════════════════════════ */

static void render_home(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 16;
    int16_t cx = CONT_X;

    /* ─ タイトル ─ */
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Dashboard");
    y += 28;

    /* ─ ステータスカード 4 列 ─ */
    int16_t cw = (CONT_W - 40) / 4;  /* 各カード幅 */
    int16_t ch = 72;
    static const char *sc_labels[4] = {
        "MACHINE", "SPINDLE RPM", "PARTS", "TEMPERATURE"
    };

    for (int i = 0; i < 4; i++) {
        int16_t sx = cx + 8 + i * (cw + 8);
        draw_card(phost, sx, y, cw, ch);

        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, sx + 8, y + 10, FONT_SM, 0, sc_labels[i]);
    }

    /* カード 0: 機械状態 */
    {
        int16_t sx = cx + 8;
        const char *s = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t sc   = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        SET_COLOR(phost, sc);
        EVE_CoCmd_text(phost, sx + 8, y + 32, FONT_MD, 0, s);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8, y + 54, FONT_SM, 0, "Today: 08:32:15");
    }
    /* カード 1: RPM */
    {
        int16_t sx = cx + 8 + (cw + 8);
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, sx + 8, y + 28, FONT_LG, 0, buf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8 + 52, y + 36, FONT_SM, 0, "RPM");
        EVE_CoCmd_text(phost, sx + 8, y + 54, FONT_SM, 0, "Set: 1200 RPM");
    }
    /* カード 2: 加工数 */
    {
        int16_t sx = cx + 8 + 2 * (cw + 8);
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->parts_total);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, sx + 8, y + 28, FONT_LG, 0, buf);
        SET_COLOR(phost, COL_TXT2);
        char buf2[16];
        snprintf(buf2, sizeof(buf2), "Today: %lu", (unsigned long)app->parts_today);
        EVE_CoCmd_text(phost, sx + 8, y + 54, FONT_SM, 0, buf2);
    }
    /* カード 3: 温度 */
    {
        int16_t sx = cx + 8 + 3 * (cw + 8);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", app->spindle_temp);
        SET_COLOR(phost, COL_YELLOW);
        EVE_CoCmd_text(phost, sx + 8, y + 28, FONT_LG, 0, buf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8 + 28, y + 36, FONT_SM, 0, "C  Rising");
    }
    y += ch + 12;

    /* ─ ゲージ 2 列 ─ */
    int16_t gw = (CONT_W - 32) / 2;
    int16_t gh = 64;

    /* スピンドル負荷 */
    draw_card(phost, cx + 8, y, gw, gh);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 16, y + 10, FONT_SM, 0, "SPINDLE LOAD");
    draw_bar(phost, cx + 16, y + 28, gw - 32, 8, app->spindle_load, COL_ACCENT);
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", app->spindle_load);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + gw / 2, y + 48, FONT_SM, EVE_OPT_CENTERX, buf);

    /* 冷却水温度 */
    draw_card(phost, cx + 8 + gw + 8, y, gw, gh);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 8 + gw + 16, y + 10, FONT_SM, 0, "COOLANT TEMP");
    draw_bar(phost, cx + 8 + gw + 16, y + 28, gw - 32, 8,
             app->coolant_temp, COL_TEAL);
    snprintf(buf, sizeof(buf), "%dC", app->coolant_temp);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8 + gw + gw / 2, y + 48,
                   FONT_SM, EVE_OPT_CENTERX, buf);
    y += gh + 12;

    /* ─ 最近のアラーム ─ */
    draw_card(phost, cx + 8, y, CONT_W - 16, 80);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 16, y + 10, FONT_SM, 0, "RECENT ALARMS");

    for (int i = 0; i < 2 && i < (int)app->alarm_count; i++) {
        AlarmRecord_t *al = &app->alarms[i];
        uint32_t lc = (al->level == ALARM_LVL_ERROR) ? COL_RED :
                      (al->level == ALARM_LVL_WARN)  ? COL_YELLOW : COL_ACCENT;
        int16_t ay = y + 30 + i * 22;
        SET_COLOR(phost, lc);
        EVE_CoCmd_text(phost, cx + 16, ay, FONT_SM, 0, ">");
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 28, ay, FONT_SM, 0, al->title);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, RIGHT_X - 80, ay, FONT_SM, 0, al->time_str);
    }

    /* ─ 右ボタン ─ */
    draw_right_btn(phost, TAG_R1, BTN_Y0, "OPER", COL_CARD, true);
    draw_right_btn(phost, TAG_R2, BTN_Y1, "ALRM", COL_CARD, true);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "LOG",  COL_CARD, true);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "EXIT", COL_RED,  true);
}

/* ═══════════════════════════════════════════════════════════
   運転操作パネル
═══════════════════════════════════════════════════════════ */

static const uint8_t feed_vals[4] = { 25, 50, 75, 100 };

static void render_operation(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 16;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Operation");
    y += 28;

    /* ─ 左ブロック：制御 ─ */
    int16_t lw = CONT_W * 55 / 100;  /* 約55% */
    int16_t rw = CONT_W - lw - 16;
    int16_t lh = SCR_H - y - 16;

    draw_card(phost, cx + 8, y, lw, lh);

    /* 運転状態 */
    {
        const char *st = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t    sc = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + 12, FONT_SM, 0, "MACHINE CONTROL");
        SET_COLOR(phost, sc);
        EVE_CoCmd_text(phost, cx + 16, y + 32, FONT_LG, 0, st);
    }

    /* RPM 表示 */
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "SET: %lu RPM", (unsigned long)app->set_rpm);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16, y + 60, FONT_MD, 0, buf);

        snprintf(buf, sizeof(buf), "ACT: %lu RPM", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 82, FONT_SM, 0, buf);

        /* RPM バー */
        draw_bar(phost, cx + 16, y + 100, lw - 32, 8,
                 (uint8_t)(app->set_rpm * 100 / 3000), COL_ACCENT);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16,        y + 114, FONT_SM, 0, "0");
        EVE_CoCmd_text(phost, cx + 16 + (lw - 32) / 2, y + 114,
                       FONT_SM, EVE_OPT_CENTERX, "1500");
        EVE_CoCmd_text(phost, cx + lw - 16, y + 114,
                       FONT_SM, EVE_OPT_RIGHTX, "3000");
    }

    /* 送り速度ボタン（表示のみ、R3 で切替） */
    {
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + 136, FONT_SM, 0, "FEED RATE");
        static const char *feed_labels[4] = { "25%", "50%", "75%", "100%" };
        for (int i = 0; i < 4; i++) {
            int16_t fx = cx + 16 + i * 56;
            bool sel = (app->feed_idx == i);
            draw_rect(phost, fx, y + 150, 50, 24,
                      sel ? COL_ACCENT : COL_CARD2);
            SET_COLOR(phost, sel ? COL_WHITE : COL_TXT2);
            EVE_CoCmd_text(phost, fx + 25, y + 162, FONT_SM,
                           EVE_OPT_CENTER, feed_labels[i]);
        }
    }

    /* スピンドル負荷 */
    {
        char buf[12];
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + 192, FONT_SM, 0, "SPINDLE LOAD");
        draw_bar(phost, cx + 16, y + 208, lw - 32, 8,
                 app->spindle_load, COL_TEAL);
        snprintf(buf, sizeof(buf), "%d%%", app->spindle_load);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16 + (lw - 32) / 2, y + 222,
                       FONT_SM, EVE_OPT_CENTERX, buf);
    }

    /* ─ 右ブロック：ステータス ─ */
    int16_t rx = cx + 8 + lw + 8;
    draw_card(phost, rx, y, rw, (lh - 8) / 2);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, rx + 8, y + 10, FONT_SM, 0, "CURRENT STATUS");

    static const char *stat_keys[] = {
        "Status", "Actual RPM", "Spindle Load", "Feed Rate",
        "Parts Today", "Run Time"
    };
    for (int i = 0; i < 6; i++) {
        int16_t ry = y + 28 + i * 20;
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, rx + 8, ry, FONT_SM, 0, stat_keys[i]);
        char vbuf[16];
        switch (i) {
        case 0:
            snprintf(vbuf, sizeof(vbuf), "%s",
                     (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED");
            break;
        case 1:
            snprintf(vbuf, sizeof(vbuf), "%lu", (unsigned long)app->actual_rpm);
            break;
        case 2:
            snprintf(vbuf, sizeof(vbuf), "%d%%", app->spindle_load);
            break;
        case 3:
            snprintf(vbuf, sizeof(vbuf), "%d%%", feed_vals[app->feed_idx]);
            break;
        case 4:
            snprintf(vbuf, sizeof(vbuf), "%lu", (unsigned long)app->parts_today);
            break;
        case 5: {
            uint32_t s = app->op_seconds;
            snprintf(vbuf, sizeof(vbuf), "%02lu:%02lu:%02lu",
                     (unsigned long)(s/3600),
                     (unsigned long)((s%3600)/60),
                     (unsigned long)(s%60));
            break;
        }
        }
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, rx + rw - 8, ry, FONT_SM,
                       EVE_OPT_RIGHTX, vbuf);
    }

    /* 温度カード */
    int16_t ty = y + (lh - 8) / 2 + 8;
    draw_card(phost, rx, ty, rw, (lh - 8) / 2);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, rx + 8, ty + 10, FONT_SM, 0, "TEMPERATURE");
    static const char *temp_keys[] = {
        "Spindle", "Coolant", "Ambient"
    };
    static const uint8_t *temp_ptr_init = NULL; /* 実機では動的 */
    uint8_t temp_vals[3];
    temp_vals[0] = app->spindle_temp;
    temp_vals[1] = app->coolant_temp;
    temp_vals[2] = app->env_temp;
    for (int i = 0; i < 3; i++) {
        int16_t ry = ty + 28 + i * 20;
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, rx + 8, ry, FONT_SM, 0, temp_keys[i]);
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%dC", temp_vals[i]);
        uint32_t tc = (temp_vals[i] > 60) ? COL_YELLOW : COL_TXT;
        SET_COLOR(phost, tc);
        EVE_CoCmd_text(phost, rx + rw - 8, ry, FONT_SM,
                       EVE_OPT_RIGHTX, tbuf);
    }

    /* ─ 右ボタン（状態依存） ─ */
    if (app->machine == MACHINE_RUNNING) {
        draw_right_btn(phost, TAG_R1, BTN_Y0, "STOP", COL_RED,    true);
    } else {
        draw_right_btn(phost, TAG_R1, BTN_Y0, "RUN",  COL_GREEN,  true);
    }
    draw_right_btn(phost, TAG_R2, BTN_Y1, "RPM+", COL_CARD, true);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "RPM-", COL_CARD, true);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "FEED", COL_CARD, true);
}

/* ═══════════════════════════════════════════════════════════
   アラームパネル
═══════════════════════════════════════════════════════════ */

static void render_alarm(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 16;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Alarms");
    y += 28;

    /* フィルタタブ */
    static const char *ftabs[4] = { "ALL", "ERR", "WARN", "INFO" };
    for (int i = 0; i < 4; i++) {
        bool sel = (app->alarm_filter == (AlarmFilter_t)i);
        draw_rect(phost, cx + 8 + i * 64, y, 58, 24,
                  sel ? COL_ACCENT : COL_CARD2);
        SET_COLOR(phost, sel ? COL_WHITE : COL_TXT2);
        EVE_CoCmd_text(phost, cx + 8 + i * 64 + 29, y + 12,
                       FONT_SM, EVE_OPT_CENTER, ftabs[i]);
    }
    y += 32;

    /* アラームリスト */
    uint8_t shown = 0;
    for (int i = 0; i < (int)app->alarm_count; i++) {
        AlarmRecord_t *al = &app->alarms[i];

        /* フィルタ適用 */
        if (app->alarm_filter == ALARM_FILTER_ERROR &&
            al->level != ALARM_LVL_ERROR) continue;
        if (app->alarm_filter == ALARM_FILTER_WARN  &&
            al->level != ALARM_LVL_WARN)  continue;
        if (app->alarm_filter == ALARM_FILTER_INFO  &&
            al->level != ALARM_LVL_INFO)  continue;

        uint32_t lc = (al->level == ALARM_LVL_ERROR) ? COL_RED :
                      (al->level == ALARM_LVL_WARN)  ? COL_YELLOW : COL_ACCENT;
        int16_t ay = y + shown * 80;
        bool    sel = (app->alarm_cursor == i);

        /* カード背景 */
        draw_rect(phost, cx + 8, ay, CONT_W - 16, 74,
                  sel ? COL_CARD2 : COL_CARD);
        /* 左カラーライン */
        draw_rect(phost, cx + 8, ay, 4, 74, lc);

        /* タイトル */
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 18, ay + 8, FONT_MD, 0, al->title);

        /* 詳細 */
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 18, ay + 28, FONT_SM, 0, al->detail);

        /* 時刻 */
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 18, ay + 50, FONT_SM, 0, al->time_str);

        /* ステータス */
        const char *slabel = al->active ? "ACTIVE" : "RESET";
        SET_COLOR(phost, al->active ? lc : COL_TXT3);
        EVE_CoCmd_text(phost, RIGHT_X - 90, ay + 24, FONT_SM, 0, slabel);

        shown++;
    }

    if (shown == 0) {
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, CONT_X + CONT_W / 2, y + 40,
                       FONT_MD, EVE_OPT_CENTERX, "No alarms");
    }

    /* ─ 右ボタン ─ */
    draw_right_btn(phost, TAG_R1, BTN_Y0, "DTL",  COL_CARD, true);
    /* RESET: Admin のみ有効 */
    bool can_reset = (app->role == ROLE_ADMIN);
    draw_right_btn(phost, TAG_R2, BTN_Y1, "RST",
                   can_reset ? COL_YELLOW : COL_SURF, can_reset);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "",    COL_SURF,  false);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "",    COL_SURF,  false);
}

/* ═══════════════════════════════════════════════════════════
   ログパネル
═══════════════════════════════════════════════════════════ */

static void render_log(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 16;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Operation Log");
    y += 28;

    /* ヘッダ行 */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 20, COL_SURF);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 16,  y + 4, FONT_SM, 0, "TIME");
    EVE_CoCmd_text(phost, cx + 120, y + 4, FONT_SM, 0, "USER");
    EVE_CoCmd_text(phost, cx + 220, y + 4, FONT_SM, 0, "EVENT");
    y += 24;

    static const struct {
        const char *time;
        const char *user;
        const char *event;
    } logs[] = {
        { "10:30:00", "I.Tanaka", "Login (Operator)" },
        { "10:28:15", "Y.Yamada", "Alarm reset: Coolant" },
        { "10:25:00", "Y.Yamada", "RPM changed: 1000->1200" },
        { "10:00:00", "Y.Yamada", "Login (Admin)" },
        { "09:45:30", "I.Tanaka", "Machine stopped" },
        { "08:32:00", "I.Tanaka", "Machine started" },
    };
    int nlog = (int)(sizeof(logs) / sizeof(logs[0]));

    for (int i = 0; i < nlog; i++) {
        int16_t ly = y + i * 22;
        if (ly + 22 > SCR_H - 8) break;

        if (i % 2 == 0) draw_rect(phost, cx + 8, ly, CONT_W - 16, 21, COL_CARD);

        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16,  ly + 4, FONT_SM, 0, logs[i].time);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 120, ly + 4, FONT_SM, 0, logs[i].user);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 220, ly + 4, FONT_SM, 0, logs[i].event);
    }

    draw_right_btn(phost, TAG_R1, BTN_Y0, "",    COL_SURF, false);
    draw_right_btn(phost, TAG_R2, BTN_Y1, "",    COL_SURF, false);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "",    COL_SURF, false);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "",    COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   設定パネル（Admin 専用）
═══════════════════════════════════════════════════════════ */

static void render_settings(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 16;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Settings");
    SET_COLOR(phost, COL_YELLOW);
    EVE_CoCmd_text(phost, cx + 120, y + 4, FONT_SM, 0, "[ADMIN ONLY]");
    y += 28;

    int16_t hw = (CONT_W - 24) / 2;

    /* 左カード：運転パラメータ */
    draw_card(phost, cx + 8, y, hw, 200);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 16, y + 10, FONT_MD, 0, "Operation Parameters");

    static const char *op_keys[] = {
        "Max RPM", "Min RPM", "Feed Override", "Tool No."
    };
    static const char *op_vals[] = { "3000", "0", "100%", "T01" };
    for (int i = 0; i < 4; i++) {
        int16_t ry = y + 36 + i * 38;
        draw_hline(phost, cx + 16, ry, hw - 16, COL_BORDER, 1);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, ry + 10, FONT_SM, 0, op_keys[i]);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + hw, ry + 10, FONT_SM,
                       EVE_OPT_RIGHTX, op_vals[i]);
    }

    /* 右カード：システム */
    int16_t rx = cx + 8 + hw + 8;
    draw_card(phost, rx, y, hw, 200);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, rx + 8, y + 10, FONT_MD, 0, "System");

    static const char *sys_keys[] = {
        "Panel ID", "FW Version", "Display", "Network"
    };
    static const char *sys_vals[] = {
        "CNC-7800", "v2.1.0", "800x480", "192.168.1.10"
    };
    for (int i = 0; i < 4; i++) {
        int16_t ry = y + 36 + i * 38;
        draw_hline(phost, rx + 8, ry, hw - 16, COL_BORDER, 1);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, rx + 8, ry + 10, FONT_SM, 0, sys_keys[i]);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, rx + hw, ry + 10, FONT_SM,
                       EVE_OPT_RIGHTX, sys_vals[i]);
    }

    draw_right_btn(phost, TAG_R1, BTN_Y0, "SAVE", COL_ACCENT, true);
    draw_right_btn(phost, TAG_R2, BTN_Y1, "",     COL_SURF,   false);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "",     COL_SURF,   false);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "",     COL_SURF,   false);
}

/* ═══════════════════════════════════════════════════════════
   ユーザー管理パネル（Admin 専用）
═══════════════════════════════════════════════════════════ */

static void render_users(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 16;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "User Management");
    SET_COLOR(phost, COL_YELLOW);
    EVE_CoCmd_text(phost, cx + 200, y + 4, FONT_SM, 0, "[ADMIN ONLY]");
    y += 28;

    /* テーブルヘッダ */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 24, COL_SURF);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 16,  y + 4, FONT_SM, 0, "USER");
    EVE_CoCmd_text(phost, cx + 180, y + 4, FONT_SM, 0, "ROLE");
    EVE_CoCmd_text(phost, cx + 300, y + 4, FONT_SM, 0, "LAST LOGIN");
    EVE_CoCmd_text(phost, cx + 450, y + 4, FONT_SM, 0, "STATUS");
    y += 28;

    static const struct {
        const char *name;
        const char *role;
        const char *last;
        const char *status;
    } users[] = {
        { "Y.Yamada",  "Admin",    "2026-04-10 10:00", "Active" },
        { "I.Tanaka",  "Operator", "2026-04-10 10:30", "Active" },
        { "K.Suzuki",  "Operator", "2026-04-09 14:20", "Active" },
        { "H.Sato",    "Viewer",   "2026-03-28 09:00", "Inactive" },
    };
    int nusr = (int)(sizeof(users) / sizeof(users[0]));

    for (int i = 0; i < nusr; i++) {
        int16_t ry = y + i * 32;
        if (i % 2 == 0) draw_rect(phost, cx + 8, ry, CONT_W - 16, 30, COL_CARD);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16,  ry + 8, FONT_SM, 0, users[i].name);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 180, ry + 8, FONT_SM, 0, users[i].role);
        EVE_CoCmd_text(phost, cx + 300, ry + 8, FONT_SM, 0, users[i].last);
        uint32_t sc = (users[i].status[0] == 'A') ? COL_GREEN : COL_TXT3;
        SET_COLOR(phost, sc);
        EVE_CoCmd_text(phost, cx + 450, ry + 8, FONT_SM, 0, users[i].status);
    }

    draw_right_btn(phost, TAG_R1, BTN_Y0, "",    COL_SURF, false);
    draw_right_btn(phost, TAG_R2, BTN_Y1, "",    COL_SURF, false);
    draw_right_btn(phost, TAG_R3, BTN_Y2, "",    COL_SURF, false);
    draw_right_btn(phost, TAG_R4, BTN_Y3, "",    COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   メニューオーバーレイ
═══════════════════════════════════════════════════════════ */

static void render_menu_overlay(EVE_HalContext *phost, AppState_t *app)
{
    /* 半透明背景 */
    EVE_CoDl_colorA(phost, 200);
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
    EVE_CoDl_colorA(phost, 255);

    /* メニューパネル */
    int16_t mx = CONT_X + 20;
    int16_t my = 20;
    int16_t mw = 220;

    draw_card(phost, mx, my, mw, 280);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, mx + 12, my + 10, FONT_SM, 0, "NAVIGATION");

    typedef struct { uint8_t tag; const char *label; bool admin; Panel_t panel; } MenuItem;
    static const MenuItem menu_items[] = {
        { TAG_MENU_HOME,  "Home",       false, PANEL_HOME      },
        { TAG_MENU_OPER,  "Operation",  false, PANEL_OPERATION },
        { TAG_MENU_ALRM,  "Alarms",     false, PANEL_ALARM     },
        { TAG_MENU_LOG,   "Log",        false, PANEL_LOG       },
        { TAG_MENU_SETT,  "Settings",   true,  PANEL_SETTINGS  },
        { TAG_MENU_USERS, "Users",      true,  PANEL_USERS     },
    };
    int nitems = (int)(sizeof(menu_items) / sizeof(menu_items[0]));

    int8_t visible_idx = 0;
    for (int i = 0; i < nitems; i++) {
        if (menu_items[i].admin && app->role != ROLE_ADMIN) continue;

        int16_t iy = my + 32 + visible_idx * 36;
        bool    sel = (app->menu_cursor == visible_idx);
        bool    cur = (app->panel == menu_items[i].panel);

        draw_rect(phost, mx + 4, iy, mw - 8, 32,
                  sel ? COL_ACCENT :
                  cur ? COL_CARD2  : COL_SURF);

        EVE_CoDl_tag(phost, menu_items[i].tag);
        SET_COLOR(phost, sel ? COL_WHITE :
                         cur ? COL_ACCENT : COL_TXT2);
        EVE_CoCmd_text(phost, mx + 16, iy + 10, FONT_MD, 0,
                       menu_items[i].label);

        if (menu_items[i].admin) {
            SET_COLOR(phost, COL_YELLOW);
            EVE_CoCmd_text(phost, mx + mw - 12, iy + 10, FONT_SM,
                           EVE_OPT_RIGHTX, "ADM");
        }
        EVE_CoDl_tag(phost, TAG_NONE);
        visible_idx++;
    }
}

/* ═══════════════════════════════════════════════════════════
   トースト通知
═══════════════════════════════════════════════════════════ */

static void render_toast(EVE_HalContext *phost, AppState_t *app)
{
    if (!app->toast_visible) return;

    int16_t tw = 300, th = 36;
    int16_t tx = CONT_X + (CONT_W - tw) / 2;
    int16_t ty = SCR_H - 50;

    draw_rect(phost, tx, ty, tw, th, COL_CARD2);
    SET_COLOR(phost, COL_BORDER);
    EVE_CoDl_lineWidth(phost, 1 * 16);
    EVE_CoDl_begin(phost, EVE_LINE_STRIP);
    EVE_CoDl_vertex2f(phost,  tx        * 16,  ty        * 16);
    EVE_CoDl_vertex2f(phost, (tx+tw)    * 16,  ty        * 16);
    EVE_CoDl_vertex2f(phost, (tx+tw)    * 16, (ty+th)    * 16);
    EVE_CoDl_vertex2f(phost,  tx        * 16, (ty+th)    * 16);
    EVE_CoDl_vertex2f(phost,  tx        * 16,  ty        * 16);
    EVE_CoDl_end(phost);

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, tx + tw / 2, ty + th / 2,
                   FONT_MD, EVE_OPT_CENTER, app->toast_msg);
}

/* ═══════════════════════════════════════════════════════════
   描画メインエントリ
═══════════════════════════════════════════════════════════ */

void panel_demo_render(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    EVE_CoCmd_dlStart(phost);
    EVE_CoDl_clearColorRgb(phost, COL_R(COL_BG), COL_G(COL_BG), COL_B(COL_BG));
    EVE_CoDl_clear(phost, 1, 1, 1);
    EVE_CoDl_tagMask(phost, 1);

    if (app->screen == SCREEN_LOCK) {
        draw_left_buttons(phost);
        render_lock(phost, app, tick_ms);

    } else if (app->screen == SCREEN_AUTH) {
        draw_left_buttons(phost);
        render_lock(phost, app, tick_ms);   /* 背景としてロック画面を維持 */
        render_auth(phost, app, tick_ms);

    } else { /* SCREEN_MAIN */
        draw_left_buttons(phost);
        clear_content(phost);
        render_header(phost, app);

        switch (app->panel) {
        case PANEL_HOME:      render_home     (phost, app); break;
        case PANEL_OPERATION: render_operation(phost, app); break;
        case PANEL_ALARM:     render_alarm    (phost, app); break;
        case PANEL_LOG:       render_log      (phost, app); break;
        case PANEL_SETTINGS:  render_settings (phost, app); break;
        case PANEL_USERS:     render_users    (phost, app); break;
        default: break;
        }

        if (app->menu_open) {
            render_menu_overlay(phost, app);
        }

        render_toast(phost, app);
    }

    EVE_CoDl_display(phost);
    EVE_CoCmd_swap(phost);
    EVE_Hal_flush(phost);
}

/* ═══════════════════════════════════════════════════════════
   タッチ処理
═══════════════════════════════════════════════════════════ */

/* メニューの可視項目数を返す */
static int8_t menu_item_count(AppState_t *app)
{
    return (app->role == ROLE_ADMIN) ? 6 : 4;
}

/* メニュー項目インデックスからパネルへ変換 */
static Panel_t menu_idx_to_panel(AppState_t *app, int8_t idx)
{
    static const Panel_t panels_op[]  = {
        PANEL_HOME, PANEL_OPERATION, PANEL_ALARM, PANEL_LOG
    };
    static const Panel_t panels_adm[] = {
        PANEL_HOME, PANEL_OPERATION, PANEL_ALARM, PANEL_LOG,
        PANEL_SETTINGS, PANEL_USERS
    };
    if (app->role == ROLE_ADMIN) {
        if (idx >= 0 && idx < 6) return panels_adm[idx];
    } else {
        if (idx >= 0 && idx < 4) return panels_op[idx];
    }
    return PANEL_HOME;
}

void panel_demo_touch(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    uint8_t tag = EVE_Hal_rd8(phost, REG_TOUCH_TAG);

    /* エッジ検出（立ち上がり：前フレームで未タッチ → 今フレームでタッチ） */
    bool rising = (tag != TAG_NONE && app->tag_prev == TAG_NONE);
    app->tag_prev = tag;

    if (!rising) return;

    /* ─── 認証中は操作無効 ─── */
    if (app->screen == SCREEN_AUTH) return;

    /* ─── ロック画面 ─── */
    if (app->screen == SCREEN_LOCK) {
        switch (tag) {
        case TAG_L2:  /* UP */
            app->lock_cursor = 0;
            break;
        case TAG_L4:  /* DOWN */
            app->lock_cursor = 1;
            break;
        case TAG_L3:  /* OK → 認証開始 */
            app->auth_pending    = (app->lock_cursor == 1)
                                   ? ROLE_ADMIN : ROLE_OPERATOR;
            app->auth_tick_start = tick_ms;
            app->screen          = SCREEN_AUTH;
            break;
        case TAG_R1:  /* OPR ショートカット */
            app->lock_cursor     = 0;
            app->auth_pending    = ROLE_OPERATOR;
            app->auth_tick_start = tick_ms;
            app->screen          = SCREEN_AUTH;
            break;
        case TAG_R2:  /* ADM ショートカット */
            app->lock_cursor     = 1;
            app->auth_pending    = ROLE_ADMIN;
            app->auth_tick_start = tick_ms;
            app->screen          = SCREEN_AUTH;
            break;
        }
        return;
    }

    /* ─── メイン画面 ─── */

    /* L1: メニュートグル */
    if (tag == TAG_L1) {
        app->menu_open = !app->menu_open;
        return;
    }

    /* メニュー開状態の操作 */
    if (app->menu_open) {
        int8_t max_item = menu_item_count(app);
        switch (tag) {
        case TAG_L2: /* UP */
            app->menu_cursor--;
            if (app->menu_cursor < 0) app->menu_cursor = max_item - 1;
            break;
        case TAG_L4: /* DOWN */
            app->menu_cursor++;
            if (app->menu_cursor >= max_item) app->menu_cursor = 0;
            break;
        case TAG_L3: /* OK */
            app->panel     = menu_idx_to_panel(app, app->menu_cursor);
            app->menu_open = false;
            break;
        /* タッチ直接選択 */
        case TAG_MENU_HOME:
            app->panel = PANEL_HOME;      app->menu_open = false; break;
        case TAG_MENU_OPER:
            app->panel = PANEL_OPERATION; app->menu_open = false; break;
        case TAG_MENU_ALRM:
            app->panel = PANEL_ALARM;     app->menu_open = false; break;
        case TAG_MENU_LOG:
            app->panel = PANEL_LOG;       app->menu_open = false; break;
        case TAG_MENU_SETT:
            if (app->role == ROLE_ADMIN) {
                app->panel = PANEL_SETTINGS; app->menu_open = false;
            }
            break;
        case TAG_MENU_USERS:
            if (app->role == ROLE_ADMIN) {
                app->panel = PANEL_USERS; app->menu_open = false;
            }
            break;
        }
        return;
    }

    /* メニュー閉時のパネル別操作 */
    switch (app->panel) {

    /* ─ ホーム ─ */
    case PANEL_HOME:
        switch (tag) {
        case TAG_R1: app->panel = PANEL_OPERATION; break;
        case TAG_R2: app->panel = PANEL_ALARM;     break;
        case TAG_R3: app->panel = PANEL_LOG;        break;
        case TAG_R4: /* EXIT → ロック画面へ */
            app->screen        = SCREEN_LOCK;
            app->role          = ROLE_NONE;
            app->user_name     = "";
            app->user_role_str = "";
            app->lock_cursor   = 0;
            break;
        }
        break;

    /* ─ 運転操作 ─ */
    case PANEL_OPERATION:
        switch (tag) {
        case TAG_R1: /* RUN / STOP */
            app->machine = (app->machine == MACHINE_RUNNING)
                           ? MACHINE_STOPPED : MACHINE_RUNNING;
            show_toast(app,
                       (app->machine == MACHINE_RUNNING)
                       ? "Machine started" : "Machine stopped",
                       tick_ms);
            break;
        case TAG_R2: /* RPM+ */
            if (app->set_rpm < 3000) {
                app->set_rpm += 100;
                app->actual_rpm = app->set_rpm;
            }
            break;
        case TAG_R3: /* RPM- */
            if (app->set_rpm > 0) {
                app->set_rpm -= 100;
                app->actual_rpm = app->set_rpm;
            }
            break;
        case TAG_R4: /* FEED 切替 */
            app->feed_idx = (app->feed_idx + 1) % 4;
            break;
        case TAG_L2: /* UP: RPM+ */
            if (app->set_rpm < 3000) {
                app->set_rpm += 100;
                app->actual_rpm = app->set_rpm;
            }
            break;
        case TAG_L4: /* DOWN: RPM- */
            if (app->set_rpm > 0) {
                app->set_rpm -= 100;
                app->actual_rpm = app->set_rpm;
            }
            break;
        }
        break;

    /* ─ アラーム ─ */
    case PANEL_ALARM:
        switch (tag) {
        case TAG_L2: /* UP */
            app->alarm_cursor--;
            if (app->alarm_cursor < 0) app->alarm_cursor = app->alarm_count - 1;
            break;
        case TAG_L4: /* DOWN */
            app->alarm_cursor++;
            if (app->alarm_cursor >= (int8_t)app->alarm_count)
                app->alarm_cursor = 0;
            break;
        case TAG_R1: /* DTL */
            show_toast(app, "Alarm detail (not implemented)", tick_ms);
            break;
        case TAG_R2: /* RST（Admin のみ） */
            if (app->role == ROLE_ADMIN &&
                app->alarm_cursor < (int8_t)app->alarm_count) {
                app->alarms[app->alarm_cursor].active = false;
                show_toast(app, "Alarm reset", tick_ms);
            }
            break;
        }
        break;

    /* ─ 設定 ─ */
    case PANEL_SETTINGS:
        if (tag == TAG_R1) {
            show_toast(app, "Settings saved", tick_ms);
        }
        break;

    /* ─ ログ / ユーザー管理 ─ */
    default:
        break;
    }
}
