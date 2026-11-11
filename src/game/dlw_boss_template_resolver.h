#ifndef DLW_BOSS_TEMPLATE_RESOLVER_H
#define DLW_BOSS_TEMPLATE_RESOLVER_H
#include <stddef.h>
#include "../core/dlw_boss_scheduler.h"

typedef enum DlumaBossTemplateResult { DLUMA_BT_OK, DLUMA_BT_INVALID } DlumaBossTemplateResult;
typedef struct DlumaBossTemplateSpec {
    const char *flash_phase, *chatter_phase, *chatter_group;
    const void *flash_vtable, *chatter_vtable;
    const char *start_phase;
    const void *start_instruction, *start_vtable;
    const void *transition_vtable;
} DlumaBossTemplateSpec;
typedef int (*DlumaBossReadable)(const void *, size_t, void *);
typedef void (*DlumaBossTemplateVisitor)(void *child, void *context);

/* CBC7 Player only. The caller supplies exact-profile vtables, a native
 * receiver-scoped lifetime borrow, fault containment, and serialization.
 * Both templates must occur after the unique Start phase in the native vector;
 * the supplied Start identity must occur exactly once in that phase's vector.
 * A readability check does not pin engine memory. The visitor runs during
 * this call only; no child or phase pointer is retained by this function.
 * Choice NONE validates the full pair without invoking a visitor (admission).
 * This disabled component does not install a hook or execute stock code. */
DlumaBossTemplateResult dluma_boss_template_visit(void *owner, const DlumaBossTemplateSpec *, DlumaBossChoice, DlumaBossReadable, void *read_context, DlumaBossTemplateVisitor, void *visit_context);
#endif
