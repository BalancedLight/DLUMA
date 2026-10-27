#include "dlw_common.h"

static int dluma_starts_word(const char* line, const char* word)
{
    SIZE_T n = strlen(word);
    if (!line || !word || memcmp(line, word, n) != 0) return 0;
    return line[n] == 0 || dluma_is_space(line[n]);
}

static void dluma_strip_comments(char* line)
{
    unsigned int i;
    int quoted = 0;
    if (!line) return;
    for (i = 0; line[i]; ++i) {
        if (quoted && line[i] == '\\' && line[i + 1]) { ++i; continue; }
        if (line[i] == '"') { quoted = !quoted; continue; }
        if (!quoted && line[i] == '#') { line[i] = 0; return; }
        if (!quoted && line[i] == '/' && line[i + 1] == '/') { line[i] = 0; return; }
    }
}

static int dluma_parse_key_code(const char* text)
{
    const char* p = dluma_skip_spaces(text);
    unsigned int n;
    if (!p || !*p) return 0;
    if ((p[0] == 'F' || p[0] == 'f') && dluma_is_digit(p[1])) {
        n = dluma_parse_u32(p + 1, 0);
        if (n >= 1 && n <= 12) return VK_F1 + (int)n - 1;
    }
    if (p[0] && !p[1]) {
        char c = p[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (int)c;
    }
    n = dluma_parse_u32(p, 0);
    return n <= 255 ? (int)n : 0;
}

static const char* dluma_basename_utf8(const WCHAR* path, char* out, unsigned int cap)
{
    const WCHAR* start = path;
    unsigned int i;
    if (!out || !cap) return "";
    out[0] = 0;
    if (!path) return out;
    for (i = 0; path[i]; ++i) if (path[i] == L'\\' || path[i] == L'/') start = path + i + 1;
    dluma_wide_to_utf8(start, out, cap);
    for (i = 0; out[i]; ++i) {
        if (out[i] == '.' && dluma_stricmp(out + i, ".dlscript") == 0) { out[i] = 0; break; }
    }
    return out;
}

static void dluma_log_parse_error(const WCHAR* path, unsigned int line_no, const char* message)
{
    char utf8[DLUMA_MAX_WPATH * 2];
    char number[32];
    char line[1400];
    dluma_wide_to_utf8(path, utf8, DLUMA_ARRAY_COUNT(utf8));
    dluma_u64_to_text(line_no, number, DLUMA_ARRAY_COUNT(number));
    line[0] = 0;
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), utf8);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), ":");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), number);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), ": ");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), message);
    dluma_log_line("ERROR", line);
}

static DlumaEvent* dluma_begin_event(DlumaScriptSet* set, DlumaScript* script, unsigned int script_index, DlumaEventType type)
{
    DlumaEvent* event;
    if (set->event_count >= DLUMA_MAX_EVENTS) return 0;
    event = &set->events[set->event_count++];
    memset(event, 0, sizeof(*event));
    event->type = (unsigned char)type;
    event->script_index = (unsigned char)script_index;
    event->first_action = (unsigned short)set->action_count;
    event->last_fire_ms = GetTickCount64();
    ++script->event_count;
    return event;
}

static DlumaAction* dluma_add_action(DlumaScriptSet* set, DlumaEvent* event, DlumaActionType type)
{
    DlumaAction* action;
    if (!event || set->action_count >= DLUMA_MAX_ACTIONS) return 0;
    action = &set->actions[set->action_count++];
    memset(action, 0, sizeof(*action));
    action->type = (unsigned char)type;
    event->action_count = (unsigned short)(set->action_count - event->first_action);
    return action;
}

