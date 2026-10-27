#ifndef DLUMA_NEW_MOD_TICKER_H
#define DLUMA_NEW_MOD_TICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Captures the immutable startup discovery result after all three language
 * registries have completed their synchronous initial scan. */
void dluma_new_mod_ticker_capture_startup(void);

/* Returns nonzero only until the captured batch has been accepted by the
 * validated stock news queue once in this process. */
int dluma_new_mod_ticker_has_pending(void);

/* Records that the notice was accepted by the stock queue, then persists
 * exact-fingerprint announcement markers. Persistence failure is retryable
 * on a later process but never duplicates the notice in this process. */
int dluma_new_mod_ticker_mark_presented(void);

#ifdef __cplusplus
}
#endif

#endif
