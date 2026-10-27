#include "dlw_common.h"
#include "dlw_path_security.h"
#include "dlw_quest_hud.h"

#include <limits.h>
#include <string.h>

#ifndef MB_ERR_INVALID_CHARS
#define MB_ERR_INVALID_CHARS 0x00000008UL
#endif
#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0x00000080UL
#endif

/*
 * Presentation-only bridge for the stock HudQuestObjective component.
 *
 * The named macOS build supplied semantic names only.  Every address, layout,
 * prefix and vtable entry below was recovered independently from the exact
 * Windows Player PE identified by both full file hashes.
 */
#define DLUMA_QH_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_QH_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_QH_ENGINE_SHA256 \
    "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c"
#define DLUMA_QH_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_QH_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_QH_GAMEDLL_SHA256 \
    "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"

#define DLUMA_QH_VTABLE_RVA 0x015DA018UL
#define DLUMA_QH_COL_RVA 0x01A6B718UL
#define DLUMA_QH_POST_SLOT_OFFSET 0x0390UL
#define DLUMA_QH_POST_SLOT_RVA 0x015DA3A8UL
#define DLUMA_QH_POST_ORIGINAL_RVA 0x01309812UL
#define DLUMA_QH_INITIALIZE_SLOT_OFFSET 0x03F8UL
#define DLUMA_QH_INITIALIZE_RVA 0x00517CD0UL
#define DLUMA_QH_UPDATE_RVA 0x005188E0UL
#define DLUMA_QH_TIMER_UPDATE_RVA 0x00519D50UL
#define DLUMA_QH_APPLY_ENTRY_RVA 0x0051AB20UL
#define DLUMA_QH_SET_TEXT_CLAMP_RVA 0x00534E10UL

#define DLUMA_QH_ROWS_OFFSET 88U
#define DLUMA_QH_ROW_COUNT_OFFSET 96U
#define DLUMA_QH_HEADER_TEXT_OFFSET 128U
#define DLUMA_QH_HEADER_CLAMP_OFFSET 136U
#define DLUMA_QH_TIMER_GROUP_OFFSET 144U
#define DLUMA_QH_TIMER_TEXT_OFFSET 160U
#define DLUMA_QH_TIMER_VISIBLE_OFFSET 168U
#define DLUMA_QH_HEADER_SOURCE_OFFSET 208U
#define DLUMA_QH_ENTRIES_OFFSET 240U
#define DLUMA_QH_ENTRY_COUNT_OFFSET 248U
#define DLUMA_QH_ROW_SIZE 64U
#define DLUMA_QH_ENTRY_SIZE 88U
#define DLUMA_QH_ENTRY_TEXT_OFFSET 8U
#define DLUMA_QH_ENTRY_ROW_OFFSET 48U
#define DLUMA_QH_ENTRY_PHASE_UID_OFFSET 80U
#define DLUMA_QH_MAX_ENTRIES 32U
#define DLUMA_QH_MAX_OVERRIDES 128U
#define DLUMA_QH_MAX_TARGETS 6U
#define DLUMA_QH_MAX_SOURCE 256U
#define DLUMA_QH_MAX_WIDE_UNITS 4096U
#define DLUMA_QH_STOP_DRAIN_MS 2000ULL

typedef struct DlumaQhString {
    const char *data;
    uint32_t size;
    uint32_t capacity;
} DlumaQhString;

typedef struct DlumaQhWideString {
    const WCHAR *data;
    uint32_t size;
    uint32_t capacity;
} DlumaQhWideString;

typedef void (__fastcall *DlumaQhPostUpdateFn)(void *component);
typedef void (__fastcall *DlumaQhSetTextClampFn)(
    void *text, const DlumaQhWideString *value, void *clamp);
typedef const DlumaQhWideString *(__fastcall *DlumaQhGetTextFn)(void *text);
typedef void (__fastcall *DlumaQhSetTextFn)(
    void *text, const DlumaQhWideString *value);

typedef struct DlumaQhRawTarget {
    uint32_t kind;
    uint32_t slot;
    uintptr_t cookie;
    unsigned char phase_uid[5];
    char source[DLUMA_QH_MAX_SOURCE];
    char current_text[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U];
    void *element;
    void *clamp;
} DlumaQhRawTarget;

typedef struct DlumaQhTarget {
    uint32_t active;
    uint32_t kind;
    uint32_t slot;
    uint32_t last_overridden;
    uintptr_t cookie;
    unsigned char phase_uid[5];
    char source[DLUMA_QH_MAX_SOURCE];
    char id[DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID];
    char native_text[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U];
    void *element;
    void *clamp;
} DlumaQhTarget;

typedef struct DlumaQhOverride {
    uint32_t active;
    uint32_t map_generation;
    uint64_t owner_id;
    uint64_t override_id;
    uint64_t sequence;
    char target_id[DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID];
    char text[DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT + 1U];
} DlumaQhOverride;

typedef struct DlumaQhApply {
    uint32_t kind;
    void *element;
    void *clamp;
    char text[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U];
} DlumaQhApply;

typedef struct DlumaQuestHudState {
    HMODULE engine;
    HMODULE gamedll;
    BYTE *engine_base;
    BYTE *gamedll_base;
    DWORD engine_image_size;
    DWORD gamedll_image_size;
    void **post_slot;
    DlumaQhPostUpdateFn post_original;
    DlumaQhSetTextClampFn set_text_clamp;
    DlumaQhGetTextFn get_text;
    DlumaQhSetTextFn set_text;
    volatile LONG lock;
    volatile LONG stopping;
    volatile LONG active_calls;
    uint32_t installed;
    uint32_t map_generation;
    uint32_t timer_was_visible;
    uint64_t timer_session;
    uint64_t next_target_id;
    uint64_t next_override_id;
    uint64_t next_sequence;
    void *last_component;
    DlumaQhTarget targets[DLUMA_QH_MAX_TARGETS];
    DlumaQhOverride overrides[DLUMA_QH_MAX_OVERRIDES];
    DlumaLua54QuestHudSnapshot snapshot;
} DlumaQuestHudState;

