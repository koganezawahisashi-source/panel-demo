/**
 * panel_demo.h
 * BT817 操作パネルデモ - ヘッダファイル
 * Target: RP2040 + BT817, 800x480
 */

#ifndef PANEL_DEMO_H
#define PANEL_DEMO_H

#include "EVE.h"
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────
   画面サイズ・レイアウト定数
───────────────────────────────────────────── */
#define SCR_W       800
#define SCR_H       480
#define BTN_W       90
#define BTN_H       90
#define BTN_GAP     40      /* (480 - 4*90) / 3 = 40 */

#define CONT_X      BTN_W                   /* コンテンツ領域 X開始 */
#define CONT_W      (SCR_W - 2 * BTN_W)    /* 620px */
#define CONT_H      SCR_H                   /* 480px */
#define RIGHT_X     (SCR_W - BTN_W)        /* 右ボタン X開始 */

/* ボタン Y座標（均等分散） */
#define BTN_Y0      0
#define BTN_Y1      130     /* 90 + 40 */
#define BTN_Y2      260     /* 130 + 90 + 40 */
#define BTN_Y3      390     /* 260 + 90 + 40 */

/* 行高さ定数 */
#define TOOL_ROW_H  42      /* 工具寿命行高さ */
#define MAINT_ROW_H 52      /* メンテナンス行高さ */

/* ─────────────────────────────────────────────
   タッチタグ定義
───────────────────────────────────────────── */
#define TAG_NONE        0
/* 左ボタン（固定ピクトグラム） */
#define TAG_L1          1   /* MENU */
#define TAG_L2          2   /* UP ↑ */
#define TAG_L3          3   /* OK */
#define TAG_L4          4   /* DOWN ↓ */
/* 右ボタン（画面・状態により可変） */
#define TAG_R1          11
#define TAG_R2          12
#define TAG_R3          13
#define TAG_R4          14
/* メニューオーバーレイ項目 (21-29) */
#define TAG_MENU_HOME   21
#define TAG_MENU_OPER   22
#define TAG_MENU_ALRM   23
#define TAG_MENU_LOG    24
#define TAG_MENU_TOOL   25
#define TAG_MENU_COORD  26
#define TAG_MENU_PROD   27  /* admin only */
#define TAG_MENU_MAINT  28  /* admin only */
#define TAG_MENU_SETT   29  /* admin only */
#define TAG_MENU_USERS  30  /* admin only */
/* ログカテゴリタブ (40-46) ※ 旧30-36から変更 */
#define TAG_LOG_TAB_BASE   40  /* +0=ALL +1=LOGIN +2=OPER +3=ALARM +4=LOG +5=SETT +6=USER */

/* ─────────────────────────────────────────────
   カラーパレット（0xRRGGBB）
───────────────────────────────────────────── */
#define COL_BG      0x080C14UL
#define COL_SURF    0x0F1623UL
#define COL_CARD    0x172032UL
#define COL_CARD2   0x1E2A3DUL
#define COL_BORDER  0x243044UL
#define COL_ACCENT  0x4F8EF7UL
#define COL_TEAL    0x22D3EEUL
#define COL_GREEN   0x34D399UL
#define COL_GREEN_LT 0x6EE7B7UL  /* 編集モード ライトグリーン */
#define COL_GREEN_BG 0x153D28UL  /* 編集モード ダークグリーン背景 */
#define COL_YELLOW  0xFBBF24UL
#define COL_ORANGE  0xF97316UL
#define COL_RED     0xF87171UL
#define COL_TXT     0xF2F6FFUL
#define COL_TXT2    0xBDCBEAUL
#define COL_TXT3    0x4A5A7AUL
#define COL_WHITE   0xFFFFFFUL

/* EVE_CoDl_colorRgb(phost, r, g, b) 用ヘルパー */
#define COL_R(c)    (uint8_t)(((c) >> 16) & 0xFF)
#define COL_G(c)    (uint8_t)(((c) >>  8) & 0xFF)
#define COL_B(c)    (uint8_t)( (c)        & 0xFF)
#define SET_COLOR(phost, c) \
    EVE_CoDl_colorRgb((phost), COL_R(c), COL_G(c), COL_B(c))

/* ─────────────────────────────────────────────
   フォント番号（BT817 内蔵 ROM フォントのみ使用）
   26 ≈ 15px / 28 ≈ 19px / 30 ≈ 29px / 31 ≈ 37px
   ※ カスタムフォント（Inter）は使用しない
───────────────────────────────────────────── */
#define FONT_SM     26  /* ROMフォント 小テキスト  ≈15px */
#define FONT_MD     28  /* ROMフォント 中テキスト  ≈19px */
#define FONT_LG     30  /* ROMフォント 大テキスト  ≈29px */
#define FONT_XL     31  /* ROMフォント 最大テキスト≈37px */

/* ─────────────────────────────────────────────
   画面・パネル・権限・状態 enum
───────────────────────────────────────────── */
typedef enum {
    SCREEN_LOCK = 0,
    SCREEN_AUTH,
    SCREEN_MAIN,
} Screen_t;

typedef enum {
    PANEL_HOME = 0,
    PANEL_OPERATION,
    PANEL_ALARM,
    PANEL_LOG,
    PANEL_SETTINGS,
    PANEL_USERS,
    PANEL_TOOL_LIFE,    /* 工具寿命管理 */
    PANEL_COORDINATE,   /* 座標・NCプログラム */
    PANEL_MAINTENANCE,  /* メンテナンス管理 */
    PANEL_PRODUCTION,   /* 生産実績サマリー */
    PANEL_COUNT,        /* = 10 */
} Panel_t;

typedef enum {
    ROLE_NONE = 0,
    ROLE_OPERATOR,
    ROLE_ADMIN,
} UserRole_t;

typedef enum {
    MACHINE_STOPPED = 0,
    MACHINE_RUNNING,
} MachineState_t;

typedef enum {
    ALARM_FILTER_ALL = 0,
    ALARM_FILTER_ERROR,
    ALARM_FILTER_WARN,
    ALARM_FILTER_INFO,
    ALARM_FILTER_COUNT,
} AlarmFilter_t;

typedef enum {
    PROD_PERIOD_DAILY = 0,
    PROD_PERIOD_WEEKLY,
    PROD_PERIOD_MONTHLY,
} ProdPeriod_t;

/* ─────────────────────────────────────────────
   ログカテゴリ・フィルタ
───────────────────────────────────────────── */
#define LOG_CAT_LOGIN     0
#define LOG_CAT_OPERATION 1
#define LOG_CAT_ALARM     2
#define LOG_CAT_LOG       3
#define LOG_CAT_SETTINGS  4
#define LOG_CAT_USER      5

typedef enum {
    LOG_FILTER_ALL = 0,
    LOG_FILTER_LOGIN,
    LOG_FILTER_OPERATION,
    LOG_FILTER_ALARM,
    LOG_FILTER_LOG,
    LOG_FILTER_SETTINGS,
    LOG_FILTER_USER,
    LOG_FILTER_COUNT,
} LogFilter_t;

/* ─────────────────────────────────────────────
   ログエントリ
───────────────────────────────────────────── */
#define LOG_MAX     32

typedef struct {
    char    time[10];   /* "HH:MM:SS\0" */
    char    user[16];
    char    event[48];
    uint8_t cat;        /* LOG_CAT_xxx */
} LogEntry_t;

/* ─────────────────────────────────────────────
   アラームレコード
───────────────────────────────────────────── */
#define ALARM_MAX   8

typedef enum {
    ALARM_LVL_INFO = 0,
    ALARM_LVL_WARN,
    ALARM_LVL_ERROR,
} AlarmLevel_t;

typedef struct {
    AlarmLevel_t level;
    const char  *title;
    const char  *detail;
    const char  *time_str;
    bool         active;        /* false = reset済み */
    bool         admin_only;    /* reset に Admin 権限要 */
    bool         viewed;        /* DTL で一度開いた */
} AlarmRecord_t;

/* ─────────────────────────────────────────────
   ワークプログラム（NCプログラム種別）
───────────────────────────────────────────── */
#define WORK_PROG_COUNT  3

typedef struct {
    const char *name;      /* "Bracket" / "Shaft" / "Cover" */
    uint16_t    target;    /* 目標数 */
    uint16_t    completed; /* 完成数 */
} WorkProgram_t;

/* ─────────────────────────────────────────────
   工具レコード
───────────────────────────────────────────── */
#define TOOL_MAX  8

typedef struct {
    char     number[4];    /* "T01" */
    char     name[20];     /* "End Mill 6mm" */
    uint32_t usage_count;
    uint32_t max_life;
} ToolRecord_t;

/* ─────────────────────────────────────────────
   メンテナンスレコード
───────────────────────────────────────────── */
#define MAINT_MAX  8

typedef struct {
    const char *name;
    uint16_t    interval_days;
    char        last_done[12]; /* "YYYY-MM-DD\0" */
    int16_t     days_remaining;/* 負 = 期限超過 */
} MaintRecord_t;

