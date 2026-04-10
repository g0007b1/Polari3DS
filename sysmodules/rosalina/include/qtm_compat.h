#pragma once
#include <3ds/types.h>
#include <stdbool.h>

/*
 * qtmGetSessionHandle: in libctru but omitted from some old headers.
 */
Handle *qtmGetSessionHandle(void);

bool rosalina_qtm_is_initialized(void);
