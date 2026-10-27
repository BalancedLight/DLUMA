#include "dlw_common.h"

/* The main loader intentionally has no CRT startup. BufferOverflowU.lib is
 * the Windows SDK's minimal user-mode /GS+CFG support library; call its cookie
 * initializer from the uninstrumented DLL entry translation unit. */
void __cdecl __security_init_cookie(void);

void __cdecl dluma_security_init_cookie(void)
{
    __security_init_cookie();
}
