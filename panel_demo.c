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
#define MENU_VIS            10      /* メニュー可視行数（全10項目表示） */
#define ARC_SEGS            64      /* 円弧分割数（滑らかさのため増量） */

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

/* draw_arc: 円弧進捗グラフ（LINE_STRIP ARC_SEGS分割）
   -PI/2(上端)から時計回りに pct% 描画
   bg_color=背景リング、fg_color=進捗弧、thickness=線幅 */
static void draw_arc(EVE_HalContext *phost,
                     int16_t cx, int16_t cy, int16_t r,
                     uint32_t bg_color, uint32_t fg_color,
                     uint8_t thickness, uint8_t pct)
{
    float step  = 6.2832f / ARC_SEGS;
    float start = -1.5708f;  /* -PI/2 = 上端 */

    /* 背景リング（全周） */
    SET_COLOR(phost, bg_color);
    EVE_CoDl_lineWidth(phost, thickness * 16);
    EVE_CoDl_begin(phost, EVE_LINE_STRIP);
    for (int i = 0; i <= ARC_SEGS; i++) {
        float    a  = start + step * i;
        int16_t  px = (int16_t)(cx + r * __builtin_cosf(a));
        int16_t  py = (int16_t)(cy + r * __builtin_sinf(a));
        EVE_CoDl_vertex2f(phost, px * 16, py * 16);
    }
    EVE_CoDl_end(phost);

    /* 進捗弧（丸キャップ） */
    if (pct == 0) return;
    int segs = (int)(ARC_SEGS * pct / 100);
    if (segs < 1) segs = 1;
    if (segs > ARC_SEGS) segs = ARC_SEGS;

    SET_COLOR(phost, fg_color);
    EVE_CoDl_begin(phost, EVE_LINE_STRIP);
    for (int i = 0; i <= segs; i++) {
        float    a  = start + step * i;
        int16_t  px = (int16_t)(cx + r * __builtin_cosf(a));
        int16_t  py = (int16_t)(cy + r * __builtin_sinf(a));
        EVE_CoDl_vertex2f(phost, px * 16, py * 16);
    }
    EVE_CoDl_end(phost);
}

/* 疑似ボールドテキスト: x と x+1 に同じテキストを重ね描きして太く見せる
   ※ EVE ROM フォントに Bold 変体がないため、2重描画で代替 */
static void draw_text_bold(EVE_HalContext *phost,
                           int16_t x, int16_t y,
                           uint8_t font, uint16_t options,
                           const char *s)
{
    EVE_CoCmd_text(phost, x,     y, font, options, s);
    EVE_CoCmd_text(phost, x + 1, y, font, options, s);
}

/* 工具残寿命に応じた色 */
static uint32_t tool_life_color(uint8_t remaining_pct)
{
    if (remaining_pct < 10) return COL_RED;
    if (remaining_pct < 20) return COL_YELLOW;
    return COL_GREEN;
}

/* 秒 → MM:SS 文字列 */
static void fmt_mmss(char *buf, size_t bufsz, uint32_t secs)
{
    uint32_t m = secs / 60;
    uint32_t s = secs % 60;
    snprintf(buf, bufsz, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
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

    /* オペレーション選択・編集 */
    app->oper_cursor  = 0;
    app->oper_editing = false;

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
    app->rtc_h    = 10;
    app->rtc_m    = 30;
    app->rtc_s    = 0;
    app->rtc_day  = 14;
    app->rtc_mon  = 4;
    app->rtc_year = 2026;

    /* ワークプログラム W1/W2/W3 */
    app->work_programs[0].name      = "Bracket";
    app->work_programs[0].target    = 100;
    app->work_programs[0].completed = 87;
    app->work_programs[1].name      = "Shaft";
    app->work_programs[1].target    = 60;
    app->work_programs[1].completed = 55;
    app->work_programs[2].name      = "Cover";
    app->work_programs[2].target    = 40;
    app->work_programs[2].completed = 35;
    app->active_program  = 0;
    app->batch_target    = 200;

    /* 加工時間 (1ワーク=45分、経過=30分38秒) */
    app->piece_time_est_s  = 2700;
    app->piece_time_elap_s = 1838;
    app->batch_time_est_s  = 27000;
    app->batch_time_elap_s = 16542;

    /* 工具寿命 */
    static const struct { const char *num; const char *name; uint32_t usage; uint32_t life; }
    tool_init[] = {
        { "T01", "End Mill 6mm",  4820, 8000 },
        { "T02", "Ball End 4mm",   980, 2000 },
        { "T03", "Drill 8mm",     1900, 2000 },
        { "T04", "Reamer 10mm",    200, 2000 },
        { "T05", "Tap M6",         500, 1000 },
        { "T06", "Face Mill",      120, 5000 },
        { "T07", "Center Drill",  2100, 3000 },
        { "T08", "Grooving",       800, 1000 },
    };
    app->tool_count = (uint8_t)(sizeof(tool_init) / sizeof(tool_init[0]));
    for (int i = 0; i < (int)app->tool_count; i++) {
        strncpy(app->tools[i].number, tool_init[i].num,  3); app->tools[i].number[3] = '\0';
        strncpy(app->tools[i].name,   tool_init[i].name, 19); app->tools[i].name[19] = '\0';
        app->tools[i].usage_count = tool_init[i].usage;
        app->tools[i].max_life    = tool_init[i].life;
    }
    app->tool_cursor = 0;
    app->tool_scroll = 0;

    /* メンテナンス */
    static const struct { const char *name; uint16_t intv; const char *last; int16_t rem; }
    maint_init[] = {
        { "Spindle Oil",      90, "2026-01-05",  -9  },
        { "Coolant Filter",   30, "2026-03-25",  10  },
        { "Way Lubrication",   7, "2026-04-07",   0  },
        { "Air Filter",       90, "2026-01-14",   0  },
        { "Spindle Grease",  180, "2025-10-15",  -1  },
        { "Belt Tension",    365, "2025-04-14",   0  },
    };
    app->maint_count = (uint8_t)(sizeof(maint_init) / sizeof(maint_init[0]));
    for (int i = 0; i < (int)app->maint_count; i++) {
        app->maint_items[i].name           = maint_init[i].name;
        app->maint_items[i].interval_days  = maint_init[i].intv;
        strncpy(app->maint_items[i].last_done, maint_init[i].last, 11);
        app->maint_items[i].last_done[11]  = '\0';
        app->maint_items[i].days_remaining = maint_init[i].rem;
    }
    app->maint_cursor = 0;
    app->maint_scroll = 0;

    /* 生産サマリー */
    app->prod_period      = PROD_PERIOD_DAILY;
    app->prod_total_parts = 247;
    app->prod_op_rate_pct = 91;
    app->prod_daily[0] = 87;
    app->prod_daily[1] = 91;
    app->prod_daily[2] = 88;
    app->prod_daily[3] = 95;
    app->prod_daily[4] = 87;
    app->prod_daily[5] = 43;
    app->prod_daily[6] = 11;

    /* 座標・NCプログラム */
    app->coord_x           = -125340;  /* -125.340 mm */
    app->coord_y           =   48720;  /*  +48.720 mm */
    app->coord_z           =  -52100;  /* -52.100 mm */
    app->nc_program_no     = 123;
    app->nc_block_no       = 45;
    strncpy(app->nc_block_text, "G01 X-125.340 Y+48.720 F200", 47);
    strncpy(app->machine_mode, "AUTO", 7);
    app->feed_override_pct = 100;
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
        if (app->rtc_h >= 24) {
            app->rtc_h = 0;
            app->rtc_day++;
            static const uint8_t days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
            uint8_t dim = days_in_month[app->rtc_mon - 1];
            if (app->rtc_mon == 2) {
                uint16_t y = app->rtc_year;
                if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) dim = 29;
            }
            if (app->rtc_day > dim) { app->rtc_day = 1; app->rtc_mon++; }
            if (app->rtc_mon > 12)  { app->rtc_mon = 1; app->rtc_year++; }
        }
        if (app->machine == MACHINE_RUNNING) {
            app->op_seconds++;
            /* 加工時間インクリメント */
            app->piece_time_elap_s++;
            if (app->piece_time_elap_s >= app->piece_time_est_s) {
                app->piece_time_elap_s = 0; /* 次ワーク開始 */
                if (app->work_programs[app->active_program].completed <
                    app->work_programs[app->active_program].target)
                    app->work_programs[app->active_program].completed++;
                if (app->parts_today < 9999) app->parts_today++;
                if (app->parts_total < 999999) app->parts_total++;
            }
            app->batch_time_elap_s++;
        }
    }

    if (app->screen == SCREEN_AUTH) {
        if (tick_ms - app->auth_tick_start >= app->auth_duration_ms) {
            app->role          = app->auth_pending;
            /* Usersパネルの変更を反映: ロールに合う最初のアクティブユーザーを検索 */
            {
                char target = (app->role == ROLE_ADMIN) ? 'A' : 'O';
                const char *found = (app->role == ROLE_ADMIN) ? "Y.Yamada" : "I.Tanaka";
                for (int _i = 0; _i < USER_COUNT; _i++) {
                    if (user_data[_i].role[0] == target && user_data[_i].active) {
                        found = user_data[_i].name; break;
                    }
                }
                app->user_name = found;
            }
            app->user_role_str = (app->role == ROLE_ADMIN) ? "Admin" : "Operator";
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

    static const uint16_t delays[3] = { 0, 800, 1600 };
    static const uint16_t sizes[3]  = { 40, 62, 84 };

    for (int i = 0; i < 3; i++) {
        uint32_t phase = (t + 2400 - delays[i]) % 2400;
        uint8_t  alpha = (phase < 1800) ? (uint8_t)((1800 - phase) * 140 / 1800) : 0;
        if (alpha == 0) continue;

        EVE_CoDl_colorA(phost, alpha);
        SET_COLOR(phost, COL_ACCENT);
        EVE_CoDl_lineWidth(phost, 2 * 16);
        int16_t sz = (int16_t)sizes[i];
        EVE_CoDl_begin(phost, EVE_LINE_STRIP);
        EVE_CoDl_vertex2f(phost, (cx - sz) * 16, (cy - sz) * 16);
        EVE_CoDl_vertex2f(phost, (cx + sz) * 16, (cy - sz) * 16);
        EVE_CoDl_vertex2f(phost, (cx + sz) * 16, (cy + sz) * 16);
        EVE_CoDl_vertex2f(phost, (cx - sz) * 16, (cy + sz) * 16);
        EVE_CoDl_vertex2f(phost, (cx - sz) * 16, (cy - sz) * 16);
        EVE_CoDl_end(phost);
    }
    EVE_CoDl_colorA(phost, 255);

    draw_rect(phost, cx - 26, cy - 26, 52, 52, COL_CARD);
    SET_COLOR(phost, COL_ACCENT);
    EVE_CoCmd_text(phost, cx, cy, FONT_LG, EVE_OPT_CENTER, "ID");

    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, 60, FONT_SM, EVE_OPT_CENTER, "NEXUS CONTROL SYSTEMS");

    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, cy + 90, FONT_MD, EVE_OPT_CENTER,
                   "Present RFID card or select role");

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             app->rtc_h, app->rtc_m, app->rtc_s);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx, SCR_H - 60, FONT_XL, EVE_OPT_CENTER, time_str);
    char date_str[12];
    snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d",
             app->rtc_year, app->rtc_mon, app->rtc_day);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, SCR_H - 24, FONT_SM, EVE_OPT_CENTER, date_str);

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
    (void)app;
    EVE_CoDl_colorA(phost, 180);
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
    EVE_CoDl_colorA(phost, 255);

    int16_t cx = CONT_X + CONT_W / 2;
    int16_t cy = SCR_H / 2;
    draw_card(phost, cx - 150, cy - 90, 300, 180);

    /* 3点滅ブロック：350ms ごとに 1個ずつ点灯 */
    int step = (int)((tick_ms / 350) % 3);
    static const int16_t dxs[3] = { -26, 0, 26 };
    for (int i = 0; i < 3; i++) {
        draw_rect(phost, cx + dxs[i] - 10, cy - 38, 20, 20,
                  (i == step) ? COL_ACCENT : COL_CARD2);
    }

    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx, cy + 16, FONT_MD, EVE_OPT_CENTER, "Authenticating...");
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
        SET_COLOR(phost, COL_TXT2);
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
   ホームパネル（3行ダッシュボード）
