#ifndef DLW_BOSS_RECEIVER_ROUTE_H
#define DLW_BOSS_RECEIVER_ROUTE_H
#include "../dlw_common.h"

/* These are OnPostUpdate, OnDestroy and scenario cleanup, not a deleting
 * destructor. Exact bodies consume only RCX and their native interface is void. */
typedef void (__fastcall *DlumaBossReceiverMethod)(void *);
typedef struct DlumaBossReceiverCallbacks {
    int (*begin)(void *owner);
    void (*update)(void *owner);
    void (*end)(void *owner);
    void (*retire)(void *owner);
} DlumaBossReceiverCallbacks;
typedef struct DlumaBossReceiverBinding {
    HMODULE module;
    void **cells[3]; /* OnPostUpdate, OnDestroy, scenario cleanup. */
    DlumaBossReceiverMethod originals[3];
    const void *owner_vtable;
} DlumaBossReceiverBinding;

/* Caller first qualifies exact module/code/cell identity. Once published, the
 * module pins and pass-through targets last until process exit. Stop never
 * frees them: an engine thread may have fetched a wrapper before cell restore.
 * Begin/End delimit the normal engine-owned receiver borrow, including original
 * instruction execution. Custom work is excluded on nested/concurrent updates.
 */
int dluma_boss_receiver_route_start(const DlumaBossReceiverBinding *, const DlumaBossReceiverCallbacks *);
int dluma_boss_receiver_route_ready(void);
int dluma_boss_receiver_route_stop(void);
#endif
