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
   定数
═══════════════════════════════════════════════════════════ */

#define HDR_H               80      /* ヘッダ高さ（50cm視認距離対応） */
#define SETT_ROW_H          50      /* Settings 1行の高さ */
#define LOG_ROW_H           34      /* Log 1行の高さ */
#define USER_ROW_H          46      /* Users 1行の高さ */
#define TITLE_OFFSET        52      /* タイトル下マージン */
#define ROW_H_AL            64      /* Alarm 2行コンパクト行高さ */
#define ALARM_DTL_H         (SCR_H - HDR_H - 40)   /* アラーム詳細パネル高さ ~360px */
#define ALARM_SLIDE_MS      300     /* アラーム詳細スライドms */
#define AUTH_NOTIF_SLIDE_MS 220     /* 認証通知スライドms */
#define AUTH_NOTIF_HOLD_MS  1000    /* 認証通知表示保持ms */
#define LOG_TAB_W           82      /* ログカテゴリタブ幅 */
#define LOG_TAB_GAP         4       /* ログタブ間隔 */

/* ═══════════════════════════════════════════════════════════
   フィードパターンテーブル
═══════════════════════════════════════════════════════════ */

static const uint8_t feed_patterns[2][4] = {
    { 25, 50, 75, 100 },   /* Pattern 0: 4-step */
    { 50, 100,  0,   0 },  /* Pattern 1: 2-step */
};
static const uint8_t feed_pattern_count[2] = { 4, 2 };

/* ═══════════════════════════════════════════════════════════
   可変ユーザーデータ（active フラグが変更可能）
═══════════════════════════════════════════════════════════ */

typedef struct {
    const char *name;
    const char *role;
    const char *last_login;
    bool        active;
} UserRecord_t;

static UserRecord_t user_data[4] = {
    { "Y.Yamada", "Admin",    "2026-04-11 10:00", true  },
    { "I.Tanaka", "Operator", "2026-04-11 10:30", true  },
    { "K.Suzuki", "Operator", "2026-04-09 14:20", true  },
    { "H.Sato",   "Viewer",   "2026-03-28 09:00", false },
};
#define USER_COUNT 4

/* ═══════════════════════════════════════════════════════════
   ログ追加ヘルパー（最新を先頭に挿入）
═══════════════════════════════════════════════════════════ */

static void add_log(AppState_t *app, uint8_t cat, const char *event)
{
    uint8_t n = app->log_count < LOG_MAX ? app->log_count : LOG_MAX;
    /* 末尾を捨てて先頭に挿入 */
    if (n == LOG_MAX) n = LOG_MAX - 1;
    memmove(&app->log_entries[1], &app->log_entries[0], n * sizeof(LogEntry_t));
    app->log_count = (app->log_count < LOG_MAX) ? app->log_count + 1 : LOG_MAX;

    snprintf(app->log_entries[0].time, sizeof(app->log_entries[0].time),
             "%02d:%02d:%02d", app->rtc_h, app->rtc_m, app->rtc_s);
    strncpy(app->log_entries[0].user,  app->user_name, sizeof(app->log_entries[0].user)  - 1);
    strncpy(app->log_entries[0].event, event,          sizeof(app->log_entries[0].event) - 1);
    app->log_entries[0].user [sizeof(app->log_entries[0].user)  - 1] = '\0';
    app->log_entries[0].event[sizeof(app->log_entries[0].event) - 1] = '\0';
    app->log_entries[0].cat  = cat;
    app->log_scroll = 0;
}

/* ═══════════════════════════════════════════════════════════
   色ルールヘルパー（共通: 赤=危険/注意のみ）
═══════════════════════════════════════════════════════════ */

/* RPM値→バー色: 0-1499=緑, 1500-2999=黄, 3000=オレンジ */
static uint32_t rpm_color(uint32_t rpm)
{
    if (rpm >= 3000) return COL_ORANGE;
    if (rpm >= 1500) return COL_YELLOW;
    return COL_GREEN;
}

/* ═══════════════════════════════════════════════════════════
   内部ヘルパー
═══════════════════════════════════════════════════════════ */

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

static void draw_vline(EVE_HalContext *phost,
                       int16_t x, int16_t y, int16_t h,
                       uint32_t color, uint8_t thickness)
{
    SET_COLOR(phost, color);
    EVE_CoDl_lineWidth(phost, thickness * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, x * 16, y * 16);
    EVE_CoDl_vertex2f(phost, x * 16, (y + h) * 16);
    EVE_CoDl_end(phost);
}

static void draw_bar(EVE_HalContext *phost,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     uint8_t pct, uint32_t fg_color)
{
    draw_rect(phost, x, y, w, h, COL_BG);
    if (pct > 100) pct = 100;
    draw_rect(phost, x, y, (int16_t)(w * pct / 100), h, fg_color);
}

static void draw_card(EVE_HalContext *phost,
                      int16_t x, int16_t y, int16_t w, int16_t h)
{
    draw_rect(phost, x, y, w, h, COL_CARD);
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

/* 左固定ボタンアイコン */
static void draw_icon_menu(EVE_HalContext *phost, int16_t bx, int16_t by)
{
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

static void draw_icon_up(EVE_HalContext *phost, int16_t bx, int16_t by)
{
    int16_t cx = bx + 45;
    SET_COLOR(phost, COL_TXT2);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, cx * 16,        (by + 28) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 22) * 16, (by + 60) * 16);
    EVE_CoDl_vertex2f(phost, cx * 16,        (by + 28) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 68) * 16, (by + 60) * 16);
    EVE_CoDl_end(phost);
}

static void draw_icon_ok(EVE_HalContext *phost, int16_t bx, int16_t by)
{
    SET_COLOR(phost, COL_GREEN);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, (bx + 22) * 16, (by + 48) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 38) * 16, (by + 62) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 38) * 16, (by + 62) * 16);
    EVE_CoDl_vertex2f(phost, (bx + 68) * 16, (by + 28) * 16);
    EVE_CoDl_end(phost);
}

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

static void draw_left_buttons(EVE_HalContext *phost)
{
    static const int16_t ys[4]    = { BTN_Y0, BTN_Y1, BTN_Y2, BTN_Y3 };
    static const uint8_t tags[4]  = { TAG_L1, TAG_L2, TAG_L3, TAG_L4 };

    for (int i = 0; i < 4; i++) {
        int16_t y = ys[i];
        EVE_CoDl_tag(phost, tags[i]);
        draw_rect(phost, 0, y, BTN_W, BTN_H, COL_SURF);
        draw_hline(phost, 0, y + BTN_H - 1, BTN_W, COL_BORDER, 1);
        draw_vline(phost, BTN_W - 1, y, BTN_H, COL_BORDER, 1);
    }
    EVE_CoDl_tag(phost, TAG_NONE);
    draw_icon_menu(phost, 0, BTN_Y0);
    draw_icon_up  (phost, 0, BTN_Y1);
    draw_icon_ok  (phost, 0, BTN_Y2);
    draw_icon_down(phost, 0, BTN_Y3);
}

/* font: 通常=FONT_MD, +/-=FONT_XL */
static void draw_right_btn(EVE_HalContext *phost,
                           uint8_t tag, int16_t y,
                           const char *label, uint32_t bg_color,
                           bool enabled, uint8_t font)
{
    uint32_t actual_bg = enabled ? bg_color : COL_SURF;
    uint32_t label_col = enabled ? COL_TXT  : COL_TXT3;

    EVE_CoDl_tag(phost, enabled ? tag : TAG_NONE);
    draw_rect(phost, RIGHT_X, y, BTN_W, BTN_H, actual_bg);
    draw_vline(phost, RIGHT_X, y, BTN_H, COL_BORDER, 1);
    draw_hline(phost, RIGHT_X, y + BTN_H - 1, BTN_W, COL_BORDER, 1);

    if (label && label[0]) {
        SET_COLOR(phost, label_col);
        EVE_CoCmd_text(phost,
                       RIGHT_X + BTN_W / 2,
                       y + BTN_H / 2,
                       font, EVE_OPT_CENTER, label);
    }
    EVE_CoDl_tag(phost, TAG_NONE);
}

#define RB(tag, y, lbl, col, en) \
    draw_right_btn(phost, tag, y, lbl, col, en, FONT_MD)
#define RB_XL(tag, y, lbl, col, en) \
    draw_right_btn(phost, tag, y, lbl, col, en, FONT_XL)