═══════════════════════════════════════════════════════════ */

static void render_home(EVE_HalContext *phost, AppState_t *app)
{
    int16_t cx = CONT_X;
    int16_t ix = cx + 8;       /* 内側 X 開始 */
    int16_t iw = CONT_W - 16;  /* 内側幅 = 604 */

    /* 行1: y=84, h=100 */
    int16_t y1 = HDR_H + 4;
    int16_t h1 = 100;
    /* 行2: y=188, h=170 */
    int16_t y2 = y1 + h1 + 4;
    int16_t h2 = 170;
    /* 行3: y=362, h=114 */
    int16_t y3 = y2 + h2 + 4;
    int16_t h3 = SCR_H - y3 - 4;

    /* ── 行1 カード A/B/C ── */
    int16_t wA = 116, wB = 176;
    int16_t xA = ix;
    int16_t xB = xA + wA + 4;
    int16_t xC = xB + wB + 4;
    int16_t wC = ix + iw - xC;  /* ≈336 */

    /* Card A: 機械状態 */
    draw_card(phost, xA, y1, wA, h1);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, xA + 8, y1 + 8, FONT_SM, 0, "MACHINE");
    {
        const char *st = (app->machine == MACHINE_RUNNING) ? "RUN" : "STP";
        uint32_t    sc = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        SET_COLOR(phost, sc);
        draw_text_bold(phost, xA + wA / 2, y1 + 56, FONT_XL, EVE_OPT_CENTER, st);
    }

    /* Card B: スピンドル RPM */
    draw_card(phost, xB, y1, wB, h1);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, xB + 8, y1 + 8, FONT_SM, 0, "SPINDLE RPM");
    {
        char buf[10];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT);
        draw_text_bold(phost, xB + wB / 2, y1 + 54, FONT_XL, EVE_OPT_CENTER, buf);
        char buf2[16];
        snprintf(buf2, sizeof(buf2), "Set:%lu", (unsigned long)app->set_rpm);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, xB + wB / 2, y1 + 90, FONT_SM, EVE_OPT_CENTER, buf2);
    }

    /* Card C: バッチ進捗 弧グラフ */
    draw_card(phost, xC, y1, wC, h1);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, xC + 8, y1 + 8, FONT_SM, 0, "BATCH");
    {
        uint32_t total_comp = 0, total_tgt = 0;
        for (int i = 0; i < WORK_PROG_COUNT; i++) {
            total_comp += app->work_programs[i].completed;
            total_tgt  += app->work_programs[i].target;
        }
        uint8_t pct = (total_tgt > 0) ? (uint8_t)(total_comp * 100 / total_tgt) : 0;
        int16_t arc_cx = xC + 100;
        int16_t arc_cy = y1 + h1 / 2 + 4;
        draw_arc(phost, arc_cx, arc_cy, 36, COL_BG, COL_GREEN, 6, pct);
        char pbuf[8];
        snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, arc_cx, arc_cy, FONT_SM, EVE_OPT_CENTER, pbuf);
        /* 右側: カウント */
        int16_t rx = arc_cx + 46;
        char countbuf[16];
        snprintf(countbuf, sizeof(countbuf), "%lu/%lu",
                 (unsigned long)total_comp, (unsigned long)total_tgt);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, rx, arc_cy - 10, FONT_MD, 0, countbuf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, rx, arc_cy + 14, FONT_SM, 0, "parts");
    }

    /* ── 行2 カード D/E ── */
    int16_t wD = 296;
    int16_t xD = ix;
    int16_t xE = xD + wD + 4;
    int16_t wE = ix + iw - xE;  /* ≈308 */

    /* Card D: 加工時間 弧グラフ */
    draw_card(phost, xD, y2, wD, h2);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, xD + 8, y2 + 8, FONT_SM, 0, "WORKPIECE TIME");
    {
        uint8_t pct = (app->piece_time_est_s > 0)
                    ? (uint8_t)(app->piece_time_elap_s * 100 / app->piece_time_est_s)
                    : 0;
        int16_t arc_cx = xD + 110;
        int16_t arc_cy = y2 + 92;
        draw_arc(phost, arc_cx, arc_cy, 58, COL_BG, COL_ACCENT, 8, pct);
        char pbuf[8];
        snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, arc_cx, arc_cy, FONT_LG, EVE_OPT_CENTER, pbuf);
        /* 右側: Est/Elap */
        int16_t tx = arc_cx + 68;
        char est_buf[10], elap_buf[10];
        fmt_mmss(est_buf,  sizeof(est_buf),  app->piece_time_est_s);
        fmt_mmss(elap_buf, sizeof(elap_buf), app->piece_time_elap_s);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, tx, arc_cy - 32, FONT_SM, 0, "Est:");
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, tx, arc_cy - 12, FONT_MD, 0, est_buf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, tx, arc_cy + 18, FONT_SM, 0, "Elap:");
        SET_COLOR(phost, COL_ACCENT);
        EVE_CoCmd_text(phost, tx, arc_cy + 38, FONT_MD, 0, elap_buf);
    }

    /* Card E: W1/W2/W3 横棒グラフ */
    draw_card(phost, xE, y2, wE, h2);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, xE + 8, y2 + 8, FONT_SM, 0, "PROGRAMS");
    {
        static const uint32_t prog_cols[3] = { COL_ACCENT, COL_TEAL, COL_GREEN };
        for (int i = 0; i < WORK_PROG_COUNT; i++) {
            WorkProgram_t *wp = &app->work_programs[i];
            int16_t ry   = y2 + 32 + i * 46;
            uint8_t pct  = (wp->target > 0)
                         ? (uint8_t)(wp->completed * 100 / wp->target)
                         : 0;
            SET_COLOR(phost, COL_TXT);
            EVE_CoCmd_text(phost, xE + 8, ry + 8, FONT_SM, 0, wp->name);
            draw_bar(phost, xE + 8, ry + 22, wE - 70, 12, pct, prog_cols[i]);
            char cbuf[12];
            snprintf(cbuf, sizeof(cbuf), "%d/%d", wp->completed, wp->target);
            SET_COLOR(phost, COL_TXT2);
            EVE_CoCmd_text(phost, xE + wE - 6, ry + 28,
                           FONT_SM, EVE_OPT_RIGHTX | EVE_OPT_CENTERY, cbuf);
        }
    }

    /* ── 行3: 直近アラーム ── */
    draw_card(phost, ix, y3, iw, h3);
    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, ix + 8, y3 + 8, FONT_SM, 0, "RECENT ALARMS");

    int shown = 0;
    for (int i = 0; i < (int)app->alarm_count && shown < 3; i++) {
        AlarmRecord_t *al = &app->alarms[i];
        if (!al->active) continue;
        uint32_t lc = (al->level == ALARM_LVL_ERROR) ? COL_RED :
                      (al->level == ALARM_LVL_WARN)  ? COL_YELLOW : COL_ACCENT;
        int16_t ay = y3 + 44 + shown * 24;
        SET_COLOR(phost, lc);
        EVE_CoCmd_text(phost, ix + 8,  ay, FONT_SM, 0, ">");
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, ix + 22, ay, FONT_SM, 0, al->title);
        char time_disp[14] = "";
        if (al->time_str && strlen(al->time_str) >= 16) {
            snprintf(time_disp, sizeof(time_disp), "%c%c/%c%c %c%c:%c%c",
                     al->time_str[5],  al->time_str[6],
                     al->time_str[8],  al->time_str[9],
                     al->time_str[11], al->time_str[12],
                     al->time_str[14], al->time_str[15]);
        }
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, CONT_X + CONT_W - 24, ay, FONT_SM, EVE_OPT_RIGHTX, time_disp);
        shown++;
    }

    /* 右ボタン: R1=OPER, R2=COORD, R3=TOOL, R4=EXIT */
    RB(TAG_R1, BTN_Y0, "OPER",  COL_CARD, true);
    RB(TAG_R2, BTN_Y1, "COORD", COL_CARD, true);
    RB(TAG_R3, BTN_Y2, "TOOL",  COL_CARD, true);
    RB(TAG_R4, BTN_Y3, "EXIT",  COL_RED,  true);
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

    /* パラメータ選択領域（lh=324 を4等分: h0=72, h1=92, h2=80, h3=80） */
    typedef struct { int16_t y0; int16_t h; } SelRegion_t;
    const SelRegion_t SEL_REGIONS[3] = {
        { (int16_t)(y +   0), 72  },  /* 0: Machine Control */
        { (int16_t)(y +  72), 92  },  /* 1: RPM */
        { (int16_t)(y + 164), 80  },  /* 2: Feed Rate */
    };
    const SelRegion_t *selReg = &SEL_REGIONS[app->oper_cursor];
    bool isEd = app->oper_editing;

    /* 選択ハイライト（左アクセントバーのみ、枠線なし） */
    draw_rect(phost, cx + 8, selReg->y0, lw, selReg->h,
              isEd ? 0x0D2E50UL : 0x1A3A5CUL);
    /* 左アクセントバー */
    draw_rect(phost, cx + 8, selReg->y0, 6, selReg->h, COL_ACCENT);
    /* 右端 ▶ */
    SET_COLOR(phost, COL_ACCENT);
    EVE_CoCmd_text(phost, cx + 8 + lw - 18,
                   selReg->y0 + selReg->h / 2,
                   FONT_SM, EVE_OPT_CENTERY | EVE_OPT_RIGHTX, ">");

    /* セクション区切り線 */
    draw_hline(phost, cx + 16, y +  72, lw - 24, COL_BORDER, 1);
    draw_hline(phost, cx + 16, y + 164, lw - 24, COL_BORDER, 1);
    draw_hline(phost, cx + 16, y + 244, lw - 24, COL_BORDER, 1);

    /* Region 0: 運転状態 (h=72) */
    {
        const char *st = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t    sc = (app->machine == MACHINE_RUNNING) ? COL_GREEN : COL_RED;
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 10, FONT_SM, 0, "MACHINE CONTROL");
        SET_COLOR(phost, sc);
        draw_text_bold(phost, cx + 16, y + 40, FONT_LG, EVE_OPT_CENTERY, st);
    }

    /* Region 1: RPM 表示・バー (starts y+72, h=92) */
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
        EVE_CoCmd_text(phost, cx + 16, y + 80, FONT_MD, 0, buf);

        snprintf(buf, sizeof(buf), "ACT: %lu RPM", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 102, FONT_SM, 0, buf);

        draw_bar(phost, cx + 16, y + 124, lw - 32, 10, pct, bar_col);

        char mn_buf[8], md_buf[8], mx_buf[8];
        snprintf(mn_buf, sizeof(mn_buf), "%lu", (unsigned long)rpm_min);
        snprintf(md_buf, sizeof(md_buf), "%lu", (unsigned long)((rpm_min + rpm_max) / 2));
        snprintf(mx_buf, sizeof(mx_buf), "%lu", (unsigned long)rpm_max);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16,                  y + 140, FONT_SM, 0,                mn_buf);
        EVE_CoCmd_text(phost, cx + 16 + (lw - 32) / 2, y + 140, FONT_SM, EVE_OPT_CENTERX,  md_buf);
        EVE_CoCmd_text(phost, cx + lw - 16,             y + 140, FONT_SM, EVE_OPT_RIGHTX,   mx_buf);
    }

    /* Region 2: フィードレートタブ (starts y+164, h=80) */
    {
        uint8_t pat_cnt = feed_pattern_count[app->settings_feed_pattern];
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 174, FONT_SM, 0, "FEED RATE");
        for (int i = 0; i < pat_cnt; i++) {
            uint8_t val = feed_patterns[app->settings_feed_pattern][i];
            int16_t fx  = cx + 16 + i * 62;
            bool    sel = (app->feed_idx == i);
            bool    is100 = (val == 100);
            uint32_t bg = sel ? (is100 ? COL_GREEN : COL_ACCENT) : COL_CARD2;
            draw_rect(phost, fx, y + 192, 56, 28, bg);
            char lbl[8]; snprintf(lbl, sizeof(lbl), "%d%%", val);
            SET_COLOR(phost, sel ? COL_WHITE : COL_TXT2);
            EVE_CoCmd_text(phost, fx + 28, y + 206, FONT_SM, EVE_OPT_CENTER, lbl);
        }
    }

    /* Spindle Load (starts y+244, h=80) */
    {
        char buf[12];
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 252, FONT_SM, 0, "SPINDLE LOAD");
        draw_bar(phost, cx + 16, y + 270, lw - 32, 14, app->spindle_load, COL_TEAL);
        snprintf(buf, sizeof(buf), "%d%%", app->spindle_load);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16 + (lw - 32) / 2, y + 294,
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

    /* ヒントテキスト */
    SET_COLOR(phost, COL_TXT2);
    if (isEd) {
        EVE_CoCmd_text(phost, cx + 16, y + lh - 14, FONT_SM, 0, "OK -> Confirm");
    } else {
        EVE_CoCmd_text(phost, cx + 16, y + lh - 14, FONT_SM, 0,
                       "UP/DN=Select  OK=Edit");
    }

    /* 右ボタン：編集中のみ +/- */
    if (isEd) {
        RB_XL(TAG_R1, BTN_Y0, "+", COL_ACCENT, true);
        RB_XL(TAG_R2, BTN_Y1, "-", COL_CARD,   true);
        RB   (TAG_R3, BTN_Y2, "",  COL_SURF,   false);
        RB   (TAG_R4, BTN_Y3, "",  COL_SURF,   false);
    } else {
        /* 編集モード前でもグレー表示で +/- を見せる */
        RB_XL(TAG_R1, BTN_Y0, "+", COL_SURF, false);
        RB_XL(TAG_R2, BTN_Y1, "-", COL_SURF, false);
        RB   (TAG_R3, BTN_Y2, "",  COL_SURF, false);
        RB   (TAG_R4, BTN_Y3, "",  COL_SURF, false);
    }
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
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, CONT_X + CONT_W / 2, y + 50,
                       FONT_MD, EVE_OPT_CENTERX, "No alarms");
    }

    bool can_reset = (app->role == ROLE_ADMIN);
    RB(TAG_R1, BTN_Y0, "DTL",  COL_CARD,                              true);
    RB(TAG_R2, BTN_Y1, "RST",  can_reset ? COL_YELLOW : COL_SURF, can_reset);
    RB(TAG_R3, BTN_Y2, "PREV", COL_CARD,                              true);
    RB(TAG_R4, BTN_Y3, "NEXT", COL_CARD,                              true);
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

    /* 7カテゴリタブ (TAG_LOG_TAB_BASE = 40) */
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
    SET_COLOR(phost, COL_TXT2);
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
        SET_COLOR(phost, COL_TXT2);
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

        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16,  ly + 8, FONT_SM, 0, le->time);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 130, ly + 8, FONT_SM, 0, le->user);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 260, ly + 8, FONT_SM, 0, le->event);
        shown++;
    }

    RB(TAG_R1, BTN_Y0, "FEED", COL_CARD, true);
    RB(TAG_R2, BTN_Y1, "",     COL_SURF, false);
    RB(TAG_R3, BTN_Y2, "",     COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "",     COL_SURF, false);
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
        SET_COLOR(phost, COL_TXT2);
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
    SET_COLOR(phost, COL_TXT2);
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
   工具寿命管理パネル
