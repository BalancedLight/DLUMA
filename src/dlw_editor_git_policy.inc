/* Read-only Editor source-control policy snapshot shared by both adapters. */
#ifndef DLW_EDITOR_GIT_POLICY_INC
#define DLW_EDITOR_GIT_POLICY_INC

#define GIT_POLICY_EDITOR_GLOBAL 0x01B70238U
#define GIT_POLICY_EDITOR_TABLE 0x01504F98U
#define GIT_POLICY_LEGACY_OFFSET 0x1057U
#define GIT_POLICY_PROVIDER_OFFSET 0x24B0U

static int git_policy_readable(const void *pointer, SIZE_T size)
{
    uintptr_t current = (uintptr_t)pointer, end;
    if (!current || size > (SIZE_T)(UINTPTR_MAX - current)) return 0;
    end = current + size;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t next;
        if (!VirtualQuery((void *)current, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        next = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (next <= current) return 0;
        current = next;
    }
    return 1;
}

/* expected_editor is optional on initial capture and mandatory thereafter. */
static int git_policy_read_off(void **editor_global, void *expected_table, DWORD owner_thread,
    void *expected_editor, void **observed_editor)
{
    BYTE *editor;
    void *provider;
    unsigned char legacy;
    if (observed_editor) *observed_editor = NULL;
    if (!owner_thread || GetCurrentThreadId() != owner_thread || !expected_table ||
        !git_policy_readable(editor_global, sizeof(*editor_global))) return 0;
    __try {
        editor = (BYTE *)*editor_global;
        if (!editor || ((uintptr_t)editor % sizeof(void *)) ||
            (uintptr_t)editor > UINTPTR_MAX - GIT_POLICY_PROVIDER_OFFSET - sizeof(void *) ||
            (expected_editor && editor != (BYTE *)expected_editor) ||
            !git_policy_readable(editor, sizeof(void *)) || *(void **)editor != expected_table ||
            !git_policy_readable(editor + GIT_POLICY_LEGACY_OFFSET, 1U) ||
            !git_policy_readable(editor + GIT_POLICY_PROVIDER_OFFSET, sizeof(void *))) return 0;
        legacy = editor[GIT_POLICY_LEGACY_OFFSET];
        provider = *(void **)(editor + GIT_POLICY_PROVIDER_OFFSET);
        if (*editor_global != editor || *(void **)editor != expected_table || legacy || provider ||
            editor[GIT_POLICY_LEGACY_OFFSET] || *(void **)(editor + GIT_POLICY_PROVIDER_OFFSET) ||
            *editor_global != editor) return 0;
        if (observed_editor) *observed_editor = editor;
        return 1;
    } __except (1) { return 0; }
}
#endif
