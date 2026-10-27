#include "dlw_mod_row_format_core.h"

static unsigned int dluma_mod_row_format_length(const char *text)
{
    unsigned int length = 0;
    if (!text)
        return 0;
    while (text[length])
        ++length;
    return length;
}

int dluma_mod_row_format_utf8_ellipsis(
    const char *source,
    unsigned int maximum_bytes,
    char *out,
    unsigned int capacity)
{
    unsigned int source_length;
    unsigned int copied;
    unsigned int index;
    int truncated;
    if (!out || capacity < 2U)
        return 0;
    out[0] = 0;
    source = source ? source : "";
    source_length = dluma_mod_row_format_length(source);
    copied = source_length;
    if (copied > maximum_bytes)
        copied = maximum_bytes;
    while (copied > 0U &&
           (((unsigned char)source[copied]) & 0xC0U) == 0x80U)
        --copied;
    truncated = source_length > copied;
    if (copied + (truncated ? 3U : 0U) >= capacity)
        return 0;
    for (index = 0; index < copied; ++index)
        out[index] = source[index];
    if (truncated)
    {
        out[copied++] = '.';
        out[copied++] = '.';
        out[copied++] = '.';
    }
    out[copied] = 0;
    return 1;
}

const char *dluma_mod_row_format_version(const char *version)
{
    if (!version || !version[0])
        return "UNKNOWN";
    return version;
}

void dluma_mod_row_format_association_plan(DlumaModRowAssociationPlan *out)
{
    if (!out)
        return;
    out->primary_association = 0;
    /* ListBoxHtmlV's renderer uses the primary data association plus its
     * secondary text presenters.  Clear both secondary slots after replacing
     * the primary table so a stock/project-id continuation cannot survive a
     * row recreation. */
    out->secondary_associations[0] = 1;
    out->secondary_associations[1] = 2;
    out->secondary_count = DLUMA_MOD_ROW_SECONDARY_ASSOCIATION_COUNT;
}

int dluma_mod_row_format_accept_live_item(
    int list_count, int list_index, const void *live_item)
{
    return live_item && list_index >= 0 && list_index < list_count;
}

int dluma_mod_row_format_rebind_live_item(
    int list_count, int list_index, void *live_item, void **cached_item)
{
    if (!cached_item || !dluma_mod_row_format_accept_live_item(
                            list_count, list_index, live_item))
        return 0;
    *cached_item = live_item;
    return 1;
}
