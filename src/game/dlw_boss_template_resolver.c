#include "dlw_boss_template_resolver.h"
#include <stdint.h>
#include <string.h>

#define DLUMA_BT_MAX_PHASES 1024u
#define DLUMA_BT_MAX_NAME 127u
#define DLUMA_BT_MAX_CHILDREN 8192u
typedef struct DlumaBtString { const char *data; uint32_t size, capacity; } DlumaBtString;

static int dluma_bt_valid_string(const DlumaBtString *s, DlumaBossReadable read, void *ctx)
{
    uint32_t i;
    if (!s->data || !s->size || s->size > DLUMA_BT_MAX_NAME || s->size > s->capacity ||
        !read(s->data, (size_t)s->size + 1, ctx) || s->data[s->size]) return 0;
    for (i = 0; i < s->size; ++i) if (!s->data[i]) return 0;
    return 1;
}

static int dluma_bt_string(const DlumaBtString *s, const char *want, DlumaBossReadable read, void *ctx)
{
    size_t n = 0;
    if (!s || !want) return 0;
    while (n <= DLUMA_BT_MAX_NAME) {
        if (!want[n]) break;
        ++n;
    }
    if (!n || n > DLUMA_BT_MAX_NAME || s->size != n || s->size > s->capacity ||
        !s->data || !read(s->data, n + 1, ctx)) return 0;
    return s->data[n] == 0 && memcmp(s->data, want, n) == 0;
}

static int dluma_bt_copy_requested(char output[128], const char *source, DlumaBossReadable read, void *ctx)
{
    size_t i;
    if (!source) return 0;
    for (i = 0; i < 128; ++i) {
        if (!read(source + i, 1, ctx)) return 0;
        output[i] = source[i];
        if (!output[i]) return i != 0;
    }
    return 0;
}

static int dluma_bt_reserved(const DlumaBtString *s, const DlumaBossTemplateSpec *spec, DlumaBossReadable read, void *ctx)
{
    return dluma_bt_string(s, spec->flash_phase, read, ctx) ||
        dluma_bt_string(s, spec->chatter_phase, read, ctx);
}

