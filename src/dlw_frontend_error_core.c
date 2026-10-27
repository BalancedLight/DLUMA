#include "dlw_frontend_error_core.h"

int dluma_frontend_error_latch(DlumaFrontendErrorState *state,
    const char *reason, unsigned long long epoch)
{
    unsigned int i = 0;
    if (!state || !reason || !reason[0]) return 0;
    if (state->blocked) return 1;
    state->epoch = epoch;
    while (reason[i] && i + 1U < DLUMA_FRONTEND_ERROR_REASON_CAPACITY) {
        state->reason[i] = reason[i];
        ++i;
    }
    state->reason[i] = 0;
    state->blocked = 1U;
    return 1;
}

int dluma_frontend_error_request_copy(DlumaFrontendErrorState *state)
{
    if (!state || !state->blocked) return 0;
    ++state->copy_requests;
    return 1;
}

int dluma_frontend_error_request_quit(DlumaFrontendErrorState *state)
{
    if (!state || !state->blocked || state->quit_requested) return 0;
    state->quit_requested = 1U;
    return 1;
}

void dluma_frontend_error_note_presentation(DlumaFrontendErrorState *state, int shown)
{
    if (!state || !state->blocked) return;
    state->presentation_attempted = 1U;
    state->presented = shown ? 1U : 0U;
}
