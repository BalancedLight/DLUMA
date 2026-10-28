/*
 * Loader-owned UI lifecycle corrections layered over the validated dluma_ui.c
 * implementation.  Keeping the original translation unit included here makes
 * every exact-build static validator and teardown path remain authoritative;
 * only the two MenuSteamWorkshop lifecycle callbacks and public start/stop
 * entry points are wrapped.
 */
#define dluma_ui_start dluma_ui_start_base
#define dluma_ui_stop dluma_ui_stop_base
#define dluma_ui_workshop_post_hook dluma_ui_workshop_post_hook_base
#define dluma_ui_workshop_show_hook dluma_ui_workshop_show_hook_base
#include "dlw_ui.c"
#undef dluma_ui_start
#undef dluma_ui_stop
#undef dluma_ui_workshop_post_hook
#undef dluma_ui_workshop_show_hook

#include "dlw_main_menu_native_presentation.h"

#define DLUMA_UI_FIX_MODS_VISIBLE_ROWS 10
#define DLUMA_UI_FIX_WORKSHOP_VISIBLE_ROWS 6
#define DLUMA_UI_FIX_LOGS_VISIBLE_ROWS 9
#define DLUMA_UI_FIX_PATCH_TIMER_MS 100U
#define DLUMA_UI_FIX_MOD_COLUMN_COUNT 3U

typedef struct DlumaUiFixModColumnSlot
{
    void *type;
    void *status;
    void *source;
} DlumaUiFixModColumnSlot;

static UINT_PTR g_dluma_ui_fix_timer_id;
static volatile LONG g_dluma_ui_fix_settings_sync_depth;
static volatile LONG g_dluma_ui_fix_settings_initial_focus_pending;
static volatile LONG g_dluma_ui_fix_mod_config_initial_focus_pending;
static volatile LONG g_dluma_ui_fix_navigation_sync_depth;
static DlumaUiFixModColumnSlot
    g_dluma_ui_fix_registry_columns[DLUMA_UI_FIX_MODS_VISIBLE_ROWS];
static void *g_dluma_ui_fix_registry_column_screen;
static unsigned char g_dluma_ui_fix_registry_columns_ready;
static void *g_dluma_ui_fix_registry_resolved_screen;
static volatile LONG g_dluma_ui_fix_registry_columns_log_once;
static void *g_dluma_ui_fix_logs_message_controls[
    DLUMA_UI_FIX_LOGS_VISIBLE_ROWS];
static void *g_dluma_ui_fix_logs_message_screen;
static unsigned char g_dluma_ui_fix_logs_messages_ready;
static volatile LONG g_dluma_ui_fix_logs_initial_focus_pending;
static volatile LONG g_dluma_ui_fix_logs_navigation_sync_depth;

static const char *const g_dluma_ui_fix_registry_column_ids
    [DLUMA_UI_FIX_MODS_VISIBLE_ROWS][DLUMA_UI_FIX_MOD_COLUMN_COUNT] =
{
    {"T_ModRow0Type", "T_ModRow0Status", "T_ModRow0Source"},
    {"T_ModRow1Type", "T_ModRow1Status", "T_ModRow1Source"},
    {"T_ModRow2Type", "T_ModRow2Status", "T_ModRow2Source"},
    {"T_ModRow3Type", "T_ModRow3Status", "T_ModRow3Source"},
    {"T_ModRow4Type", "T_ModRow4Status", "T_ModRow4Source"},
    {"T_ModRow5Type", "T_ModRow5Status", "T_ModRow5Source"},
    {"T_ModRow6Type", "T_ModRow6Status", "T_ModRow6Source"},
    {"T_ModRow7Type", "T_ModRow7Status", "T_ModRow7Source"},
    {"T_ModRow8Type", "T_ModRow8Status", "T_ModRow8Source"},
    {"T_ModRow9Type", "T_ModRow9Status", "T_ModRow9Source"}
};

static const char *const g_dluma_ui_fix_logs_message_ids[
    DLUMA_UI_FIX_LOGS_VISIBLE_ROWS] =
{
    "T_LogRow0Message", "T_LogRow1Message", "T_LogRow2Message",
    "T_LogRow3Message", "T_LogRow4Message", "T_LogRow5Message",
    "T_LogRow6Message", "T_LogRow7Message", "T_LogRow8Message"
};

static void __fastcall dluma_ui_workshop_post_hook(void *screen);
static void __fastcall dluma_ui_workshop_show_hook(void *screen, void *ui_data);

static void dluma_ui_fix_hide_logs_messages(void)
{
    unsigned int slot;
    for (slot = 0; slot < DLUMA_UI_FIX_LOGS_VISIBLE_ROWS; ++slot)
    {
        void *control = g_dluma_ui_fix_logs_message_controls[slot];
        if (!control)
            continue;
        dluma_ui_set_text_element_text(control, "");
        dluma_ui_set_element_visible(control, 0);
    }
}

