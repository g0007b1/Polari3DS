/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2020 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include <3ds.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "luminance.h"
#include "utils.h"
#include "draw.h"
#include "ifile.h"
#include "menu.h"
#include "luma_config.h"
#include "luma_shared_config.h"

/* Extended BlPwmData: stock layout + bottom-only luminance curve + magic (Polari). */
#define POLARI_BL_PWM_EXT_MAGIC 0x504F4C53u /* 'POLS' */
/* Separate file: CFG block 0x50002 is fixed ~56 bytes; larger Set/Get may fail silently. */
#define POLARI_BOT_LUM_FILE_MAGIC 0x504F4C44u /* 'POLD' */
#define POLARI_BOT_LUM_PATH "/luma/polari_bot_lum.bin"

typedef struct PolariBotLumFile {
    u32 magic;
    u16 bot[7];
} PolariBotLumFile;

typedef struct BlPwmData
{
    float coeffs[3][3];
    u8 numLevels;
    u8 unk;
    u16  luminanceLevels[7]; /* top screen presets (stock + Polari top) */
    u16  brightnessMax;
    u16  brightnessMin;
    u16  luminanceLevelsBot[7];
    u32  polari_ext_magic;
} BlPwmData;

/* Stock nn CFG block ends before luminanceLevelsBot[] (~56 bytes). */
#define BL_PWM_CFG_SAVE_SIZE offsetof(BlPwmData, luminanceLevelsBot)
_Static_assert(BL_PWM_CFG_SAVE_SIZE == 56, "expected stock BlPwmData CFG size");

// Calibration, with (dubious) default values as fallback
static BlPwmData s_blPwmData = {
    .coeffs = {
        { 0.00111639f, 1.41412f, 0.07178809f },
        { 0.000418169f, 0.66567f, 0.06098654f },
        { 0.00208543f, 1.55639f, 0.0385939f }
    },
    .numLevels = 5,
    .unk = 0,
    .luminanceLevels = { 20, 43, 73, 95, 117, 172, 172 },
    .brightnessMax = 512,
    .brightnessMin = 13,
    .luminanceLevelsBot = { 20, 43, 73, 95, 117, 172, 172 },
    .polari_ext_magic = 0,
};

static inline float getPwmRatio(u32 brightnessMax, u32 pwmCnt)
{
    u32 val = (pwmCnt & 0x10000) ? pwmCnt & 0x3FF : 511; // check pwm enabled flag
    return (float)brightnessMax / (val + 1);
}

// nn's asm has rounding errors (originally at 10^-3)
static inline u32 luminanceToBrightness(u32 luminance, const float coeffs[3], u32 minLuminance, float pwmRatio)
{
    float x = (float)luminance;
    float y = coeffs[0]*x*x + coeffs[1]*x + coeffs[2];
    y = (y <= minLuminance ? (float)minLuminance : y) / pwmRatio;

    return (u32)(y + 0.5f);
}

static inline u32 brightnessToLuminance(u32 brightness, const float coeffs[3], float pwmRatio)
{
    // Find polynomial root of ax^2 + bx + c = y

    float y = (float)brightness * pwmRatio;
    float a = coeffs[0];
    float b = coeffs[1];
    float c = coeffs[2] - y;

    float x0 = (-b + sqrtf(b*b - 4.0f*a*c)) / (a + a);

    return (u32)(x0 + 0.5f);
}

static void polari_bot_lum_fallback_if_needed(void)
{
    if (s_blPwmData.polari_ext_magic != POLARI_BL_PWM_EXT_MAGIC) {
        memcpy(s_blPwmData.luminanceLevelsBot, s_blPwmData.luminanceLevels, sizeof(s_blPwmData.luminanceLevelsBot));
        s_blPwmData.polari_ext_magic = 0;
    }
}

static FS_ArchiveID polari_luma_archive(void)
{
    s64 out;
    if (R_FAILED(svcGetSystemInfo(&out, 0x10000, 0x203)))
        return ARCHIVE_SDMC;
    return (bool)out ? ARCHIVE_SDMC : ARCHIVE_NAND_RW;
}

