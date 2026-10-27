#ifndef DLUMA_MOD_ROW_FORMAT_CORE_H
#define DLUMA_MOD_ROW_FORMAT_CORE_H

/* These values are intentionally byte bounds because the Chrome HTML list
 * presenter measures UTF-8 input.  The helper below always backs up to a
 * complete UTF-8 sequence before appending the visual ellipsis. */
#define DLUMA_MOD_ROW_TITLE_RENDER_BYTES 24U
#define DLUMA_MOD_ROW_VERSION_RENDER_BYTES 12U
#define DLUMA_MOD_ROW_SECONDARY_ASSOCIATION_COUNT 2U

typedef struct DlumaModRowAssociationPlan {
    int primary_association;
    int secondary_associations[DLUMA_MOD_ROW_SECONDARY_ASSOCIATION_COUNT];
    unsigned int secondary_count;
} DlumaModRowAssociationPlan;

int dluma_mod_row_format_utf8_ellipsis(
    const char *source,
    unsigned int maximum_bytes,
    char *out,
    unsigned int capacity);

const char *dluma_mod_row_format_version(const char *version);

void dluma_mod_row_format_association_plan(DlumaModRowAssociationPlan *out);

/* The native ListBox recreates items during ShowScreen/PostUpdate.  A stored
 * pointer is therefore only a cache; callers must use their stable list index
 * and accept a pointer only after this bounded, pure precondition check. */
int dluma_mod_row_format_accept_live_item(
    int list_count, int list_index, const void *live_item);

/* Rebind a cached mapped row to the item presently returned by the native
 * list.  The cache remains untouched when the list/index/pointer contract is
 * not valid, so a stale object can never become authoritative. */
int dluma_mod_row_format_rebind_live_item(
    int list_count, int list_index, void *live_item, void **cached_item);

#endif