static int dluma_ui_fix_resolve_logs_messages(void *screen)
{
    unsigned int slot;
    if (!screen)
        return 0;
    if (g_dluma_ui_fix_logs_message_screen != screen)
    {
        memset(g_dluma_ui_fix_logs_message_controls, 0,
               sizeof(g_dluma_ui_fix_logs_message_controls));
        g_dluma_ui_fix_logs_message_screen = screen;
        g_dluma_ui_fix_logs_messages_ready = 0;
    }
    if (g_dluma_ui_fix_logs_messages_ready)
        return 1;
    for (slot = 0; slot < DLUMA_UI_FIX_LOGS_VISIBLE_ROWS; ++slot)
    {
        g_dluma_ui_fix_logs_message_controls[slot] =
            dluma_ui_find_required_child(
                screen, g_dluma_ui_fix_logs_message_ids[slot]);
        if (!g_dluma_ui_fix_logs_message_controls[slot])
        {
            memset(g_dluma_ui_fix_logs_message_controls, 0,
                   sizeof(g_dluma_ui_fix_logs_message_controls));
            return 0;
        }
    }
    g_dluma_ui_fix_logs_messages_ready = 1;
    return 1;
}

static void dluma_ui_fix_present_logs_messages(void *screen)
{
    unsigned int slot;
    int first_visible = -1;
    if (!dluma_ui_fix_resolve_logs_messages(screen) ||
        !g_dluma_ui_logs_list ||
        !dluma_ui_workshop_get_first_visible(
            g_dluma_ui_logs_list, &first_visible) ||
        first_visible < 0)
    {
        dluma_ui_fix_hide_logs_messages();
        return;
    }
    for (slot = 0; slot < DLUMA_UI_FIX_LOGS_VISIBLE_ROWS; ++slot)
    {
        int row_index = first_visible + (int)slot;
        void *control = g_dluma_ui_fix_logs_message_controls[slot];
        int visible = row_index >= 0 &&
            row_index < (int)g_dluma_ui_logs_row_count;
        if (visible)
        {
            const DlumaLogsRowMap *mapping =
                &g_dluma_ui_logs_rows[row_index];
            const DlumaLogHistoryEntry *entry =
                &g_dluma_ui_logs_snapshot[mapping->snapshot_index];
            char message[DLUMA_LOGS_ROW_MESSAGE_LIMIT + 4U];
            DlumaUiVec4 header_color;
            DlumaUiVec4 message_color;
            void *header_text = mapping->item
                ? dluma_ui_find_required_child(mapping->item, "Text")
                : 0;
            void *value_wrap = mapping->item
                ? dluma_ui_find_required_child(mapping->item, "wrap")
                : 0;
            if (value_wrap)
                dluma_ui_set_element_visible(value_wrap, 0);
            dluma_logs_format_row_message(
                entry, message, DLUMA_ARRAY_COUNT(message));
            dluma_ui_set_text_element_text(control, message);
            dluma_ui_logs_severity_colors(
                entry->severity, &header_color, &message_color);
            if (mapping->item && g_dluma_set_element_text_color)
                g_dluma_set_element_text_color(
                    mapping->item, &header_color, 0);
            if (g_dluma_set_text_element_color)
            {
                DlumaUiVec3 header_rgb;
                DlumaUiVec3 message_rgb;
                header_rgb.x = header_color.x;
                header_rgb.y = header_color.y;
                header_rgb.z = header_color.z;
                message_rgb.x = message_color.x;
                message_rgb.y = message_color.y;
                message_rgb.z = message_color.z;
                if (header_text)
                    g_dluma_set_text_element_color(
                        header_text, &header_rgb);
                g_dluma_set_text_element_color(control, &message_rgb);
            }
        }
        else
        {
            dluma_ui_set_text_element_text(control, "");
        }
        dluma_ui_set_element_visible(control, visible ? 1 : 0);
    }
}

static int dluma_ui_fix_write_slot(
    void **slot, void *expected, void *replacement)
{
    DWORD old_protect;
    DWORD restored_protect;
    int changed = 0;
    if (!slot || !dluma_is_readable(slot, sizeof(*slot)))
        return 0;
    if (*slot == replacement)
        return 1;
    if (*slot != expected)
        return 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return 0;
    if (*slot == expected)
    {
        *slot = replacement;
        changed = 1;
    }
    if (!VirtualProtect(slot, sizeof(*slot), old_protect, &restored_protect))
    {
        if (changed)
            *slot = expected;
        VirtualProtect(slot, sizeof(*slot), old_protect, &restored_protect);
        return 0;
    }
    return changed;
}

static void dluma_ui_fix_install_lifecycle_hooks(void)
{
    int post_ready = 0;
    int show_ready = 0;
    if (g_dluma_workshop_post_vtable_slot)
        post_ready = dluma_ui_fix_write_slot(
            g_dluma_workshop_post_vtable_slot,
            (void *)dluma_ui_workshop_post_hook_base,
            (void *)dluma_ui_workshop_post_hook);
    if (g_dluma_workshop_show_vtable_slot)
        show_ready = dluma_ui_fix_write_slot(
            g_dluma_workshop_show_vtable_slot,
            (void *)dluma_ui_workshop_show_hook_base,
            (void *)dluma_ui_workshop_show_hook);
    if (post_ready && show_ready)
    {
        static volatile LONG logged;
        if (dluma_atomic_compare_exchange(&logged, 1, 0) == 0)
            dluma_log_line(
                "INFO",
                "Menu row, preview re-entry, and pointer-focus lifecycle wrapper installed.");
    }
}

