/*
 * Polari: backlight "levels" + optional persistence in CFG (NAND config save).
 */
#pragma once

#include <3ds/types.h>

#define POLARI_BACKLIGHT_LEVEL_MAX   5
#define POLARI_CFG_BACKLIGHT_BLK_ID  0x30003u
#define POLARI_CFG_BACKLIGHT_MAGIC   0x504F4C42u /* 'POLB' */
#define POLARI_CFG_BACKLIGHT_VER     1u

void Polari_ApplyBacklightLevel(u8 level);
Result Polari_LoadBacklightFromCfg(void);
Result Polari_SaveBacklightToCfg(u8 level);
const char *Polari_BacklightLevelLabel(u8 level);
