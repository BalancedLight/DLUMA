#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_boss_template_resolver.h"
#include "../src/game/dlw_boss_template_resolver.c"

typedef struct Str { const char *data; uint32_t size, capacity; } Str;
typedef struct Child { const void *vtable; uint64_t done; void *phase; Str mode, group, extra; } Child;
typedef struct Phase { void *owner; uint64_t unused; Str name; Str target; Child **children; uint32_t count, cap; } Phase;
typedef struct Owner { unsigned char padding[88]; Phase *current; Phase **phases; uint32_t count, cap; } Owner;
static unsigned int reads;
static int readable(const void *p, size_t n, void *ctx) { (void)n; (void)ctx; ++reads; return p != NULL; }
static void visit(void *p, void *ctx) { *(void **)ctx = p; }
static Str str(const char *s) { Str v = { s, (uint32_t)strlen(s), (uint32_t)strlen(s) }; return v; }
int main(void)
{
    int vt_flash, vt_chat, vt_other, vt_start, vt_transition;
    Owner o = {0}; Phase live = {0}, flash = {0}, chat = {0};
    Child a = {0}, b = {0}, start = {0}; Child *ca[] = { &a }, *cb[] = { &b }, *cs[] = { &start };
    Phase *phases[] = { &live, &flash, &chat };
    DlumaBossTemplateSpec spec = { "FlashTemplate", "ChatTemplate", "FinalAmbient", &vt_flash, &vt_chat, "Active", &start, &vt_start, &vt_transition };
    void *seen = NULL;
    o.current = &live; o.phases = phases; o.count = o.cap = 3;
    live.owner = flash.owner = chat.owner = &o;
    live.children = cs; live.count = live.cap = 1; start.phase = &live; start.vtable = &vt_start;
    live.name = str("Active"); flash.name = str("FlashTemplate"); chat.name = str("ChatTemplate");
    flash.children = ca; flash.count = flash.cap = 1;
    chat.children = cb; chat.count = chat.cap = 1;
    a.vtable = &vt_flash; a.phase = &flash;
    b.vtable = &vt_chat; b.phase = &chat; b.mode = str("Play"); b.group = str("FinalAmbient");
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_OK);
    assert(reads < 100); /* Requested names are validated once, not per phase. */
    {
        Phase earlier = {0}; Phase *ordered[] = { &earlier, &live, &flash, &chat };
        earlier.owner = &o; earlier.name = str("Earlier");
        o.current = &earlier; o.phases = ordered; o.count = o.cap = 4;
        assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
        o.current = &live; o.phases = phases; o.count = o.cap = 3;
    }
    live.target = str("FlashTemplate");
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_INVALID);
    live.target = str("ChatTemplate");
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    memset(&live.target, 0, sizeof(live.target));
    {
        Child transition = {0}; Child *normal[] = { &start, &transition };
        transition.vtable = &vt_transition; transition.phase = &live;
        transition.mode = str("FlashTemplate");
        live.children = normal; live.count = live.cap = 2;
        assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_INVALID);
        transition.mode = str("ChatTemplate");
        assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_INVALID);
        transition.mode = str("Active");
        assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_OK);
        live.children = cs; live.count = live.cap = 1;
    }
    o.current = NULL;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_INVALID);
    o.current = &live;
    start.phase = &chat;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_INVALID);
    start.phase = &live;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_OK && seen == &a);
    seen = NULL;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_CHATTER, readable, NULL, visit, &seen) == DLUMA_BT_OK && seen == &b);
    phases[0] = &flash; phases[1] = &live; seen = NULL;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_NONE, readable, NULL, NULL, NULL) == DLUMA_BT_INVALID);
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    phases[0] = &live; phases[1] = &flash;
    seen = NULL; chat.count = 2;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    chat.count = 1; b.vtable = &vt_other;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    b.vtable = &vt_chat; b.group = str("Other");
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_CHATTER, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    b.group = str("FinalAmbient"); flash.owner = &live;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    flash.owner = &o; live.name.capacity = 0;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    live.name = str("FlashTemplate");
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    live.name = str("Active"); o.current = &flash;
    assert(dluma_boss_template_visit(&o, &spec, DLUMA_BOSS_CHOICE_FLASHBACK, readable, NULL, visit, &seen) == DLUMA_BT_INVALID && !seen);
    puts("PASS: bounded boss templates");
    return 0;
}