static void dluma_ui_fix_restore_lifecycle_hooks(void)
{
    if (g_dluma_workshop_post_vtable_slot &&
        dluma_is_readable(g_dluma_workshop_post_vtable_slot, sizeof(void *)) &&
        *g_dluma_workshop_post_vtable_slot ==
            (void *)dluma_ui_workshop_post_hook)
        (void)dluma_ui_fix_write_slot(
            g_dluma_workshop_post_vtable_slot,
            (void *)dluma_ui_workshop_post_hook,
            (void *)dluma_ui_workshop_post_hook_base);
    if (g_dluma_workshop_show_vtable_slot &&
        dluma_is_readable(g_dluma_workshop_show_vtable_slot, sizeof(void *)) &&
        *g_dluma_workshop_show_vtable_slot ==
            (void *)dluma_ui_workshop_show_hook)
        (void)dluma_ui_fix_write_slot(
            g_dluma_workshop_show_vtable_slot,
            (void *)dluma_ui_workshop_show_hook,
            (void *)dluma_ui_workshop_show_hook_base);
}

static void dluma_ui_fix_set_capacity(void *list, int rows)
{
    if (list && rows > 0 && g_dluma_list_set_capacity)
        (void)g_dluma_list_set_capacity(list, rows, 1);
}

static int dluma_ui_fix_logs_row_index_for_sequence(
    ULONGLONG sequence)
{
    unsigned int index;
    if (!sequence)
        return -1;
    for (index = 0; index < g_dluma_ui_logs_row_count; ++index)
        if (g_dluma_ui_logs_rows[index].sequence == sequence)
            return (int)index;
    return -1;
}

static void dluma_ui_fix_sync_logs_navigation(int apply_initial_focus)
{
    int count;
    int selected;
    int focused;
    int previous;
    int navigation_candidate;
    int candidate;
    int target;
    int clamped_wrap = 0;
    if (!g_dluma_ui_logs_list ||
        dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_logs_navigation_sync_depth, 1, 0) != 0)
        return;
    if (dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_logs_initial_focus_pending, 0, 0) != 0 &&
        !apply_initial_focus)
    {
        dluma_atomic_exchange(
            &g_dluma_ui_fix_logs_navigation_sync_depth, 0);
        return;
    }
    count = (int)g_dluma_ui_logs_row_count;
    if (count <= 0)
    {
        if (apply_initial_focus)
            dluma_atomic_exchange(
                &g_dluma_ui_fix_logs_initial_focus_pending, 0);
        dluma_atomic_exchange(
            &g_dluma_ui_fix_logs_navigation_sync_depth, 0);
        return;
    }
    if (apply_initial_focus &&
        dluma_atomic_exchange(
            &g_dluma_ui_fix_logs_initial_focus_pending, 0))
    {
        int active = dluma_ui_fix_logs_row_index_for_sequence(
            g_dluma_ui_logs_selected_sequence);
        if (active < 0) active = 0;
        dluma_ui_list_set_selection(g_dluma_ui_logs_list, active);
        (void)dluma_ui_workshop_set_focus(
            g_dluma_ui_logs_list, active, 1);
        (void)dluma_ui_workshop_set_first_visible(
            g_dluma_ui_logs_list, active);
        dluma_ui_focus_element(g_dluma_ui_logs_list);
        dluma_ui_logs_update_detail((unsigned int)active);
        dluma_atomic_exchange(
            &g_dluma_ui_fix_logs_navigation_sync_depth, 0);
        return;
    }

    selected = dluma_ui_list_get_selection(g_dluma_ui_logs_list);
    focused = dluma_ui_list_get_focused_index(g_dluma_ui_logs_list);
    previous = dluma_ui_fix_logs_row_index_for_sequence(
        g_dluma_ui_logs_selected_sequence);
    navigation_candidate = focused >= 0 && focused < count
                               ? focused
                               : selected;
    if (selected >= 0 && selected < count &&
        g_dluma_ui_logs_rows[selected].sequence ==
            g_dluma_ui_logs_selected_sequence)
        candidate = selected;
    else
        candidate = focused >= 0 && focused < count
                        ? focused
                        : selected;
    if (candidate < 0 || candidate >= count)
        candidate = previous >= 0 ? previous : 0;
    target = candidate;

    /* UIListBox wraps end-to-end before its owner observes the directional
     * input.  A pointer click has already updated selected_sequence in the
     * button hook, while keyboard traversal has not.  That distinction lets
     * us clamp only an actual boundary wrap without breaking direct clicks. */
    if (count > 1 && previous == 0 &&
        navigation_candidate == count - 1)
    {
        target = 0;
        clamped_wrap = 1;
    }
    else if (count > 1 && previous == count - 1 &&
             navigation_candidate == 0)
    {
        target = count - 1;
        clamped_wrap = 1;
    }

    if (selected != target)
        dluma_ui_list_set_selection(g_dluma_ui_logs_list, target);
    if (focused != target)
        (void)dluma_ui_workshop_set_focus(
            g_dluma_ui_logs_list, target, 1);
    if (previous != target)
        dluma_ui_logs_update_detail((unsigned int)target);
    if (clamped_wrap)
    {
        int first_visible = target == 0
                                ? 0
                                : count - DLUMA_UI_FIX_LOGS_VISIBLE_ROWS;
        if (first_visible < 0)
            first_visible = 0;
        (void)dluma_ui_workshop_set_first_visible(
            g_dluma_ui_logs_list, first_visible);
    }
    dluma_atomic_exchange(
        &g_dluma_ui_fix_logs_navigation_sync_depth, 0);
}

static void dluma_ui_fix_hide_registry_columns(void)
{
    unsigned int slot;
    for (slot = 0; slot < DLUMA_UI_FIX_MODS_VISIBLE_ROWS; ++slot)
    {
        DlumaUiFixModColumnSlot *columns =
            &g_dluma_ui_fix_registry_columns[slot];
        if (columns->type)
            dluma_ui_set_element_visible(columns->type, 0);
        if (columns->status)
            dluma_ui_set_element_visible(columns->status, 0);
        if (columns->source)
            dluma_ui_set_element_visible(columns->source, 0);
    }
}

static int dluma_ui_fix_resolve_registry_columns(void *screen)
{
    unsigned int slot;
    if (!screen)
        return 0;
    if (g_dluma_ui_fix_registry_columns_ready &&
        g_dluma_ui_fix_registry_column_screen == screen)
        return 1;
    /* A different screen pointer means the previous XUI scene may already
     * have been destroyed. Drop its cached controls without dereferencing
     * them; the newly resolved scene owns the replacement controls. */
    memset(g_dluma_ui_fix_registry_columns, 0,
           sizeof(g_dluma_ui_fix_registry_columns));
    g_dluma_ui_fix_registry_columns_ready = 0;
    g_dluma_ui_fix_registry_column_screen = screen;
    for (slot = 0; slot < DLUMA_UI_FIX_MODS_VISIBLE_ROWS; ++slot)
    {
        DlumaUiFixModColumnSlot *columns =
            &g_dluma_ui_fix_registry_columns[slot];
        columns->type = dluma_ui_registry_find_control(
            screen, g_dluma_ui_fix_registry_column_ids[slot][0]);
        columns->status = dluma_ui_registry_find_control(
            screen, g_dluma_ui_fix_registry_column_ids[slot][1]);
        columns->source = dluma_ui_registry_find_control(
            screen, g_dluma_ui_fix_registry_column_ids[slot][2]);
        if (!columns->type || !columns->status || !columns->source)
        {
            memset(g_dluma_ui_fix_registry_columns, 0,
                   sizeof(g_dluma_ui_fix_registry_columns));
            return 0;
        }
    }
    g_dluma_ui_fix_registry_columns_ready = 1;
    if (dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_registry_columns_log_once, 1, 0) == 0)
        dluma_log_line(
            "INFO",
            "MenuDLUMAMods fixed Type, Status, and Source columns resolved.");
    return 1;
}

static DlumaRegistryRowMap *dluma_ui_fix_registry_row_for_list_index(
    int list_index)
{
    unsigned int index;
    for (index = 0; index < g_dluma_ui_registry_row_count; ++index)
        if (g_dluma_ui_registry_rows[index].list_index == list_index)
            return &g_dluma_ui_registry_rows[index];
    return 0;
}

static void dluma_ui_fix_set_registry_column_text(
    void *control, const char *value, int visible)
{
    if (!control)
        return;
    dluma_ui_set_text_element_text(
        control, visible && value ? value : "");
    dluma_ui_set_element_visible(control, visible ? 1 : 0);
}

static void dluma_ui_fix_set_registry_native_value_visible(
    DlumaRegistryRowMap *mapping, int visible)
{
    void *value_wrap;
    if (!mapping || !mapping->item)
        return;
    value_wrap = dluma_ui_find_required_child(mapping->item, "wrap");
    if (value_wrap)
        dluma_ui_set_element_visible(value_wrap, visible ? 1 : 0);
}

static void dluma_ui_fix_prepare_registry_column_row(
    DlumaRegistryRowMap *mapping)
{
    char compact_name[DLUMA_MOD_ROW_TITLE_RENDER_BYTES + 4U];
    if (!mapping || !mapping->item)
        return;
    if (dluma_mod_row_format_utf8_ellipsis(
            mapping->display_name, DLUMA_MOD_ROW_TITLE_RENDER_BYTES,
            compact_name, DLUMA_ARRAY_COUNT(compact_name)))
        (void)dluma_ui_set_element_text_association(
            mapping->item, compact_name, 0);
    dluma_ui_fix_set_registry_native_value_visible(mapping, 0);
}

