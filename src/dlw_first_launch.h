#ifndef DLUMA_FIRST_LAUNCH_H
#define DLUMA_FIRST_LAUNCH_H

int dluma_first_launch_is_pending(void);

/* Acceptance applies immediately for this session. The return value reports
 * whether it was also saved for later launches. */
int dluma_first_launch_accept(void);

#endif