═══════════════════════════════════════════════════════════ */

static void render_tool_life(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Tool Life");
    if (app->role == ROLE_ADMIN) {
        SET_COLOR(phost, COL_YELLOW);
        EVE_CoCmd_text(phost, cx + CONT_W - 8, y + 8, FONT_SM, EVE_OPT_RIGHTX, "[RST=ADMIN]");
    }
    y += TITLE_OFFSET;

    /* テーブルヘッダ */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 26, COL_SURF);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx + 20,  y + 4, FONT_SM, 0, "No");
    EVE_CoCmd_text(phost, cx + 52,  y + 4, FONT_SM, 0, "Tool Name");
    EVE_CoCmd_text(phost, cx + 290, y + 4, FONT_SM, 0, "Usage/Max");
    EVE_CoCmd_text(phost, cx + 430, y + 4, FONT_SM, 0, "Life Bar");
    EVE_CoCmd_text(phost, cx + 574, y + 4, FONT_SM, 0, "Rem%");
    y += 30;

    int16_t visible = (int16_t)((SCR_H - y - 8) / TOOL_ROW_H);

    for (int i = 0; i < (int)app->tool_count; i++) {
        int16_t row = (int16_t)(i - (int)app->tool_scroll);
        if (row < 0 || row >= visible) continue;

        ToolRecord_t *t   = &app->tools[i];
        int16_t       ry  = y + row * TOOL_ROW_H;
        bool          sel = (app->tool_cursor == (uint8_t)i);

        /* 行背景 */
        draw_rect(phost, cx + 8, ry, CONT_W - 16, TOOL_ROW_H - 2,
                  sel ? 0x0E2A45UL : (i % 2 == 0 ? COL_CARD : COL_BG));
        /* 左サイドバー */
        draw_rect(phost, cx + 8, ry, 4, TOOL_ROW_H - 2,
                  sel ? COL_ACCENT : COL_BORDER);

        uint32_t used   = (t->usage_count < t->max_life) ? t->usage_count : t->max_life;
        uint8_t  rem_pct = (t->max_life > 0)
                         ? (uint8_t)((t->max_life - used) * 100 / t->max_life)
                         : 0;
        uint32_t lcol   = tool_life_color(rem_pct);

        int16_t cy2 = ry + TOOL_ROW_H / 2;
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 20, cy2, FONT_SM, EVE_OPT_CENTERY, t->number);
        SET_COLOR(phost, sel ? COL_WHITE : COL_TXT);
        EVE_CoCmd_text(phost, cx + 52, cy2, FONT_SM, EVE_OPT_CENTERY, t->name);

        char ubuf[16];
        snprintf(ubuf, sizeof(ubuf), "%lu/%lu",
                 (unsigned long)t->usage_count, (unsigned long)t->max_life);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 290, cy2, FONT_SM, EVE_OPT_CENTERY, ubuf);

        /* 寿命バー */
        draw_bar(phost, cx + 430, cy2 - 5, 130, 10, rem_pct, lcol);

        /* 残% */
        char rbuf[8];
        if (rem_pct < 20)
            snprintf(rbuf, sizeof(rbuf), "%d%%!!", rem_pct);
        else
            snprintf(rbuf, sizeof(rbuf), "%d%%", rem_pct);
        SET_COLOR(phost, lcol);
        EVE_CoCmd_text(phost, cx + CONT_W - 14, cy2,
                       FONT_SM, EVE_OPT_RIGHTX | EVE_OPT_CENTERY, rbuf);
    }

    bool can_rst = (app->role == ROLE_ADMIN);

    RB(TAG_R1, BTN_Y0, "RST", can_rst ? COL_YELLOW : COL_SURF, can_rst);
    RB(TAG_R2, BTN_Y1, "",    COL_SURF, false);
    RB(TAG_R3, BTN_Y2, "",    COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "",    COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   座標・NCプログラムパネル
═══════════════════════════════════════════════════════════ */

static void render_coordinate(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 8;
    int16_t cx = CONT_X;

    /* プログラム情報カード */
    draw_card(phost, cx + 8, y, CONT_W - 16, 44);
    {
        char pbuf[8];  snprintf(pbuf, sizeof(pbuf), "O%04d", app->nc_program_no);
        char nbuf[8];  snprintf(nbuf, sizeof(nbuf), "N%04d", app->nc_block_no);
        char fbuf[8];  snprintf(fbuf, sizeof(fbuf), "%d%%",  app->feed_override_pct);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16,  y + 22, FONT_SM, EVE_OPT_CENTERY, "PROG:");
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 68,  y + 22, FONT_MD, EVE_OPT_CENTERY, pbuf);
        EVE_CoCmd_text(phost, cx + 148, y + 22, FONT_MD, EVE_OPT_CENTERY, nbuf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 248, y + 22, FONT_SM, EVE_OPT_CENTERY, "Mode:");
        SET_COLOR(phost, COL_TEAL);
        EVE_CoCmd_text(phost, cx + 298, y + 22, FONT_MD, EVE_OPT_CENTERY, app->machine_mode);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 390, y + 22, FONT_SM, EVE_OPT_CENTERY, "Feed:");
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 438, y + 22, FONT_MD, EVE_OPT_CENTERY, fbuf);
    }
    y += 52;

    /* XYZ 座標カード */
    draw_card(phost, cx + 8, y, CONT_W - 16, 194);
    {
        static const char *axis_names[3] = { "X", "Y", "Z" };
        int32_t coords[3] = { app->coord_x, app->coord_y, app->coord_z };
        for (int i = 0; i < 3; i++) {
            int16_t cy2 = y + 30 + i * 56;
            /* 軸ラベル */
            SET_COLOR(phost, COL_TXT2);
            EVE_CoCmd_text(phost, cx + 22, cy2, FONT_LG, EVE_OPT_CENTERY, axis_names[i]);
            /* 値フォーマット: ±XXX.XXX */
            int32_t  v        = coords[i];
            char     sign     = (v < 0) ? '-' : '+';
            uint32_t av       = (uint32_t)((v < 0) ? -v : v);
            uint32_t int_part = av / 1000;
            uint32_t dec_part = av % 1000;
            char vbuf[16];
            snprintf(vbuf, sizeof(vbuf), "%c%03lu.%03lu",
                     sign, (unsigned long)int_part, (unsigned long)dec_part);
            uint32_t vcol = (v < 0) ? COL_ORANGE : (v == 0 ? COL_TXT2 : COL_TXT);
            SET_COLOR(phost, vcol);
            draw_text_bold(phost, cx + CONT_W - 24, cy2,
                           FONT_XL, EVE_OPT_RIGHTX | EVE_OPT_CENTERY, vbuf);
            if (i < 2)
                draw_hline(phost, cx + 16, cy2 + 26, CONT_W - 32, COL_BORDER, 1);
        }
    }
    y += 202;

    /* NC ブロックカード */
    draw_card(phost, cx + 8, y, CONT_W - 16, 44);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx + 16, y + 22, FONT_SM, EVE_OPT_CENTERY, "BLOCK:");
    SET_COLOR(phost, COL_TEAL);
    EVE_CoCmd_text(phost, cx + 74, y + 22, FONT_SM, EVE_OPT_CENTERY, app->nc_block_text);
    y += 52;

    /* ステータスバー */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 38, COL_SURF);
    {
        const char *run_str = (app->machine == MACHINE_RUNNING) ? "RUNNING" : "STOPPED";
        uint32_t    run_col = (app->machine == MACHINE_RUNNING) ? COL_GREEN  : COL_RED;
        SET_COLOR(phost, run_col);
        EVE_CoCmd_text(phost, cx + 18,  y + 19, FONT_SM, EVE_OPT_CENTERY, run_str);
        char rbuf[12];
        snprintf(rbuf, sizeof(rbuf), "%lu RPM", (unsigned long)app->actual_rpm);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 140, y + 19, FONT_SM, EVE_OPT_CENTERY, rbuf);
        char lbuf[12];
        snprintf(lbuf, sizeof(lbuf), "Load:%d%%", app->spindle_load);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 280, y + 19, FONT_SM, EVE_OPT_CENTERY, lbuf);
        char tbuf[10];
        snprintf(tbuf, sizeof(tbuf), "Tmp:%dC", app->spindle_temp);
        SET_COLOR(phost, app->spindle_temp > 70 ? COL_YELLOW : COL_TXT2);
        EVE_CoCmd_text(phost, cx + 400, y + 19, FONT_SM, EVE_OPT_CENTERY, tbuf);
    }

    RB(TAG_R1, BTN_Y0, "", COL_SURF, false);
    RB(TAG_R2, BTN_Y1, "", COL_SURF, false);
    RB(TAG_R3, BTN_Y2, "", COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "", COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   メンテナンス管理パネル
═══════════════════════════════════════════════════════════ */

static void render_maintenance(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Maintenance");
    if (app->role == ROLE_ADMIN) {
        SET_COLOR(phost, COL_YELLOW);
        EVE_CoCmd_text(phost, cx + CONT_W - 8, y + 8, FONT_SM, EVE_OPT_RIGHTX, "[ADMIN ONLY]");
    }
    y += TITLE_OFFSET;

    /* テーブルヘッダ */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 26, COL_SURF);
    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, cx + 16,  y + 4, FONT_SM, 0, "Item");
    EVE_CoCmd_text(phost, cx + 220, y + 4, FONT_SM, 0, "Intv");
    EVE_CoCmd_text(phost, cx + 298, y + 4, FONT_SM, 0, "Last Done");
    EVE_CoCmd_text(phost, cx + 430, y + 4, FONT_SM, 0, "Status");
    y += 30;

    int16_t visible = (int16_t)((SCR_H - y - 8) / MAINT_ROW_H);

    for (int i = 0; i < (int)app->maint_count; i++) {
        int16_t row = (int16_t)(i - (int)app->maint_scroll);
        if (row < 0 || row >= visible) continue;

        MaintRecord_t *m   = &app->maint_items[i];
        int16_t        ry  = y + row * MAINT_ROW_H;
        bool           sel = (app->maint_cursor == (uint8_t)i);

        draw_rect(phost, cx + 8, ry, CONT_W - 16, MAINT_ROW_H - 2,
                  sel ? 0x0E2A45UL : (i % 2 == 0 ? COL_CARD : COL_BG));
        draw_rect(phost, cx + 8, ry, 4, MAINT_ROW_H - 2,
                  sel ? COL_ACCENT : COL_BORDER);

        /* 状態色・文字列 */
        uint32_t   st_col;
        char       st_buf[12];
        const char *st_str;
        if (m->days_remaining < 0) {
            st_col = COL_RED;    st_str = "OVERDUE";
        } else if (m->days_remaining == 0) {
            st_col = COL_ORANGE; st_str = "TODAY";
        } else if (m->days_remaining <= 7) {
            st_col = COL_YELLOW;
            snprintf(st_buf, sizeof(st_buf), "%dd", m->days_remaining);
            st_str = st_buf;
        } else {
            st_col = COL_GREEN;
            snprintf(st_buf, sizeof(st_buf), "%dd", m->days_remaining);
            st_str = st_buf;
        }

        int16_t cy2 = ry + MAINT_ROW_H / 2;
        SET_COLOR(phost, sel ? COL_WHITE : COL_TXT);
        EVE_CoCmd_text(phost, cx + 16,  cy2, FONT_SM, EVE_OPT_CENTERY, m->name);
        char ibuf[8];
        snprintf(ibuf, sizeof(ibuf), "%dd", m->interval_days);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 220, cy2, FONT_SM, EVE_OPT_CENTERY, ibuf);
        EVE_CoCmd_text(phost, cx + 298, cy2, FONT_SM, EVE_OPT_CENTERY, m->last_done);
        SET_COLOR(phost, st_col);
        EVE_CoCmd_text(phost, cx + 430, cy2, FONT_SM, EVE_OPT_CENTERY, st_str);
    }

    bool can_done = (app->role == ROLE_ADMIN);

    RB(TAG_R1, BTN_Y0, "DONE", can_done ? COL_GREEN : COL_SURF, can_done);
    RB(TAG_R2, BTN_Y1, "",     COL_SURF, false);
    RB(TAG_R3, BTN_Y2, "",     COL_SURF, false);
    RB(TAG_R4, BTN_Y3, "",     COL_SURF, false);
}