static void dluma_ui_fix_present_registry_columns(void *screen)
{
    unsigned int slot;
    int first_visible = -1;
    int list_count;
    if (!dluma_ui_fix_resolve_registry_columns(screen) ||
        !g_dluma_ui_registry_list ||
        !dluma_ui_workshop_get_first_visible(
            g_dluma_ui_registry_list, &first_visible))
    {
        dluma_ui_fix_hide_registry_columns();
        for (slot = 0; slot < g_dluma_ui_registry_row_count; ++slot)
            dluma_ui_fix_set_registry_native_value_visible(
                &g_dluma_ui_registry_rows[slot], 1);
        return;
    }
    list_count = dluma_ui_workshop_list_count(g_dluma_ui_registry_list);
    if (first_visible < 0 || first_visible >= list_count)
    {
        dluma_ui_fix_hide_registry_columns();
        for (slot = 0; slot < g_dluma_ui_registry_row_count; ++slot)
            dluma_ui_fix_set_registry_native_value_visible(
                &g_dluma_ui_registry_rows[slot], 1);
        return;
    }
    for (slot = 0; slot < DLUMA_UI_FIX_MODS_VISIBLE_ROWS; ++slot)
    {
        DlumaUiFixModColumnSlot *columns =
            &g_dluma_ui_fix_registry_columns[slot];
        int list_index = first_visible + (int)slot;
        int row_ready;
        DlumaRegistryRowMap *mapping =
            list_index < list_count
                ? dluma_ui_fix_registry_row_for_list_index(list_index)
                : 0;
        row_ready = mapping && mapping->item;
        if (row_ready)
            dluma_ui_fix_prepare_registry_column_row(mapping);
        dluma_ui_fix_set_registry_column_text(
            columns->type,
            row_ready ? mapping->display_type : "", row_ready);
        dluma_ui_fix_set_registry_column_text(
            columns->status,
            row_ready ? mapping->display_status : "", row_ready);
        dluma_ui_fix_set_registry_column_text(
            columns->source,
            row_ready ? mapping->display_source : "", row_ready);
    }
}

static void dluma_ui_fix_set_item_navigation(void *item, int enabled)
{
    if (!item)
        return;
    /* UIMenuScreen's native list traversal deliberately skips disabled items;
     * CanFocus alone only suppresses focus visuals and leaves a divider as a
     * directional dead end. Keep category rows visible, but make them real
     * non-selectable list entries for both keyboard and controller input. */
    dluma_ui_set_element_disabled(item, enabled ? 0 : 1);
    if (g_dluma_set_element_can_focus)
        g_dluma_set_element_can_focus(
            item, (unsigned char)(enabled ? 1 : 0));
    if (g_dluma_process_element_inputs)
        g_dluma_process_element_inputs(
            item, (unsigned char)(enabled ? 1 : 0));
}

static int dluma_ui_fix_registry_index_is_selectable(int list_index)
{
    unsigned int i;
    if (list_index < 0)
        return 0;
    for (i = 0; i < g_dluma_ui_registry_row_count; ++i)
    {
        const DlumaRegistryRowMap *mapping = &g_dluma_ui_registry_rows[i];
        if (mapping->item && mapping->list_index == list_index)
            return 1;
    }
    return 0;
}

static void dluma_ui_fix_rebind_registry_rows(void)
{
    unsigned int i;
    int list_count;
    /* ShowScreen/PostUpdate can replace native UIOptionTextSelect row
     * objects. Rebind validated live items before the fixed-column presenter
     * applies the visible label/value layout. */
    list_count = dluma_ui_workshop_list_count(g_dluma_ui_registry_list);
    for (i = 0; i < g_dluma_ui_registry_row_count; ++i)
    {
        DlumaRegistryRowMap *mapping = &g_dluma_ui_registry_rows[i];
        void *live_item;
        if (mapping->list_index < 0 || mapping->list_index >= list_count)
        {
            mapping->item = 0;
            continue;
        }
        live_item = dluma_ui_workshop_list_item(
            g_dluma_ui_registry_list, (unsigned int)mapping->list_index);
        if (!dluma_mod_row_format_rebind_live_item(
                list_count, mapping->list_index, live_item, &mapping->item))
        {
            mapping->item = 0;
            continue;
        }
        /* The original registry population already assigns navigation state
         * to option rows and disables category headers.  Rebinding only
         * refreshes the cached object pointer used by the fixed-column
         * presenter.  Reprocessing native inputs here replays the highlight
         * presenter and its sound whenever the engine swaps a row object. */
    }
}

static int dluma_ui_fix_registry_nearest_selectable_index(int preferred)
{
    int count;
    int index;
    int distance;
    if (!g_dluma_ui_registry_list)
        return -1;
    count = dluma_ui_workshop_list_count(g_dluma_ui_registry_list);
    if (count <= 0)
        return -1;
    if (preferred < 0)
        preferred = 0;
    if (preferred >= count)
        preferred = count - 1;
    for (distance = 0; distance < count; ++distance)
    {
        index = preferred + distance;
        if (index < count && dluma_ui_fix_registry_index_is_selectable(index))
            return index;
        index = preferred - distance;
        if (distance && index >= 0 &&
            dluma_ui_fix_registry_index_is_selectable(index))
            return index;
    }
    return -1;
}

static void dluma_ui_fix_repair_registry_navigation(void)
{
    int selected;
    int focused;
    int target = -1;
    int list_count;
    if (!g_dluma_ui_registry_list ||
        dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_navigation_sync_depth, 1, 0) != 0)
        return;

    list_count = dluma_ui_workshop_list_count(g_dluma_ui_registry_list);

    selected = dluma_ui_list_get_selection(g_dluma_ui_registry_list);
    focused = dluma_ui_list_get_focused_index(g_dluma_ui_registry_list);
    if (dluma_ui_fix_registry_index_is_selectable(focused))
        target = focused;
    else if (dluma_ui_fix_registry_index_is_selectable(selected))
        target = selected;
    else
        target = dluma_ui_fix_registry_nearest_selectable_index(
            focused >= 0 ? focused : selected);

    if (target >= 0)
    {
        if (selected != target)
            dluma_ui_list_set_selection(g_dluma_ui_registry_list, target);
        if (focused != target)
            (void)dluma_ui_workshop_set_focus(
                g_dluma_ui_registry_list, target, 1);
    }
    dluma_atomic_exchange(&g_dluma_ui_fix_navigation_sync_depth, 0);
}

