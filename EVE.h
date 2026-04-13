/**
 * EVE.h
 * EveApps ヘッダ集約ラッパー
 * panel_demo.h の #include "EVE.h" に対応
 */

#ifndef EVE_H
#define EVE_H

#include "EVE_Platform.h"
#include "EVE_Hal.h"
#include "EVE_CoCmd.h"
#include "EVE_CoDl.h"
#include "EVE_Util.h"
#include "EVE_GpuDefs.h"

/* ─────────────────────────────────────────────
   互換定義：EveApps は EVE_ プレフィックスなし
───────────────────────────────────────────── */

/* 描画プリミティブ */
#define EVE_RECTS      RECTS
#define EVE_LINES      LINES
#define EVE_LINE_STRIP LINE_STRIP
#define EVE_POINTS     POINTS

/* テキスト配置オプション */
#define EVE_OPT_CENTER  OPT_CENTER
#define EVE_OPT_CENTERY OPT_CENTERY
#define EVE_OPT_CENTERX OPT_CENTERX
#define EVE_OPT_RIGHTX  OPT_RIGHTX

/* タッチモード */
#define EVE_TMODE_CONTINUOUS TOUCHMODE_CONTINUOUS

/* 関数名の揺れ */
#define EVE_CoCmd_bgcolor EVE_CoCmd_bgColor

#endif /* EVE_H */