static void polari_load_bot_lum_from_sd(void)
{
    IFile file;
    PolariBotLumFile blk;
    Result res;
    u64 total;

    memset(&blk, 0, sizeof(blk));
    res = IFile_Open(&file, polari_luma_archive(), fsMakePath(PATH_EMPTY, ""),
        fsMakePath(PATH_ASCII, POLARI_BOT_LUM_PATH), FS_OPEN_READ);
    if (R_FAILED(res))
        return;
    res = IFile_Read(&file, &total, &blk, sizeof(blk));
    IFile_Close(&file);
    if (R_FAILED(res) || total != sizeof(blk))
        return;
    if (blk.magic != POLARI_BOT_LUM_FILE_MAGIC)
        return;
    memcpy(s_blPwmData.luminanceLevelsBot, blk.bot, sizeof(blk.bot));
    s_blPwmData.polari_ext_magic = POLARI_BL_PWM_EXT_MAGIC;
}

static void polari_save_bot_lum_to_sd(void)
{
    IFile file;
    PolariBotLumFile blk;
    u64 total;
    Result res;

    blk.magic = POLARI_BOT_LUM_FILE_MAGIC;
    memcpy(blk.bot, s_blPwmData.luminanceLevelsBot, sizeof(blk.bot));
    res = IFile_Open(&file, polari_luma_archive(), fsMakePath(PATH_EMPTY, ""),
        fsMakePath(PATH_ASCII, POLARI_BOT_LUM_PATH), FS_OPEN_CREATE | FS_OPEN_WRITE);
    if (R_FAILED(res))
        return;
    (void)IFile_Write(&file, &total, &blk, sizeof(blk), 0);
    IFile_Close(&file);
}

static void polari_pwm_sync_row(u16 levels[7]);

static void luminance_load_pwm_data(void)
{
    cfguInit();
    (void)CFG_GetConfigInfoBlk8(BL_PWM_CFG_SAVE_SIZE, 0x50002, &s_blPwmData);
    cfguExit();
    polari_load_bot_lum_from_sd();
    polari_bot_lum_fallback_if_needed();
    polari_pwm_sync_row(s_blPwmData.luminanceLevels);
    polari_pwm_sync_row(s_blPwmData.luminanceLevelsBot);
}

static void readCalibration(void)
{
    static bool calibRead = false;

    if (!calibRead) {
        luminance_load_pwm_data();
        calibRead = true;
    }
}

u32 getMinLuminancePreset(bool top)
{
    readCalibration();
    return top ? s_blPwmData.luminanceLevels[0] : s_blPwmData.luminanceLevelsBot[0];
}

u32 getMaxLuminancePreset(bool top)
{
    // Unlike SetLuminanceLevel, SetLuminance doesn't
    // check if preset <= 5, and actually allows the lumiance
    // levels provisioned for the "brightness boost mode" (brighter
    // when adapter is plugged in), even when the feature is disabled
    // (it is disabled for anything but the OG model, iirc)
    readCalibration();
    return top ? s_blPwmData.luminanceLevels[6] : s_blPwmData.luminanceLevelsBot[6];
}

u32 getCurrentLuminance(bool top)
{
    u32 regbase = top ? 0x10202200 : 0x10202A00;

    readCalibration();

    bool is3d = (REG32(0x10202000 + 0x000) & 1) != 0;
    const float *coeffs = s_blPwmData.coeffs[top ? (is3d ? 2 : 1) : 0];
    u32 brightness = REG32(regbase + 0x40);
    float ratio = getPwmRatio(s_blPwmData.brightnessMax, REG32(regbase + 0x44));

    return brightnessToLuminance(brightness, coeffs, ratio);
}

/* Keep each preset row [0]..[6] non-decreasing (OS expects monotonic presets). */
static void polari_pwm_sync_row(u16 levels[7])
{
    unsigned i;
    for (i = 1; i < 7; i++) {
        if (levels[i] < levels[i - 1])
            levels[i] = levels[i - 1];
    }
}

void setBrightnessAlt(u32 lumTop, u32 lumBot) 
{
    u32 regbaseTop = 0x10202200;
    u32 regbaseBot = 0x10202A00; 
    u32 offset = 0x40; // https://www.3dbrew.org/wiki/LCD_Registers — bits 9-0 PWM
    const float *coeffsTop = s_blPwmData.coeffs[1];
    const float *coeffsBot = s_blPwmData.coeffs[0];
    float ratioTop = getPwmRatio(s_blPwmData.brightnessMax, REG32(regbaseTop + 0x44));
    float ratioBot = getPwmRatio(s_blPwmData.brightnessMax, REG32(regbaseBot + 0x44));
    u32 valTop = luminanceToBrightness(lumTop, coeffsTop, 0, ratioTop) & 0x3FFu;
    u32 valBot = luminanceToBrightness(lumBot, coeffsBot, 0, ratioBot) & 0x3FFu;

    REG32(regbaseTop + offset) = (REG32(regbaseTop + offset) & ~0x3FFu) | valTop;
    REG32(regbaseBot + offset) = (REG32(regbaseBot + offset) & ~0x3FFu) | valBot;
}

