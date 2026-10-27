#ifndef DLUMA_SHA256_INTERNAL_H
#define DLUMA_SHA256_INTERNAL_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_sha256_handle_hex(
    HANDLE file, ULONGLONG max_bytes,
    char out[DLUMA_SHA256_HEX_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif
