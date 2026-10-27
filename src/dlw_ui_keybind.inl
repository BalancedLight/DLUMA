/* The stock controls-menu key selector owns keyboard/mouse capture and key
 * rendering. Only its persistence callbacks are supplied by the loader. */
#define DLUMA_UI_KEYBIND_VTABLE_RVA 0x01991DA8UL
#define DLUMA_UI_KEYBIND_BEGIN_RVA 0x0120B500UL
#define DLUMA_UI_KEYBIND_FINISH_RVA 0x0120B3D0UL
#define DLUMA_UI_KEYBIND_UPDATE_TEXT_RVA 0x0120B630UL
#define DLUMA_UI_KEYBIND_FORMAT_RVA 0x008862C0UL
#define DLUMA_UI_KEYBIND_INPUT_OFFSET 160U
#define DLUMA_UI_KEYBIND_OWNER_OFFSET 256U
#define DLUMA_UI_KEYBIND_EDITING_OFFSET 264U

typedef struct DlumaUiKeybindInput {
    int fields[11];
} DlumaUiKeybindInput;
typedef char DlumaUiKeybindInputMustBe44[(sizeof(DlumaUiKeybindInput) == 44) ? 1 : -1];
typedef UINT (WINAPI *DlumaUiMapVirtualKeyFn)(UINT code, UINT mode);
typedef void (__fastcall *DlumaUiKeybindBeginFn)(void *item);
typedef void *(__fastcall *DlumaUiKeybindFinishFn)(void *item, char accepted);
typedef void (__fastcall *DlumaUiKeybindUpdateFn)(void *item);

static void *g_dluma_ui_keybind_callbacks[4];
static void **g_dluma_ui_keybind_interface = g_dluma_ui_keybind_callbacks;
static BYTE *g_dluma_ui_keybind_module;
static DlumaUiMapVirtualKeyFn g_dluma_ui_map_virtual_key;
static void *g_dluma_ui_keybind_active_item;
static void *g_dluma_ui_keybind_commit_item;
static unsigned int g_dluma_ui_keybind_setting_index = 0xFFFFFFFFUL;
static char g_dluma_ui_keybind_setting_key[DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY];
static char g_dluma_ui_keybind_schema[DLUMA_SHA256_HEX_CAPACITY];

static int dluma_ui_mod_config_is_keybind(const DlumaLuaConfigSetting *setting)
{
    return setting && setting->kind == DLUMA_LUA_CONFIG_INTEGER &&
           setting->editor == DLUMA_LUA_CONFIG_EDITOR_KEYBIND;
}

static unsigned int dluma_ui_keybind_row(void *item)
{
    unsigned int row;
    for (row = 0; row < g_dluma_ui_mod_config_row_count; ++row)
        if (g_dluma_ui_mod_config_rows[row].item == item)
            return row;
    return 0xFFFFFFFFUL;
}

static int dluma_ui_keybind_native_valid(void *item)
{
    return item && g_dluma_ui_keybind_module &&
           dluma_is_readable(item, 288) &&
           *(void **)item == g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_VTABLE_RVA &&
           *(void **)((BYTE *)item + DLUMA_UI_KEYBIND_OWNER_OFFSET) ==
               &g_dluma_ui_keybind_interface;
}

static int dluma_ui_keybind_to_virtual(const DlumaUiKeybindInput *input)
{
    UINT scan;
    if (input->fields[2] == 0) {
        switch (input->fields[4]) {
        case 0xF000: return 1;
        case 0xF001: return 2;
        case 0xF002: return 4;
        case 0xF003: return 5;
        case 0xF004: return 6;
        default: return 0;
        }
    }
    if (input->fields[2] != 1 || input->fields[4] <= 0 ||
        input->fields[4] > 255 || !g_dluma_ui_map_virtual_key)
        return 0;
    if (input->fields[4] == 0x45) return 144;
    if (input->fields[4] == 0xC5) return 19;
    scan = (UINT)input->fields[4];
    if (scan & 0x80U) scan = (scan & 0x7FU) | 0xE000U;
    return (int)g_dluma_ui_map_virtual_key(scan, 3U);
}

static void dluma_ui_keybind_from_virtual(int key, DlumaUiKeybindInput *input)
{
    UINT scan = 0;
    memset(input, 0, sizeof(*input));
    input->fields[0] = -1;
    input->fields[1] = 4;
    input->fields[2] = 1;
    input->fields[4] = -1;
    input->fields[6] = 0x3F000000;
    input->fields[7] = 0x3F000000;
    input->fields[10] = -1;
    switch (key) {
    case 1: input->fields[2] = 0; input->fields[4] = 0xF000; return;
    case 2: input->fields[2] = 0; input->fields[4] = 0xF001; return;
    case 4: input->fields[2] = 0; input->fields[4] = 0xF002; return;
    case 5: input->fields[2] = 0; input->fields[4] = 0xF003; return;
    case 6: input->fields[2] = 0; input->fields[4] = 0xF004; return;
    case 144: input->fields[4] = 0x45; return;
    case 19: input->fields[4] = 0xC5; return;
    default: break;
    }
    if (key > 0 && key < 255 && g_dluma_ui_map_virtual_key)
        scan = g_dluma_ui_map_virtual_key((UINT)key, 4U);
    if (scan)
        input->fields[4] = (int)((scan & 0xFFU) | ((scan & 0xFF00U) ? 0x80U : 0U));
}

static unsigned char __fastcall dluma_ui_keybind_begin_callback(void *owner, void *item)
{
    unsigned int row;
    unsigned int index;
    unsigned char accepted = 0;
    (void)owner;
    dluma_atomic_increment(&g_dluma_registry_active_calls);
    if (g_dluma_registry_hooks_stopping || g_dluma_registry_runtime_disabled || g_dluma.stop_requested)
        goto finished;
    row = dluma_ui_keybind_row(item);
    if (row >= g_dluma_ui_mod_config_row_count ||
        !dluma_ui_mod_config_revalidate() || !dluma_ui_keybind_native_valid(item))
        goto finished;
    index = g_dluma_ui_mod_config_rows[row].setting_index;
    if (index >= g_dluma_ui_mod_config_schema.setting_count ||
        !dluma_ui_mod_config_is_keybind(&g_dluma_ui_mod_config_schema.settings[index]))
        goto finished;
    g_dluma_ui_keybind_active_item = item;
    g_dluma_ui_keybind_commit_item = item;
    g_dluma_ui_keybind_setting_index = index;
    dluma_str_copy(g_dluma_ui_keybind_setting_key,
        DLUMA_ARRAY_COUNT(g_dluma_ui_keybind_setting_key),
        g_dluma_ui_mod_config_schema.settings[index].key);
    dluma_str_copy(g_dluma_ui_keybind_schema,
        DLUMA_ARRAY_COUNT(g_dluma_ui_keybind_schema),
        g_dluma_ui_mod_config_schema.fingerprint);
    dluma_ui_mod_config_update_for_row(row);
    g_dluma_ui_mod_config_edit_mode = 2;
    dluma_ui_mod_config_set_status("DLUMA_ModConfig_PressKey", "PRESS A KEY OR MOUSE BUTTON. ESC CANCELS.");
    accepted = 1;
finished:
    dluma_atomic_decrement(&g_dluma_registry_active_calls);
    return accepted;
}

static void __fastcall dluma_ui_keybind_end_callback(void *owner, void *item)
{
    (void)owner;
    dluma_atomic_increment(&g_dluma_registry_active_calls);
    if (g_dluma_ui_keybind_active_item == item) {
        g_dluma_ui_keybind_active_item = 0;
        g_dluma_ui_mod_config_edit_mode = 0;
        if (!g_dluma_registry_hooks_stopping && !g_dluma.stop_requested)
            dluma_ui_mod_config_set_status("DLUMA_ModConfig_KeybindHint", "SELECT A BINDING TO CHANGE IT.");
    }
    dluma_atomic_decrement(&g_dluma_registry_active_calls);
}