static DlumaQuestHudState g_dluma_qh;

static const BYTE g_dluma_qh_post_prefix[] = {
    0xFF, 0x25, 0x68, 0x4D, 0x0C, 0x00, 0xFF, 0x25,
    0x6A, 0x4D, 0x0C, 0x00, 0xFF, 0x25, 0x6C, 0x4D};
static const BYTE g_dluma_qh_initialize_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
static const BYTE g_dluma_qh_update_prefix[] = {
    0x40, 0x55, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
    0x24, 0xD0, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};
static const BYTE g_dluma_qh_timer_prefix[] = {
    0x40, 0x55, 0x56, 0x57, 0x48, 0x8B, 0xEC, 0x48,
    0x83, 0xEC, 0x50, 0x48, 0x83, 0xB9, 0x90, 0x00, 0x00, 0x00};
static const BYTE g_dluma_qh_apply_entry_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
static const BYTE g_dluma_qh_set_clamp_prefix[] = {
    0x48, 0x85, 0xC9, 0x0F, 0x84, 0x08, 0x01, 0x00,
    0x00, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89,
    0x74, 0x24, 0x10, 0x57};

static void __fastcall dluma_qh_post_hook(void *component);

static void dluma_qh_lock(void)
{
    while (dluma_atomic_compare_exchange(&g_dluma_qh.lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_qh_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_qh.lock, 0);
}

static int dluma_qh_equal(const char *left, const char *right)
{
    if (!left || !right)
        return left == right;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static void dluma_qh_error(
    DlumaLua54Error *error, const char *code, const char *message,
    const char *permission, const char *object_name, int retryable)
{
    if (!error || error->struct_size < sizeof(*error))
        return;
    error->retryable = retryable ? 1U : 0U;
    dluma_str_copy(error->code, DLUMA_ARRAY_COUNT(error->code),
                 code ? code : "quest_hud_error");
    dluma_str_copy(error->message, DLUMA_ARRAY_COUNT(error->message),
                 message ? message : "quest HUD adapter failed");
    dluma_str_copy(error->capability, DLUMA_ARRAY_COUNT(error->capability),
                 "ui.quest_hud");
    dluma_str_copy(error->permission, DLUMA_ARRAY_COUNT(error->permission),
                 permission ? permission : "");
    dluma_str_copy(error->object_name, DLUMA_ARRAY_COUNT(error->object_name),
                 object_name ? object_name : "");
}

static int dluma_qh_pe_identity(
    HMODULE module, DWORD *timestamp_out, DWORD *image_size_out)
{
    const BYTE *image;
    const BYTE *nt;
    const BYTE *optional;
    DWORD pe_offset;
    if (timestamp_out) *timestamp_out = 0;
    if (image_size_out) *image_size_out = 0;
    if (!module || !dluma_is_readable(module, 0x40U))
        return 0;
    image = (const BYTE *)module;
    if (*(const WORD *)image != 0x5A4DU)
        return 0;
    pe_offset = *(const DWORD *)(image + 0x3CU);
    if (pe_offset < 0x40U || pe_offset > 0x100000U ||
        !dluma_is_readable(image + pe_offset, 0x80U))
        return 0;
    nt = image + pe_offset;
    optional = nt + 0x18U;
    if (*(const DWORD *)nt != 0x00004550UL ||
        *(const WORD *)optional != 0x020BU)
        return 0;
    if (timestamp_out) *timestamp_out = *(const DWORD *)(nt + 8U);
    if (image_size_out) *image_size_out = *(const DWORD *)(optional + 0x38U);
    return 1;
}

static int dluma_qh_module_hash_matches(
    HMODULE module, const char *expected)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    if (!module || !expected)
        return 0;
    if (!dluma_get_module_path_checked(
            module, path, DLUMA_ARRAY_COUNT(path)))
        return 0;
    digest[0] = 0;
    return dluma_sha256_file_hex(
               path, 1024ULL * 1024ULL * 1024ULL, digest) &&
           dluma_stricmp(digest, expected) == 0;
}

static int dluma_qh_bytes(
    const void *address, const BYTE *expected, uint32_t size)
{
    return address && expected && size &&
           dluma_is_readable(address, size) &&
           memcmp(address, expected, size) == 0;
}

static int dluma_qh_pointer_in_image(
    const void *pointer, const BYTE *base, DWORD size)
{
    uintptr_t value = (uintptr_t)pointer;
    uintptr_t begin = (uintptr_t)base;
    return pointer && base && value >= begin && value - begin < size;
}

static int dluma_qh_text_element_valid(void *element)
{
    void *vtable;
    const DlumaQhWideString *value;
    if (!element || !g_dluma_qh.get_text ||
        !dluma_is_readable(element, sizeof(vtable)))
        return 0;
    vtable = *(void **)element;
    if (!dluma_qh_pointer_in_image(
            vtable, g_dluma_qh.engine_base, g_dluma_qh.engine_image_size))
        return 0;
    value = g_dluma_qh.get_text(element);
    if (!value || !dluma_is_readable(value, sizeof(*value)) ||
        value->size > value->capacity ||
        value->capacity > DLUMA_QH_MAX_WIDE_UNITS)
        return 0;
    return !value->size ||
           (value->data &&
            dluma_is_readable(value->data,
                            (SIZE_T)value->size * sizeof(WCHAR)));
}

static int dluma_qh_wide_to_utf8(
    const WCHAR *data, uint32_t size, char *output, uint32_t capacity)
{
    int needed;
    if (!output || !capacity || size > (uint32_t)INT_MAX)
        return 0;
    output[0] = 0;
    if (!size)
        return 1;
    if (!data || !dluma_is_readable(data, (SIZE_T)size * sizeof(WCHAR)))
        return 0;
    needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, data, (int)size,
        0, 0, 0, 0);
    if (needed <= 0 || (uint32_t)needed >= capacity)
        return 0;
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, data, (int)size,
            output, needed, 0, 0) != needed)
        return 0;
    output[needed] = 0;
    return 1;
}