static void clear_content(EVE_HalContext *phost)
{
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
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
   初期化
═══════════════════════════════════════════════════════════ */

void panel_demo_init(AppState_t *app)
{
    memset(app, 0, sizeof(*app));

    app->screen        = SCREEN_LOCK;
    app->panel         = PANEL_HOME;
    app->role          = ROLE_NONE;
    app->user_name     = "";
    app->user_role_str = "";
    app->lock_cursor   = 0;
    app->menu_open     = false;
    app->menu_cursor   = 0;

    /* 機械初期値 */
    app->machine      = MACHINE_RUNNING;
    app->set_rpm      = 1200;
    app->actual_rpm   = 1200;
    app->feed_idx     = 0;
    app->spindle_load = 68;
    app->spindle_temp = 72;
    app->coolant_temp = 48;
    app->env_temp     = 24;
    app->parts_total  = 1247;
    app->parts_today  = 247;
    app->op_seconds   = 30735;

    /* Settings 初期値 */
    app->settings_max_rpm      = 3000;
    app->settings_min_rpm      = 0;
    app->settings_feed_pattern = 0;
    app->settings_tool_no      = 1;
    app->settings_cursor       = 0;
    app->settings_editing      = false;
    app->settings_changed      = false;

    /* ログ・ユーザー */
    app->log_scroll  = 0;
    app->log_filter  = LOG_FILTER_ALL;
    app->user_cursor = 0;

    /* アラーム初期データ（8件） */
    app->alarms[0] = (AlarmRecord_t){
        ALARM_LVL_ERROR, "Spindle Overload",
        "Spindle current exceeded 150% limit. Emergency stop executed.",
        "2026-04-12 08:05", true, true, false
    };
    app->alarms[1] = (AlarmRecord_t){
        ALARM_LVL_ERROR, "Tool Wear Limit",
        "Tool T03 exceeded maximum wear threshold. Replacement required.",
        "2026-04-12 09:30", true, false, false
    };
    app->alarms[2] = (AlarmRecord_t){
        ALARM_LVL_WARN, "Coolant Temp High",
        "Coolant temperature exceeded 45C limit. Currently 48C.",
        "2026-04-12 09:45", true, true, false
    };
    app->alarms[3] = (AlarmRecord_t){
        ALARM_LVL_WARN, "Coolant Level Low",
        "Coolant reservoir below minimum level. Refill required.",
        "2026-04-12 10:00", false, false, false
    };
    app->alarms[4] = (AlarmRecord_t){
        ALARM_LVL_WARN, "Vibration Detected",
        "Abnormal vibration on spindle axis. Check tool mounting.",
        "2026-04-12 10:15", false, false, false
    };
    app->alarms[5] = (AlarmRecord_t){
        ALARM_LVL_INFO, "Maintenance Due",
        "Scheduled maintenance at 2000h. Current: 1987h. 13h remaining.",
        "2026-04-11 08:00", true, false, false
    };
    app->alarms[6] = (AlarmRecord_t){
        ALARM_LVL_INFO, "Backup Complete",
        "System configuration backup completed successfully.",
        "2026-04-11 07:00", false, false, false
    };
    app->alarms[7] = (AlarmRecord_t){
        ALARM_LVL_INFO, "Filter Replacement",
        "Air filter replacement recommended. Last changed 90 days ago.",
        "2026-04-10 06:00", false, false, false
    };
    app->alarm_count  = 8;
    app->alarm_filter = ALARM_FILTER_ALL;
    app->alarm_cursor = 0;
    app->alarm_scroll = 0;

    /* 初期ログデータ（20件） */
    static const struct {
        const char *time; const char *user; const char *event; uint8_t cat;
    } init_logs[] = {
        { "10:30:00", "I.Tanaka", "Login (Operator)",          LOG_CAT_LOGIN     },
        { "10:28:15", "Y.Yamada", "Alarm reset: Coolant",      LOG_CAT_ALARM     },
        { "10:25:00", "Y.Yamada", "RPM set: 1200",             LOG_CAT_OPERATION },
        { "10:15:30", "Y.Yamada", "Feed rate: 75%",            LOG_CAT_OPERATION },
        { "10:00:00", "Y.Yamada", "Login (Admin)",             LOG_CAT_LOGIN     },
        { "09:55:10", "I.Tanaka", "Settings saved",            LOG_CAT_SETTINGS  },
        { "09:45:30", "I.Tanaka", "Machine stopped",           LOG_CAT_OPERATION },
        { "09:30:00", "I.Tanaka", "Alarm ack: Coolant",        LOG_CAT_ALARM     },
        { "09:10:00", "I.Tanaka", "RPM set: 1000",             LOG_CAT_OPERATION },
        { "08:55:00", "K.Suzuki", "Login (Operator)",          LOG_CAT_LOGIN     },
        { "08:32:00", "I.Tanaka", "Machine started",           LOG_CAT_OPERATION },
        { "08:30:00", "I.Tanaka", "Login (Operator)",          LOG_CAT_LOGIN     },
        { "08:20:00", "Y.Yamada", "User K.Suzuki activated",   LOG_CAT_USER      },
        { "08:10:00", "Y.Yamada", "Max RPM changed: 3000",     LOG_CAT_SETTINGS  },
        { "08:05:00", "Y.Yamada", "Alarm: Spindle Overload",   LOG_CAT_ALARM     },
        { "08:03:00", "Y.Yamada", "Login (Admin)",             LOG_CAT_LOGIN     },
        { "08:00:00", "System",   "System startup",            LOG_CAT_LOG       },
        { "07:55:00", "Y.Yamada", "Backup completed",          LOG_CAT_LOG       },
        { "07:30:00", "I.Tanaka", "Feed rate: 50%",            LOG_CAT_OPERATION },
        { "07:00:00", "I.Tanaka", "Login (Operator)",          LOG_CAT_LOGIN     },
    };
    app->log_count = (uint8_t)(sizeof(init_logs) / sizeof(init_logs[0]));
    if (app->log_count > LOG_MAX) app->log_count = LOG_MAX;
    for (int i = 0; i < (int)app->log_count; i++) {
        strncpy(app->log_entries[i].time,  init_logs[i].time,  sizeof(app->log_entries[i].time)  - 1);
        strncpy(app->log_entries[i].user,  init_logs[i].user,  sizeof(app->log_entries[i].user)  - 1);
        strncpy(app->log_entries[i].event, init_logs[i].event, sizeof(app->log_entries[i].event) - 1);
        app->log_entries[i].cat = init_logs[i].cat;
    }

    app->auth_duration_ms = 1500;
    app->rtc_h = 10;
    app->rtc_m = 30;
    app->rtc_s = 0;
}

/* ═══════════════════════════════════════════════════════════
   状態更新
═══════════════════════════════════════════════════════════ */

void panel_demo_update(AppState_t *app, uint32_t tick_ms)
{
    if (tick_ms - app->rtc_last_tick >= 1000) {
        app->rtc_last_tick = tick_ms;
        app->rtc_s++;
        if (app->rtc_s >= 60) { app->rtc_s = 0; app->rtc_m++; }
        if (app->rtc_m >= 60) { app->rtc_m = 0; app->rtc_h++; }
        if (app->rtc_h >= 24) { app->rtc_h = 0; }
        if (app->machine == MACHINE_RUNNING) app->op_seconds++;
    }

    if (app->screen == SCREEN_AUTH) {
        if (tick_ms - app->auth_tick_start >= app->auth_duration_ms) {
            app->role          = app->auth_pending;
            app->user_name     = (app->role == ROLE_ADMIN) ? "Y.Yamada" : "I.Tanaka";
            app->user_role_str = (app->role == ROLE_ADMIN) ? "Admin"    : "Operator";
            app->screen        = SCREEN_MAIN;
            app->panel         = PANEL_HOME;
            app->menu_open     = false;

            /* 認証成功通知スライドイン開始 */
            app->auth_notif_active = true;
            app->auth_notif_start  = tick_ms;
            app->auth_notif_name   = app->user_name;
            app->auth_notif_role   = app->user_role_str;

            /* ログイン履歴記録 */
            add_log(app, LOG_CAT_LOGIN,
                    (app->role == ROLE_ADMIN) ? "Login (Admin)" : "Login (Operator)");
        }
    }

    /* 認証通知アニメーション終了チェック */
    if (app->auth_notif_active) {
        uint32_t total = AUTH_NOTIF_SLIDE_MS * 2 + AUTH_NOTIF_HOLD_MS;
        if (tick_ms - app->auth_notif_start >= total)
            app->auth_notif_active = false;
    }

    /* アラーム詳細クローズアニメーション終了チェック */
    if (app->alarm_detail_open && app->alarm_detail_closing) {
        if (tick_ms - app->alarm_detail_start >= ALARM_SLIDE_MS) {
            app->alarm_detail_open    = false;
            app->alarm_detail_closing = false;
        }
    }

    app->anim_tick = tick_ms;

    if (app->toast_visible && tick_ms - app->toast_show_tick >= 3000) {
        app->toast_visible = false;
    }
}

/* ═══════════════════════════════════════════════════════════
   ロック画面
═══════════════════════════════════════════════════════════ */

static void render_lock(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);

    uint32_t t  = tick_ms % 2400;
    int16_t  cx = CONT_X + CONT_W / 2;
    int16_t  cy = SCR_H / 2 - 40;

    static const uint16_t delays[3] = { 0, 600, 1200 };
    static const uint16_t radii[3]  = { 30, 50, 70 };

    for (int i = 0; i < 3; i++) {
        uint32_t phase = (t + 2400 - delays[i]) % 2400;
        uint8_t  alpha = (phase < 1200) ? (uint8_t)(179 - phase * 179 / 1200) : 0;
        if (alpha == 0) continue;

        EVE_CoDl_colorA(phost, alpha);
        SET_COLOR(phost, COL_ACCENT);
        EVE_CoDl_lineWidth(phost, 2 * 16);
        EVE_CoDl_begin(phost, EVE_LINE_STRIP);
        for (int j = 0; j <= 32; j++) {
            float angle = (float)j * 6.2832f / 32.0f;
            int16_t px  = (int16_t)(cx + radii[i] * __builtin_cosf(angle));
            int16_t py  = (int16_t)(cy + radii[i] * __builtin_sinf(angle));
            EVE_CoDl_vertex2f(phost, px * 16, py * 16);
        }
        EVE_CoDl_end(phost);
    }
    EVE_CoDl_colorA(phost, 255);

    draw_rect(phost, cx - 26, cy - 26, 52, 52, COL_CARD);
    SET_COLOR(phost, COL_ACCENT);
    EVE_CoCmd_text(phost, cx, cy, FONT_LG, EVE_OPT_CENTER, "ID");

    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx, 60, FONT_SM, EVE_OPT_CENTER, "NEXUS CONTROL SYSTEMS");

    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, cy + 90, FONT_MD, EVE_OPT_CENTER,
                   "Present RFID card or select role");

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             app->rtc_h, app->rtc_m, app->rtc_s);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx, SCR_H - 60, FONT_XL, EVE_OPT_CENTER, time_str);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx, SCR_H - 24, FONT_SM, EVE_OPT_CENTER, "2026/04/11");

    RB(TAG_R1, BTN_Y0, "OPR", app->lock_cursor == 0 ? COL_ACCENT : COL_CARD, true);
    RB(TAG_R2, BTN_Y1, "ADM", app->lock_cursor == 1 ? COL_ACCENT : COL_CARD, true);
    RB(TAG_R3, BTN_Y2, "", COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "", COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   認証オーバーレイ
═══════════════════════════════════════════════════════════ */

static void render_auth(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    (void)tick_ms;
    EVE_CoDl_colorA(phost, 180);
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
    EVE_CoDl_colorA(phost, 255);

    int16_t cx = CONT_X + CONT_W / 2;
    int16_t cy = SCR_H / 2;
    draw_card(phost, cx - 140, cy - 80, 280, 160);
    EVE_CoCmd_bgcolor(phost, COL_CARD);
    EVE_CoCmd_spinner(phost, cx, cy - 20, 0, 1);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, cy + 30, FONT_MD, EVE_OPT_CENTER, "Authenticating...");
}

