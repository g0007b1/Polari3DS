#pragma once
#include <3ds/types.h>

/*
 * qtmGetSessionHandle() exists in libctru but older devkitARM headers omitted the
 * prototype; Rosalina still needs it for QTM session steal (menu.c).
 */
Handle *qtmGetSessionHandle(void);
