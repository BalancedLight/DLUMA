#ifndef DLW_NIGHTLY_DOWNLOAD_H
#define DLW_NIGHTLY_DOWNLOAD_H
#include <stddef.h>

/* Single asynchronous HTTPS download. Destination must not already exist. */
int dlw_nightly_download_start(const wchar_t *url, const wchar_t *destination,
                                unsigned long long byte_limit);
/* 0 pending, 1 complete, -1 failed or idle. */
int dlw_nightly_download_poll(void);
unsigned long dlw_nightly_download_error(void);
unsigned long dlw_nightly_download_http_status(void);
int dlw_nightly_download_sha256(char out[65]);
int dlw_nightly_private_directory(const wchar_t *path);
void dlw_nightly_download_shutdown(void);

#endif