/* ═══════════════════════════════════════════════════════════
   認証成功通知（上からスライドイン）
═══════════════════════════════════════════════════════════ */

static void render_auth_notif(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    if (!app->auth_notif_active) return;

    uint32_t elapsed  = tick_ms - app->auth_notif_start;
    uint32_t total_ms = AUTH_NOTIF_SLIDE_MS * 2 + AUTH_NOTIF_HOLD_MS;
    if (elapsed >= total_ms) return;

    int16_t panelH = SCR_H / 4;   /* 120px */
    int32_t offsetY;               /* 0=全表示, -panelH=完全隠れ */

    if (elapsed < AUTH_NOTIF_SLIDE_MS) {
        /* スライドイン（ease-out cubic） */
        uint32_t t = elapsed * 1000 / AUTH_NOTIF_SLIDE_MS;   /* 0-1000 */
        uint32_t inv = 1000 - t;
        uint32_t ease = inv * inv / 1000 * inv / 1000;        /* (1-t)^3 */
        offsetY = -(int32_t)(panelH * ease / 1000);
    } else if (elapsed < AUTH_NOTIF_SLIDE_MS + AUTH_NOTIF_HOLD_MS) {
        offsetY = 0;
    } else {
        /* スライドアウト（ease-in cubic） */
        uint32_t t = (elapsed - AUTH_NOTIF_SLIDE_MS - AUTH_NOTIF_HOLD_MS)
                     * 1000 / AUTH_NOTIF_SLIDE_MS;
        uint32_t ease = t * t / 1000 * t / 1000;             /* t^3 */
        offsetY = -(int32_t)(panelH * ease / 1000);
    }

    int16_t clipH = (int16_t)(panelH + offsetY);
    if (clipH <= 0) return;

    /* Scissor でクリップ */
    EVE_CoDl_scissorXY(phost, (uint16_t)CONT_X, 0);
    EVE_CoDl_scissorSize(phost, (uint16_t)CONT_W, (uint16_t)clipH);

    int16_t py = (int16_t)offsetY;  /* パネル上端（負=はみ出し中） */

    /* 背景（ダークグリーン） */
    draw_rect(phost, CONT_X, py, CONT_W, panelH, 0x064E3BUL);
    draw_hline(phost, CONT_X, py + panelH - 2, CONT_W, COL_GREEN, 2);

    /* チェックマークサークル */
    int16_t cx = CONT_X + 60;
    int16_t cy = py + panelH / 2;
    SET_COLOR(phost, COL_GREEN);
    EVE_CoDl_lineWidth(phost, 3 * 16);
    EVE_CoDl_begin(phost, EVE_LINE_STRIP);
    for (int j = 0; j <= 32; j++) {
        float angle = (float)j * 6.2832f / 32.0f;
        int16_t px2 = (int16_t)(cx + 20 * __builtin_cosf(angle));
        int16_t py2 = (int16_t)(cy + 20 * __builtin_sinf(angle));
        EVE_CoDl_vertex2f(phost, px2 * 16, py2 * 16);
    }
    EVE_CoDl_end(phost);
    EVE_CoDl_begin(phost, EVE_LINES);
    EVE_CoDl_vertex2f(phost, (cx - 10) * 16,  cy        * 16);
    EVE_CoDl_vertex2f(phost, (cx - 2)  * 16, (cy + 8)   * 16);
    EVE_CoDl_vertex2f(phost, (cx - 2)  * 16, (cy + 8)   * 16);
    EVE_CoDl_vertex2f(phost, (cx + 12) * 16, (cy - 8)   * 16);
    EVE_CoDl_end(phost);

    /* テキスト */
    SET_COLOR(phost, COL_WHITE);
    EVE_CoCmd_text(phost, CONT_X + 100, cy - 14, FONT_MD, EVE_OPT_CENTERY,
                   app->auth_notif_name);
    SET_COLOR(phost, COL_GREEN);
    EVE_CoCmd_text(phost, CONT_X + 100, cy + 14, FONT_SM, EVE_OPT_CENTERY,
                   app->auth_notif_role);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, CONT_X + CONT_W / 2, cy, FONT_MD,
                   EVE_OPT_CENTER, "Authentication Successful");

    /* Scissor リセット */
    EVE_CoDl_scissorXY(phost, 0, 0);
    EVE_CoDl_scissorSize(phost, 2048, 2048);
}

/* ═══════════════════════════════════════════════════════════
   アラーム詳細パネル（下からスライドイン）
═══════════════════════════════════════════════════════════ */

/* テキスト折り返しヘルパー（文字数ベース） */
static void draw_text_wrap(EVE_HalContext *phost,
                            int16_t x, int16_t y1, int16_t y2,
                            const char *str, uint32_t color, uint8_t font,
                            int max_chars)
{
    size_t len = strlen(str);
    if ((int)len <= max_chars) {
        SET_COLOR(phost, color);
        EVE_CoCmd_text(phost, x, y1, font, 0, str);
        return;
    }
    /* スペースで折り返し位置を探す */
    int split = max_chars;
    while (split > 0 && str[split] != ' ') split--;
    if (split == 0) split = max_chars;

    char buf1[64], buf2[64];
    int l1 = split < 63 ? split : 63;
    strncpy(buf1, str, (size_t)l1); buf1[l1] = '\0';
    const char *rest = str + split;
    while (*rest == ' ') rest++;
    strncpy(buf2, rest, 63); buf2[63] = '\0';

    SET_COLOR(phost, color);
    EVE_CoCmd_text(phost, x, y1, font, 0, buf1);
    if (buf2[0]) EVE_CoCmd_text(phost, x, y2, font, 0, buf2);
}

static void render_alarm_detail(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms)
{
    if (!app->alarm_detail_open) return;

    uint32_t elapsed = tick_ms - app->alarm_detail_start;
    int16_t  panelH  = ALARM_DTL_H;
    int16_t  targetY = SCR_H - panelH;   /* 完全展開時のパネル上端 */
    int32_t  offsetY;                     /* 0=開, panelH=閉 */

    if (app->alarm_detail_closing) {
        if (elapsed >= ALARM_SLIDE_MS) { offsetY = panelH; }
        else {
            offsetY = (int32_t)(panelH * elapsed / ALARM_SLIDE_MS);
        }
    } else {
        if (elapsed >= ALARM_SLIDE_MS) { offsetY = 0; }
        else {
            uint32_t t   = elapsed * 1000 / ALARM_SLIDE_MS;
            uint32_t inv = 1000 - t;
            uint32_t ease = inv * inv / 1000 * inv / 1000;   /* (1-t)^3 */
            offsetY = (int32_t)(panelH * ease / 1000);
        }
    }

    int16_t panelY = targetY + (int16_t)offsetY;
    int16_t clipH  = SCR_H - panelY;
    if (clipH <= 0) return;

    /* Scissor クリップ */
    EVE_CoDl_scissorXY(phost, (uint16_t)CONT_X, (uint16_t)panelY);
    EVE_CoDl_scissorSize(phost, (uint16_t)CONT_W, (uint16_t)clipH);

    /* パネル背景 */
    draw_rect(phost, CONT_X, panelY, CONT_W, panelH, COL_CARD);
    draw_hline(phost, CONT_X, panelY, CONT_W, COL_BORDER, 1);

    /* 選択アラーム */
    AlarmRecord_t *al = &app->alarms[app->alarm_cursor];
    uint32_t lc = (al->level == ALARM_LVL_ERROR) ? COL_RED :
                  (al->level == ALARM_LVL_WARN)  ? COL_YELLOW : COL_ACCENT;

    /* 左カラーバー */
    draw_rect(phost, CONT_X, panelY, 6, panelH, lc);

    /* タイトル */
    int16_t ty = panelY + 20;
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, CONT_X + 20, ty, FONT_LG, 0, al->title);

    /* 詳細テキスト（折り返し） */
    ty += 38;
    draw_text_wrap(phost, CONT_X + 20, ty, ty + 22,
                   al->detail, COL_TXT2, FONT_SM, 60);

    /* メタグリッド */
    ty += 54;
    static const char *meta_keys[4]  = { "Time", "Level", "Reset Auth", "Status" };
    const char *level_str  = (al->level == ALARM_LVL_ERROR) ? "ERROR" :
                             (al->level == ALARM_LVL_WARN)  ? "WARN"  : "INFO";
    const char *status_str = al->active    ? "ACTIVE"   : "RESET";
    const char *auth_str   = al->admin_only? "Admin"    : "Operator";
    const char *meta_vals[4] = { al->time_str, level_str, auth_str, status_str };
    uint32_t    meta_cols[4] = { COL_TXT, lc,
                                 al->admin_only ? COL_YELLOW : COL_TXT,
                                 al->active ? lc : COL_TXT3 };

    int16_t gw = CONT_W / 4;
    for (int i = 0; i < 4; i++) {
        int16_t gx = CONT_X + i * gw;
        draw_rect(phost, gx + 2, ty, gw - 6, 56, COL_CARD2);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, gx + 10, ty + 12, FONT_SM, 0, meta_keys[i]);
        SET_COLOR(phost, meta_cols[i]);
        EVE_CoCmd_text(phost, gx + 10, ty + 36, FONT_SM, 0, meta_vals[i]);
    }

    /* Scissor リセット */
    EVE_CoDl_scissorXY(phost, 0, 0);
    EVE_CoDl_scissorSize(phost, 2048, 2048);
}

/* ═══════════════════════════════════════════════════════════
   ヘッダバー
═══════════════════════════════════════════════════════════ */

static void render_header(EVE_HalContext *phost, AppState_t *app)
{
    draw_rect(phost, CONT_X, 0, CONT_W, HDR_H, COL_SURF);
    draw_hline(phost, CONT_X, HDR_H - 1, CONT_W, COL_BORDER, 1);

    /* ロゴ */
    SET_COLOR(phost, COL_ACCENT);
    EVE_CoCmd_text(phost, CONT_X + 12, HDR_H / 2, FONT_MD, EVE_OPT_CENTERY, "NCS");

    /* 区切り線（x=76 に移動、NCS文字と重ならない位置） */
    draw_vline(phost, CONT_X + 76, 10, HDR_H - 20, COL_BORDER, 1);

    /* 機械名 */
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, CONT_X + 86, HDR_H / 2, FONT_MD, EVE_OPT_CENTERY, "CNC-8100S");

    /* ステータスチップ */
    const char *st_label = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
    uint32_t    st_col   = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
    int16_t     st_x     = CONT_X + 330;
    draw_rect(phost, st_x, 14, 80, 22, (st_col >> 2) & 0x3F3F3FUL);
    SET_COLOR(phost, st_col);
    EVE_CoCmd_text(phost, st_x + 40, 25, FONT_SM, EVE_OPT_CENTER, st_label);

    /* アクティブアラームバッジ */
    {
        uint8_t act = 0;
        for (int i = 0; i < (int)app->alarm_count; i++)
            if (app->alarms[i].active) act++;
        if (act > 0) {
            int16_t bx = st_x + 90;
            draw_rect(phost, bx, 14, 30, 22, COL_RED);
            char abuf[8]; snprintf(abuf, sizeof(abuf), "%d", act);
            SET_COLOR(phost, COL_WHITE);
            EVE_CoCmd_text(phost, bx + 15, 25, FONT_SM, EVE_OPT_CENTER, abuf);
        }
    }

    /* 右端：ユーザー名（3行縦並び） */
    char time_str[10];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             app->rtc_h, app->rtc_m, app->rtc_s);

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, RIGHT_X - 8, 10, FONT_SM, EVE_OPT_RIGHTX, app->user_name);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, RIGHT_X - 8, 36, FONT_SM, EVE_OPT_RIGHTX, app->user_role_str);
    EVE_CoCmd_text(phost, RIGHT_X - 8, 58, FONT_SM, EVE_OPT_RIGHTX, time_str);
}

/* ═══════════════════════════════════════════════════════════
   ホームパネル
═══════════════════════════════════════════════════════════ */

static void render_home(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Dashboard");
    y += TITLE_OFFSET;

    /* ステータスカード 4列 */
    int16_t cw = (CONT_W - 40) / 4;
    int16_t ch = 88;
    static const char *sc_labels[4] = { "MACHINE", "SPINDLE RPM", "PARTS", "TEMPERATURE" };

    for (int i = 0; i < 4; i++) {
        int16_t sx = cx + 8 + i * (cw + 8);
        draw_card(phost, sx, y, cw, ch);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, sx + 8, y + 10, FONT_SM, 0, sc_labels[i]);
    }

    /* カード0: 機械状態 */
    {
        int16_t sx = cx + 8;
        const char *s = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t sc   = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        SET_COLOR(phost, sc);
        EVE_CoCmd_text(phost, sx + 8, y + 36, FONT_MD, 0, s);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8, y + 64, FONT_SM, 0, "Today: 08:32:15");
    }
    /* カード1: RPM */
    {
        int16_t sx = cx + 8 + (cw + 8);
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, sx + 8, y + 32, FONT_LG, 0, buf);
        char buf2[24];
        snprintf(buf2, sizeof(buf2), "Set: %lu", (unsigned long)app->set_rpm);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8, y + 64, FONT_SM, 0, buf2);
    }
    /* カード2: 加工数 */
    {
        int16_t sx = cx + 8 + 2 * (cw + 8);
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->parts_total);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, sx + 8, y + 32, FONT_LG, 0, buf);
        char buf2[20];
        snprintf(buf2, sizeof(buf2), "Today: %lu", (unsigned long)app->parts_today);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8, y + 64, FONT_SM, 0, buf2);
    }
    /* カード3: 温度 */
    {
        int16_t sx = cx + 8 + 3 * (cw + 8);
        char buf[8];
        snprintf(buf, sizeof(buf), "%dC", app->spindle_temp);
        SET_COLOR(phost, COL_YELLOW);
        EVE_CoCmd_text(phost, sx + 8, y + 32, FONT_LG, 0, buf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, sx + 8, y + 64, FONT_SM, 0, "Rising");
    }
    y += ch + 10;

    /* ゲージ 2列 */
    int16_t gw = (CONT_W - 32) / 2;
    int16_t gh = 72;

    draw_card(phost, cx + 8, y, gw, gh);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 16, y + 10, FONT_SM, 0, "SPINDLE LOAD");
    draw_bar(phost, cx + 16, y + 32, gw - 32, 10, app->spindle_load, COL_ACCENT);
    {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", app->spindle_load);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + gw / 2, y + 54, FONT_SM, EVE_OPT_CENTERX, buf);
    }

    draw_card(phost, cx + 8 + gw + 8, y, gw, gh);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 8 + gw + 16, y + 10, FONT_SM, 0, "COOLANT TEMP");
    draw_bar(phost, cx + 8 + gw + 16, y + 32, gw - 32, 10, app->coolant_temp, COL_TEAL);
    {
        char buf[8]; snprintf(buf, sizeof(buf), "%dC", app->coolant_temp);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 8 + gw + gw / 2, y + 54, FONT_SM, EVE_OPT_CENTERX, buf);
    }
    y += gh + 10;

    /* 最近のアラーム */
    int16_t ah = SCR_H - y - 8;
    draw_card(phost, cx + 8, y, CONT_W - 16, ah);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 16, y + 10, FONT_SM, 0, "RECENT ALARMS");

    for (int i = 0; i < 2 && i < (int)app->alarm_count; i++) {
        AlarmRecord_t *al = &app->alarms[i];
        uint32_t lc = (al->level == ALARM_LVL_ERROR) ? COL_RED :
                      (al->level == ALARM_LVL_WARN)  ? COL_YELLOW : COL_ACCENT;
        int16_t ay = y + 34 + i * 26;
        SET_COLOR(phost, lc);
        EVE_CoCmd_text(phost, cx + 16, ay, FONT_SM, 0, ">");
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 30, ay, FONT_SM, 0, al->title);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, RIGHT_X - 80, ay, FONT_SM, 0, al->time_str);
    }

    RB(TAG_R1, BTN_Y0, "OPER", COL_CARD, true);
    RB(TAG_R2, BTN_Y1, "ALRM", COL_CARD, true);
    RB(TAG_R3, BTN_Y2, "LOG",  COL_CARD, true);
    RB(TAG_R4, BTN_Y3, "EXIT", COL_RED,  true);
}

/* ═══════════════════════════════════════════════════════════
   運転操作パネル
═══════════════════════════════════════════════════════════ */

static void render_operation(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Operation");
    y += TITLE_OFFSET;

    int16_t lw = CONT_W * 55 / 100;
    int16_t rw = CONT_W - lw - 16;
    int16_t lh = SCR_H - y - 12;

    draw_card(phost, cx + 8, y, lw, lh);

    /* 運転状態 */
    {
        const char *st = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t    sc = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + 12, FONT_SM, 0, "MACHINE CONTROL");
        SET_COLOR(phost, sc);
        EVE_CoCmd_text(phost, cx + 16, y + 30, FONT_LG, 0, st);
    }

    /* RPM 表示・バー（Settings の max/min 連動） */
    {
        uint32_t rpm_max   = app->settings_max_rpm;
        uint32_t rpm_min   = app->settings_min_rpm;
        uint32_t rpm_range = (rpm_max > rpm_min) ? rpm_max - rpm_min : 1;
        uint32_t clamped   = (app->set_rpm > rpm_min) ? app->set_rpm - rpm_min : 0;
        uint8_t  pct       = (uint8_t)(clamped * 100 / rpm_range);
        uint32_t bar_col   = rpm_color(app->set_rpm);

        char buf[24];
        snprintf(buf, sizeof(buf), "SET: %lu RPM", (unsigned long)app->set_rpm);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16, y + 64, FONT_MD, 0, buf);

        snprintf(buf, sizeof(buf), "ACT: %lu RPM", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 86, FONT_SM, 0, buf);

        draw_bar(phost, cx + 16, y + 108, lw - 32, 10, pct, bar_col);

        char mn_buf[8], md_buf[8], mx_buf[8];
        snprintf(mn_buf, sizeof(mn_buf), "%lu", (unsigned long)rpm_min);
        snprintf(md_buf, sizeof(md_buf), "%lu", (unsigned long)((rpm_min + rpm_max) / 2));
        snprintf(mx_buf, sizeof(mx_buf), "%lu", (unsigned long)rpm_max);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16,                  y + 124, FONT_SM, 0,                mn_buf);
        EVE_CoCmd_text(phost, cx + 16 + (lw - 32) / 2, y + 124, FONT_SM, EVE_OPT_CENTERX,  md_buf);
        EVE_CoCmd_text(phost, cx + lw - 16,             y + 124, FONT_SM, EVE_OPT_RIGHTX,   mx_buf);
    }

    /* フィードレートタブ（パターン対応） */
    {
        uint8_t pat_cnt = feed_pattern_count[app->settings_feed_pattern];
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + 148, FONT_SM, 0, "FEED RATE");
        for (int i = 0; i < pat_cnt; i++) {
            uint8_t val = feed_patterns[app->settings_feed_pattern][i];
            int16_t fx  = cx + 16 + i * 62;
            bool    sel = (app->feed_idx == i);
            bool    is100 = (val == 100);
            uint32_t bg = sel ? (is100 ? COL_GREEN : COL_ACCENT) : COL_CARD2;
            draw_rect(phost, fx, y + 166, 56, 28, bg);
            char lbl[8]; snprintf(lbl, sizeof(lbl), "%d%%", val);
            SET_COLOR(phost, sel ? COL_WHITE : COL_TXT2);
            EVE_CoCmd_text(phost, fx + 28, y + 180, FONT_SM, EVE_OPT_CENTER, lbl);
        }
    }

    /* スピンドル負荷（バーと%テキストを重ならないよう分離） */
    {
        char buf[12];
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + 212, FONT_SM, 0, "SPINDLE LOAD");
        draw_bar(phost, cx + 16, y + 232, lw - 32, 14, app->spindle_load, COL_TEAL);
        snprintf(buf, sizeof(buf), "%d%%", app->spindle_load);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16 + (lw - 32) / 2, y + 264,
                       FONT_SM, EVE_OPT_CENTERX, buf);
    }

    /* 右カード上：CURRENT STATUS */
    int16_t rx = cx + 8 + lw + 8;
    int16_t rh = (lh - 8) / 2;
    draw_card(phost, rx, y, rw, rh);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, rx + 8, y + 10, FONT_SM, 0, "CURRENT STATUS");

    {
        const char *st_str = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t    st_col = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        uint8_t     fv     = feed_patterns[app->settings_feed_pattern][app->feed_idx];
        uint32_t    fc     = (fv == 100) ? COL_GREEN : COL_TXT;
        uint32_t    opSec  = app->op_seconds;

        typedef struct { const char *k; char v[16]; uint32_t vc; } Row;
        Row rows[6];
        snprintf(rows[0].v, 16, "%s",   st_str);                          rows[0].vc = st_col;
        snprintf(rows[1].v, 16, "%lu",  (unsigned long)app->actual_rpm);  rows[1].vc = rpm_color(app->actual_rpm);
        snprintf(rows[2].v, 16, "%d%%", app->spindle_load);               rows[2].vc = COL_TXT;
        snprintf(rows[3].v, 16, "%d%%", fv);                              rows[3].vc = fc;
        snprintf(rows[4].v, 16, "%lu",  (unsigned long)app->parts_today); rows[4].vc = COL_TXT;
        snprintf(rows[5].v, 16, "%02lu:%02lu:%02lu",
                 (unsigned long)(opSec/3600),
                 (unsigned long)((opSec%3600)/60),
                 (unsigned long)(opSec%60));                               rows[5].vc = COL_TXT;
        static const char *keys[6] = {
            "Status", "Actual RPM", "Spindle Load", "Feed Rate",
            "Parts Today", "Run Time"
        };
        for (int i = 0; i < 6; i++) {
            rows[i].k = keys[i];
            int16_t ry = y + 32 + i * 22;
            SET_COLOR(phost, COL_TXT2);
            EVE_CoCmd_text(phost, rx + 8,      ry, FONT_SM, 0,             rows[i].k);
            SET_COLOR(phost, rows[i].vc);
            EVE_CoCmd_text(phost, rx + rw - 8, ry, FONT_SM, EVE_OPT_RIGHTX, rows[i].v);
        }
    }

    /* 右カード下：温度 */
    int16_t ty = y + rh + 8;
    draw_card(phost, rx, ty, rw, rh);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, rx + 8, ty + 10, FONT_SM, 0, "TEMPERATURE");
    static const char *temp_keys[3] = { "Spindle", "Coolant", "Ambient" };
    uint8_t temp_vals[3] = { app->spindle_temp, app->coolant_temp, app->env_temp };
    for (int i = 0; i < 3; i++) {
        int16_t ry = ty + 32 + i * 24;
        char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%dC", temp_vals[i]);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, rx + 8,      ry, FONT_SM, 0,             temp_keys[i]);
        SET_COLOR(phost, temp_vals[i] > 60 ? COL_YELLOW : COL_TXT);
        EVE_CoCmd_text(phost, rx + rw - 8, ry, FONT_SM, EVE_OPT_RIGHTX, tbuf);
    }

    /* 右ボタン */
    if (app->machine == MACHINE_RUNNING) {
        RB(TAG_R1, BTN_Y0, "STOP", COL_RED,   true);
    } else {
        RB(TAG_R1, BTN_Y0, "RUN",  COL_GREEN, true);
    }
    RB(TAG_R2, BTN_Y1, "RPM+", COL_CARD, true);
    RB(TAG_R3, BTN_Y2, "RPM-", COL_CARD, true);
    RB(TAG_R4, BTN_Y3, "FEED", COL_CARD, true);
}