static void dluma_ui_fix_repair_workshop_navigation(void *screen)
{
    void *list;
    int selected;
    int focused = -1;
    int target;
    if (!screen ||
        dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_navigation_sync_depth, 1, 0) != 0)
        return;
    list = dluma_ui_workshop_screen_list(screen);
    if (!list)
    {
        dluma_atomic_exchange(&g_dluma_ui_fix_navigation_sync_depth, 0);
        return;
    }
    for (target = 0;
         target < dluma_ui_workshop_list_count(list); ++target)
        dluma_ui_fix_set_item_navigation(
            dluma_ui_workshop_list_item(list, (unsigned int)target),
            dluma_ui_workshop_is_selectable_index(list, target));

    selected = dluma_ui_list_get_selection(list);
    (void)dluma_ui_workshop_get_focus(list, &focused);
    if (dluma_ui_workshop_is_selectable_index(list, focused))
        target = focused;
    else
        target = dluma_ui_workshop_nearest_selectable_index(
            list, focused >= 0 ? focused : selected);
    if (target >= 0)
    {
        if (selected != target)
            dluma_ui_list_set_selection(list, target);
        if (focused != target)
            (void)dluma_ui_workshop_set_focus(list, target, 1);
    }
    dluma_atomic_exchange(&g_dluma_ui_fix_navigation_sync_depth, 0);
}

static void dluma_ui_fix_sync_settings_active_row(void)
{
    int selected;
    unsigned int active = g_dluma_ui_settings_selected_row;
    if (!g_dluma_ui_settings_list || g_dluma_ui_settings_edit_mode ||
        g_dluma_ui_settings_experimental_warning_mode ||
        active >= g_dluma_ui_settings_row_count ||
        dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_settings_sync_depth, 1, 0) != 0)
        return;
    selected = dluma_ui_list_get_selection(g_dluma_ui_settings_list);
    if (selected != (int)active)
        dluma_ui_list_set_selection(g_dluma_ui_settings_list, (int)active);
    dluma_atomic_exchange(&g_dluma_ui_fix_settings_sync_depth, 0);
}

static void dluma_ui_fix_apply_settings_initial_focus(void)
{
    if (dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_settings_initial_focus_pending, 0, 0) == 0 ||
        !g_dluma_ui_settings_list || g_dluma_ui_settings_edit_mode ||
        g_dluma_ui_settings_experimental_warning_mode ||
        dluma_atomic_compare_exchange(
            &g_dluma_ui_fix_settings_sync_depth, 1, 0) != 0)
        return;
    if (!g_dluma_ui_settings_row_count)
    {
        dluma_atomic_exchange(
            &g_dluma_ui_fix_settings_initial_focus_pending, 0);
        dluma_atomic_exchange(&g_dluma_ui_fix_settings_sync_depth, 0);
        return;
    }
    {
        unsigned int active = g_dluma_ui_settings_selected_row < g_dluma_ui_settings_row_count
            ? g_dluma_ui_settings_selected_row : 0;
        dluma_ui_list_set_selection(g_dluma_ui_settings_list, (int)active);
        (void)dluma_ui_workshop_set_focus(g_dluma_ui_settings_list, (int)active, 1);
        dluma_ui_focus_element(g_dluma_ui_settings_list);
        dluma_ui_settings_update_for_row(active);
    }
    dluma_atomic_exchange(
        &g_dluma_ui_fix_settings_initial_focus_pending, 0);
    dluma_atomic_exchange(&g_dluma_ui_fix_settings_sync_depth, 0);
}

static void dluma_ui_fix_reset_workshop_to_top(void *screen)
{
    void *list = dluma_ui_workshop_screen_list(screen);
    int target;
    if (!list)
        return;
    target = dluma_ui_workshop_nearest_selectable_index(list, 0);
    if (target < 0)
        return;
    dluma_ui_list_set_selection(list, target);
    (void)dluma_ui_workshop_set_focus(list, target, 1);
    (void)dluma_ui_workshop_set_first_visible(list, target);
    dluma_ui_focus_element(list);
    dluma_ui_workshop_present(screen);
}

static void dluma_ui_fix_refresh_workshop_after_capacity(
    void *screen, int preserve_selection)
{
    void *list = dluma_ui_workshop_screen_list(screen);
    if (!list)
        return;
    dluma_ui_fix_set_capacity(list, DLUMA_UI_FIX_WORKSHOP_VISIBLE_ROWS);
    if (g_dluma_workshop_fill_maps_original)
        dluma_ui_workshop_refresh_now(
            screen, g_dluma_workshop_fill_maps_original);
    else
        dluma_ui_workshop_present(screen);
    if (!preserve_selection)
        dluma_ui_fix_reset_workshop_to_top(screen);
    if (g_dluma_workshop_menu_level)
        dluma_ui_workshop_texture_asset_tick(
            g_dluma_workshop_menu_level, 1);
}