static void * __fastcall dluma_ui_keybind_commit_callback(
    void *owner, void *out, void *item,
    const DlumaUiKeybindInput *previous, const DlumaUiKeybindInput *next)
{
    unsigned int index = g_dluma_ui_keybind_setting_index;
    DlumaLuaConfigValue value;
    int key;
    int accepted = 0;
    (void)owner;
    dluma_atomic_increment(&g_dluma_registry_active_calls);
    memcpy(out, previous, sizeof(*previous));
    if (g_dluma_registry_hooks_stopping || g_dluma_registry_runtime_disabled || g_dluma.stop_requested ||
        item != g_dluma_ui_keybind_commit_item ||
        index >= g_dluma_ui_mod_config_schema.setting_count ||
        !dluma_ui_mod_config_revalidate() ||
        dluma_stricmp(g_dluma_ui_keybind_schema, g_dluma_ui_mod_config_schema.fingerprint) != 0 ||
        dluma_stricmp(g_dluma_ui_keybind_setting_key,
            g_dluma_ui_mod_config_schema.settings[index].key) != 0)
        goto finished;
    key = dluma_ui_keybind_to_virtual(next);
    if (key > 0 && key < 255) {
        memset(&value, 0, sizeof(value));
        value.kind = DLUMA_LUA_CONFIG_INTEGER;
        value.present = 1;
        value.integer = key;
        accepted = dluma_ui_mod_config_write_value(
            g_dluma_ui_mod_config_screen, index, &value, 1);
    }
    if (accepted)
        memcpy(out, next, sizeof(*next));
    else {
        if (dluma_ui_keybind_native_valid(item))
            ((DlumaUiKeybindUpdateFn)(g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_UPDATE_TEXT_RVA))(item);
        if (!key)
            dluma_ui_mod_config_set_status("DLUMA_ModConfig_KeyboardMouseOnly", "USE A KEYBOARD KEY OR MOUSE BUTTON.");
    }
    g_dluma_ui_keybind_commit_item = 0;
finished:
    dluma_atomic_decrement(&g_dluma_registry_active_calls);
    return out;
}

static int dluma_ui_keybind_ready(void)
{
    HMODULE module = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    DWORD timestamp = 0;
    DWORD image_size = 0;
    if (!module || !user32 ||
        !dluma_ui_get_pe_identity(module, &timestamp, &image_size) ||
        timestamp != DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP ||
        image_size != DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE)
        return 0;
    {
        static const BYTE begin_prefix[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xF9};
        static const BYTE finish_prefix[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60};
        static const BYTE update_prefix[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10};
        static const BYTE format_prefix[] = {0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
        BYTE *base = (BYTE *)module;
        if (memcmp(base + DLUMA_UI_KEYBIND_BEGIN_RVA, begin_prefix, sizeof(begin_prefix)) != 0 ||
            memcmp(base + DLUMA_UI_KEYBIND_FINISH_RVA, finish_prefix, sizeof(finish_prefix)) != 0 ||
            memcmp(base + DLUMA_UI_KEYBIND_UPDATE_TEXT_RVA, update_prefix, sizeof(update_prefix)) != 0 ||
            memcmp(base + DLUMA_UI_KEYBIND_FORMAT_RVA, format_prefix, sizeof(format_prefix)) != 0)
            return 0;
    }
    g_dluma_ui_map_virtual_key = (DlumaUiMapVirtualKeyFn)GetProcAddress(user32, "MapVirtualKeyW");
    if (!g_dluma_ui_map_virtual_key) return 0;
    g_dluma_ui_keybind_module = (BYTE *)module;
    g_dluma_ui_keybind_callbacks[0] = (void *)dluma_ui_keybind_begin_callback;
    g_dluma_ui_keybind_callbacks[1] = (void *)dluma_ui_keybind_end_callback;
    g_dluma_ui_keybind_callbacks[2] = (void *)dluma_ui_keybind_commit_callback;
    g_dluma_ui_keybind_callbacks[3] = g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_FORMAT_RVA;
    return 1;
}

static void *dluma_ui_mod_config_add_keybind_row(void *list, const char *label, int virtual_key)
{
    DlumaTtlString option_template;
    DlumaTtlWideString row_text;
    WCHAR row_storage[DLUMA_UI_WIDE_ROW_CAPACITY];
    DlumaUiKeybindInput input;
    void *item;
    if (!g_dluma_list_add_item || !dluma_ui_keybind_ready() ||
        !dluma_ttl_wide_string_init_utf8(&row_text, row_storage, DLUMA_ARRAY_COUNT(row_storage), label))
        return 0;
    dluma_ttl_string_init(&option_template, "KeySelectDummy");
    item = g_dluma_list_add_item(list, &row_text, 0, &option_template, 1, -1);
    if (!item || !dluma_ui_range_is_writable(item, 288) ||
        *(void **)item != g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_VTABLE_RVA)
        return 0;
    *(void **)((BYTE *)item + DLUMA_UI_KEYBIND_OWNER_OFFSET) = &g_dluma_ui_keybind_interface;
    dluma_ui_keybind_from_virtual(virtual_key, &input);
    memcpy((BYTE *)item + DLUMA_UI_KEYBIND_INPUT_OFFSET, &input, sizeof(input));
    ((DlumaUiKeybindUpdateFn)(g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_UPDATE_TEXT_RVA))(item);
    dluma_ui_set_element_disabled(item, 0);
    if (g_dluma_set_element_can_focus) g_dluma_set_element_can_focus(item, 1);
    return item;
}

static int dluma_ui_mod_config_begin_keybind(void *screen, unsigned int index)
{
    unsigned int row;
    (void)screen;
    for (row = 0; row < g_dluma_ui_mod_config_row_count; ++row) {
        void *item = g_dluma_ui_mod_config_rows[row].item;
        if (g_dluma_ui_mod_config_rows[row].setting_index == index && dluma_ui_keybind_native_valid(item)) {
            dluma_ui_focus_element(item);
            ((DlumaUiKeybindBeginFn)(g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_BEGIN_RVA))(item);
            return g_dluma_ui_keybind_active_item == item;
        }
    }
    return 0;
}

static void dluma_ui_mod_config_cancel_keybind(void)
{
    void *item = g_dluma_ui_keybind_active_item;
    if (dluma_ui_keybind_native_valid(item) &&
        *((BYTE *)item + DLUMA_UI_KEYBIND_EDITING_OFFSET))
        ((DlumaUiKeybindFinishFn)(g_dluma_ui_keybind_module + DLUMA_UI_KEYBIND_FINISH_RVA))(item, 0);
    g_dluma_ui_keybind_active_item = 0;
    g_dluma_ui_keybind_commit_item = 0;
    g_dluma_ui_keybind_setting_index = 0xFFFFFFFFUL;
    if (g_dluma_ui_mod_config_edit_mode == 2) g_dluma_ui_mod_config_edit_mode = 0;
}

static int dluma_ui_mod_config_release_keybind_rows(void)
{
    unsigned int row;
    ULONGLONG started;
    if (dluma_atomic_compare_exchange(&g_dluma_registry_active_calls, 0, 0) != 0)
        return 0;
    dluma_ui_mod_config_cancel_keybind();
    for (row = 0; row < g_dluma_ui_mod_config_row_count; ++row) {
        void *item = g_dluma_ui_mod_config_rows[row].item;
        void * volatile *owner;
        if (!dluma_ui_keybind_native_valid(item)) continue;
        owner = (void * volatile *)((BYTE *)item + DLUMA_UI_KEYBIND_OWNER_OFFSET);
        if (!dluma_ui_range_is_writable((void *)owner, sizeof(void *))) return 0;
        (void)dluma_atomic_pointer_compare_exchange(owner, 0, &g_dluma_ui_keybind_interface);
    }
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(&g_dluma_registry_active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < 1000)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&g_dluma_registry_active_calls, 0, 0) != 0)
        return 0;
    g_dluma_ui_keybind_module = 0;
    g_dluma_ui_map_virtual_key = 0;
    return 1;
}