/* ═══════════════════════════════════════════════════════════
   アラームパネル
═══════════════════════════════════════════════════════════ */

static void render_alarm(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Alarms");
    y += TITLE_OFFSET;

    /* フィルタタブ */
    static const char *ftabs[4] = { "ALL", "ERR", "WARN", "INFO" };
    for (int i = 0; i < 4; i++) {
        bool sel = (app->alarm_filter == (AlarmFilter_t)i);
        draw_rect(phost, cx + 8 + i * 74, y, 68, 30,
                  sel ? COL_ACCENT : COL_CARD2);
        SET_COLOR(phost, sel ? COL_WHITE : COL_TXT2);
        EVE_CoCmd_text(phost, cx + 8 + i * 74 + 34, y + 15,
                       FONT_SM, EVE_OPT_CENTER, ftabs[i]);
    }
    y += 38;

    /* フィルタ件数カウント */
    int filtered_count = 0;
    for (int i = 0; i < (int)app->alarm_count; i++) {
        AlarmRecord_t *al = &app->alarms[i];
        if (app->alarm_filter == ALARM_FILTER_ERROR && al->level != ALARM_LVL_ERROR) continue;
        if (app->alarm_filter == ALARM_FILTER_WARN  && al->level != ALARM_LVL_WARN)  continue;
        if (app->alarm_filter == ALARM_FILTER_INFO  && al->level != ALARM_LVL_INFO)  continue;
        filtered_count++;
    }

    /* 表示可能行数 */
    int visible = (SCR_H - y - 8) / ROW_H_AL;
    int max_scroll = filtered_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (app->alarm_scroll > (uint8_t)max_scroll) app->alarm_scroll = (uint8_t)max_scroll;

    int shown = 0;
    int skip  = app->alarm_scroll;

    for (int i = 0; i < (int)app->alarm_count && shown < visible; i++) {
        AlarmRecord_t *al = &app->alarms[i];
        if (app->alarm_filter == ALARM_FILTER_ERROR && al->level != ALARM_LVL_ERROR) continue;
        if (app->alarm_filter == ALARM_FILTER_WARN  && al->level != ALARM_LVL_WARN)  continue;
        if (app->alarm_filter == ALARM_FILTER_INFO  && al->level != ALARM_LVL_INFO)  continue;
        if (skip > 0) { skip--; continue; }

        uint32_t lc  = (al->level == ALARM_LVL_ERROR) ? COL_RED :
                       (al->level == ALARM_LVL_WARN)  ? COL_YELLOW : COL_ACCENT;
        int16_t  ay  = y + shown * ROW_H_AL;
        bool     sel = (app->alarm_cursor == i);

        /* 選択行背景 */
        draw_rect(phost, cx + 8, ay, CONT_W - 16, ROW_H_AL - 2,
                  sel ? 0x0E2A45UL : COL_CARD);
        if (sel) {
            SET_COLOR(phost, COL_ACCENT);
            EVE_CoDl_lineWidth(phost, 2 * 16);
            EVE_CoDl_begin(phost, EVE_LINE_STRIP);
            EVE_CoDl_vertex2f(phost, (cx + 8)          * 16,  ay                    * 16);
            EVE_CoDl_vertex2f(phost, (cx + CONT_W - 8) * 16,  ay                    * 16);
            EVE_CoDl_vertex2f(phost, (cx + CONT_W - 8) * 16, (ay + ROW_H_AL - 2)   * 16);
            EVE_CoDl_vertex2f(phost, (cx + 8)          * 16, (ay + ROW_H_AL - 2)   * 16);
            EVE_CoDl_vertex2f(phost, (cx + 8)          * 16,  ay                    * 16);
            EVE_CoDl_end(phost);
        }
        /* 左カラーバー */
        draw_rect(phost, cx + 8, ay, 6, ROW_H_AL - 2, sel ? lc : (lc >> 1) & 0x7F7F7FUL);

        /* 行1: タイトル + ステータス */
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 20, ay + 16, FONT_MD, EVE_OPT_CENTERY, al->title);
        const char *slabel = al->active ? "ACTIVE" : "RESET";
        SET_COLOR(phost, al->active ? lc : COL_TXT3);
        EVE_CoCmd_text(phost, RIGHT_X - 90, ay + 16, FONT_SM, EVE_OPT_CENTERY, slabel);

        /* 行2: viewed ドット + 時刻 */
        uint32_t dot_col = al->viewed ? COL_TXT3 : lc;
        SET_COLOR(phost, dot_col);
        EVE_CoDl_pointSize(phost, 4 * 16);
        EVE_CoDl_begin(phost, EVE_POINTS);
        EVE_CoDl_vertex2f(phost, (cx + 23) * 16, (ay + 46) * 16);
        EVE_CoDl_end(phost);

        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 34, ay + 46, FONT_SM, EVE_OPT_CENTERY, al->time_str);

        shown++;
    }

    if (filtered_count == 0) {
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, CONT_X + CONT_W / 2, y + 50,
                       FONT_MD, EVE_OPT_CENTERX, "No alarms");
    }

    bool can_reset = (app->role == ROLE_ADMIN);
    bool feed_en   = !app->alarm_detail_open;
    RB(TAG_R1, BTN_Y0, "DTL",  COL_CARD, true);
    RB(TAG_R2, BTN_Y1, "RST",  can_reset ? COL_YELLOW : COL_SURF, can_reset);
    RB(TAG_R3, BTN_Y2, "FEED", feed_en ? COL_CARD : COL_SURF, feed_en);
    RB(TAG_R4, BTN_Y3, "",     COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   ログパネル（スクロール対応）
═══════════════════════════════════════════════════════════ */

static void render_log(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Operation Log");
    y += TITLE_OFFSET;

    /* 7カテゴリタブ (TAG_LOG_TAB_BASE = 30) */
    static const char *cat_labels[7] = {
        "ALL", "LOGIN", "OPER", "ALARM", "LOG", "SETT", "USER"
    };
    for (int i = 0; i < 7; i++) {
        bool is_adm = (i >= 5);   /* SETT/USER は Admin のみ */
        bool en     = !is_adm || (app->role == ROLE_ADMIN);
        bool sel    = ((int)app->log_filter == i);
        uint32_t bg = !en ? COL_SURF : sel ? COL_ACCENT : COL_CARD2;
        uint32_t fg = !en ? COL_TXT3 : sel ? COL_WHITE  : COL_TXT2;
        int16_t tx  = cx + 8 + i * (LOG_TAB_W + LOG_TAB_GAP);
        EVE_CoDl_tag(phost, en ? (uint8_t)(TAG_LOG_TAB_BASE + i) : TAG_NONE);
        draw_rect(phost, tx, y, LOG_TAB_W, 30, bg);
        SET_COLOR(phost, fg);
        EVE_CoCmd_text(phost, tx + LOG_TAB_W / 2, y + 15, FONT_SM, EVE_OPT_CENTER, cat_labels[i]);
        EVE_CoDl_tag(phost, TAG_NONE);
    }
    y += 38;

    /* テーブルヘッダ */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 34, COL_SURF);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 16,  y + 8, FONT_SM, 0, "TIME");
    EVE_CoCmd_text(phost, cx + 130, y + 8, FONT_SM, 0, "USER");
    EVE_CoCmd_text(phost, cx + 260, y + 8, FONT_SM, 0, "EVENT");
    y += 38;

    /* フィルタ件数 */
    int filtered_count = 0;
    for (int i = 0; i < (int)app->log_count; i++) {
        LogEntry_t *le = &app->log_entries[i];
        if (app->log_filter != LOG_FILTER_ALL &&
            (int)le->cat != (int)app->log_filter - 1) continue;
        filtered_count++;
    }

    int visible    = (SCR_H - 8 - y) / LOG_ROW_H;
    int max_scroll = filtered_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (app->log_scroll > (uint8_t)max_scroll) app->log_scroll = (uint8_t)max_scroll;

    /* スクロールインジケータ */
    {
        int end = app->log_scroll + visible;
        if (end > filtered_count) end = filtered_count;
        char ibuf[16];
        snprintf(ibuf, sizeof(ibuf), "%d-%d/%d",
                 filtered_count ? app->log_scroll + 1 : 0, end, filtered_count);
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, RIGHT_X - 8, HDR_H + 12 + 14, FONT_SM, EVE_OPT_RIGHTX, ibuf);
    }

    int shown = 0, skip = app->log_scroll;
    for (int i = 0; i < (int)app->log_count && shown < visible; i++) {
        LogEntry_t *le = &app->log_entries[i];
        if (app->log_filter != LOG_FILTER_ALL &&
            (int)le->cat != (int)app->log_filter - 1) continue;
        if (skip > 0) { skip--; continue; }

        int16_t ly = y + shown * LOG_ROW_H;
        draw_rect(phost, cx + 8, ly, CONT_W - 16, LOG_ROW_H - 1,
                  shown % 2 == 0 ? COL_CARD : COL_BG);

        /* カテゴリ別カラーバー */
        uint32_t cat_col;
        switch (le->cat) {
        case LOG_CAT_LOGIN:     cat_col = COL_ACCENT; break;
        case LOG_CAT_OPERATION: cat_col = COL_GREEN;  break;
        case LOG_CAT_ALARM:     cat_col = COL_RED;    break;
        case LOG_CAT_LOG:       cat_col = COL_TEAL;   break;
        case LOG_CAT_SETTINGS:  cat_col = COL_YELLOW; break;
        case LOG_CAT_USER:      cat_col = COL_ORANGE; break;
        default:                cat_col = COL_TXT2;   break;
        }
        draw_rect(phost, cx + 8, ly, 4, LOG_ROW_H - 1, cat_col);

        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16,  ly + 8, FONT_SM, 0, le->time);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 130, ly + 8, FONT_SM, 0, le->user);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 260, ly + 8, FONT_SM, 0, le->event);
        shown++;
    }

    bool can_up = (app->log_scroll > 0);
    bool can_dn = (app->log_scroll < (uint8_t)max_scroll);
    RB(TAG_R1, BTN_Y0, "UP", can_up ? COL_CARD : COL_SURF, can_up);
    RB(TAG_R2, BTN_Y1, "DN", can_dn ? COL_CARD : COL_SURF, can_dn);
    RB(TAG_R3, BTN_Y2, "", COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "", COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   設定パネル（カーソル・編集モード対応）
═══════════════════════════════════════════════════════════ */

static void render_settings(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Settings");
    SET_COLOR(phost, COL_YELLOW);
    EVE_CoCmd_text(phost, cx + CONT_W - 8, y + 8, FONT_SM, EVE_OPT_RIGHTX, "[ADMIN ONLY]");
    y += TITLE_OFFSET;

    int16_t hw    = (CONT_W - 24) / 2;
    int16_t cardH = 34 + 4 * SETT_ROW_H + 8;

    /* ─ Operation Parameters (左カード) ─ */
    draw_card(phost, cx + 8, y, hw, cardH);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 16, y + 8, FONT_MD, 0, "Operation Parameters");

    /* Feed Pattern ラベル */
    char fp_lbl[24];
    snprintf(fp_lbl, sizeof(fp_lbl), "%s",
             app->settings_feed_pattern == 0 ? "Pattern1(4step)" : "Pattern2(2step)");

    const char *param_keys[4] = { "Max RPM", "Min RPM", "Feed Pattern", "Tool No." };
    char        param_vals[4][20];
    snprintf(param_vals[0], 20, "%lu", (unsigned long)app->settings_max_rpm);
    snprintf(param_vals[1], 20, "%lu", (unsigned long)app->settings_min_rpm);
    snprintf(param_vals[2], 20, "%s",  fp_lbl);
    snprintf(param_vals[3], 20, "T%02d", app->settings_tool_no);

    for (int i = 0; i < 4; i++) {
        int16_t ry  = y + 34 + i * SETT_ROW_H;
        bool    sel = (app->settings_cursor == i);
        bool    ed  = sel && app->settings_editing;

        /* 行背景：非選択/選択/編集中で3段階 */
        uint32_t row_bg = ed  ? 0x0D2E50UL :
                          sel ? 0x1A3A5CUL :
                          (i % 2 == 0 ? COL_CARD : COL_BG);
        draw_rect(phost, cx + 8, ry, hw, SETT_ROW_H, row_bg);

        draw_hline(phost, cx + 16, ry, hw - 24, COL_BORDER, 1);

        /* 左サイドバー */
        uint32_t bar_col = ed ? COL_ACCENT : sel ? COL_ACCENT : COL_BORDER;
        draw_rect(phost, cx + 8, ry, 6, SETT_ROW_H, bar_col);

        SET_COLOR(phost, ed ? COL_WHITE : sel ? COL_TXT : COL_TXT2);
        EVE_CoCmd_text(phost, cx + 22, ry + 14, FONT_SM, 0, param_keys[i]);

        SET_COLOR(phost, ed ? COL_ACCENT : sel ? COL_WHITE : COL_TXT);
        EVE_CoCmd_text(phost, cx + 8 + hw - 10, ry + 14,
                       FONT_SM, EVE_OPT_RIGHTX, param_vals[i]);
    }

    /* ─ System Info (右カード) ─ */
    int16_t rx = cx + 8 + hw + 8;
    draw_card(phost, rx, y, hw, cardH);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, rx + 8, y + 8, FONT_MD, 0, "System");

    static const char *sys_keys[4] = { "Panel ID", "FW Version", "Display", "Network" };
    static const char *sys_vals[4] = { "CNC-8100S", "v2.1.0", "800x480", "192.168.1.10" };
    for (int i = 0; i < 4; i++) {
        int16_t ry = y + 34 + i * SETT_ROW_H;
        draw_hline(phost, rx + 8, ry, hw - 24, COL_BORDER, 1);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, rx + 8,      ry + 14, FONT_SM, 0,             sys_keys[i]);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, rx + hw - 10, ry + 14, FONT_SM, EVE_OPT_RIGHTX, sys_vals[i]);
    }

    /* ヒント */
    if (!app->settings_editing) {
        SET_COLOR(phost, COL_TXT3);
        EVE_CoCmd_text(phost, cx + 16, y + cardH + 10, FONT_SM, 0,
                       "UP/DN=Select  OK=Edit");
    }

    /* 右ボタン */
    if (app->settings_editing) {
        RB_XL(TAG_R1, BTN_Y0, "+",  COL_ACCENT, true);
        RB_XL(TAG_R2, BTN_Y1, "-",  COL_CARD,   true);
        RB   (TAG_R3, BTN_Y2, "OK", COL_GREEN,  true);
        RB   (TAG_R4, BTN_Y3, "",   COL_SURF,   false);
    } else {
        bool can_save = app->settings_changed;
        RB(TAG_R1, BTN_Y0, "SAVE", can_save ? COL_ACCENT : COL_SURF, can_save);
        RB(TAG_R2, BTN_Y1, "", COL_SURF, false);
        RB(TAG_R3, BTN_Y2, "", COL_SURF, false);
        RB(TAG_R4, BTN_Y3, "", COL_SURF, false);
    }
}

