/* Included after the loader instruction vtable; its engine borrow is supplied
 * by the normal receiver route, never by a saved raw owner pointer. */
typedef struct DlumaBossEffectBorrow {
    DlumaBossInstruction *start;
    uint64_t owner, token, epoch;
} DlumaBossEffectBorrow;
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST) && defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
static void dluma_boss_periodic_diagnostic_effect(int flashback);
#endif

static int dluma_boss_periodic_matches_locked(const DlumaBossEffectBorrow *borrow)
{
    unsigned int i;
    DlumaBossInstruction *self = borrow->start;
    if (!dluma_boss_periodic_available() || self->periodic_retired || self->periodic_epoch != borrow->epoch ||
        self->periodic_token != borrow->token || self->periodic_owner != borrow->owner) return 0;
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        if (g_dluma_boss_periodic_registry.slots[i].owner_key == borrow->owner &&
            g_dluma_boss_periodic_registry.slots[i].token == borrow->token) return 1;
    return 0;
}

static void dluma_boss_periodic_effect(void *child, void *context)
{
    DlumaBossEffectBorrow *borrow = context;
    const DlumaBossVtable *vtable = *(const DlumaBossVtable **)child;
    void (__fastcall *execute)(DlumaBossInstruction *);
    int current;
    MEMORY_BASIC_INFORMATION memory;
    DWORD protection;
    if (!VirtualQuery((BYTE *)child + 8U, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return;
    protection = memory.Protect & 0xffU;
    if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return;
    if (!vtable || !dluma_is_readable(vtable, sizeof(*vtable)) || !vtable->reset || !vtable->execute) return;
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST)
    if (vtable->reset != (void (__fastcall *)(DlumaBossInstruction *))((BYTE *)g_dluma_boss.module + 0xA91420U) ||
        (vtable->execute != (void (__fastcall *)(DlumaBossInstruction *))((BYTE *)g_dluma_boss.module + 0xA9ABD0U) &&
         vtable->execute != (void (__fastcall *)(DlumaBossInstruction *))((BYTE *)g_dluma_boss.module + 0xAA0CF0U))) return;
#endif
    execute = vtable->execute;
    dluma_boss_periodic_enter(); current = dluma_boss_periodic_matches_locked(borrow); dluma_boss_periodic_leave();
    if (!current) return;
    vtable->reset(child);
    /* Reset is a qualified leaf in the real profile. Keep this check even for
     * a test or future reset implementation which retires the Start reentrantly. */
    dluma_boss_periodic_enter(); current = dluma_boss_periodic_matches_locked(borrow); dluma_boss_periodic_leave();
    if (!current) return;
    execute(child);
    /* Execute may retire our instruction. Do not inspect engine memory here. */
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST) && defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
    dluma_boss_periodic_diagnostic_effect(execute ==
        (void (__fastcall *)(DlumaBossInstruction *))((BYTE *)g_dluma_boss.module + 0xA9ABD0U));
#endif
}

static int dluma_boss_periodic_current_stop(void *owner)
{
    BYTE *phase;
    void **children;
    unsigned int i, count;
    if (!dluma_is_readable(owner, 112U)) return -1;
    phase = *(BYTE **)((BYTE *)owner + 88U);
    if (!phase || !dluma_is_readable(phase, 64U) || *(void **)phase != owner) return -1;
    count = *(unsigned int *)(phase + 56U); children = *(void ***)(phase + 48U);
    if (count > 1024U || count > *(unsigned int *)(phase + 60U) ||
        (count && (!children || !dluma_is_readable(children, count * sizeof(void *))))) return -1;
    for (i = 0; i < count; ++i) {
        DlumaBossInstruction *child = children[i];
        if (!child || !dluma_is_readable(child, 24U)) return -1;
        if (child->vtable == &g_dluma_boss_vtable &&
            (!dluma_is_readable(child, sizeof(*child)) || child->phase != phase)) return -1;
        if (child->vtable == &g_dluma_boss_vtable && child->kind == DLUMA_BOSS_PERIODIC_STOP && child->valid) return 1;
    }
    return 0;
}

static void dluma_boss_periodic_native_update(void *owner)
{
    DlumaBossEffectBorrow borrow;
    DlumaBossTemplateSpec spec;
    DlumaBossInstruction *self = 0;
    const void *flash = 0, *chatter = 0, *transition = 0;
    char names[4][128];
    uint64_t now;
    uint32_t delay, choice_random;
    DlumaBossChoice choice = DLUMA_BOSS_CHOICE_NONE;
    unsigned int i;
    int stop, current, free_now;
    dluma_boss_periodic_enter();
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        if (g_dluma_boss_periodic_starts[i] &&
            g_dluma_boss_periodic_starts[i]->periodic_owner == (uint64_t)(ULONG_PTR)owner) { self = g_dluma_boss_periodic_starts[i]; break; }
    if (!self || self->periodic_pins == UINT_MAX || self->periodic_retired) { dluma_boss_periodic_leave(); return; }
    ++self->periodic_pins;
    borrow.start = self; borrow.owner = self->periodic_owner; borrow.token = self->periodic_token; borrow.epoch = self->periodic_epoch;
    memcpy(names[0], self->mod_id, 128); memcpy(names[1], self->handler_id, 128);
    memcpy(names[2], self->arguments, 128); memcpy(names[3], self->periodic_start_phase, 128);
    dluma_boss_periodic_leave();
    __try {
        __try {
            stop = dluma_boss_periodic_current_stop(owner);
            if (stop || !dluma_boss_periodic_host_sample(owner, &now, &delay, &choice_random, &flash, &chatter, &transition)) {
                dluma_boss_periodic_enter(); dluma_boss_periodic_retire_locked(self); dluma_boss_periodic_leave();
            } else {
                dluma_boss_periodic_enter();
                current = dluma_boss_periodic_matches_locked(&borrow);
                if (current) current = dluma_boss_registry_tick(&g_dluma_boss_periodic_registry, borrow.owner,
                    borrow.token, now, choice_random, delay, &choice) == DLUMA_BS_OK;
                dluma_boss_periodic_leave();
                if (current) {
                    spec.flash_phase = names[0]; spec.chatter_phase = names[1]; spec.chatter_group = names[2]; spec.start_phase = names[3];
                    spec.flash_vtable = flash; spec.chatter_vtable = chatter; spec.transition_vtable = transition;
                    spec.start_instruction = self; spec.start_vtable = &g_dluma_boss_vtable;
                    if (dluma_boss_template_visit(owner, &spec, choice, dluma_boss_periodic_read, 0,
                        dluma_boss_periodic_effect, &borrow) != DLUMA_BT_OK) {
                        dluma_boss_periodic_enter(); dluma_boss_periodic_retire_locked(self); dluma_boss_periodic_leave();
                    }
                }
            }
        } __except (1) {
            dluma_boss_periodic_enter(); dluma_boss_periodic_retire_locked(self); dluma_boss_periodic_leave();
        }
    } __finally {
        dluma_boss_periodic_enter(); --self->periodic_pins;
        free_now = !self->periodic_pins && self->periodic_free_pending;
        dluma_boss_periodic_leave();
        if (free_now) { dluma_free(self); dluma_atomic_decrement(&g_dluma_boss.instructions); }
    }
}

static void dluma_boss_periodic_retire_owner_locked(void *owner)
{
    unsigned int i;
    dluma_boss_periodic_note_stop_locked((uint64_t)(ULONG_PTR)owner);
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i) {
        DlumaBossInstruction *self = g_dluma_boss_periodic_starts[i];
        if (self && (!owner || self->periodic_owner == (uint64_t)(ULONG_PTR)owner)) dluma_boss_periodic_retire_locked(self);
    }
}
static void dluma_boss_periodic_retire_owner(void *owner)
{
    dluma_boss_periodic_enter(); dluma_boss_periodic_retire_owner_locked(owner); dluma_boss_periodic_leave();
}

#if !defined(DLUMA_BOSS_LIFECYCLE_TEST)
typedef LONG (WINAPI *DlumaBossRandom)(void *, BYTE *, unsigned long, unsigned long);
/* Public availability remains an immutable source gate. The separately
 * compiled, consented diagnostic below exercises the same prepared route
 * without advertising a public periodic capability. */
static const unsigned int g_dluma_boss_periodic_reviewed = 0;
#if defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
/* Startup configuration is immutable after the qualified cold installer. Only
 * copied context facts are consulted on the receiver thread: no filesystem,
 * saved native object or mod-supplied request grants this private consent. */
static struct {
    unsigned int configured;
    volatile LONG generation;
    volatile LONG effects_logged;
    DlumaDirectoryIdentity project;
    char map[DLUMA_MAX_MAP_NAME];
} g_dluma_boss_periodic_diagnostic;
static void dluma_boss_periodic_diagnostic_effect(int flashback)
{
    if (dluma_atomic_increment(&g_dluma_boss_periodic_diagnostic.effects_logged) <= 8)
        dluma_log_pair("INFO", "boss_periodic_effect_returned", flashback ? "flashback" : "chatter");
}
static int dluma_boss_periodic_diagnostic_configure(void)
{
    WCHAR enabled[4], project[DLUMA_MAX_WPATH], map[DLUMA_MAX_MAP_NAME];
    DWORD p, m;
    unsigned int i;
    g_dluma_boss_periodic_diagnostic.configured = 0;
    if (!g_dluma.config.developer_mode ||
        GetEnvironmentVariableW(L"DLUMA_BOSS_PERIODIC_DIAGNOSTIC", enabled, DLUMA_ARRAY_COUNT(enabled)) != 1U || enabled[0] != L'1') return 0;
    p = GetEnvironmentVariableW(L"DLUMA_BOSS_PERIODIC_PROJECT", project, DLUMA_ARRAY_COUNT(project));
    m = GetEnvironmentVariableW(L"DLUMA_BOSS_PERIODIC_MAP", map, DLUMA_ARRAY_COUNT(map));
    if (!p || p >= DLUMA_ARRAY_COUNT(project) || !m || m >= DLUMA_ARRAY_COUNT(map)) return 0;
    for (i = 0; i < m; ++i) {
        if (map[i] > 127U) return 0;
        g_dluma_boss_periodic_diagnostic.map[i] = (char)map[i];
    }
    g_dluma_boss_periodic_diagnostic.map[m] = 0;
    if (!dluma_valid_map_segment(g_dluma_boss_periodic_diagnostic.map) ||
        !dluma_open_directory_identity(project, &g_dluma_boss_periodic_diagnostic.project)) return 0;
    g_dluma_boss_periodic_diagnostic.configured = 1; return 1;
}
static int dluma_boss_periodic_diagnostic_allowed(void)
{
    unsigned int generation = (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss_periodic_diagnostic.generation, 0, 0);
    return g_dluma_boss_periodic_diagnostic.configured && g_dluma.config.developer_mode && generation &&
        generation == (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0);
}
#else
static int dluma_boss_periodic_diagnostic_configure(void) { return 0; }
static int dluma_boss_periodic_diagnostic_allowed(void) { return 0; }
#endif
static int dluma_boss_periodic_available(void)
{
    return (g_dluma_boss_periodic_reviewed || dluma_boss_periodic_diagnostic_allowed()) &&
        !dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0) && dluma_boss_receiver_route_ready();
}
static struct { void *owner; DWORD thread; unsigned int generation; DlumaBossRandom random; } g_dluma_boss_borrow;

static int dluma_boss_periodic_native_begin(void *owner)
{
    unsigned int generation;
    if (!dluma_boss_periodic_available()) { dluma_boss_periodic_retire_owner(owner); return 0; }
    if (!dluma_is_readable(owner, 112U) ||
        *(unsigned int *)((BYTE *)owner + 64U) != 1U) return 0;
    generation = (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0);
    if (!generation || dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) return 0;
    dluma_boss_periodic_enter();
    g_dluma_boss_borrow.owner = owner; g_dluma_boss_borrow.thread = GetCurrentThreadId(); g_dluma_boss_borrow.generation = generation;
    dluma_boss_periodic_leave(); return 1;
}
static void dluma_boss_periodic_native_end(void *owner)
{
    dluma_boss_periodic_enter();
    if (g_dluma_boss_borrow.owner == owner && g_dluma_boss_borrow.thread == GetCurrentThreadId()) {
        g_dluma_boss_borrow.owner = 0; g_dluma_boss_borrow.thread = 0; g_dluma_boss_borrow.generation = 0;
    }
    dluma_boss_periodic_leave();
}
static void dluma_boss_periodic_native_retire(void *owner)
{
    dluma_boss_periodic_retire_owner(owner);
    dluma_boss_periodic_native_end(owner);
}

static int dluma_boss_periodic_host_sample(void *owner, uint64_t *now, uint32_t *delay, uint32_t *choice,
    const void **flash, const void **chatter, const void **transition)
{
    uint32_t random[2];
    int valid;
    if (!dluma_boss_periodic_available()) return 0;
    dluma_boss_periodic_enter();
    valid = g_dluma_boss_borrow.owner == owner && g_dluma_boss_borrow.thread == GetCurrentThreadId() &&
        g_dluma_boss_borrow.generation && g_dluma_boss_borrow.generation == (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0);
    dluma_boss_periodic_leave();
    if (!valid || !g_dluma_boss_borrow.random || g_dluma_boss_borrow.random(0, (BYTE *)random, sizeof(random), 2U) < 0) return 0;
    *now = GetTickCount64(); *delay = random[0] % 20001U; *choice = random[1] & 1U;
    *flash = (BYTE *)g_dluma_boss.module + 0x17C23A8U; *chatter = (BYTE *)g_dluma_boss.module + 0x17C2268U;
    *transition = (BYTE *)g_dluma_boss.module + 0x17C2D48U;
    return 1;
}

static int dluma_boss_periodic_native_start(HMODULE module)
{
    static const DWORD cells[3] = { 0x17C0DC0U, 0x17C0D68U, 0x17C1138U };
    static const DWORD methods[6] = { 0xA8EE30U, 0xA8E0D0U, 0xA8E640U, 0xA91420U, 0xA9ABD0U, 0xAA0CF0U };
    static const DWORD child_tables[3] = { 0x17C23A8U, 0x17C2268U, 0x17C2D48U };
    static const DWORD child_execute[3] = { 0xA9ABD0U, 0xAA0CF0U, 0xA9C770U };
    static const BYTE prefixes[6][24] = {
        {0x40,0x57,0x48,0x83,0xec,0x20,0x8b,0x51,0x40,0x48,0x8b,0xf9,0xff,0xca,0x75,0x6b,0x48,0x89,0x5c,0x24,0x30,0x48,0x89,0x74},
        {0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x48,0x83,0xec,0x20,0x45,0x33,0xe4,0x48,0x8b,0xf9,0x41,0x8b},
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x48,0x83},
        {0xc6,0x41,0x08,0x00,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83},
        {0x48,0x8b,0x05,0xb1,0xd3,0x26,0x01,0x48,0x8b,0x80,0x08,0x05,0x00,0x00,0x48,0x85,0xc0,0x74,0x2b,0x48,0x8b,0x80,0xb8,0x00},
        {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x54,0x48,0x83,0xec,0x70,0x48,0x8b,0x41,0x18,0x48,0x8b}
    };
    DlumaBossReceiverBinding binding;
    DlumaBossReceiverCallbacks callbacks = { dluma_boss_periodic_native_begin, dluma_boss_periodic_native_update,
        dluma_boss_periodic_native_end, dluma_boss_periodic_native_retire };
    WCHAR path[DLUMA_MAX_WPATH]; char hash[DLUMA_SHA256_HEX_CAPACITY];
    BYTE *base = (BYTE *)module;
    unsigned int i;
    HMODULE bcrypt;
    if (!(g_dluma_boss_periodic_reviewed || dluma_boss_periodic_diagnostic_configure()) || !module || module != g_dluma_boss.module ||
        !dluma_get_module_path_checked(module, path, DLUMA_ARRAY_COUNT(path)) ||
        !dluma_sha256_file_hex(path, 512ULL * 1024ULL * 1024ULL, hash) ||
        dluma_stricmp(hash, "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95")) return 0;
    for (i = 0; i < 6; ++i)
        if (!dluma_boss_section(base, 0x2082000U, methods[i], 24U, 0x60000000U, 0x80000000U) ||
            memcmp(base + methods[i], prefixes[i], 24U)) return 0;
    for (i = 0; i < 3; ++i)
        if (!dluma_boss_section(base, 0x2082000U, child_tables[i], 32U, 0x40000000U, 0xA0000000U) ||
            *(ULONG_PTR *)(base + child_tables[i] + 16U) != (ULONG_PTR)(base + child_execute[i]) ||
            *(ULONG_PTR *)(base + child_tables[i] + 24U) != (ULONG_PTR)(base + 0xA91420U)) return 0;
    memset(&binding, 0, sizeof(binding)); binding.module = module; binding.owner_vtable = base + 0x17C09B8U;
    for (i = 0; i < 3; ++i) {
        if (!dluma_boss_section(base, 0x2082000U, cells[i], sizeof(void *), 0x40000000U, 0xA0000000U)) return 0;
        binding.cells[i] = (void **)(base + cells[i]); binding.originals[i] = (DlumaBossReceiverMethod)(base + methods[i]);
    }
    bcrypt = GetModuleHandleW(L"bcrypt.dll");
    g_dluma_boss_borrow.random = bcrypt ? (DlumaBossRandom)GetProcAddress(bcrypt, "BCryptGenRandom") : 0;
    if (!g_dluma_boss_borrow.random) return 0;
    if (!dluma_boss_receiver_route_start(&binding, &callbacks)) return 0;
    dluma_log_line("INFO", "Private periodic boss receiver ready; project/map consent required");
    return 1;
}
#else
static int dluma_boss_periodic_available(void) { return dluma_boss_periodic_host_available(); }
#endif

void dluma_boss_actions_set_periodic_context(unsigned int generation, const WCHAR *project, const char *map)
{
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST) && defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
    DlumaDirectoryIdentity actual;
    int valid = generation && project && project[0] && map && map[0] &&
        g_dluma_boss_periodic_diagnostic.configured && g_dluma.config.developer_mode &&
        dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) &&
        !dluma_strcmp(map, g_dluma_boss_periodic_diagnostic.map) &&
        dluma_open_directory_identity(project, &actual) &&
        dluma_directory_identity_equal(&actual, &g_dluma_boss_periodic_diagnostic.project) &&
        dluma_validate_map_anchor_under_project(project, map, &actual);
    dluma_boss_periodic_enter();
    dluma_atomic_exchange(&g_dluma_boss_periodic_diagnostic.generation, 0);
    dluma_atomic_exchange(&g_dluma_boss_periodic_diagnostic.effects_logged, 0);
    dluma_boss_periodic_retire_owner_locked(0);
    if (valid && generation == (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0) &&
        !dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0))
        dluma_atomic_exchange(&g_dluma_boss_periodic_diagnostic.generation, (LONG)generation);
    dluma_boss_periodic_leave();
    if (dluma_boss_periodic_diagnostic_allowed()) dluma_log_u64("INFO", "boss_periodic_private_context_generation", generation);
#else
    (void)generation; (void)project; (void)map;
#endif
}
