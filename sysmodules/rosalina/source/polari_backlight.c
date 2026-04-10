/*
 * Polari backlight levels (0–5) + CFG savegame block 0x30003 (same family as NTP 0x30001/2).
 */
#include <3ds.h>
#include <string.h>
#include "polari_backlight.h"
#include "luminance.h"
#include "menu.h"
#include "menus/config_extra.h"
#include "utils.h"

typedef struct {
    u32 magic;
    u8  version;
    u8  level;
    u8  _pad[2];
} PolariBacklightCfgBlk;

static void polari_gsp_backlight_begin(void)
{
    svcKernelSetState(0x10000, 2);
    gspLcdInit();
}

static void polari_gsp_backlight_end(void)
{
    gspLcdExit();
    svcKernelSetState(0x10000, 2);
}

void Polari_ApplyBacklightLevel(u8 level)
{
    u32 minLum;
    u32 maxLum;
    u32 midLum;

    if (!hasTopScreen)
        return;
    if (!isServiceUsable("gsp::Lcd"))
        return;

    level = (u8)(level % (POLARI_BACKLIGHT_LEVEL_MAX + 1));
    minLum = getMinLuminancePreset();
    maxLum = getMaxLuminancePreset();
    midLum = (minLum + maxLum) / 2u;

    polari_gsp_backlight_begin();

    switch (level) {
    case 0:
        GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM));
        break;
    case 1:
        GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_TOP));
        GSPLCD_PowerOffBacklight(BIT(GSP_SCREEN_BOTTOM));
        break;
    case 2:
        GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_BOTTOM));
        GSPLCD_PowerOffBacklight(BIT(GSP_SCREEN_TOP));
        break;
    case 3:
        GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM));
        GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM), minLum);
        break;
    case 4:
        GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM));
        GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM), midLum);
        break;
    default:
        GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM));
        GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_TOP) | BIT(GSP_SCREEN_BOTTOM), maxLum);
        break;
    }

    polari_gsp_backlight_end();
}

Result Polari_LoadBacklightFromCfg(void)
{
    PolariBacklightCfgBlk blk;
    Result res;

    memset(&blk, 0, sizeof(blk));
    cfguInit();
    res = CFG_GetConfigInfoBlk8(sizeof(blk), POLARI_CFG_BACKLIGHT_BLK_ID, &blk);
    cfguExit();
    if (R_FAILED(res))
        return res;
    if (blk.magic != POLARI_CFG_BACKLIGHT_MAGIC || blk.version != POLARI_CFG_BACKLIGHT_VER)
        return (Result)1;
    if (blk.level > POLARI_BACKLIGHT_LEVEL_MAX)
        return (Result)2;
    configExtra.backlightLevel = blk.level;
    return 0;
}

Result Polari_SaveBacklightToCfg(u8 level)
{
    PolariBacklightCfgBlk blk;
    Result res;

    if (level > POLARI_BACKLIGHT_LEVEL_MAX)
        level = POLARI_BACKLIGHT_LEVEL_MAX;

    blk.magic = POLARI_CFG_BACKLIGHT_MAGIC;
    blk.version = (u8)POLARI_CFG_BACKLIGHT_VER;
    blk.level = level;
    blk._pad[0] = blk._pad[1] = 0;

    cfguInit();
    res = CFG_SetConfigInfoBlk8(sizeof(blk), POLARI_CFG_BACKLIGHT_BLK_ID, &blk);
    if (R_SUCCEEDED(res))
        res = CFG_UpdateConfigSavegame();
    cfguExit();
    return res;
}

const char *Polari_BacklightLevelLabel(u8 level)
{
    static const char *const names[] = {
        "Both on",
        "Top only",
        "Bottom only",
        "Both + min br.",
        "Both + mid br.",
        "Both + max br.",
    };

    return names[level % (POLARI_BACKLIGHT_LEVEL_MAX + 1)];
}
