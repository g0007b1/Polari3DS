/*
 * Old devkitARM/libctru: qtmCheckInitialized + incomplete qtm.h
 * Current libctru: qtmIsInitialized + full qtm.h
 * Use weak refs so one .elf links against either era.
 */
#include <3ds/types.h>
#include <stdbool.h>

bool qtmIsInitialized(void) __attribute__((weak));
bool qtmCheckInitialized(void) __attribute__((weak));

bool rosalina_qtm_is_initialized(void)
{
    if (qtmIsInitialized)
        return qtmIsInitialized();
    if (qtmCheckInitialized)
        return qtmCheckInitialized();
    return false;
}