static int dluma_qh_read_text(
    void *element, char output[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U])
{
    const DlumaQhWideString *value;
    if (!dluma_qh_text_element_valid(element))
        return 0;
    value = g_dluma_qh.get_text(element);
    return dluma_qh_wide_to_utf8(
        value->data, value->size, output,
        DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U);
}

static int dluma_qh_read_entry_text(
    const BYTE *entry,
    char output[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U])
{
    const DlumaQhWideString *value =
        (const DlumaQhWideString *)(entry + DLUMA_QH_ENTRY_TEXT_OFFSET);
    if (!entry || !dluma_is_readable(value, sizeof(*value)) ||
        value->size > value->capacity ||
        value->capacity > DLUMA_QH_MAX_WIDE_UNITS)
        return 0;
    return dluma_qh_wide_to_utf8(
        value->data, value->size, output,
        DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U);
}

static int dluma_qh_read_source(
    const BYTE *component, char output[DLUMA_QH_MAX_SOURCE])
{
    const DlumaQhString *source =
        (const DlumaQhString *)(component + DLUMA_QH_HEADER_SOURCE_OFFSET);
    if (!component || !output || !dluma_is_readable(source, sizeof(*source)) ||
        source->size >= DLUMA_QH_MAX_SOURCE ||
        source->size > source->capacity)
        return 0;
    output[0] = 0;
    if (!source->size)
        return 1;
    if (!source->data || !dluma_is_readable(source->data, source->size))
        return 0;
    memcpy(output, source->data, source->size);
    output[source->size] = 0;
    return 1;
}

static int dluma_qh_component_layout(
    void *component, BYTE **rows_out, uint32_t *row_count_out,
    BYTE **entries_out, uint32_t *entry_count_out)
{
    BYTE *bytes = (BYTE *)component;
    void *vtable;
    BYTE *rows;
    BYTE *entries;
    int32_t row_count;
    int32_t entry_count;
    if (!component || !g_dluma_qh.installed || g_dluma_qh.stopping ||
        !dluma_is_readable(component, DLUMA_QH_ENTRY_COUNT_OFFSET + 4U))
        return 0;
    vtable = *(void **)component;
    if (vtable != g_dluma_qh.gamedll_base + DLUMA_QH_VTABLE_RVA ||
        !g_dluma_qh.post_slot ||
        !dluma_is_readable(g_dluma_qh.post_slot, sizeof(void *)) ||
        *g_dluma_qh.post_slot != (void *)dluma_qh_post_hook)
        return 0;
    rows = *(BYTE **)(bytes + DLUMA_QH_ROWS_OFFSET);
    row_count = *(int32_t *)(bytes + DLUMA_QH_ROW_COUNT_OFFSET);
    entries = *(BYTE **)(bytes + DLUMA_QH_ENTRIES_OFFSET);
    entry_count = *(int32_t *)(bytes + DLUMA_QH_ENTRY_COUNT_OFFSET);
    if (row_count < 0 ||
        row_count > (int32_t)DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES ||
        entry_count < 0 || entry_count > (int32_t)DLUMA_QH_MAX_ENTRIES)
        return 0;
    if (row_count &&
        (!rows ||
         !dluma_is_readable(rows, (SIZE_T)row_count * DLUMA_QH_ROW_SIZE)))
        return 0;
    if (entry_count &&
        (!entries ||
         !dluma_is_readable(entries, (SIZE_T)entry_count * DLUMA_QH_ENTRY_SIZE)))
        return 0;
    if (rows_out) *rows_out = rows;
    if (row_count_out) *row_count_out = (uint32_t)row_count;
    if (entries_out) *entries_out = entries;
    if (entry_count_out) *entry_count_out = (uint32_t)entry_count;
    return 1;
}