static int dluma_parse_script_file(DlumaScriptSet* set, const WCHAR* path, const WCHAR* project_root)
{
    char* data = 0;
    DWORD size = 0;
    DWORD pos = 0;
    unsigned int line_no = 0;
    unsigned int script_index = set->script_count;
    unsigned int event_start = set->event_count;
    unsigned int action_start = set->action_count;
    DlumaScript* script;
    DlumaEvent* current = 0;
    int saw_header = 0;
    int valid = 1;
    int warned_future = 0;

    if (script_index >= DLUMA_MAX_SCRIPTS) return 0;
    if (!dluma_read_file_utf8(path, &data, &size)) {
        dluma_log_parse_error(path, 0, "could not read script or script exceeds 256 KiB");
        return 0;
    }

    script = &set->scripts[script_index];
    memset(script, 0, sizeof(*script));
    dluma_wstr_copy(script->file_path, DLUMA_MAX_WPATH, path);
    dluma_wstr_copy(script->project_root, DLUMA_MAX_WPATH, project_root);
    script->first_event = (unsigned short)event_start;
    dluma_basename_utf8(path, script->id, DLUMA_ARRAY_COUNT(script->id));
    dluma_str_copy(script->name, DLUMA_ARRAY_COUNT(script->name), script->id);

    while (pos <= size) {
        char line[1024];
        DWORD start = pos;
        DWORD len;
        unsigned int cursor;
        ++line_no;
        while (pos < size && data[pos] != '\n' && data[pos] != '\r') ++pos;
        len = pos - start;
        while (pos < size && (data[pos] == '\n' || data[pos] == '\r')) ++pos;
        if (len >= DLUMA_ARRAY_COUNT(line)) {
            dluma_log_parse_error(path, line_no, "line is longer than 1023 bytes");
            valid = 0;
            break;
        }
        if (len) memcpy(line, data + start, len);
        line[len] = 0;
        dluma_strip_comments(line);
        dluma_trim_in_place(line);
        if (!line[0]) {
            if (start >= size) break;
            continue;
        }

        if (!saw_header) {
            if (dluma_stricmp(line, "dlscript 1") != 0) {
                dluma_log_parse_error(path, line_no, "first statement must be: dlscript 1");
                valid = 0;
                break;
            }
            saw_header = 1;
            continue;
        }

        if (!current && dluma_starts_word(line, "id")) {
            cursor = 2;
            if (!dluma_extract_quoted(line, &cursor, script->id, DLUMA_ARRAY_COUNT(script->id)) || !script->id[0]) {
                dluma_log_parse_error(path, line_no, "id requires a quoted nonempty value"); valid = 0; break;
            }
            dluma_sanitize_filename(script->id);
            continue;
        }
        if (!current && dluma_starts_word(line, "name")) {
            cursor = 4;
            if (!dluma_extract_quoted(line, &cursor, script->name, DLUMA_ARRAY_COUNT(script->name))) {
                dluma_log_parse_error(path, line_no, "name requires a quoted value"); valid = 0; break;
            }
            continue;
        }
        if (!current && dluma_starts_word(line, "on")) {
            const char* spec = dluma_skip_spaces(line + 2);
            if (dluma_starts_word(spec, "map_load")) current = dluma_begin_event(set, script, script_index, DLUMA_EVENT_MAP_LOAD);
            else if (dluma_starts_word(spec, "map_unload")) current = dluma_begin_event(set, script, script_index, DLUMA_EVENT_MAP_UNLOAD);
            else if (dluma_starts_word(spec, "key")) {
                int key = dluma_parse_key_code(dluma_skip_spaces(spec + 3));
                current = key ? dluma_begin_event(set, script, script_index, DLUMA_EVENT_KEY) : 0;
                if (current) current->key_code = (unsigned short)key;
                else { dluma_log_parse_error(path, line_no, "key must be F1-F12, A-Z, 0-9, or a virtual-key number"); valid = 0; break; }
            }
            else if (dluma_starts_word(spec, "trigger_enter") || dluma_starts_word(spec, "trigger_exit") || dluma_starts_word(spec, "interact")) {
                DlumaEventType type = dluma_starts_word(spec, "trigger_enter") ? DLUMA_EVENT_TRIGGER_ENTER :
                                    (dluma_starts_word(spec, "trigger_exit") ? DLUMA_EVENT_TRIGGER_EXIT : DLUMA_EVENT_INTERACT);
                unsigned int word_len = type == DLUMA_EVENT_TRIGGER_ENTER ? 13u : (type == DLUMA_EVENT_TRIGGER_EXIT ? 12u : 8u);
                current = dluma_begin_event(set, script, script_index, type);
                cursor = word_len;
                if (!current || !dluma_extract_quoted(spec, &cursor, current->target, DLUMA_ARRAY_COUNT(current->target)) || !current->target[0]) {
                    dluma_log_parse_error(path, line_no, "trigger/interact event requires a quoted editor object name"); valid = 0; break;
                }
                warned_future = 1;
            }
            else {
                dluma_log_parse_error(path, line_no, "unknown on-event"); valid = 0; break;
            }
            if (!current) { dluma_log_parse_error(path, line_no, "event capacity exhausted"); valid = 0; break; }
            continue;
        }
        if (!current && dluma_starts_word(line, "every")) {
            unsigned int interval = dluma_parse_u32(dluma_skip_spaces(line + 5), 0);
            if (interval < 50 || interval > 86400000u) {
                dluma_log_parse_error(path, line_no, "every interval must be 50..86400000 milliseconds"); valid = 0; break;
            }
            current = dluma_begin_event(set, script, script_index, DLUMA_EVENT_TIMER);
            if (!current) { dluma_log_parse_error(path, line_no, "event capacity exhausted"); valid = 0; break; }
            current->interval_ms = interval;
            continue;
        }
        if (current && dluma_stricmp(line, "end") == 0) {
            current = 0;
            continue;
        }
        if (!current) {
            dluma_log_parse_error(path, line_no, "statement is not valid outside an event block"); valid = 0; break;
        }

        if (dluma_starts_word(line, "log")) {
            DlumaAction* action = dluma_add_action(set, current, DLUMA_ACTION_LOG);
            cursor = 3;
            if (!action || !dluma_extract_quoted(line, &cursor, action->first, DLUMA_ARRAY_COUNT(action->first))) {
                dluma_log_parse_error(path, line_no, "log requires one quoted message"); valid = 0; break;
            }
            continue;
        }
        if (dluma_starts_word(line, "marker")) {
            DlumaAction* action = dluma_add_action(set, current, DLUMA_ACTION_MARKER);
            cursor = 6;
            if (!action || !dluma_extract_quoted(line, &cursor, action->first, DLUMA_ARRAY_COUNT(action->first)) ||
                !dluma_extract_quoted(line, &cursor, action->second, DLUMA_ARRAY_COUNT(action->second))) {
                dluma_log_parse_error(path, line_no, "marker requires quoted filename and quoted contents"); valid = 0; break;
            }
            continue;
        }
        if (dluma_stricmp(line, "dump_context") == 0) {
            if (!dluma_add_action(set, current, DLUMA_ACTION_DUMP_CONTEXT)) {
                dluma_log_parse_error(path, line_no, "action capacity exhausted"); valid = 0; break;
            }
            continue;
        }
        dluma_log_parse_error(path, line_no, "unknown action");
        valid = 0;
        break;
    }

    if (current) { dluma_log_parse_error(path, line_no, "event block is missing end"); valid = 0; }
    if (!saw_header || script->event_count == 0) { dluma_log_parse_error(path, line_no, "script has no events"); valid = 0; }
    dluma_free(data);

    if (!valid) {
        set->event_count = event_start;
        set->action_count = action_start;
        memset(script, 0, sizeof(*script));
        return 0;
    }
    script->valid = 1;
    ++set->script_count;
    {
        char file_utf8[DLUMA_MAX_WPATH * 2];
        dluma_wide_to_utf8(path, file_utf8, DLUMA_ARRAY_COUNT(file_utf8));
        dluma_log_pair("INFO", "loaded_dlscript", file_utf8);
    }
    if (warned_future) dluma_log_line("WARN", "This script declares trigger/interact events. They are parsed but not dispatched by prototype 0.1.2.");
    return 1;
}