/* ═══════════════════════════════════════════════════════════
   生産実績サマリーパネル
═══════════════════════════════════════════════════════════ */

static void render_production(EVE_HalContext *phost, AppState_t *app)
{
    int16_t y  = HDR_H + 12;
    int16_t cx = CONT_X;

    SET_COLOR(phost, COL_TXT);
    EVE_CoCmd_text(phost, cx + 8, y, FONT_LG, 0, "Production");
    y += TITLE_OFFSET;

    /* 期間タブ */
    static const char *period_labels[3] = { "DAILY", "WEEKLY", "MONTHLY" };
    for (int i = 0; i < 3; i++) {
        bool sel = (app->prod_period == (ProdPeriod_t)i);
        draw_rect(phost, cx + 8 + i * 100, y, 94, 28,
                  sel ? COL_ACCENT : COL_CARD2);
        SET_COLOR(phost, sel ? COL_WHITE : COL_TXT2);
        EVE_CoCmd_text(phost, cx + 8 + i * 100 + 47, y + 14,
                       FONT_SM, EVE_OPT_CENTER, period_labels[i]);
    }
    y += 36;

    /* メトリクスバー */
    draw_rect(phost, cx + 8, y, CONT_W - 16, 36, COL_SURF);
    {
        char totbuf[20];
        snprintf(totbuf, sizeof(totbuf), "Total:%lu", (unsigned long)app->prod_total_parts);
        char ratbuf[12];
        snprintf(ratbuf, sizeof(ratbuf), "OpRate:%d%%", app->prod_op_rate_pct);
        char datebuf[12];
        snprintf(datebuf, sizeof(datebuf), "%04d/%02d/%02d",
                 app->rtc_year, app->rtc_mon, app->rtc_day);
        SET_COLOR(phost, COL_TXT);
        EVE_CoCmd_text(phost, cx + 16,  y + 18, FONT_SM, EVE_OPT_CENTERY, totbuf);
        EVE_CoCmd_text(phost, cx + 220, y + 18, FONT_SM, EVE_OPT_CENTERY, ratbuf);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + CONT_W - 24, y + 18,
                       FONT_SM, EVE_OPT_RIGHTX | EVE_OPT_CENTERY, datebuf);
    }
    y += 44;

    /* W1/W2/W3 実績バー */
    {
        static const uint32_t prog_cols[3] = { COL_ACCENT, COL_TEAL, COL_GREEN };
        for (int i = 0; i < WORK_PROG_COUNT; i++) {
            WorkProgram_t *wp  = &app->work_programs[i];
            int16_t        ry  = y + i * 36;
            uint8_t        pct = (wp->target > 0)
                               ? (uint8_t)(wp->completed * 100 / wp->target)
                               : 0;
            SET_COLOR(phost, COL_TXT2);
            EVE_CoCmd_text(phost, cx + 10, ry + 18, FONT_SM, EVE_OPT_CENTERY, wp->name);
            int16_t bx = cx + 112, bw = (int16_t)(CONT_W - 192);
            draw_rect(phost, bx, ry + 8,  bw,               18, COL_CARD2);
            draw_rect(phost, bx, ry + 8,  bw * pct / 100,   18, prog_cols[i]);
            char cbuf[12];
            snprintf(cbuf, sizeof(cbuf), "%d/%d", wp->completed, wp->target);
            SET_COLOR(phost, COL_TXT);
            EVE_CoCmd_text(phost, cx + CONT_W - 10, ry + 18,
                           FONT_SM, EVE_OPT_RIGHTX | EVE_OPT_CENTERY, cbuf);
        }
    }
    y += WORK_PROG_COUNT * 36 + 8;

    /* トレンドバー（期間切替対応） */
    static const uint16_t s_weekly[7]  = { 2240, 2100, 2380, 2050, 2290, 2410, 2180 };
    static const uint16_t s_monthly[7] = { 9200, 8700, 9500, 8900, 9800, 9100, 9350 };
    const uint16_t *trend_data;
    const char     *trend_label;
    if (app->prod_period == PROD_PERIOD_WEEKLY) {
        trend_data  = s_weekly;
        trend_label = "WEEKLY TREND (7 weeks)";
    } else if (app->prod_period == PROD_PERIOD_MONTHLY) {
        trend_data  = s_monthly;
        trend_label = "MONTHLY TREND (7 months)";
    } else {
        trend_data  = app->prod_daily;
        trend_label = "DAILY TREND (7 days)";
    }

    int16_t trend_h = SCR_H - y - 8;
    if (trend_h > 20) {
        draw_card(phost, cx + 8, y, CONT_W - 16, trend_h);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, cx + 16, y + 8, FONT_SM, 0, trend_label);

        uint16_t max_d = 1;
        for (int i = 0; i < 7; i++)
            if (trend_data[i] > max_d) max_d = trend_data[i];

        int16_t bar_h = trend_h - 44; /* 上マージン34+下マージン10 */
        int16_t slot_w = (CONT_W - 24) / 7;
        int16_t bar_w  = slot_w - 6;
        for (int i = 0; i < 7; i++) {
            int16_t bx = cx + 12 + i * slot_w;
            int16_t bh = (int16_t)((int32_t)bar_h * trend_data[i] / max_d);
            int16_t by = y + 34 + (bar_h - bh); /* 34px上マージン→ラベルと数値が重ならない */
            draw_rect(phost, bx, by, bar_w, bh, i == 0 ? COL_ACCENT : COL_TXT3);
            if (trend_data[i] > 0) {
                char db[6];
                snprintf(db, sizeof(db), "%d", trend_data[i]);
                SET_COLOR(phost, i == 0 ? COL_TXT : COL_TXT2);
                EVE_CoCmd_text(phost, bx + bar_w / 2, by - 2, FONT_SM, EVE_OPT_CENTERX, db);
            }
        }
    }

    RB(TAG_R1, BTN_Y0, "PRD", COL_ACCENT, true);  /* 期間サイクル */
    RB(TAG_R2, BTN_Y1, "",    COL_SURF,   false);
    RB(TAG_R3, BTN_Y2, "",    COL_SURF,   false);
    RB(TAG_R4, BTN_Y3, "",    COL_SURF,   false);
}