static uint32_t dluma_qh_collect_raw(
    void *component, DlumaQhRawTarget raw[DLUMA_QH_MAX_TARGETS],
    int *timer_visible_out)
{
    BYTE *bytes = (BYTE *)component;
    BYTE *rows = 0;
    BYTE *entries = 0;
    uint32_t row_count = 0;
    uint32_t entry_count = 0;
    uint32_t count = 0;
    uint32_t index;
    void *header;
    void *timer;
    if (timer_visible_out) *timer_visible_out = 0;
    if (!raw ||
        !dluma_qh_component_layout(
            component, &rows, &row_count, &entries, &entry_count))
        return 0;
    memset(raw, 0, sizeof(DlumaQhRawTarget) * DLUMA_QH_MAX_TARGETS);

    header = *(void **)(bytes + DLUMA_QH_HEADER_TEXT_OFFSET);
    if (header && dluma_qh_text_element_valid(header)) {
        DlumaQhRawTarget *target = &raw[count];
        target->kind = DLUMA_LUA54_QUEST_HUD_HEADER;
        target->cookie = (uintptr_t)component;
        target->element = header;
        target->clamp = *(void **)(bytes + DLUMA_QH_HEADER_CLAMP_OFFSET);
        if (dluma_qh_read_source(bytes, target->source) &&
            target->source[0] &&
            dluma_qh_read_text(header, target->current_text) &&
            target->current_text[0])
            ++count;
    }

    for (index = 0;
         index < entry_count &&
         count < 1U + DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES;
         ++index) {
        BYTE *entry = entries + (SIZE_T)index * DLUMA_QH_ENTRY_SIZE;
        BYTE *row = *(BYTE **)(entry + DLUMA_QH_ENTRY_ROW_OFFSET);
        uintptr_t row_address;
        uintptr_t rows_address;
        uintptr_t row_delta;
        DlumaQhRawTarget *target;
        uint32_t slot;
        row_address = (uintptr_t)row;
        rows_address = (uintptr_t)rows;
        if (!row || !rows || row_address < rows_address)
            continue;
        row_delta = row_address - rows_address;
        if (row_delta % DLUMA_QH_ROW_SIZE != 0 ||
            row_delta / DLUMA_QH_ROW_SIZE >= row_count)
            continue;
        slot = (uint32_t)(row_delta / DLUMA_QH_ROW_SIZE);
        if (!*(unsigned char *)(row + 56U) ||
            !*(unsigned char *)(row + 58U) ||
            !dluma_qh_text_element_valid(*(void **)(row + 8U)))
            continue;
        target = &raw[count];
        target->kind = DLUMA_LUA54_QUEST_HUD_OBJECTIVE;
        target->slot = slot + 1U;
        target->cookie = (uintptr_t)entry;
        target->element = *(void **)(row + 8U);
        target->clamp = *(void **)(row + 16U);
        memcpy(target->phase_uid, entry + DLUMA_QH_ENTRY_PHASE_UID_OFFSET,
               sizeof(target->phase_uid));
        if (dluma_qh_read_entry_text(entry, target->current_text) &&
            target->current_text[0])
            ++count;
    }

    timer = *(void **)(bytes + DLUMA_QH_TIMER_TEXT_OFFSET);
    if (count < DLUMA_QH_MAX_TARGETS && timer &&
        *(void **)(bytes + DLUMA_QH_TIMER_GROUP_OFFSET) &&
        *(unsigned char *)(bytes + DLUMA_QH_TIMER_VISIBLE_OFFSET) &&
        dluma_qh_read_text(timer, raw[count].current_text) &&
        raw[count].current_text[0]) {
        raw[count].kind = DLUMA_LUA54_QUEST_HUD_TIMER;
        raw[count].cookie = (uintptr_t)component;
        raw[count].element = timer;
        if (timer_visible_out) *timer_visible_out = 1;
        ++count;
    }
    return count;
}

static int dluma_qh_base_identity_equal(
    const DlumaQhTarget *target, const DlumaQhRawTarget *raw,
    uint64_t timer_session)
{
    if (!target || !target->active || !raw ||
        target->kind != raw->kind)
        return 0;
    if (raw->kind == DLUMA_LUA54_QUEST_HUD_TIMER)
        return target->cookie == (uintptr_t)timer_session;
    if (target->cookie != raw->cookie)
        return 0;
    if (raw->kind == DLUMA_LUA54_QUEST_HUD_HEADER)
        return dluma_qh_equal(target->source, raw->source);
    return memcmp(target->phase_uid, raw->phase_uid,
                  sizeof(target->phase_uid)) == 0;
}

static const DlumaQhOverride *dluma_qh_winner_locked(const char *target_id)
{
    const DlumaQhOverride *winner = 0;
    uint32_t index;
    for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index) {
        const DlumaQhOverride *candidate = &g_dluma_qh.overrides[index];
        if (!candidate->active ||
            candidate->map_generation != g_dluma_qh.map_generation ||
            !dluma_qh_equal(candidate->target_id, target_id))
            continue;
        if (!winner || candidate->sequence > winner->sequence)
            winner = candidate;
    }
    return winner;
}

static int dluma_qh_target_id_present_locked(const char *target_id)
{
    uint32_t index;
    if (!target_id || !target_id[0])
        return 0;
    for (index = 0; index < DLUMA_QH_MAX_TARGETS; ++index)
        if (g_dluma_qh.targets[index].active &&
            dluma_qh_equal(g_dluma_qh.targets[index].id, target_id))
            return 1;
    return 0;
}

static void dluma_qh_make_id(char output[DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID])
{
    char number[32];
    uint64_t value = ++g_dluma_qh.next_target_id;
    if (!value)
        value = ++g_dluma_qh.next_target_id;
    output[0] = 0;
    dluma_str_append(
        output, DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID, "quest-hud-");
    dluma_u64_to_text(g_dluma_qh.map_generation, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(output, DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID, number);
    dluma_str_append(output, DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID, "-");
    dluma_u64_to_text(value, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(output, DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID, number);
}

static void dluma_qh_fill_public(
    DlumaLua54QuestHudTarget *output, const DlumaQhTarget *target,
    const DlumaQhOverride *winner)
{
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    output->kind = target->kind;
    output->slot = target->slot;
    output->overridden = winner ? 1U : 0U;
    dluma_str_copy(output->id, DLUMA_ARRAY_COUNT(output->id), target->id);
    dluma_str_copy(output->text, DLUMA_ARRAY_COUNT(output->text),
                 target->native_text);
    dluma_str_copy(output->display_text, DLUMA_ARRAY_COUNT(output->display_text),
                 winner ? winner->text : target->native_text);
}

static void dluma_qh_publish_empty_locked(void)
{
    DlumaLua54QuestHudSnapshot empty;
    uint64_t revision = g_dluma_qh.snapshot.revision;
    uint32_t index;
    memset(&empty, 0, sizeof(empty));
    empty.struct_size = sizeof(empty);
    empty.map_generation = g_dluma_qh.map_generation;
    empty.revision = revision;
    if (memcmp(&empty, &g_dluma_qh.snapshot, sizeof(empty)) != 0) {
        ++revision;
        if (!revision) ++revision;
        empty.revision = revision;
    }
    g_dluma_qh.snapshot = empty;
    memset(g_dluma_qh.targets, 0, sizeof(g_dluma_qh.targets));
    for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index)
        g_dluma_qh.overrides[index].active = 0;
    g_dluma_qh.timer_was_visible = 0;
    g_dluma_qh.last_component = 0;
}

static uint32_t dluma_qh_reconcile_locked(
    void *component, const DlumaQhRawTarget *raw, uint32_t raw_count,
    int timer_visible, DlumaQhApply apply[DLUMA_QH_MAX_TARGETS])
{
    DlumaQhTarget previous[DLUMA_QH_MAX_TARGETS];
    DlumaQhTarget next[DLUMA_QH_MAX_TARGETS];
    DlumaLua54QuestHudSnapshot snapshot;
    uint32_t previous_used[DLUMA_QH_MAX_TARGETS];
    uint32_t index;
    uint32_t apply_count = 0;
    uint32_t objective_index = 0;
    uint64_t revision = g_dluma_qh.snapshot.revision;
    memcpy(previous, g_dluma_qh.targets, sizeof(previous));
    memset(previous_used, 0, sizeof(previous_used));
    memset(next, 0, sizeof(next));
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.map_generation = g_dluma_qh.map_generation;
    snapshot.revision = revision;
    if (timer_visible && !g_dluma_qh.timer_was_visible) {
        ++g_dluma_qh.timer_session;
        if (!g_dluma_qh.timer_session) ++g_dluma_qh.timer_session;
    }
    g_dluma_qh.timer_was_visible = timer_visible ? 1U : 0U;

    for (index = 0; index < raw_count && index < DLUMA_QH_MAX_TARGETS; ++index) {
        const DlumaQhRawTarget *source = &raw[index];
        DlumaQhTarget *target = &next[index];
        const DlumaQhOverride *winner;
        int matched = -1;
        uint32_t scan;
        char native_text[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U];
        dluma_str_copy(native_text, DLUMA_ARRAY_COUNT(native_text),
                     source->current_text);
        for (scan = 0; scan < DLUMA_QH_MAX_TARGETS; ++scan) {
            if (previous_used[scan] ||
                !dluma_qh_base_identity_equal(
                    &previous[scan], source, g_dluma_qh.timer_session))
                continue;
            if (source->kind == DLUMA_LUA54_QUEST_HUD_HEADER &&
                previous[scan].last_overridden &&
                dluma_qh_equal(
                    source->current_text,
                    g_dluma_qh.snapshot.header.display_text))
                dluma_str_copy(native_text, DLUMA_ARRAY_COUNT(native_text),
                             previous[scan].native_text);
            if (source->kind != DLUMA_LUA54_QUEST_HUD_TIMER &&
                !dluma_qh_equal(previous[scan].native_text, native_text))
                continue;
            matched = (int)scan;
            previous_used[scan] = 1;
            break;
        }
        target->active = 1;
        target->kind = source->kind;
        target->slot = source->slot;
        target->cookie = source->kind == DLUMA_LUA54_QUEST_HUD_TIMER
            ? (uintptr_t)g_dluma_qh.timer_session : source->cookie;
        target->element = source->element;
        target->clamp = source->clamp;
        memcpy(target->phase_uid, source->phase_uid,
               sizeof(target->phase_uid));
        dluma_str_copy(target->source, DLUMA_ARRAY_COUNT(target->source),
                     source->source);
        dluma_str_copy(target->native_text,
                     DLUMA_ARRAY_COUNT(target->native_text), native_text);
        if (matched >= 0) {
            dluma_str_copy(target->id, DLUMA_ARRAY_COUNT(target->id),
                         previous[matched].id);
            target->last_overridden = previous[matched].last_overridden;
        } else {
            dluma_qh_make_id(target->id);
        }
        winner = dluma_qh_winner_locked(target->id);
        if (winner || target->last_overridden) {
            DlumaQhApply *command = &apply[apply_count++];
            command->kind = target->kind;
            command->element = target->element;
            command->clamp = target->clamp;
            dluma_str_copy(command->text, DLUMA_ARRAY_COUNT(command->text),
                         winner ? winner->text : target->native_text);
        }
        target->last_overridden = winner ? 1U : 0U;
        if (target->kind == DLUMA_LUA54_QUEST_HUD_HEADER) {
            snapshot.has_header = 1;
            dluma_qh_fill_public(&snapshot.header, target, winner);
        } else if (target->kind == DLUMA_LUA54_QUEST_HUD_OBJECTIVE &&
                   objective_index <
                       DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES) {
            dluma_qh_fill_public(
                &snapshot.objectives[objective_index++], target, winner);
        } else if (target->kind == DLUMA_LUA54_QUEST_HUD_TIMER) {
            snapshot.has_timer = 1;
            dluma_qh_fill_public(&snapshot.timer, target, winner);
        }
    }
    snapshot.objective_count = objective_index;
    memcpy(g_dluma_qh.targets, next, sizeof(next));
    for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index)
        if (g_dluma_qh.overrides[index].active &&
            !dluma_qh_target_id_present_locked(
                g_dluma_qh.overrides[index].target_id))
            g_dluma_qh.overrides[index].active = 0;
    if (memcmp(&snapshot, &g_dluma_qh.snapshot, sizeof(snapshot)) != 0) {
        ++revision;
        if (!revision) ++revision;
        snapshot.revision = revision;
    }
    g_dluma_qh.snapshot = snapshot;
    g_dluma_qh.last_component = component;
    return apply_count;
}

static int dluma_qh_utf8_to_wide(
    const char *text, WCHAR storage[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U],
    DlumaQhWideString *value)
{
    int needed;
    size_t length;
    if (!text || !storage || !value)
        return 0;
    length = strlen(text);
    if (!length || length > DLUMA_LUA54_MAX_QUEST_HUD_TEXT ||
        length > (size_t)INT_MAX)
        return 0;
    needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, 0, 0);
    if (needed <= 0 ||
        needed > (int)DLUMA_LUA54_MAX_QUEST_HUD_TEXT)
        return 0;
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length,
            storage, needed) != needed)
        return 0;
    storage[needed] = 0;
    value->data = storage;
    value->size = (uint32_t)needed;
    value->capacity = (uint32_t)needed;
    return 1;
}

static void dluma_qh_apply(const DlumaQhApply *commands, uint32_t count)
{
    uint32_t index;
    for (index = 0; index < count; ++index) {
        const DlumaQhApply *command = &commands[index];
        WCHAR wide[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1U];
        DlumaQhWideString value;
        if (!dluma_qh_text_element_valid(command->element) ||
            !dluma_qh_utf8_to_wide(command->text, wide, &value))
            continue;
        if (command->kind == DLUMA_LUA54_QUEST_HUD_TIMER)
            g_dluma_qh.set_text(command->element, &value);
        else
            g_dluma_qh.set_text_clamp(
                command->element, &value, command->clamp);
    }
}

static void dluma_qh_process_component(void *component)
{
    DlumaQhRawTarget raw[DLUMA_QH_MAX_TARGETS];
    DlumaQhApply apply[DLUMA_QH_MAX_TARGETS];
    uint32_t raw_count;
    uint32_t apply_count;
    int timer_visible = 0;
    raw_count = dluma_qh_collect_raw(component, raw, &timer_visible);
    dluma_qh_lock();
    if (!raw_count) {
        dluma_qh_publish_empty_locked();
        dluma_qh_unlock();
        return;
    }
    memset(apply, 0, sizeof(apply));
    apply_count = dluma_qh_reconcile_locked(
        component, raw, raw_count, timer_visible, apply);
    dluma_qh_unlock();
    dluma_qh_apply(apply, apply_count);
}

static void __fastcall dluma_qh_post_hook(void *component)
{
    DlumaQhPostUpdateFn original;
    dluma_atomic_increment(&g_dluma_qh.active_calls);
    original = g_dluma_qh.post_original;
    if (original)
        original(component);
    if (!dluma_atomic_compare_exchange(&g_dluma_qh.stopping, 0, 0))
        dluma_qh_process_component(component);
    dluma_atomic_decrement(&g_dluma_qh.active_calls);
}

static int dluma_qh_patch_slot(void **slot, void *expected, void *replacement)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    if (!slot || !expected || !replacement ||
        !dluma_is_readable(slot, sizeof(*slot)) ||
        *slot != expected ||
        !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return 0;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)slot, replacement, expected);
    if (!VirtualProtect(slot, sizeof(*slot), old_protect, &restored)) {
        if (previous == expected)
            (void)dluma_atomic_pointer_compare_exchange(
                (PVOID volatile *)slot, expected, replacement);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        (void)VirtualProtect(slot, sizeof(*slot), old_protect, &restored);
        return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return previous == expected;
}

static int dluma_qh_restore_slot(void **slot, void *hook, void *original)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    if (!slot || !hook || !original ||
        !dluma_is_readable(slot, sizeof(*slot)))
        return 0;
    if (*slot == original)
        return 1;
    if (*slot != hook ||
        !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return 0;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)slot, original, hook);
    if (!VirtualProtect(slot, sizeof(*slot), old_protect, &restored)) {
        if (previous == hook)
            (void)dluma_atomic_pointer_compare_exchange(
                (PVOID volatile *)slot, hook, original);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        (void)VirtualProtect(slot, sizeof(*slot), old_protect, &restored);
        return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return previous == hook;
}

static int dluma_qh_validate_profile(DlumaQuestHudState *state)
{
    DWORD engine_timestamp;
    DWORD engine_size;
    DWORD gamedll_timestamp;
    DWORD gamedll_size;
    void *col;
    void *get_text;
    void *set_text;
    if (!state)
        return 0;
    state->engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    state->gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!state->engine || !state->gamedll ||
        !dluma_qh_pe_identity(
            state->engine, &engine_timestamp, &engine_size) ||
        !dluma_qh_pe_identity(
            state->gamedll, &gamedll_timestamp, &gamedll_size) ||
        engine_timestamp != DLUMA_QH_ENGINE_TIMESTAMP ||
        engine_size != DLUMA_QH_ENGINE_IMAGE_SIZE ||
        gamedll_timestamp != DLUMA_QH_GAMEDLL_TIMESTAMP ||
        gamedll_size != DLUMA_QH_GAMEDLL_IMAGE_SIZE ||
        !dluma_qh_module_hash_matches(state->engine, DLUMA_QH_ENGINE_SHA256) ||
        !dluma_qh_module_hash_matches(state->gamedll, DLUMA_QH_GAMEDLL_SHA256))
        return 0;
    state->engine_base = (BYTE *)state->engine;
    state->gamedll_base = (BYTE *)state->gamedll;
    state->engine_image_size = engine_size;
    state->gamedll_image_size = gamedll_size;
    if (!dluma_is_readable(
            state->gamedll_base + DLUMA_QH_VTABLE_RVA - sizeof(void *),
            DLUMA_QH_INITIALIZE_SLOT_OFFSET + 2U * sizeof(void *)) ||
        DLUMA_QH_POST_SLOT_RVA !=
            DLUMA_QH_VTABLE_RVA + DLUMA_QH_POST_SLOT_OFFSET ||
        *(void **)(state->gamedll_base + DLUMA_QH_POST_SLOT_RVA) !=
            state->gamedll_base + DLUMA_QH_POST_ORIGINAL_RVA ||
        *(void **)(state->gamedll_base + DLUMA_QH_VTABLE_RVA +
                   DLUMA_QH_INITIALIZE_SLOT_OFFSET) !=
            state->gamedll_base + DLUMA_QH_INITIALIZE_RVA)
        return 0;
    col = *(void **)(state->gamedll_base + DLUMA_QH_VTABLE_RVA -
                     sizeof(void *));
    if (col != state->gamedll_base + DLUMA_QH_COL_RVA ||
        !dluma_qh_bytes(
            state->gamedll_base + DLUMA_QH_POST_ORIGINAL_RVA,
            g_dluma_qh_post_prefix, DLUMA_ARRAY_COUNT(g_dluma_qh_post_prefix)) ||
        !dluma_qh_bytes(
            state->gamedll_base + DLUMA_QH_INITIALIZE_RVA,
            g_dluma_qh_initialize_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_qh_initialize_prefix)) ||
        !dluma_qh_bytes(
            state->gamedll_base + DLUMA_QH_UPDATE_RVA,
            g_dluma_qh_update_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_qh_update_prefix)) ||
        !dluma_qh_bytes(
            state->gamedll_base + DLUMA_QH_TIMER_UPDATE_RVA,
            g_dluma_qh_timer_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_qh_timer_prefix)) ||
        !dluma_qh_bytes(
            state->gamedll_base + DLUMA_QH_APPLY_ENTRY_RVA,
            g_dluma_qh_apply_entry_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_qh_apply_entry_prefix)) ||
        !dluma_qh_bytes(
            state->gamedll_base + DLUMA_QH_SET_TEXT_CLAMP_RVA,
            g_dluma_qh_set_clamp_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_qh_set_clamp_prefix)))
        return 0;
    get_text = GetProcAddress(
        state->engine,
        "?GetTextExplicit@IUIText@@UEAAAEBV?$string_base@_W@ttl@@XZ");
    set_text = GetProcAddress(
        state->engine,
        "?SetTextExplicit@IUIText@@UEAAXAEBV?$string_base@_W@ttl@@@Z");
    if (!dluma_qh_pointer_in_image(
            get_text, state->engine_base, state->engine_image_size) ||
        !dluma_qh_pointer_in_image(
            set_text, state->engine_base, state->engine_image_size))
        return 0;
    state->post_slot =
        (void **)(state->gamedll_base + DLUMA_QH_POST_SLOT_RVA);
    state->post_original = (DlumaQhPostUpdateFn)(
        state->gamedll_base + DLUMA_QH_POST_ORIGINAL_RVA);
    state->set_text_clamp = (DlumaQhSetTextClampFn)(
        state->gamedll_base + DLUMA_QH_SET_TEXT_CLAMP_RVA);
    state->get_text = (DlumaQhGetTextFn)get_text;
    state->set_text = (DlumaQhSetTextFn)set_text;
    return 1;
}