/* ─────────────────────────────────────────────
   アプリケーション状態構造体
───────────────────────────────────────────── */
typedef struct {
    /* 画面遷移 */
    Screen_t        screen;
    Panel_t         panel;
    UserRole_t      role;
    const char     *user_name;
    const char     *user_role_str;

    /* 機械状態 */
    MachineState_t  machine;
    uint32_t        set_rpm;        /* 設定回転数 (0-3000, step 100) */
    uint8_t         feed_idx;       /* フィードインデックス */

    /* センサ値 */
    uint32_t        actual_rpm;
    uint8_t         spindle_load;   /* % */
    uint8_t         spindle_temp;   /* °C */
    uint8_t         coolant_temp;   /* °C */
    uint8_t         env_temp;       /* °C */
    uint32_t        parts_total;
    uint32_t        parts_today;
    uint32_t        op_seconds;     /* 積算稼働秒 */

    /* ワークプログラム */
    WorkProgram_t   work_programs[WORK_PROG_COUNT];
    uint8_t         active_program; /* 0-2 */
    uint16_t        batch_target;   /* 全体目標数 */

    /* 加工時間 */
    uint32_t        piece_time_est_s;   /* 1ワーク予想秒 */
    uint32_t        piece_time_elap_s;  /* 1ワーク経過秒 */
    uint32_t        batch_time_est_s;   /* バッチ予想秒 */
    uint32_t        batch_time_elap_s;  /* バッチ経過秒 */

    /* 工具寿命 */
    ToolRecord_t    tools[TOOL_MAX];
    uint8_t         tool_count;
    uint8_t         tool_cursor;
    uint8_t         tool_scroll;

    /* メンテナンス */
    MaintRecord_t   maint_items[MAINT_MAX];
    uint8_t         maint_count;
    uint8_t         maint_cursor;
    uint8_t         maint_scroll;

    /* 生産サマリー */
    ProdPeriod_t    prod_period;
    uint32_t        prod_total_parts;
    uint8_t         prod_op_rate_pct;
    uint16_t        prod_daily[7];  /* 直近7日の日産数 [0]=今日 */

    /* 座標・NCプログラム */
    int32_t         coord_x, coord_y, coord_z; /* mm×1000 */
    uint16_t        nc_program_no;    /* Oxxxx */
    uint16_t        nc_block_no;      /* Nxxxx */
    char            nc_block_text[48];
    char            machine_mode[8];  /* "AUTO"/"MDI"/"JOG" */
    uint8_t         feed_override_pct;

    /* メニューオーバーレイ */
    bool            menu_open;
    int8_t          menu_cursor;
    uint8_t         menu_scroll;    /* 10項目対応スクロール */

    /* 認証アニメーション */
    uint32_t        auth_tick_start;
    UserRole_t      auth_pending;
    uint32_t        auth_duration_ms;

    /* 認証成功バナー */
    bool            auth_notif_active;
    uint32_t        auth_notif_start;
    const char     *auth_notif_name;
    const char     *auth_notif_role;

    /* アラーム */
    AlarmRecord_t   alarms[ALARM_MAX];
    uint8_t         alarm_count;
    AlarmFilter_t   alarm_filter;
    int8_t          alarm_cursor;
    uint8_t         alarm_scroll;

    /* アラーム詳細パネル */
    bool            alarm_detail_open;
    bool            alarm_detail_closing;
    uint32_t        alarm_detail_start;

    /* ロック画面カーソル (0=OPR, 1=ADM) */
    int8_t          lock_cursor;

    /* ログ */
    LogEntry_t      log_entries[LOG_MAX];
    uint8_t         log_count;
    uint8_t         log_scroll;
    LogFilter_t     log_filter;

    /* Settings */
    uint8_t         settings_cursor;
    bool            settings_editing;
    bool            settings_changed;
    uint32_t        settings_max_rpm;
    uint32_t        settings_min_rpm;
    uint8_t         settings_feed_pattern;
    uint8_t         settings_tool_no;

    /* ユーザー管理 */
    uint8_t         user_cursor;

    /* オペレーション画面 選択・編集モード */
    uint8_t         oper_cursor;    /* 0=Machine, 1=RPM, 2=Feed */
    bool            oper_editing;

    /* アニメーション tick */
    uint32_t        anim_tick;

    /* タッチエッジ検出 */
    uint8_t         tag_prev;
    uint8_t         tag_cur;

    /* 簡易 RTC */
    uint8_t         rtc_h, rtc_m, rtc_s;
    uint8_t         rtc_day, rtc_mon;
    uint16_t        rtc_year;
    uint32_t        rtc_last_tick;

    /* トースト通知 */
    char            toast_msg[48];
    uint32_t        toast_show_tick;
    bool            toast_visible;

} AppState_t;

/* ─────────────────────────────────────────────
   公開関数
───────────────────────────────────────────── */
void panel_demo_init        (AppState_t *app);
void panel_demo_load_fonts  (EVE_HalContext *phost);   /* Flash→RAM_G展開+ハンドル登録 */
void panel_demo_update      (AppState_t *app, uint32_t tick_ms);
void panel_demo_render      (EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms);
void panel_demo_touch       (EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms);

#endif /* PANEL_DEMO_H */
