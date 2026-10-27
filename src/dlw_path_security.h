#ifndef DLUMA_PATH_SECURITY_H
#define DLUMA_PATH_SECURITY_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_get_final_path_checked(
    HANDLE file, WCHAR* out, unsigned int capacity);
int dluma_get_module_path_checked(
    HMODULE module, WCHAR* out, unsigned int capacity);

#ifdef __cplusplus
}
#endif

#endif