static void dluma_append_variable(char* out, unsigned int cap, const char* name, const DlumaScript* script)
{
    char temp[DLUMA_MAX_WPATH * 2];
    if (dluma_stricmp(name, "map") == 0) dluma_str_append(out, cap, g_dluma.active_map);
    else if (dluma_stricmp(name, "project") == 0) {
        dluma_wide_to_utf8(g_dluma.active_project_root, temp, DLUMA_ARRAY_COUNT(temp));
        dluma_str_append(out, cap, temp);
    }
    else if (dluma_stricmp(name, "script") == 0) dluma_str_append(out, cap, script ? script->id : "");
    else if (dluma_stricmp(name, "elapsed_ms") == 0) {
        dluma_u64_to_text(GetTickCount64() - g_dluma.map_loaded_ms, temp, DLUMA_ARRAY_COUNT(temp));
        dluma_str_append(out, cap, temp);
    }
    else if (dluma_stricmp(name, "loader_version") == 0) dluma_str_append(out, cap, DLUMA_VERSION);
    else {
        dluma_str_append(out, cap, "${");
        dluma_str_append(out, cap, name);
        dluma_str_append(out, cap, "}");
    }
}

static void dluma_expand_text(const char* input, const DlumaScript* script, char* out, unsigned int cap)
{
    unsigned int i = 0;
    if (!out || !cap) return;
    out[0] = 0;
    if (!input) return;
    while (input[i]) {
        if (input[i] == '$' && input[i + 1] == '{') {
            char name[64];
            unsigned int n = 0;
            i += 2;
            while (input[i] && input[i] != '}' && n + 1 < DLUMA_ARRAY_COUNT(name)) name[n++] = input[i++];
            name[n] = 0;
            if (input[i] == '}') ++i;
            dluma_append_variable(out, cap, name, script);
        } else {
            char one[2];
            one[0] = input[i++]; one[1] = 0;
            dluma_str_append(out, cap, one);
        }
    }
}