int dluma_quest_hud_start(void)
{
    DlumaQuestHudState prepared;
    uint32_t generation;
    if (g_dluma_qh.installed)
        return 1;
    generation = g_dluma_qh.map_generation;
    memset(&prepared, 0, sizeof(prepared));
    prepared.map_generation = generation;
    prepared.next_target_id = g_dluma_qh.next_target_id;
    prepared.next_override_id = g_dluma_qh.next_override_id;
    prepared.next_sequence = g_dluma_qh.next_sequence;
    prepared.snapshot.struct_size = sizeof(prepared.snapshot);
    prepared.snapshot.map_generation = generation;
    if (!dluma_qh_validate_profile(&prepared)) {
        dluma_log_line("WARN", "quest_hud_profile_unavailable_fail_closed");
        return 0;
    }
    g_dluma_qh = prepared;
    if (!dluma_qh_patch_slot(
            g_dluma_qh.post_slot, (void *)g_dluma_qh.post_original,
            (void *)dluma_qh_post_hook)) {
        memset(&g_dluma_qh, 0, sizeof(g_dluma_qh));
        g_dluma_qh.map_generation = generation;
        dluma_log_line("WARN", "quest_hud_post_update_hook_not_installed");
        return 0;
    }
    g_dluma_qh.installed = 1;
    dluma_log_line("INFO", "quest_hud_exact_profile_validated");
    dluma_log_line("INFO", "quest_hud_post_update_hook_installed");
    return 1;
}

void dluma_quest_hud_quiesce(void)
{
    if (!g_dluma_qh.installed)
        return;
    dluma_atomic_exchange(&g_dluma_qh.stopping, 1);
    if (!dluma_qh_restore_slot(
            g_dluma_qh.post_slot, (void *)dluma_qh_post_hook,
            (void *)g_dluma_qh.post_original)) {
        dluma_log_line(
            "WARN",
            "quest_hud_hook_restore_ownership_lost_retained");
        return;
    }
    g_dluma_qh.installed = 0;
    dluma_log_line("INFO", "quest_hud_post_update_hook_restored");
}

int dluma_quest_hud_stop(void)
{
    ULONGLONG started;
    uint32_t generation = g_dluma_qh.map_generation;
    if (!g_dluma_qh.installed && !g_dluma_qh.gamedll)
        return 1;
    dluma_quest_hud_quiesce();
    if (g_dluma_qh.installed)
        return 0;
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(&g_dluma_qh.active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < DLUMA_QH_STOP_DRAIN_MS)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&g_dluma_qh.active_calls, 0, 0) != 0) {
        dluma_log_line("WARN", "quest_hud_hook_drain_timeout_retained");
        return 0;
    }
    memset(&g_dluma_qh, 0, sizeof(g_dluma_qh));
    g_dluma_qh.map_generation = generation;
    return 1;
}

int dluma_quest_hud_is_available(void)
{
    return g_dluma_qh.installed &&
           !dluma_atomic_compare_exchange(&g_dluma_qh.stopping, 0, 0);
}

void dluma_quest_hud_on_map_generation_changed(uint32_t map_generation)
{
    dluma_qh_lock();
    g_dluma_qh.map_generation = map_generation;
    dluma_qh_publish_empty_locked();
    g_dluma_qh.snapshot.map_generation = map_generation;
    dluma_qh_unlock();
}