static void __fastcall dluma_ui_workshop_show_hook(
    void *screen, void *ui_data)
{
    /* ShowScreen can rebuild children while retaining the same screen
     * address. Never carry the previous generation's controls across it. */
    memset(g_dluma_ui_fix_registry_columns, 0,
           sizeof(g_dluma_ui_fix_registry_columns));
    g_dluma_ui_fix_registry_column_screen = 0;
    g_dluma_ui_fix_registry_columns_ready = 0;
    g_dluma_ui_fix_registry_resolved_screen = 0;
    if (dluma_ui_is_stock_workshop_screen(screen))
        dluma_ui_fix_set_capacity(
            dluma_ui_workshop_screen_list(screen),
            DLUMA_UI_FIX_WORKSHOP_VISIBLE_ROWS);
    else if (dluma_ui_is_mods_registry_screen(screen) &&
             dluma_ui_registry_resolve_controls(screen))
        dluma_ui_fix_set_capacity(
            g_dluma_ui_registry_list,
            DLUMA_UI_FIX_MODS_VISIBLE_ROWS);
    else if (dluma_ui_is_mod_config_screen(screen) &&
             dluma_ui_mod_config_resolve_controls(screen))
        dluma_ui_fix_set_capacity(
            g_dluma_ui_mod_config_list,
            DLUMA_UI_MOD_CONFIG_VISIBLE_ROWS);
    dluma_ui_workshop_show_hook_base(screen, ui_data);

    if (dluma_ui_is_stock_workshop_screen(screen))
    {
        /* ShowScreen recreated row objects.  Rebuild once against the final
         * six-row capacity and immediately start cache-backed texture jobs;
         * this removes the previous need to press Refresh after Back/re-entry. */
        dluma_ui_fix_refresh_workshop_after_capacity(screen, 0);
        dluma_ui_fix_repair_workshop_navigation(screen);
    }
    else if (dluma_ui_is_mods_registry_screen(screen))
    {
        /* Base ShowScreen may recreate controls during its call. Resolve the
         * new scene and list rather than following cached row pointers. */
        memset(g_dluma_ui_fix_registry_columns, 0,
               sizeof(g_dluma_ui_fix_registry_columns));
        g_dluma_ui_fix_registry_column_screen = 0;
        g_dluma_ui_fix_registry_columns_ready = 0;
        if (!dluma_ui_registry_resolve_controls(screen))
        {
            unsigned int i;
            g_dluma_ui_registry_screen = 0;
            g_dluma_ui_registry_list = 0;
            for (i = 0; i < g_dluma_ui_registry_row_count; ++i)
                g_dluma_ui_registry_rows[i].item = 0;
            return;
        }
        g_dluma_ui_fix_registry_resolved_screen = screen;
        dluma_ui_fix_rebind_registry_rows();
        dluma_ui_fix_repair_registry_navigation();
        dluma_ui_fix_present_registry_columns(screen);
    }
    else if (dluma_ui_is_loader_settings_screen(screen))
    {
        dluma_atomic_exchange(
            &g_dluma_ui_fix_settings_initial_focus_pending, 1);
    }
    else if (dluma_ui_is_mod_config_screen(screen))
        dluma_atomic_exchange(
            &g_dluma_ui_fix_mod_config_initial_focus_pending, 1);
    else if (dluma_ui_is_logs_screen(screen))
    {
        /* The base ShowScreen only schedules the log list for deferred
         * population.  Discard every cached child pointer here and wait for
         * the first base PostUpdate to rebuild the list before presenting
         * message controls.  Touching the previous screen generation here
         * can call into an already-retired XUI object. */
        if (dluma_ui_logs_resolve_controls(screen))
            dluma_ui_fix_set_capacity(
                g_dluma_ui_logs_list,
                DLUMA_UI_FIX_LOGS_VISIBLE_ROWS);
        memset(g_dluma_ui_fix_logs_message_controls, 0,
               sizeof(g_dluma_ui_fix_logs_message_controls));
        g_dluma_ui_fix_logs_message_screen = screen;
        g_dluma_ui_fix_logs_messages_ready = 0;
        dluma_atomic_exchange(
            &g_dluma_ui_fix_logs_initial_focus_pending, 1);
    }
}

