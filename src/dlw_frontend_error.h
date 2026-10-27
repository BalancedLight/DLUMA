#ifndef DLUMA_FRONTEND_ERROR_H
#define DLUMA_FRONTEND_ERROR_H
#include "dlw_win32.h"
#include "dlw_frontend_error_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_FRONTEND_ERROR_PRESENT_MESSAGE (0x8000U + 0x04D3U)
#define DLUMA_FRONTEND_ERROR_QUIT_MESSAGE (0x8000U + 0x04D4U)

/* Safe from a non-UI hook: latch first, then post to the loader's validated
 * window owner. A failed post never clears the blocker or quits Player. */
int dluma_frontend_error_present(const char *reason, unsigned long long epoch);
int dluma_frontend_error_blocks_project_launch(void);
int dluma_frontend_error_snapshot(DlumaFrontendErrorState *out);
void dluma_frontend_error_bind_owner_window(HWND window);
void dluma_frontend_error_present_on_owner(HWND window);
int dluma_frontend_error_consume_user_quit(HWND window);
int dluma_frontend_error_stop(void);

#ifdef __cplusplus
}
#endif
#endif