/*
 * Startup split: bottom polynomial can map the same "luminance" to higher PWM than top — cap
 * bottom duty so the panel is never brighter than the top (user expects dimmer bottom).
 */
static void polari_set_split_brightness_mmio(u32 lumTop, u32 lumBot)
{
    u32 regbaseTop = 0x10202200;
    u32 regbaseBot = 0x10202A00;
    u32 offset = 0x40;
    const float *coeffsTop = s_blPwmData.coeffs[1];
    const float *coeffsBot = s_blPwmData.coeffs[0];
    float ratioTop = getPwmRatio(s_blPwmData.brightnessMax, REG32(regbaseTop + 0x44));
    float ratioBot = getPwmRatio(s_blPwmData.brightnessMax, REG32(regbaseBot + 0x44));
    u32 valTop = luminanceToBrightness(lumTop, coeffsTop, 0, ratioTop) & 0x3FFu;
    u32 valBot = luminanceToBrightness(lumBot, coeffsBot, 0, ratioBot) & 0x3FFu;

    if (valBot > valTop)
        valBot = valTop;

    REG32(regbaseTop + offset) = (REG32(regbaseTop + offset) & ~0x3FFu) | valTop;
    REG32(regbaseBot + offset) = (REG32(regbaseBot + offset) & ~0x3FFu) | valBot;
}

static bool polari_lum_preset_tables_equal(void)
{
    return memcmp(s_blPwmData.luminanceLevels, s_blPwmData.luminanceLevelsBot,
        sizeof(s_blPwmData.luminanceLevels)) == 0;
}

/*
 * The OS uses one luminance target for both panels. After loading separate top/bottom
 * calibration, map the current global luminance onto the bottom preset range and poke HW.
 *
 * Called a few seconds after boot (menu thread) so PWM/luminance reads are stable.
 * MMIO only — no gspLcdInit. Bottom duty capped to not exceed top (polynomial mismatch fix).
 */
void polari_apply_startup_luminance_split(void)
{
    u32 minT, maxT, minB, maxB, L, lumBot;
    u32 t, b, best, i, r;

    if (!hasTopScreen)
        return;
    if (!isServiceUsable("gsp::Lcd"))
        return;

    readCalibration();
    if (polari_lum_preset_tables_equal())
        return;

    minT = getMinLuminancePreset(true);
    maxT = getMaxLuminancePreset(true);
    minB = getMinLuminancePreset(false);
    maxB = getMaxLuminancePreset(false);

    if (maxT <= minT || maxB <= minB)
        return;

    svcSleepThread(400 * 1000LL);

    for (r = 0; r < 2; r++) {
        best = 0;
        for (i = 0; i < 5; i++) {
            t = getCurrentLuminance(true);
            b = getCurrentLuminance(false);
            if (t == b && t > best)
                best = t;
            svcSleepThread(150 * 1000LL);
        }
        L = best;
        if (r == 0 && maxT > minT + 80 && L < minT + 40)
            svcSleepThread(1500 * 1000LL);
        else
            break;
    }

    if (L < minT)
        return;

    if (L <= minT)
        lumBot = minB;
    else if (L >= maxT)
        lumBot = maxB;
    else {
        u64 den = (u64)(maxT - minT);
        u64 num = (u64)(L - minT) * (u64)(maxB - minB);
        lumBot = minB + (u32)(num / den);
    }

    if (lumBot > L)
        lumBot = L;

    polari_set_split_brightness_mmio(L, lumBot);
}

void Luminance_RecalibrateBrightnessDefaults(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    u32 kHeld = 0;
    int sel = 0, maxBri = (int)POLARI_ROSALINA_BRIGHTNESS_TRUE_MAX;
    int editTop = 1; /* 1 = top screen table, 0 = bottom */
    char fmtbuf[0x40];
    u16 *activeRow;

    luminance_load_pwm_data();

    s_blPwmData.brightnessMin = 1;

    do
    {
        kHeld = HID_PAD;
        u32 pressed = waitInputWithTimeout(1000);

        if (pressed & KEY_X)
            editTop = !editTop;

        activeRow = editTop ? s_blPwmData.luminanceLevels : s_blPwmData.luminanceLevelsBot;

        if (pressed & DIRECTIONAL_KEYS)
        {
            if(pressed & KEY_DOWN)
            {
                if(++sel > 4) sel = 4;
            }
            else if(pressed & KEY_UP)
            {
                if(--sel < 0) sel = 0;
            }
            else if (pressed & KEY_RIGHT)
            {
                int step = (kHeld & (KEY_L | KEY_R)) ? 10 : 1;
                s32 v = (s32)activeRow[sel] + step;
                if (v < 0) v = 0;
                if (v > maxBri) v = maxBri;
                activeRow[sel] = (u16)v;
                polari_pwm_sync_row(activeRow);
            }
            else if (pressed & KEY_LEFT)
            {
                int step = (kHeld & (KEY_L | KEY_R)) ? 10 : 1;
                s32 v = (s32)activeRow[sel] - step;
                if (v < 0) v = 0;
                if (v > maxBri) v = maxBri;
                activeRow[sel] = (u16)v;
                polari_pwm_sync_row(activeRow);
            }
        }
        
        if (pressed & KEY_B)
            break;

        if(pressed & KEY_START)
        {
            s_blPwmData.polari_ext_magic = POLARI_BL_PWM_EXT_MAGIC;
            polari_pwm_sync_row(s_blPwmData.luminanceLevels);
            polari_pwm_sync_row(s_blPwmData.luminanceLevelsBot);
            cfguInit();
            if (R_SUCCEEDED(CFG_SetConfigInfoBlk8(BL_PWM_CFG_SAVE_SIZE, 0x50002, &s_blPwmData)))
                CFG_UpdateConfigSavegame();
            cfguExit();
            polari_save_bot_lum_to_sd();
            break;
        }

        activeRow = editTop ? s_blPwmData.luminanceLevels : s_blPwmData.luminanceLevelsBot;

        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "Permanent brightness recalibration - by Nutez");
        u32 posY = 30;

        posY = Draw_DrawFormattedString(
            10, posY, COLOR_GREEN,
            "Editing: %s  (X: switch top/bottom)\n",
            editTop ? "TOP screen" : "BOTTOM screen") + SPACING_Y;
        
        posY = Draw_DrawString(10, posY, COLOR_RED, "WARNING: ") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "  * brightness preview not possible here\n    due to glitch risk.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "  * test values via 'Change screen brightness'.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "  * avoid frequent use to minimise NAND(!) wear.") + SPACING_Y;
        posY = Draw_DrawFormattedString(
            10, posY, COLOR_WHITE,
            "  * %u is only presumed(!) safe for prolonged raw use.",
            (unsigned)POLARI_ROSALINA_BRIGHTNESS_TRUE_MAX) + (SPACING_Y * 2);

        sprintf(fmtbuf, "%c Level 1 value: %u", (sel == 0 ? '>' : ' '), (unsigned)activeRow[0]);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        sprintf(fmtbuf, "%c Level 2 value: %u", (sel == 1 ? '>' : ' '), (unsigned)activeRow[1]);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        sprintf(fmtbuf, "%c Level 3 value: %u", (sel == 2 ? '>' : ' '), (unsigned)activeRow[2]);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        sprintf(fmtbuf, "%c Level 4 value: %u", (sel == 3 ? '>' : ' '), (unsigned)activeRow[3]);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        sprintf(fmtbuf, "%c Level 5 value: %u", (sel == 4 ? '>' : ' '), (unsigned)activeRow[4]);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE,
            "  (auto) Boost slot: %u  Max cap: %u\n",
            (unsigned)activeRow[5], (unsigned)activeRow[6]) + (SPACING_Y * 2);

        posY = Draw_DrawFormattedString(
            10, posY, COLOR_WHITE,
            "Other screen — L1:%u L5:%u Max:%u\n",
            (unsigned)(editTop ? s_blPwmData.luminanceLevelsBot[0] : s_blPwmData.luminanceLevels[0]),
            (unsigned)(editTop ? s_blPwmData.luminanceLevelsBot[4] : s_blPwmData.luminanceLevels[4]),
            (unsigned)(editTop ? s_blPwmData.luminanceLevelsBot[6] : s_blPwmData.luminanceLevels[6])) + SPACING_Y;

        posY = Draw_DrawString(10, posY, COLOR_GREEN, "Controls:") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " X: switch TOP / BOTTOM preset table.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " UP/DOWN to choose level to edit.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " RIGHT/LEFT for +/-1, +hold L1 or R1 for +/-10.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " Higher presets auto-raise Boost/Max rows.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " Press START to save (CFG top + /luma/polari_bot_lum.bin).") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " Reboot may be required to see applied changes.") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, " Press B to exit.");

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while (!menuShouldExit);
}