int dluma_quest_hud_snapshot(
    DlumaLua54QuestHudSnapshot *snapshot, DlumaLua54Error *error)
{
    if (!snapshot || snapshot->struct_size < sizeof(*snapshot)) {
        dluma_qh_error(error, "invalid_quest_hud_snapshot",
            "a complete copied quest HUD snapshot is required",
            "game.read", "", 0);
        return 0;
    }
    if (!dluma_quest_hud_is_available()) {
        dluma_qh_error(error, "unsupported_capability",
            "the exact quest HUD presentation binding is unavailable",
            "game.read", "", 0);
        return 0;
    }
    dluma_qh_lock();
    *snapshot = g_dluma_qh.snapshot;
    dluma_qh_unlock();
    return 1;
}

static int dluma_qh_override_text_valid(const char *text)
{
    const char *terminator;
    size_t length;
    int needed;
    if (!text)
        return 0;
    terminator = (const char *)memchr(
        text, 0, DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT + 1U);
    if (!terminator)
        return 0;
    length = (size_t)(terminator - text);
    if (!length || length > DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT)
        return 0;
    needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, 0, 0);
    return needed > 0;
}

int dluma_quest_hud_override_set(
    const DlumaLua54QuestHudOverrideRequest *request,
    DlumaLua54QuestHudOverrideResult *result,
    DlumaLua54Error *error)
{
    DlumaQhOverride *entry = 0;
    uint32_t index;
    if (result && result->struct_size >= sizeof(*result)) {
        result->active = 0;
        result->override_id = 0;
    }
    if (!request || request->struct_size < sizeof(*request) ||
        !result || result->struct_size < sizeof(*result) ||
        !request->owner_id || !request->target_id[0] ||
        !memchr(request->target_id, 0, sizeof(request->target_id)) ||
        !dluma_qh_override_text_valid(request->text)) {
        dluma_qh_error(error, "invalid_quest_hud_override",
            "target and valid non-empty UTF-8 text of at most 512 bytes are required",
            "ui.overlay", request ? request->target_id : "", 0);
        return 0;
    }
    if (!dluma_quest_hud_is_available()) {
        dluma_qh_error(error, "unsupported_capability",
            "the exact quest HUD presentation binding is unavailable",
            "ui.overlay", request->target_id, 0);
        return 0;
    }
    dluma_qh_lock();
    if (request->map_generation != g_dluma_qh.map_generation ||
        !dluma_qh_target_id_present_locked(request->target_id)) {
        dluma_qh_unlock();
        dluma_qh_error(error, "quest_hud_target_expired",
            "the exact quest HUD target is no longer displayed",
            "ui.overlay", request->target_id, 1);
        return 0;
    }
    if (request->override_id) {
        for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index)
            if (g_dluma_qh.overrides[index].active &&
                g_dluma_qh.overrides[index].override_id ==
                    request->override_id &&
                g_dluma_qh.overrides[index].owner_id == request->owner_id) {
                entry = &g_dluma_qh.overrides[index];
                break;
            }
        if (!entry ||
            !dluma_qh_equal(entry->target_id, request->target_id)) {
            dluma_qh_unlock();
            dluma_qh_error(error, "quest_hud_target_expired",
                "the quest HUD override handle is no longer active",
                "ui.overlay", request->target_id, 1);
            return 0;
        }
    } else {
        for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index)
            if (!g_dluma_qh.overrides[index].active) {
                entry = &g_dluma_qh.overrides[index];
                break;
            }
        if (!entry) {
            dluma_qh_unlock();
            dluma_qh_error(error, "quest_hud_override_limit",
                "the bounded quest HUD override registry is full",
                "ui.overlay", request->target_id, 0);
            return 0;
        }
        memset(entry, 0, sizeof(*entry));
        entry->override_id = ++g_dluma_qh.next_override_id;
        if (!entry->override_id)
            entry->override_id = ++g_dluma_qh.next_override_id;
        entry->owner_id = request->owner_id;
        entry->map_generation = request->map_generation;
        dluma_str_copy(entry->target_id, DLUMA_ARRAY_COUNT(entry->target_id),
                     request->target_id);
        entry->active = 1;
    }
    entry->sequence = ++g_dluma_qh.next_sequence;
    if (!entry->sequence)
        entry->sequence = ++g_dluma_qh.next_sequence;
    dluma_str_copy(entry->text, DLUMA_ARRAY_COUNT(entry->text), request->text);
    result->active = 1;
    result->override_id = entry->override_id;
    dluma_qh_unlock();
    return 1;
}

int dluma_quest_hud_override_clear(
    uint64_t owner_id, uint32_t map_generation, uint64_t override_id,
    DlumaLua54QuestHudOverrideResult *result, DlumaLua54Error *error)
{
    uint32_t index;
    if (!result || result->struct_size < sizeof(*result) ||
        !owner_id || !override_id) {
        dluma_qh_error(error, "invalid_quest_hud_override",
            "an owned quest HUD override handle is required",
            "ui.overlay", "", 0);
        return 0;
    }
    result->active = 0;
    result->override_id = override_id;
    dluma_qh_lock();
    for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index) {
        DlumaQhOverride *entry = &g_dluma_qh.overrides[index];
        if (entry->override_id != override_id ||
            entry->owner_id != owner_id)
            continue;
        if (entry->active &&
            entry->map_generation != map_generation) {
            entry->active = 0;
            dluma_qh_unlock();
            dluma_qh_error(error, "quest_hud_target_expired",
                "the quest HUD override belongs to an earlier map generation",
                "ui.overlay", entry->target_id, 1);
            return 0;
        }
        entry->active = 0;
        dluma_qh_unlock();
        return 1;
    }
    dluma_qh_unlock();
    /* Clear is deliberately idempotent for GC and repeated teardown. */
    return 1;
}

void dluma_quest_hud_clear_owner(uint64_t owner_id)
{
    uint32_t index;
    if (!owner_id)
        return;
    dluma_qh_lock();
    for (index = 0; index < DLUMA_QH_MAX_OVERRIDES; ++index)
        if (g_dluma_qh.overrides[index].owner_id == owner_id)
            g_dluma_qh.overrides[index].active = 0;
    dluma_qh_unlock();
}
