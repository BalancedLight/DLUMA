#include "../src/dlw_frontend_error_core.h"
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { printf("FAIL line %d: %s\n", __LINE__, #value); return 1; } } while (0)

int main(void)
{
    DlumaFrontendErrorState state = {0};
    char long_reason[768];
    DlumaFrontendErrorState bounded = {0};
    CHECK(!dluma_frontend_error_request_copy(&state));
    CHECK(!dluma_frontend_error_request_quit(&state));
    CHECK(!dluma_frontend_error_latch(&state, "", 0));
    CHECK(!state.blocked && !state.quit_requested);

    CHECK(dluma_frontend_error_latch(&state, "audio_provider_still_owned", 17));
    CHECK(state.blocked && state.epoch == 17);
    CHECK(!state.quit_requested && !state.quit_delivered);
    dluma_frontend_error_note_presentation(&state, 0);
    CHECK(state.blocked && state.presentation_attempted && !state.presented);
    CHECK(!state.quit_requested); /* Failed presentation cannot close Player. */
    dluma_frontend_error_note_presentation(&state, 1);
    CHECK(state.presented && !state.quit_requested);
    dluma_frontend_error_note_presentation(&state, 0);
    CHECK(state.blocked && !state.presented && !state.quit_requested);
    CHECK(dluma_frontend_error_request_copy(&state));
    CHECK(state.copy_requests == 1 && !state.quit_requested);

    CHECK(dluma_frontend_error_latch(&state, "later_unrelated_failure", 18));
    CHECK(state.epoch == 17 && strcmp(state.reason, "audio_provider_still_owned") == 0);
    CHECK(state.blocked && !state.quit_requested);
    CHECK(dluma_frontend_error_request_quit(&state));
    CHECK(state.quit_requested && !state.quit_delivered);
    CHECK(!dluma_frontend_error_request_quit(&state));

    memset(long_reason, 'x', sizeof(long_reason));
    long_reason[sizeof(long_reason)-1] = 0;
    CHECK(dluma_frontend_error_latch(&bounded, long_reason, 1));
    CHECK(strlen(bounded.reason) == DLUMA_FRONTEND_ERROR_REASON_CAPACITY-1U);
    CHECK(!bounded.quit_requested);
    puts("frontend error state: fail-closed admission, persistent blocker, no automatic quit, one user quit, bounded diagnostics passed");
    return 0;
}