static void __fastcall dluma_ui_workshop_post_hook(void *screen)
{
    dluma_ui_workshop_post_hook_base(screen);

    if (dluma_ui_is_loader_settings_screen(screen))
    {
        dluma_ui_fix_apply_settings_initial_focus();
        dluma_ui_fix_sync_settings_active_row();
    }
    else if (dluma_ui_is_mods_registry_screen(screen))
    {
        if (g_dluma_ui_fix_registry_resolved_screen != screen ||
            g_dluma_ui_registry_screen != screen ||
            !g_dluma_ui_registry_list)
            return;
        dluma_ui_fix_set_capacity(
            g_dluma_ui_registry_list,
            DLUMA_UI_FIX_MODS_VISIBLE_ROWS);
        dluma_ui_fix_rebind_registry_rows();
        /* Do not force selection or focus from PostUpdate.  Mouse-wheel
         * scrolling may intentionally move the visible window while the
         * selected row remains elsewhere; refocusing that row every frame
         * snaps the list back and retriggers the native highlight cue. */
        dluma_ui_fix_present_registry_columns(screen);
    }
    else if (dluma_ui_is_mod_config_screen(screen))
    {
        dluma_ui_fix_set_capacity(
            g_dluma_ui_mod_config_list,
            DLUMA_UI_MOD_CONFIG_VISIBLE_ROWS);
        if (dluma_atomic_exchange(
                &g_dluma_ui_fix_mod_config_initial_focus_pending, 0) &&
            g_dluma_ui_mod_config_row_count)
        {
            unsigned int active = g_dluma_ui_mod_config_selected_row < g_dluma_ui_mod_config_row_count
                ? g_dluma_ui_mod_config_selected_row : 0;
            dluma_ui_list_set_selection(g_dluma_ui_mod_config_list, (int)active);
            (void)dluma_ui_workshop_set_focus(g_dluma_ui_mod_config_list, (int)active, 1);
            dluma_ui_focus_element(g_dluma_ui_mod_config_list);
        }
    }
    else if (dluma_ui_is_logs_screen(screen))
    {
        dluma_ui_fix_sync_logs_navigation(1);
        dluma_ui_fix_present_logs_messages(screen);
    }
    else if (dluma_ui_is_stock_workshop_screen(screen))
    {
        dluma_ui_fix_repair_workshop_navigation(screen);
    }
}

static VOID CALLBACK dluma_ui_fix_timer_proc(
    HWND window, unsigned int message, UINT_PTR timer_id, DWORD time_ms)
{
    (void)window;
    (void)message;
    (void)timer_id;
    (void)time_ms;
    if (!g_dluma.stop_requested && !g_dluma_ui_stopping)
        dluma_ui_fix_install_lifecycle_hooks();
}

int dluma_ui_start(void)
{
    int result = dluma_ui_start_base();
    if (!result)
        return 0;
    g_dluma_ui_fix_settings_sync_depth = 0;
    g_dluma_ui_fix_settings_initial_focus_pending = 0;
    g_dluma_ui_fix_mod_config_initial_focus_pending = 0;
    g_dluma_ui_fix_navigation_sync_depth = 0;
    g_dluma_ui_fix_logs_initial_focus_pending = 0;
    g_dluma_ui_fix_logs_navigation_sync_depth = 0;
    memset(g_dluma_ui_fix_registry_columns, 0,
           sizeof(g_dluma_ui_fix_registry_columns));
    g_dluma_ui_fix_registry_column_screen = 0;
    g_dluma_ui_fix_registry_columns_ready = 0;
    g_dluma_ui_fix_registry_resolved_screen = 0;
    g_dluma_ui_fix_registry_columns_log_once = 0;
    memset(g_dluma_ui_fix_logs_message_controls, 0,
           sizeof(g_dluma_ui_fix_logs_message_controls));
    g_dluma_ui_fix_logs_message_screen = 0;
    g_dluma_ui_fix_logs_messages_ready = 0;
    dluma_ui_fix_install_lifecycle_hooks();
    if (!dluma_main_menu_native_presentation_start())
        dluma_log_line(
            "WARN",
            "Stock main-menu news integration is unavailable for this exact build; no ticker ownership was installed.");
    if (!g_dluma_ui_fix_timer_id)
        g_dluma_ui_fix_timer_id = SetTimer(
            0, 0, DLUMA_UI_FIX_PATCH_TIMER_MS,
            dluma_ui_fix_timer_proc);
    return 1;
}

void dluma_ui_stop(void)
{
    const int native_stopped = dluma_main_menu_native_presentation_stop();
    if (g_dluma_ui_fix_timer_id)
    {
        KillTimer(0, g_dluma_ui_fix_timer_id);
        g_dluma_ui_fix_timer_id = 0;
    }
    dluma_ui_fix_restore_lifecycle_hooks();
    g_dluma_ui_fix_settings_initial_focus_pending = 0;
    g_dluma_ui_fix_logs_initial_focus_pending = 0;
    g_dluma_ui_fix_logs_navigation_sync_depth = 0;
    memset(g_dluma_ui_fix_registry_columns, 0,
           sizeof(g_dluma_ui_fix_registry_columns));
    g_dluma_ui_fix_registry_column_screen = 0;
    g_dluma_ui_fix_registry_columns_ready = 0;
    g_dluma_ui_fix_registry_resolved_screen = 0;
    memset(g_dluma_ui_fix_logs_message_controls, 0,
           sizeof(g_dluma_ui_fix_logs_message_controls));
    g_dluma_ui_fix_logs_message_screen = 0;
    g_dluma_ui_fix_logs_messages_ready = 0;
    dluma_ui_stop_base();
    if (!native_stopped)
    {
        /* The native subsystem retained its code/data after a hook ownership
         * loss or drain timeout. Do not report a clean UI teardown. */
        dluma_atomic_exchange(&g_dluma_ui_teardown_failed, 1);
        dluma_atomic_exchange(&g_dluma_ui_stop_ok, 0);
    }
}