/* ═══════════════════════════════════════════════════════════
   メニューオーバーレイ
═══════════════════════════════════════════════════════════ */

#define MENU_ITEM_H 40  /* 10項目×40px + 38 + 16 = 454px (my=20 → 474px, 480px内) */

static void render_menu_overlay(EVE_HalContext *phost, AppState_t *app)
{
    EVE_CoDl_colorA(phost, 200);
    draw_rect(phost, CONT_X, 0, CONT_W, CONT_H, COL_BG);
    EVE_CoDl_colorA(phost, 255);

    int16_t mx = CONT_X + 20;
    int16_t my = 20;
    int16_t mw = 260;

    typedef struct { uint8_t tag; const char *label; bool admin; Panel_t panel; } MenuItem;
    static const MenuItem menu_items[] = {
        { TAG_MENU_HOME,  "Home",        false, PANEL_HOME        },
        { TAG_MENU_OPER,  "Operation",   false, PANEL_OPERATION   },
        { TAG_MENU_COORD, "Coordinate",  false, PANEL_COORDINATE  },
        { TAG_MENU_PROD,  "Production",  false, PANEL_PRODUCTION  },
        { TAG_MENU_TOOL,  "Tool Life",   false, PANEL_TOOL_LIFE   },
        { TAG_MENU_ALRM,  "Alarms",      false, PANEL_ALARM       },
        { TAG_MENU_LOG,   "Log",         false, PANEL_LOG         },
        { TAG_MENU_MAINT, "Maintenance", true,  PANEL_MAINTENANCE },
        { TAG_MENU_SETT,  "Settings",    true,  PANEL_SETTINGS    },
        { TAG_MENU_USERS, "Users",       true,  PANEL_USERS       },
    };
    int nitems = (int)(sizeof(menu_items) / sizeof(menu_items[0]));

    /* 全10項目を常に表示（ADM専用はOPRにグレーアウト） */
    int8_t vis_idx[10];
    int8_t n_vis = 0;
    for (int i = 0; i < nitems; i++) {
        vis_idx[n_vis++] = (int8_t)i;
    }

    /* スクロール範囲クランプ */
    int8_t max_scroll = n_vis - MENU_VIS;
    if (max_scroll < 0) max_scroll = 0;
    if ((int8_t)app->menu_scroll > max_scroll) app->menu_scroll = (uint8_t)max_scroll;

    int8_t show_count = (n_vis < MENU_VIS) ? n_vis : MENU_VIS;
    int16_t totalH = 38 + show_count * MENU_ITEM_H + 16;
    draw_card(phost, mx, my, mw, totalH);

    SET_COLOR(phost, COL_TXT2);
    EVE_CoCmd_text(phost, mx + 14, my + 12, FONT_SM, 0, "NAVIGATION");

    /* スクロールインジケータ */
    if (max_scroll > 0) {
        char sbuf[10];
        snprintf(sbuf, sizeof(sbuf), "%d/%d", (int)app->menu_scroll + 1, (int)n_vis);
        SET_COLOR(phost, COL_TXT2);
        EVE_CoCmd_text(phost, mx + mw - 10, my + 12, FONT_SM, EVE_OPT_RIGHTX, sbuf);
    }

    for (int vi = 0; vi < show_count; vi++) {
        int8_t  real_i  = vis_idx[(int)app->menu_scroll + vi];
        int16_t iy      = my + 38 + vi * MENU_ITEM_H;
        bool    sel     = (app->menu_cursor == (int8_t)((int)app->menu_scroll + vi));
        bool    cur     = (app->panel == menu_items[real_i].panel);
        bool    enabled = (!menu_items[real_i].admin || app->role == ROLE_ADMIN);

        draw_rect(phost, mx + 4, iy, mw - 8, MENU_ITEM_H - 4,
                  (sel && enabled) ? COL_ACCENT : cur ? COL_CARD2 : COL_SURF);

        EVE_CoDl_tag(phost, enabled ? menu_items[real_i].tag : TAG_NONE);
        SET_COLOR(phost, !enabled ? COL_TXT3 :
                         sel      ? COL_WHITE :
                         cur      ? COL_ACCENT : COL_TXT2);
        EVE_CoCmd_text(phost, mx + 18, iy + (MENU_ITEM_H - 4) / 2,
                       FONT_MD, EVE_OPT_CENTERY, menu_items[real_i].label);
        if (menu_items[real_i].admin) {
            SET_COLOR(phost, enabled ? COL_YELLOW : COL_TXT3);
            EVE_CoCmd_text(phost, mx + mw - 14, iy + (MENU_ITEM_H - 4) / 2,
                           FONT_SM, EVE_OPT_CENTERY | EVE_OPT_RIGHTX, "ADM");
        }
        EVE_CoDl_tag(phost, TAG_NONE);
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
        case PANEL_HOME:        render_home       (phost, app); break;
        case PANEL_OPERATION:   render_operation  (phost, app); break;
        case PANEL_ALARM:       render_alarm      (phost, app); break;
        case PANEL_LOG:         render_log        (phost, app); break;
        case PANEL_SETTINGS:    render_settings   (phost, app); break;
        case PANEL_USERS:       render_users      (phost, app); break;
        case PANEL_TOOL_LIFE:   render_tool_life  (phost, app); break;
        case PANEL_COORDINATE:  render_coordinate (phost, app); break;
        case PANEL_MAINTENANCE: render_maintenance(phost, app); break;
        case PANEL_PRODUCTION:  render_production (phost, app); break;
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
    /* OPRは非ADM項目のみ操作可（index 0-6 の7項目）、ADMは全10項目 */
    return (app->role == ROLE_ADMIN) ? 10 : 7;
}

static Panel_t menu_idx_to_panel(AppState_t *app, int8_t idx)
{
    (void)app;
    static const Panel_t panels[] = {
        PANEL_HOME, PANEL_OPERATION, PANEL_COORDINATE, PANEL_PRODUCTION,
        PANEL_TOOL_LIFE, PANEL_ALARM, PANEL_LOG,
        PANEL_MAINTENANCE, PANEL_SETTINGS, PANEL_USERS
    };
    if (idx >= 0 && idx < 10) return panels[idx];
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
        case TAG_L2:  /* カーソル UP（トップ止まり） */
            if (app->menu_cursor > 0) {
                app->menu_cursor--;
                if (app->menu_cursor < (int8_t)app->menu_scroll)
                    app->menu_scroll = (uint8_t)app->menu_cursor;
            }
            break;
        case TAG_L4:  /* カーソル DOWN（ボトム止まり） */
            if (app->menu_cursor < max_item - 1) {
                app->menu_cursor++;
                if (app->menu_cursor >= (int8_t)(app->menu_scroll + MENU_VIS))
                    app->menu_scroll = (uint8_t)(app->menu_cursor - MENU_VIS + 1);
            }
            break;
        case TAG_L3: {
            /* ADM専用項目（index 7-9: Maintenance/Settings/Users）はOPRから選択不可 */
            bool admin_item = (app->menu_cursor >= 7);
            if (!admin_item || app->role == ROLE_ADMIN) {
                app->panel = menu_idx_to_panel(app, app->menu_cursor);
                app->menu_open = false;
            }
            break;
        }
        case TAG_MENU_HOME:
            app->panel = PANEL_HOME;        app->menu_open = false; break;
        case TAG_MENU_OPER:
            app->panel = PANEL_OPERATION;   app->menu_open = false; break;
        case TAG_MENU_COORD:
            app->panel = PANEL_COORDINATE;  app->menu_open = false; break;
        case TAG_MENU_TOOL:
            app->panel = PANEL_TOOL_LIFE;   app->menu_open = false; break;
        case TAG_MENU_ALRM:
            app->panel = PANEL_ALARM;       app->menu_open = false; break;
        case TAG_MENU_LOG:
            app->panel = PANEL_LOG;         app->menu_open = false; break;
        case TAG_MENU_PROD:
            app->panel = PANEL_PRODUCTION;  app->menu_open = false;
            break;
        case TAG_MENU_MAINT:
            if (app->role == ROLE_ADMIN) { app->panel = PANEL_MAINTENANCE; app->menu_open = false; }
            break;
        case TAG_MENU_SETT:
            if (app->role == ROLE_ADMIN) { app->panel = PANEL_SETTINGS;    app->menu_open = false; }
            break;
        case TAG_MENU_USERS:
            if (app->role == ROLE_ADMIN) { app->panel = PANEL_USERS;       app->menu_open = false; }
            break;
        }
        return;
    }

    /* パネル別操作 */
    switch (app->panel) {

    /* ─ ホーム ─ */
    case PANEL_HOME:
        switch (tag) {
        case TAG_R1: app->panel = PANEL_OPERATION;  break;
        case TAG_R2: app->panel = PANEL_COORDINATE; break;
        case TAG_R3: app->panel = PANEL_TOOL_LIFE;  break;
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

        if (!app->oper_editing) {
            /* 項目選択モード: L2=上, L4=下, L3=編集開始 */
            if (tag == TAG_L2)
                app->oper_cursor = (uint8_t)((app->oper_cursor + 2) % 3);
            else if (tag == TAG_L4)
                app->oper_cursor = (uint8_t)((app->oper_cursor + 1) % 3);
            else if (tag == TAG_L3)
                app->oper_editing = true;
        } else {
            /* 編集モード: R1=+, R2=-, L3=確定 */
            if (tag == TAG_R1) {  /* + */
                if (app->oper_cursor == 0) {
                    if (app->machine != MACHINE_RUNNING) {
                        app->machine = MACHINE_RUNNING;
                        add_log(app, LOG_CAT_OPERATION, "Machine started");
                        show_toast(app, "Machine started", tick_ms);
                    }
                } else if (app->oper_cursor == 1) {
                    if (app->set_rpm < rpm_max) {
                        uint32_t prev = app->set_rpm;
                        app->set_rpm = (app->set_rpm + 100 > rpm_max) ? rpm_max : app->set_rpm + 100;
                        app->actual_rpm = app->set_rpm;
                        char buf[32];
                        snprintf(buf, sizeof(buf), "RPM: %lu->%lu",
                                 (unsigned long)prev, (unsigned long)app->set_rpm);
                        add_log(app, LOG_CAT_OPERATION, buf);
                    }
                } else if (app->oper_cursor == 2) {
                    uint8_t prev_idx = app->feed_idx;
                    app->feed_idx = (app->feed_idx + 1) % pat_cnt;
                    {
                        char buf[48];
                        snprintf(buf, sizeof(buf), "Feed: %d%%->%d%%",
                                 feed_patterns[app->settings_feed_pattern][prev_idx],
                                 feed_patterns[app->settings_feed_pattern][app->feed_idx]);
                        add_log(app, LOG_CAT_OPERATION, buf);
                    }
                }
            } else if (tag == TAG_R2) {  /* - */
                if (app->oper_cursor == 0) {
                    if (app->machine != MACHINE_STOPPED) {
                        app->machine = MACHINE_STOPPED;
                        add_log(app, LOG_CAT_OPERATION, "Machine stopped");
                        show_toast(app, "Machine stopped", tick_ms);
                    }
                } else if (app->oper_cursor == 1) {
                    if (app->set_rpm > rpm_min) {
                        uint32_t prev = app->set_rpm;
                        app->set_rpm = (app->set_rpm < rpm_min + 100) ? rpm_min : app->set_rpm - 100;
                        app->actual_rpm = app->set_rpm;
                        char buf[32];
                        snprintf(buf, sizeof(buf), "RPM: %lu->%lu",
                                 (unsigned long)prev, (unsigned long)app->set_rpm);
                        add_log(app, LOG_CAT_OPERATION, buf);
                    }
                } else if (app->oper_cursor == 2) {
                    uint8_t prev_idx = app->feed_idx;
                    app->feed_idx = (uint8_t)((app->feed_idx + pat_cnt - 1) % pat_cnt);
                    {
                        char buf[48];
                        snprintf(buf, sizeof(buf), "Feed: %d%%->%d%%",
                                 feed_patterns[app->settings_feed_pattern][prev_idx],
                                 feed_patterns[app->settings_feed_pattern][app->feed_idx]);
                        add_log(app, LOG_CAT_OPERATION, buf);
                    }
                }
            } else if (tag == TAG_L3) {  /* 確定 */
                app->oper_editing = false;
                show_toast(app, "Confirmed", tick_ms);
            }
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
        case TAG_R3: {  /* PREV: フィルタを前へ */
            int idx = (int)app->alarm_filter;
            app->alarm_filter = (AlarmFilter_t)((idx + ALARM_FILTER_COUNT - 1) % ALARM_FILTER_COUNT);
            app->alarm_cursor = 0;
            app->alarm_scroll = 0;
            break;
        }
        case TAG_R4: {  /* NEXT: フィルタを次へ */
            int idx = (int)app->alarm_filter;
            app->alarm_filter = (AlarmFilter_t)((idx + 1) % ALARM_FILTER_COUNT);
            app->alarm_cursor = 0;
            app->alarm_scroll = 0;
            break;
        }
        case TAG_R2:  /* RST */
            if (app->role == ROLE_ADMIN && app->alarm_cursor < (int8_t)app->alarm_count) {
                app->alarms[app->alarm_cursor].active = false;
                add_log(app, LOG_CAT_ALARM, "Alarm reset");
                show_toast(app, "Alarm reset", tick_ms);
            }
            break;
        }
        break;
    }

    /* ─ ログ ─ */
    case PANEL_LOG: {
        /* カテゴリタブ（タグ 40-46） */
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
        case TAG_L2: /* UP */
            if (app->log_scroll > 0) app->log_scroll--;
            break;
        case TAG_L4: /* DN */
            if ((int16_t)app->log_scroll + visible < fcount) app->log_scroll++;
            break;
        case TAG_R1: { /* FEED: カテゴリタブ順送り（Admin は全7、Operator は5まで） */
            int max_cat = (app->role == ROLE_ADMIN) ? LOG_FILTER_COUNT : 5;
            app->log_filter = (LogFilter_t)((app->log_filter + 1) % max_cat);
            app->log_scroll = 0;
            break;
        }
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

    /* ─ 工具寿命 ─ */
    case PANEL_TOOL_LIFE: {
        int16_t hdr_off = HDR_H + 12 + TITLE_OFFSET + 30;
        int16_t vis_tl  = (int16_t)((SCR_H - hdr_off - 8) / TOOL_ROW_H);
        switch (tag) {
        case TAG_L2:  /* UP */
            if (app->tool_cursor > 0) {
                app->tool_cursor--;
                if ((int)app->tool_cursor < (int)app->tool_scroll)
                    app->tool_scroll = app->tool_cursor;
            }
            break;
        case TAG_L4:  /* DN */
            if (app->tool_cursor < app->tool_count - 1) {
                app->tool_cursor++;
                if ((int)app->tool_cursor >= (int)app->tool_scroll + vis_tl)
                    app->tool_scroll = (uint8_t)(app->tool_cursor - vis_tl + 1);
            }
            break;
        case TAG_R1:  /* RST (Admin のみ) */
            if (app->role == ROLE_ADMIN && app->tool_cursor < app->tool_count) {
                app->tools[app->tool_cursor].usage_count = 0;
                char buf[32];
                snprintf(buf, sizeof(buf), "Tool %s life reset",
                         app->tools[app->tool_cursor].number);
                add_log(app, LOG_CAT_OPERATION, buf);
                show_toast(app, "Tool life reset", tick_ms);
            }
            break;
        }
        break;
    }

    /* ─ 座標・NCプログラム ─ */
    case PANEL_COORDINATE:
        /* ボタン操作なし */
        break;

    /* ─ メンテナンス ─ */
    case PANEL_MAINTENANCE: {
        int16_t hdr_off2 = HDR_H + 12 + TITLE_OFFSET + 30;
        int16_t vis_mt   = (int16_t)((SCR_H - hdr_off2 - 8) / MAINT_ROW_H);
        switch (tag) {
        case TAG_L2:  /* UP */
            if (app->maint_cursor > 0) {
                app->maint_cursor--;
                if ((int)app->maint_cursor < (int)app->maint_scroll)
                    app->maint_scroll = app->maint_cursor;
            }
            break;
        case TAG_L4:  /* DN */
            if (app->maint_cursor < app->maint_count - 1) {
                app->maint_cursor++;
                if ((int)app->maint_cursor >= (int)app->maint_scroll + vis_mt)
                    app->maint_scroll = (uint8_t)(app->maint_cursor - vis_mt + 1);
            }
            break;
        case TAG_R1:  /* DONE (Admin のみ): 最終実施日を今日に更新 */
            if (app->role == ROLE_ADMIN && app->maint_cursor < app->maint_count) {
                MaintRecord_t *m = &app->maint_items[app->maint_cursor];
                snprintf(m->last_done, sizeof(m->last_done), "%04d-%02d-%02d",
                         app->rtc_year, app->rtc_mon, app->rtc_day);
                m->days_remaining = (int16_t)m->interval_days;
                add_log(app, LOG_CAT_OPERATION, "Maintenance completed");
                show_toast(app, "Maintenance completed", tick_ms);
            }
            break;
        }
        break;
    }

    /* ─ 生産実績 ─ */
    case PANEL_PRODUCTION:
        switch (tag) {
        case TAG_R1:  /* PRD: 期間サイクル */
            app->prod_period = (ProdPeriod_t)((app->prod_period + 1) % 3);
            break;
        }
        break;

    default:
        break;
    }
}