static void dluma_write_marker(const DlumaScript* script, const DlumaAction* action)
{
    char filename[DLUMA_MAX_TEXT];
    char contents[DLUMA_MAX_TEXT * 2];
    char map_part[DLUMA_MAX_MAP_NAME];
    char script_part[128];
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];
    WCHAR script_wide[128];
    WCHAR file_wide[DLUMA_MAX_TEXT];
    WCHAR path[DLUMA_MAX_WPATH];
    dluma_expand_text(action->first, script, filename, DLUMA_ARRAY_COUNT(filename));
    dluma_expand_text(action->second, script, contents, DLUMA_ARRAY_COUNT(contents));
    dluma_sanitize_filename(filename);
    if (!filename[0]) dluma_str_copy(filename, DLUMA_ARRAY_COUNT(filename), "marker.txt");
    dluma_str_copy(map_part, DLUMA_ARRAY_COUNT(map_part), g_dluma.active_map[0] ? g_dluma.active_map : "no_map");
    dluma_str_copy(script_part, DLUMA_ARRAY_COUNT(script_part), script && script->id[0] ? script->id : "unknown_script");
    dluma_sanitize_filename(map_part);
    dluma_sanitize_filename(script_part);
    dluma_utf8_to_wide(map_part, map_wide, DLUMA_ARRAY_COUNT(map_wide));
    dluma_utf8_to_wide(script_part, script_wide, DLUMA_ARRAY_COUNT(script_wide));
    dluma_utf8_to_wide(filename, file_wide, DLUMA_ARRAY_COUNT(file_wide));
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), g_dluma.marker_root, map_wide);
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), path, script_wide);
    dluma_mkdirs(path);
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), path, file_wide);
    dluma_str_append(contents, DLUMA_ARRAY_COUNT(contents), "\r\n");
    if (dluma_write_utf8_file(path, contents, 0)) {
        char utf8[DLUMA_MAX_WPATH * 2];
        dluma_wide_to_utf8(path, utf8, DLUMA_ARRAY_COUNT(utf8));
        dluma_log_pair("INFO", "marker_written", utf8);
    } else {
        dluma_log_line("ERROR", "Could not write script marker file.");
    }
}

