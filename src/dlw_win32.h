/* Compatibility wrapper: keep the established no-CRT Win32 surface intact in
 * dluma_win32_base.h, then layer newer typed declarations and read-only helpers
 * for every direct and transitive include. */
#include "dlw_win32_base.h"
#include "dlw_win32_extensions.h"
