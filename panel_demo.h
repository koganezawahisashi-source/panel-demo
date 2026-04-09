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
/* メニューオーバーレイ項目 */
#define TAG_MENU_HOME   21
#define TAG_MENU_OPER   22
#define TAG_MENU_ALRM   23
#define TAG_MENU_LOG    24
#define TAG_MENU_SETT   25  /* admin only */
#define TAG_MENU_USERS  26  /* admin only */

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
#define COL_YELLOW  0xFBBF24UL
#define COL_RED     0xF87171UL
#define COL_TXT     0xE8EEF8UL
#define COL_TXT2    0x8B9BBFUL
#define COL_TXT3    0x4A5A7AUL
#define COL_WHITE   0xFFFFFFUL

/* EVE_CoDl_colorRgb(phost, r, g, b) 用ヘルパー */
#define COL_R(c)    (uint8_t)(((c) >> 16) & 0xFF)
#define COL_G(c)    (uint8_t)(((c) >>  8) & 0xFF)
#define COL_B(c)    (uint8_t)( (c)        & 0xFF)
#define SET_COLOR(phost, c) \
    EVE_CoDl_colorRgb((phost), COL_R(c), COL_G(c), COL_B(c))

/* ─────────────────────────────────────────────
   フォント番号（EVE ROM フォント）
───────────────────────────────────────────── */
#define FONT_SM     26  /* 小（約12px） */
#define FONT_MD     28  /* 中（約16px） */
#define FONT_LG     30  /* 大（約20px） */
#define FONT_XL     33  /* 特大（約36px） */

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
    PANEL_COUNT,
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
} AlarmRecord_t;

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
    uint8_t         feed_idx;       /* 0=25% 1=50% 2=75% 3=100% */

    /* センサ値（実機ではポーリング更新） */
    uint32_t        actual_rpm;
    uint8_t         spindle_load;   /* % */
    uint8_t         spindle_temp;   /* °C */
    uint8_t         coolant_temp;   /* °C */
    uint8_t         env_temp;       /* °C */
    uint32_t        parts_total;
    uint32_t        parts_today;
    uint32_t        op_seconds;     /* 積算稼働秒 */

    /* メニューオーバーレイ */
    bool            menu_open;
    int8_t          menu_cursor;

    /* 認証アニメーション */
    uint32_t        auth_tick_start;
    UserRole_t      auth_pending;
    uint32_t        auth_duration_ms;

    /* アラーム */
    AlarmRecord_t   alarms[ALARM_MAX];
    uint8_t         alarm_count;
    AlarmFilter_t   alarm_filter;
    int8_t          alarm_cursor;

    /* ロック画面カーソル (0=OPR, 1=ADM) */
    int8_t          lock_cursor;

    /* RFID アニメーション tick */
    uint32_t        anim_tick;

    /* タッチエッジ検出 */
    uint8_t         tag_prev;
    uint8_t         tag_cur;

    /* 簡易 RTC */
    uint8_t         rtc_h, rtc_m, rtc_s;
    uint32_t        rtc_last_tick;

    /* トースト通知 */
    char            toast_msg[48];
    uint32_t        toast_show_tick;
    bool            toast_visible;

} AppState_t;

/* ─────────────────────────────────────────────
   公開関数
───────────────────────────────────────────── */
void panel_demo_init  (AppState_t *app);
void panel_demo_update(AppState_t *app, uint32_t tick_ms);
void panel_demo_render(EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms);
void panel_demo_touch (EVE_HalContext *phost, AppState_t *app, uint32_t tick_ms);

#endif /* PANEL_DEMO_H */