static void dluma_execute_event(DlumaScriptSet* set, DlumaEvent* event)
{
    DlumaScript* script;
    unsigned int i;
    if (!set || !event || event->script_index >= set->script_count) return;
    script = &set->scripts[event->script_index];
    for (i = 0; i < event->action_count; ++i) {
        DlumaAction* action = &set->actions[event->first_action + i];
        if (action->type == DLUMA_ACTION_LOG) {
            char text[DLUMA_MAX_TEXT * 2];
            dluma_expand_text(action->first, script, text, DLUMA_ARRAY_COUNT(text));
            dluma_log_line("SCRIPT", text);
        } else if (action->type == DLUMA_ACTION_MARKER) {
            dluma_write_marker(script, action);
        } else if (action->type == DLUMA_ACTION_DUMP_CONTEXT) {
            char project[DLUMA_MAX_WPATH * 2];
            char file[DLUMA_MAX_WPATH * 2];
            dluma_wide_to_utf8(g_dluma.active_project_root, project, DLUMA_ARRAY_COUNT(project));
            dluma_wide_to_utf8(script->file_path, file, DLUMA_ARRAY_COUNT(file));
            dluma_log_pair("CONTEXT", "map", g_dluma.active_map);
            dluma_log_pair("CONTEXT", "project", project);
            dluma_log_pair("CONTEXT", "script_id", script->id);
            dluma_log_pair("CONTEXT", "script_file", file);
        }
    }
}

int dluma_scripts_load(DlumaScriptSet* set, const WCHAR* project_root, const char* map_name)
{
    WCHAR paths[DLUMA_MAX_SCRIPT_PATHS][DLUMA_MAX_WPATH];
    unsigned int count;
    unsigned int i;
    unsigned int loaded = 0;
    if (!set) return 0;
    dluma_scripts_clear(set);
    count = dluma_collect_script_paths(project_root, map_name, paths, DLUMA_MAX_SCRIPT_PATHS);
    for (i = 0; i < count && set->script_count < DLUMA_MAX_SCRIPTS; ++i) {
        if (dluma_parse_script_file(set, paths[i], project_root)) ++loaded;
    }
    dluma_log_u64("INFO", "dlscript_files_found", count);
    dluma_log_u64("INFO", "dlscripts_loaded", loaded);
    return loaded != 0;
}

void dluma_scripts_fire(DlumaScriptSet* set, DlumaEventType type, const char* target)
{
    unsigned int i;
    if (!set) return;
    for (i = 0; i < set->event_count; ++i) {
        DlumaEvent* event = &set->events[i];
        if ((DlumaEventType)event->type != type) continue;
        if ((type == DLUMA_EVENT_TRIGGER_ENTER || type == DLUMA_EVENT_TRIGGER_EXIT || type == DLUMA_EVENT_INTERACT) &&
            (!target || dluma_stricmp(event->target, target) != 0)) continue;
        dluma_execute_event(set, event);
    }
}

void dluma_scripts_tick(DlumaScriptSet* set, ULONGLONG now_ms)
{
    unsigned int i;
    if (!set) return;
    for (i = 0; i < set->event_count; ++i) {
        DlumaEvent* event = &set->events[i];
        if (event->type == DLUMA_EVENT_TIMER && event->interval_ms && now_ms - event->last_fire_ms >= event->interval_ms) {
            event->last_fire_ms = now_ms;
            if (g_dluma.config.log_timer_events) dluma_log_line("TRACE", "timer event fired");
            dluma_execute_event(set, event);
        } else if (event->type == DLUMA_EVENT_KEY && g_dluma.config.enable_key_events) {
            short key_state = GetAsyncKeyState(event->key_code);
            /* The low bit records a press since the previous query, so taps
             * shorter than the worker's polling interval are not lost. */
            unsigned char down = (key_state < 0 || (key_state & 1) != 0) ? 1 : 0;
            if (down && !event->key_was_down) {
                if (event->key_code == VK_F8) g_dluma.last_f8_ms = now_ms;
                dluma_execute_event(set, event);
            }
            event->key_was_down = down;
        }
    }
}

void dluma_scripts_clear(DlumaScriptSet* set)
{
    if (set) memset(set, 0, sizeof(*set));
}