DlumaBossTemplateResult dluma_boss_template_visit(void *owner, const DlumaBossTemplateSpec *spec, DlumaBossChoice choice, DlumaBossReadable read, void *read_ctx, DlumaBossTemplateVisitor visit, void *visit_ctx)
{
    unsigned char *base = (unsigned char *)owner;
    void **phases, *current, *flash = 0, *chat = 0, *a, *b;
    uint32_t count, capacity, i, start_index = UINT32_MAX, flash_index = UINT32_MAX, chat_index = UINT32_MAX;
    uint32_t current_index = UINT32_MAX;
    unsigned char *p, *c, *start;
    void **start_children;
    uint32_t start_count, matches, current_matches = 0, total_children = 0;
    DlumaBossTemplateSpec local_spec;
    char requested[4][128];
    if (!spec || !read || !dluma_bt_copy_requested(requested[0], spec->flash_phase, read, read_ctx) ||
        !dluma_bt_copy_requested(requested[1], spec->chatter_phase, read, read_ctx) ||
        !dluma_bt_copy_requested(requested[2], spec->chatter_group, read, read_ctx) ||
        !dluma_bt_copy_requested(requested[3], spec->start_phase, read, read_ctx)) return DLUMA_BT_INVALID;
    local_spec = *spec;
    local_spec.flash_phase = requested[0]; local_spec.chatter_phase = requested[1];
    local_spec.chatter_group = requested[2]; local_spec.start_phase = requested[3]; spec = &local_spec;
    if (!base || !spec || !read || (choice != DLUMA_BOSS_CHOICE_NONE && !visit) || !spec->flash_vtable || !spec->chatter_vtable || !spec->start_instruction || !spec->start_vtable || !spec->transition_vtable ||
        spec->flash_vtable == spec->chatter_vtable ||
        (choice != DLUMA_BOSS_CHOICE_NONE && choice != DLUMA_BOSS_CHOICE_FLASHBACK && choice != DLUMA_BOSS_CHOICE_CHATTER) ||
        !read(base, 112, read_ctx)) return DLUMA_BT_INVALID;
    current = *(void **)(base + 88);
    phases = *(void ***)(base + 96);
    count = *(uint32_t *)(base + 104);
    capacity = *(uint32_t *)(base + 108);
    if (!count || count > DLUMA_BT_MAX_PHASES || count > capacity || !phases ||
        !read(phases, (size_t)count * sizeof(void *), read_ctx)) return DLUMA_BT_INVALID;
    for (i = 0; i < count; ++i) {
        void **children;
        uint32_t child_count, j;
        const DlumaBtString *target;
        p = (unsigned char *)phases[i];
        if (!p || !read(p, 64, read_ctx) || *(void **)p != owner ||
            !dluma_bt_valid_string((const DlumaBtString *)(p + 16), read, read_ctx)) return DLUMA_BT_INVALID;
        /* PhaseTransition copies its authored +24 string into phase +32.
         * Reserved template phases must be unreachable by both published and
         * not-yet-executed transitions, even in unrelated phases. */
        target = (const DlumaBtString *)(p + 32);
        if (target->size && (!dluma_bt_valid_string(target, read, read_ctx) ||
            dluma_bt_reserved(target, spec, read, read_ctx))) return DLUMA_BT_INVALID;
        children = *(void ***)(p + 48);
        child_count = *(uint32_t *)(p + 56);
        if (child_count > *(uint32_t *)(p + 60) || child_count > DLUMA_BT_MAX_CHILDREN - total_children ||
            (child_count && (!children || !read(children, (size_t)child_count * sizeof(void *), read_ctx)))) return DLUMA_BT_INVALID;
        total_children += child_count;
        for (j = 0; j < child_count; ++j) {
            unsigned char *child = (unsigned char *)children[j];
            if (!child || !read(child, 24, read_ctx) || *(void **)(child + 16) != p) return DLUMA_BT_INVALID;
            if (*(const void **)child == spec->transition_vtable) {
                if (!read(child, 40, read_ctx)) return DLUMA_BT_INVALID;
                target = (const DlumaBtString *)(child + 24);
                if (target->size && (!dluma_bt_valid_string(target, read, read_ctx) ||
                    dluma_bt_reserved(target, spec, read, read_ctx))) return DLUMA_BT_INVALID;
            }
        }
        if (p == current) { ++current_matches; current_index = i; }
        if (dluma_bt_string((const DlumaBtString *)(p + 16), spec->start_phase, read, read_ctx)) {
            if (start_index != UINT32_MAX) return DLUMA_BT_INVALID;
            start_index = i;
        }
        if (dluma_bt_string((const DlumaBtString *)(p + 16), spec->flash_phase, read, read_ctx)) {
            if (flash) return DLUMA_BT_INVALID;
            flash = p;
            flash_index = i;
        }
        if (dluma_bt_string((const DlumaBtString *)(p + 16), spec->chatter_phase, read, read_ctx)) {
            if (chat) return DLUMA_BT_INVALID;
            chat = p;
            chat_index = i;
        }
    }
    if (!flash || !chat || current_matches != 1 || start_index == UINT32_MAX || flash_index <= start_index || chat_index <= start_index ||
        current_index < start_index || flash == chat || current == flash || current == chat) return DLUMA_BT_INVALID;
    start = (unsigned char *)phases[start_index];
    if (choice == DLUMA_BOSS_CHOICE_NONE && current != start) return DLUMA_BT_INVALID;
    start_children = *(void ***)(start + 48);
    start_count = *(uint32_t *)(start + 56);
    if (!start_count || start_count > DLUMA_BT_MAX_PHASES || start_count > *(uint32_t *)(start + 60) ||
        !start_children || !read(start_children, (size_t)start_count * sizeof(void *), read_ctx)) return DLUMA_BT_INVALID;
    matches = 0;
    for (i = 0; i < start_count; ++i)
        if (start_children[i] == spec->start_instruction) ++matches;
    if (matches != 1 || !read(spec->start_instruction, 24, read_ctx)) return DLUMA_BT_INVALID;
    c = (unsigned char *)spec->start_instruction;
    if (*(void **)c != spec->start_vtable || *(void **)(c + 16) != start) return DLUMA_BT_INVALID;
    p = (unsigned char *)flash;
    if (*(uint32_t *)(p + 56) != 1 || *(uint32_t *)(p + 60) < 1 ||
        !*(void ***)(p + 48) || !read(*(void ***)(p + 48), sizeof(void *), read_ctx)) return DLUMA_BT_INVALID;
    a = **(void ***)(p + 48);
    p = (unsigned char *)chat;
    if (*(uint32_t *)(p + 56) != 1 || *(uint32_t *)(p + 60) < 1 ||
        !*(void ***)(p + 48) || !read(*(void ***)(p + 48), sizeof(void *), read_ctx)) return DLUMA_BT_INVALID;
    b = **(void ***)(p + 48);
    if (!a || !b || !read(a, 24, read_ctx) || !read(b, 72, read_ctx)) return DLUMA_BT_INVALID;
    c = (unsigned char *)a;
    if (*(void **)c != spec->flash_vtable || *(void **)(c + 16) != flash) return DLUMA_BT_INVALID;
    c = (unsigned char *)b;
    if (*(void **)c != spec->chatter_vtable || *(void **)(c + 16) != chat ||
        !dluma_bt_string((const DlumaBtString *)(c + 24), "Play", read, read_ctx) ||
        !dluma_bt_string((const DlumaBtString *)(c + 40), spec->chatter_group, read, read_ctx)) return DLUMA_BT_INVALID;
    if (choice != DLUMA_BOSS_CHOICE_NONE)
        visit(choice == DLUMA_BOSS_CHOICE_FLASHBACK ? a : b, visit_ctx);
    return DLUMA_BT_OK;
}
