#ifndef DLW_NIGHTLY_H
#define DLW_NIGHTLY_H

typedef enum DlwNightlyKind {
    DLW_NIGHTLY_NONE, DLW_NIGHTLY_UPDATE, DLW_NIGHTLY_RESTORE, DLW_NIGHTLY_ERROR,
    DLW_NIGHTLY_DOWNLOADING, DLW_NIGHTLY_READY
} DlwNightlyKind;
typedef struct DlwNightlyOffer {
    DlwNightlyKind kind;
    char commit[41];
    unsigned int ticket;
} DlwNightlyOffer;
void dlw_nightly_initialize(int enabled);
void dlw_nightly_set_enabled(int enabled);
const DlwNightlyOffer* dlw_nightly_get_offer(void);
void dlw_nightly_respond(int accept);
void dlw_nightly_respond_to(unsigned int ticket, int accept);
void dlw_nightly_tick(void);
const char *dlw_nightly_status_text(void);
void dlw_nightly_shutdown(void);
#endif
