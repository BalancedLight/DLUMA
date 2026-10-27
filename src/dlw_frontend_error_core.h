#ifndef DLUMA_FRONTEND_ERROR_CORE_H
#define DLUMA_FRONTEND_ERROR_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_FRONTEND_ERROR_REASON_CAPACITY 384U
typedef struct DlumaFrontendErrorState {
    unsigned long long epoch;
    unsigned int blocked;
    unsigned int presentation_attempted;
    unsigned int presented;
    unsigned int copy_requests;
    unsigned int quit_requested;
    unsigned int quit_delivered;
    unsigned int reason_capture_failures;
    unsigned int presentation_queue_failures;
    char reason[DLUMA_FRONTEND_ERROR_REASON_CAPACITY];
} DlumaFrontendErrorState;

/* The caller serializes access. Failure is process-lifetime: hiding or closing
 * its panel never restores launch admission or requests process termination. */
int dluma_frontend_error_latch(DlumaFrontendErrorState *state,
    const char *reason, unsigned long long epoch);
int dluma_frontend_error_request_copy(DlumaFrontendErrorState *state);
int dluma_frontend_error_request_quit(DlumaFrontendErrorState *state);
void dluma_frontend_error_note_presentation(DlumaFrontendErrorState *state, int shown);

#ifdef __cplusplus
}
#endif
#endif