/* ═══════════════════════════════════════════════════════════
   ユーザー管理パネル（カーソル・Active/Inactive切替）
═══════════════════════════════════════════════════════════ */

static void render_users(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "User Management");
    SET_COLOR(phost, COL_YELLOW);
    EVE_CoCmd_text(phost, cx + CONT_W - 8, y + 8, FONT_SM, EVE_OPT_RIGHTX, "[ADMIN ONLY]");
    y += TITLE_OFFSET;

    /* テーブルヘッダ */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 30, COL_SURF);
    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, cx + 16,  y + 6, FONT_SM, 0, "USER");
    EVE_CoCmd_text(phost, cx + 180, y + 6, FONT_SM, 0, "ROLE");
    EVE_CoCmd_text(phost, cx + 290, y + 6, FONT_SM, 0, "LAST LOGIN");
    EVE_CoCmd_text(phost, cx + 460, y + 6, FONT_SM, 0, "STATUS");
    y += 34;

    for (int i = 0; i < USER_COUNT; i++) {
        int16_t ry  = y + i * USER_ROW_H;
        bool    sel = (app->user_cursor == i);
        bool    is_admin = (user_data[i].role[0] == 'A'); /* Admin */

        /* 行背景 */
        draw_rect(phost, cx + 8, ry, CONT_W - 16, USER_ROW_H - 2,
                  sel ? 0x0E2A45UL : (i % 2 == 0 ? COL_CARD : COL_BG));
        if (sel) {
            SET_COLOR(phost, COL_ACCENT);
            EVE_CoDl_lineWidth(phost, 1 * 16);
            EVE_CoDl_begin(phost, EVE_LINE_STRIP);
            EVE_CoDl_vertex2f(phost, (cx + 8)          * 16, ry * 16);
            EVE_CoDl_vertex2f(phost, (cx + CONT_W - 8) * 16, ry * 16);
            EVE_CoDl_vertex2f(phost, (cx + CONT_W - 8) * 16, (ry + USER_ROW_H - 2) * 16);
            EVE_CoDl_vertex2f(phost, (cx + 8)          * 16, (ry + USER_ROW_H - 2) * 16);
            EVE_CoDl_vertex2f(phost, (cx + 8)          * 16, ry * 16);
            EVE_CoDl_end(phost);
            draw_rect(phost, cx + 8, ry, 4, USER_ROW_H - 2, COL_ACCENT);
        }

        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16,  ry + 12, FONT_SM, 0, user_data[i].name);
        SET_COLOR(phost, is_admin ? COL_YELLOW : COL_TXT2);
        EVE_CoCmd_text(phost, cx + 180, ry + 12, FONT_SM, 0, user_data[i].role);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 290, ry + 12, FONT_SM, 0, user_data[i].last_login);
        SET_COLOR(phost, user_data[i].active ? COL_GREEN : COL_TXT3);
        EVE_CoCmd_text(phost, cx + 460, ry + 12, FONT_SM, 0,
                       user_data[i].active ? "Active" : "Inactive");
        if (is_admin && sel) {
            SET_COLOR(phost, COL_TXT3);
            EVE_CoCmd_text(phost, cx + 540, ry + 12, FONT_SM, 0, "(fixed)");
        }
    }

    /* 右ボタン（選択ユーザーに応じて切替） */
    bool is_admin   = (user_data[app->user_cursor].role[0] == 'A');
    bool is_active  = user_data[app->user_cursor].active;

    RB(TAG_R1, BTN_Y0, "ACTV",
       (!is_admin && !is_active) ? COL_GREEN  : COL_SURF,
       !is_admin && !is_active);
    RB(TAG_R2, BTN_Y1, "INAC",
       (!is_admin && is_active)  ? COL_YELLOW : COL_SURF,
       !is_admin && is_active);
    RB(TAG_R3, BTN_Y2, "", COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "", COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   メニューオーバーレイ
═══════════════════════════════════════════════════════════ */

#define MENU_ITEM_H 44

static void render_menu_overlay(EVE_HalContext *phost, AppState_t *app)
{
    EVE_CoDl_colorA(phost, 200);
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
    EVE_CoDl_colorA(phost, 255);

    int16_t mx = CONT_X + 20;
    int16_t my = 20;
    int16_t mw = 240;

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

    /* 可視数カウント */
    int8_t n_vis = 0;
    for (int i = 0; i < nitems; i++) {
        if (!menu_items[i].admin || app->role == ROLE_ADMIN) n_vis++;
    }
    int16_t totalH = 38 + n_vis * MENU_ITEM_H + 16;
    draw_card(phost, mx, my, mw, totalH);

    SET_COLOR(phost, COL_TXT3);
    EVE_CoCmd_text(phost, mx + 14, my + 12, FONT_SM, 0, "NAVIGATION");

    int8_t vi = 0;
    for (int i = 0; i < nitems; i++) {
        if (menu_items[i].admin && app->role != ROLE_ADMIN) continue;
        int16_t iy  = my + 38 + vi * MENU_ITEM_H;
        bool    sel = (app->menu_cursor == vi);
        bool    cur = (app->panel == menu_items[i].panel);

        draw_rect(phost, mx + 4, iy, mw - 8, MENU_ITEM_H - 4,
                  sel ? COL_ACCENT : cur ? COL_CARD2 : COL_SURF);

        EVE_CoDl_tag(phost, menu_items[i].tag);
        SET_COLOR(phost, sel ? COL_WHITE : cur ? COL_ACCENT : COL_TXT2);
        EVE_CoCmd_text(phost, mx + 18, iy + (MENU_ITEM_H - 4) / 2,
                       FONT_MD, EVE_OPT_CENTERY, menu_items[i].label);
        if (menu_items[i].admin) {
            SET_COLOR(phost, COL_YELLOW);
            EVE_CoCmd_text(phost, mx + mw - 14, iy + (MENU_ITEM_H - 4) / 2,
                           FONT_SM, EVE_OPT_CENTERY | EVE_OPT_RIGHTX, "ADM");
        }
        EVE_CoDl_tag(phost, TAG_NONE);
        vi++;
    }
}

/* ═══════════════════════════════════════════════════════════
   トースト通知
═══════════════════════════════════════════════════════════ */

static void render_toast(EVE_HalContext *phost, AppState_t *app)
{
    if (!app->toast_visible) return;

    int16_t tw = 320, th = 42;
    int16_t tx = CONT_X + (CONT_W - tw) / 2;
    int16_t ty = SCR_H - 54;

    draw_rect(phost, tx, ty, tw, th, COL_CARD2);
    SET_COLOR(phost, COL_BORDER);
    EVE_CoDl_lineWidth(phost, 1 * 16);
    EVE_CoDl_begin(phost, EVE_LINE_STRIP);
    EVE_CoDl_vertex2f(phost,  tx       * 16,  ty       * 16);
    EVE_CoDl_vertex2f(phost, (tx + tw) * 16,  ty       * 16);
    EVE_CoDl_vertex2f(phost, (tx + tw) * 16, (ty + th) * 16);
    EVE_CoDl_vertex2f(phost,  tx       * 16, (ty + th) * 16);
    EVE_CoDl_vertex2f(phost,  tx       * 16,  ty       * 16);
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
        render_lock(phost, app, tick_ms);
        render_auth(phost, app, tick_ms);

    } else {
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

        /* アラーム詳細スライドパネル */
        if (app->panel == PANEL_ALARM && app->alarm_detail_open)
            render_alarm_detail(phost, app, tick_ms);

        if (app->menu_open) render_menu_overlay(phost, app);

        /* 認証成功通知（最前面） */
        if (app->auth_notif_active)
            render_auth_notif(phost, app, tick_ms);

        render_toast(phost, app);
    }

    EVE_CoDl_display(phost);
    EVE_CoCmd_swap(phost);
    EVE_Hal_flush(phost);
}

/* ═══════════════════════════════════════════════════════════
   タッチ処理
═══════════════════════════════════════════════════════════ */

static int8_t menu_item_count(AppState_t *app)
{
    return (app->role == ROLE_ADMIN) ? 6 : 4;
}

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
    uint8_t tag    = EVE_Hal_rd8(phost, REG_TOUCH_TAG);
    bool    rising = (tag != TAG_NONE && app->tag_prev == TAG_NONE);
    app->tag_prev  = tag;
    if (!rising) return;

    if (app->screen == SCREEN_AUTH) return;

    /* ─ ロック画面 ─ */
    if (app->screen == SCREEN_LOCK) {
        switch (tag) {
        case TAG_L2: app->lock_cursor = 0; break;
        case TAG_L4: app->lock_cursor = 1; break;
        case TAG_L3:
            app->auth_pending    = (app->lock_cursor == 1) ? ROLE_ADMIN : ROLE_OPERATOR;
            app->auth_tick_start = tick_ms;
            app->screen          = SCREEN_AUTH;
            break;
        case TAG_R1:
            app->lock_cursor = 0; app->auth_pending = ROLE_OPERATOR;
            app->auth_tick_start = tick_ms; app->screen = SCREEN_AUTH; break;
        case TAG_R2:
            app->lock_cursor = 1; app->auth_pending = ROLE_ADMIN;
            app->auth_tick_start = tick_ms; app->screen = SCREEN_AUTH; break;
        }
        return;
    }

    /* L1: メニュートグル */
    if (tag == TAG_L1) { app->menu_open = !app->menu_open; return; }

    /* メニュー開状態 */
    if (app->menu_open) {
        int8_t max_item = menu_item_count(app);
        switch (tag) {
        case TAG_L2:
            app->menu_cursor--;
            if (app->menu_cursor < 0) app->menu_cursor = max_item - 1;
            break;
        case TAG_L4:
            app->menu_cursor++;
            if (app->menu_cursor >= max_item) app->menu_cursor = 0;
            break;
        case TAG_L3:
            app->panel = menu_idx_to_panel(app, app->menu_cursor);
            app->menu_open = false;
            break;
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

    /* パネル別操作 */
    switch (app->panel) {

    /* ─ ホーム ─ */
    case PANEL_HOME:
        switch (tag) {
        case TAG_R1: app->panel = PANEL_OPERATION; break;
        case TAG_R2: app->panel = PANEL_ALARM;     break;
        case TAG_R3: app->panel = PANEL_LOG;       break;
        case TAG_R4:
            add_log(app, LOG_CAT_LOGIN, "Logout");
            app->screen        = SCREEN_LOCK;
            app->role          = ROLE_NONE;
            app->user_name     = "";
            app->user_role_str = "";
            app->lock_cursor   = 0;
            break;
        }
        break;

    /* ─ 運転操作 ─ */
    case PANEL_OPERATION: {
        uint32_t rpm_max = app->settings_max_rpm;
        uint32_t rpm_min = app->settings_min_rpm;
        uint8_t  pat_cnt = feed_pattern_count[app->settings_feed_pattern];
        switch (tag) {
        case TAG_R1:
            app->machine = (app->machine == MACHINE_RUNNING)
                           ? MACHINE_STOPPED : MACHINE_RUNNING;
            if (app->machine == MACHINE_RUNNING) {
                add_log(app, LOG_CAT_OPERATION, "Machine started");
                show_toast(app, "Machine started", tick_ms);
            } else {
                add_log(app, LOG_CAT_OPERATION, "Machine stopped");
                show_toast(app, "Machine stopped", tick_ms);
            }
            break;
        case TAG_R2: case TAG_L2: /* RPM+ */
            if (app->set_rpm < rpm_max) {
                app->set_rpm = (app->set_rpm + 100 > rpm_max) ? rpm_max : app->set_rpm + 100;
                app->actual_rpm = app->set_rpm;
                { char buf[32]; snprintf(buf, sizeof(buf), "RPM set: %lu",
                  (unsigned long)app->set_rpm); add_log(app, LOG_CAT_OPERATION, buf); }
            }
            break;
        case TAG_R3: case TAG_L4: /* RPM- */
            if (app->set_rpm > rpm_min) {
                app->set_rpm = (app->set_rpm < rpm_min + 100) ? rpm_min : app->set_rpm - 100;
                app->actual_rpm = app->set_rpm;
                { char buf[32]; snprintf(buf, sizeof(buf), "RPM set: %lu",
                  (unsigned long)app->set_rpm); add_log(app, LOG_CAT_OPERATION, buf); }
            }
            break;
        case TAG_R4: /* FEED 切替 */
            app->feed_idx = (app->feed_idx + 1) % pat_cnt;
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "Feed rate: %d%%",
                         feed_patterns[app->settings_feed_pattern][app->feed_idx]);
                add_log(app, LOG_CAT_OPERATION, buf);
            }
            break;
        }
        break;
    }

    /* ─ アラーム ─ */
    case PANEL_ALARM: {
        /* DTL ボタン: 開閉トグル */
        if (tag == TAG_R1) {
            if (app->alarm_detail_open && !app->alarm_detail_closing) {
                app->alarm_detail_closing = true;
                app->alarm_detail_start   = tick_ms;
            } else if (!app->alarm_detail_open) {
                if (app->alarm_cursor < (int8_t)app->alarm_count)
                    app->alarms[app->alarm_cursor].viewed = true;
                app->alarm_detail_open    = true;
                app->alarm_detail_closing = false;
                app->alarm_detail_start   = tick_ms;
                add_log(app, LOG_CAT_ALARM, "Alarm detail viewed");
            }
            break;
        }
        /* 詳細オープン中は他操作を無視 */
        if (app->alarm_detail_open) break;

        switch (tag) {
        case TAG_L2: {  /* UP: クランプ（フィルタ考慮） */
            for (int i = app->alarm_cursor - 1; i >= 0; i--) {
                AlarmRecord_t *al = &app->alarms[i];
                if (app->alarm_filter == ALARM_FILTER_ERROR && al->level != ALARM_LVL_ERROR) continue;
                if (app->alarm_filter == ALARM_FILTER_WARN  && al->level != ALARM_LVL_WARN)  continue;
                if (app->alarm_filter == ALARM_FILTER_INFO  && al->level != ALARM_LVL_INFO)  continue;
                app->alarm_cursor = (int8_t)i; break;
            }
            break;
        }
        case TAG_L4: {  /* DOWN: クランプ */
            for (int i = app->alarm_cursor + 1; i < (int)app->alarm_count; i++) {
                AlarmRecord_t *al = &app->alarms[i];
                if (app->alarm_filter == ALARM_FILTER_ERROR && al->level != ALARM_LVL_ERROR) continue;
                if (app->alarm_filter == ALARM_FILTER_WARN  && al->level != ALARM_LVL_WARN)  continue;
                if (app->alarm_filter == ALARM_FILTER_INFO  && al->level != ALARM_LVL_INFO)  continue;
                app->alarm_cursor = (int8_t)i; break;
            }
            break;
        }
        case TAG_R2:  /* RST */
            if (app->role == ROLE_ADMIN && app->alarm_cursor < (int8_t)app->alarm_count) {
                app->alarms[app->alarm_cursor].active = false;
                add_log(app, LOG_CAT_ALARM, "Alarm reset");
                show_toast(app, "Alarm reset", tick_ms);
            }
            break;
        case TAG_R3: { /* FEED: フィルタ切替 + カーソル補正 */
            app->alarm_filter = (AlarmFilter_t)((app->alarm_filter + 1) % ALARM_FILTER_COUNT);
            /* 現在のカーソルが新フィルタに含まれるか確認 */
            bool cur_valid  = false;
            int  first_match = -1;
            for (int i = 0; i < (int)app->alarm_count; i++) {
                AlarmRecord_t *al = &app->alarms[i];
                bool pass = true;
                if (app->alarm_filter == ALARM_FILTER_ERROR && al->level != ALARM_LVL_ERROR) pass = false;
                if (app->alarm_filter == ALARM_FILTER_WARN  && al->level != ALARM_LVL_WARN)  pass = false;
                if (app->alarm_filter == ALARM_FILTER_INFO  && al->level != ALARM_LVL_INFO)  pass = false;
                if (pass) {
                    if (first_match < 0) first_match = i;
                    if (i == (int)app->alarm_cursor) { cur_valid = true; break; }
                }
            }
            if (!cur_valid && first_match >= 0) app->alarm_cursor = (int8_t)first_match;
            app->alarm_scroll = 0;
            break;
        }
        }
        break;
    }

    /* ─ ログ ─ */
    case PANEL_LOG: {
        /* カテゴリタブ（タグ 30-36） */
        if (tag >= TAG_LOG_TAB_BASE && tag < TAG_LOG_TAB_BASE + 7) {
            int ti = tag - TAG_LOG_TAB_BASE;
            bool is_adm = (ti >= 5);
            if (!is_adm || app->role == ROLE_ADMIN) {
                app->log_filter = (LogFilter_t)ti;
                app->log_scroll = 0;
            }
            break;
        }
        /* フィルタ適用後の件数を計算 */
        int fcount = 0;
        for (int i = 0; i < (int)app->log_count; i++) {
            if (app->log_filter == LOG_FILTER_ALL ||
                (int)app->log_entries[i].cat == (int)app->log_filter - 1) fcount++;
        }
        int16_t y0      = HDR_H + 12 + TITLE_OFFSET + 38 + 38; /* タブ行+ヘッダ行含む */
        int16_t visible = (int16_t)((SCR_H - 8 - y0) / LOG_ROW_H);
        switch (tag) {
        case TAG_L2: case TAG_R1: /* UP */
            if (app->log_scroll > 0) app->log_scroll--;
            break;
        case TAG_L4: case TAG_R2: /* DN */
            if ((int16_t)app->log_scroll + visible < fcount) app->log_scroll++;
            break;
        }
        break;
    }

    /* ─ 設定 ─ */
    case PANEL_SETTINGS: {
        if (!app->settings_editing) {
            switch (tag) {
            case TAG_L2:
                if (app->settings_cursor > 0) app->settings_cursor--;
                else app->settings_cursor = 3;
                break;
            case TAG_L4:
                if (app->settings_cursor < 3) app->settings_cursor++;
                else app->settings_cursor = 0;
                break;
            case TAG_L3:
                app->settings_editing = true;
                break;
            case TAG_R1:
                if (app->settings_changed) {
                    app->settings_changed = false;
                    add_log(app, LOG_CAT_SETTINGS, "Settings saved");
                    show_toast(app, "Settings saved", tick_ms);
                }
                break;
            }
        } else {
            /* 編集モード中: +/- で値変更 */
            bool changed = false;
            if (tag == TAG_R1) { /* + */
                switch (app->settings_cursor) {
                case 0:
                    if (app->settings_max_rpm < 3000)
                        app->settings_max_rpm += 100;
                    break;
                case 1:
                    if (app->settings_min_rpm + 100 < app->settings_max_rpm)
                        app->settings_min_rpm += 100;
                    break;
                case 2:
                    app->settings_feed_pattern = (app->settings_feed_pattern + 1) % 2;
                    {
                        uint8_t mx = feed_pattern_count[app->settings_feed_pattern] - 1;
                        if (app->feed_idx > mx) app->feed_idx = mx;
                    }
                    break;
                case 3:
                    if (app->settings_tool_no < 5) app->settings_tool_no++;
                    break;
                }
                changed = true;
            }
            if (tag == TAG_R2) { /* - */
                switch (app->settings_cursor) {
                case 0:
                    if (app->settings_max_rpm > app->settings_min_rpm + 100)
                        app->settings_max_rpm -= 100;
                    break;
                case 1:
                    if (app->settings_min_rpm >= 100)
                        app->settings_min_rpm -= 100;
                    break;
                case 2:
                    app->settings_feed_pattern = (app->settings_feed_pattern + 1) % 2;
                    {
                        uint8_t mx = feed_pattern_count[app->settings_feed_pattern] - 1;
                        if (app->feed_idx > mx) app->feed_idx = mx;
                    }
                    break;
                case 3:
                    if (app->settings_tool_no > 1) app->settings_tool_no--;
                    break;
                }
                changed = true;
            }
            if (changed) app->settings_changed = true;
            if (tag == TAG_R3 || tag == TAG_L3) {
                app->settings_editing = false;
                /* 範囲外の set_rpm をクランプ */
                if (app->set_rpm > app->settings_max_rpm)
                    app->set_rpm = app->actual_rpm = app->settings_max_rpm;
                if (app->set_rpm < app->settings_min_rpm)
                    app->set_rpm = app->actual_rpm = app->settings_min_rpm;
            }
        }
        break;
    }

    /* ─ ユーザー管理 ─ */
    case PANEL_USERS: {
        switch (tag) {
        case TAG_L2:
            if (app->user_cursor > 0) app->user_cursor--;
            else app->user_cursor = USER_COUNT - 1;
            break;
        case TAG_L4:
            if (app->user_cursor < USER_COUNT - 1) app->user_cursor++;
            else app->user_cursor = 0;
            break;
        case TAG_L3: { /* OK: トグル */
            UserRecord_t *u = &user_data[app->user_cursor];
            if (u->role[0] != 'A') { /* Admin以外のみ */
                u->active = !u->active;
                show_toast(app,
                           u->active ? "User -> Active" : "User -> Inactive",
                           tick_ms);
            }
            break;
        }
        case TAG_R1: { /* ACTV */
            UserRecord_t *u = &user_data[app->user_cursor];
            if (u->role[0] != 'A' && !u->active) {
                u->active = true;
                char buf[32]; snprintf(buf, sizeof(buf), "User %s activated", u->name);
                add_log(app, LOG_CAT_USER, buf);
                show_toast(app, "User -> Active", tick_ms);
            }
            break;
        }
        case TAG_R2: { /* INAC */
            UserRecord_t *u = &user_data[app->user_cursor];
            if (u->role[0] != 'A' && u->active) {
                u->active = false;
                char buf[32]; snprintf(buf, sizeof(buf), "User %s deactivated", u->name);
                add_log(app, LOG_CAT_USER, buf);
                show_toast(app, "User -> Inactive", tick_ms);
            }
            break;
        }
        }
        break;
    }

    default:
        break;
    }
}
