#include "dlw_custom_main_menu_core.h"
#include "core/dlw_json_reader.h"
#include "dlw_common.h"

#define DLUMA_CUSTOM_MENU_ROOT_RECORD_TAG 0x0077U
#define DLUMA_CUSTOM_MENU_LEVEL_CLASS "LevelDI"
#define DLUMA_CUSTOM_MENU_BACKGROUND_CLASS "Menu3DBackgroundDI"

static void dluma_custom_menu_copy(
    char *output, unsigned int capacity, const char *value)
{
    unsigned int index = 0;
    if (!output || !capacity)
        return;
    if (value)
    {
        while (value[index] && index + 1U < capacity)
        {
            output[index] = value[index];
            ++index;
        }
    }
    output[index] = 0;
}

static void dluma_custom_menu_diagnostic(
    char *output, unsigned int capacity, const char *value)
{
    dluma_custom_menu_copy(output, capacity, value ? value : "invalid_manifest");
}

static unsigned char dluma_custom_menu_lower(unsigned char value)
{
    return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
        ? (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'))
        : value;
}

static int dluma_custom_menu_equal(const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right)
        return 0;
    while (left[index] && right[index])
    {
        if (dluma_custom_menu_lower((unsigned char)left[index]) !=
            dluma_custom_menu_lower((unsigned char)right[index]))
            return 0;
        ++index;
    }
    return left[index] == 0 && right[index] == 0;
}

int dluma_custom_main_menu_id_equal(const char *left, const char *right)
{
    return dluma_custom_menu_equal(left, right);
}

static int dluma_custom_menu_has_text(const char *value)
{
    return value && value[0] != 0;
}

int dluma_custom_main_menu_asset_token_valid(const char *value)
{
    unsigned int index;
    if (!value || !value[0])
        return 0;
    for (index = 0; value[index]; ++index)
    {
        unsigned char character = (unsigned char)value[index];
        if (!((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' && character <= (unsigned char)'9') ||
              character == (unsigned char)'_' || character == (unsigned char)'-'))
            return 0;
    }
    return index < DLUMA_CUSTOM_MENU_OBJECT_CAPACITY;
}

int dluma_custom_main_menu_id_valid(const char *id)
{
    unsigned int index;
    if (!id || !id[0] || dluma_custom_menu_equal(id, "base_game"))
        return 0;
    for (index = 0; id[index]; ++index)
    {
        unsigned char character = (unsigned char)id[index];
        if (!((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' && character <= (unsigned char)'9') ||
              character == (unsigned char)'_' || character == (unsigned char)'-' ||
              character == (unsigned char)'.'))
            return 0;
    }
    return index < DLUMA_CUSTOM_MENU_ID_CAPACITY;
}

static int dluma_custom_menu_device_segment(
    const char *segment, unsigned int length)
{
    static const char *const names[] = {
        "con", "prn", "aux", "nul", "clock$"
    };
    unsigned int index;
    unsigned int base_length = 0;
    while (base_length < length && segment[base_length] != '.')
        ++base_length;
    for (index = 0; index < DLUMA_ARRAY_COUNT(names); ++index)
    {
        unsigned int name_index = 0;
        while (names[index][name_index] && name_index < base_length &&
               dluma_custom_menu_lower((unsigned char)segment[name_index]) ==
                   (unsigned char)names[index][name_index])
            ++name_index;
        if (!names[index][name_index] && name_index == base_length)
            return 1;
    }
    if (base_length == 4U &&
        ((dluma_custom_menu_lower((unsigned char)segment[0]) == (unsigned char)'c' &&
          dluma_custom_menu_lower((unsigned char)segment[1]) == (unsigned char)'o' &&
          dluma_custom_menu_lower((unsigned char)segment[2]) == (unsigned char)'m') ||
         (dluma_custom_menu_lower((unsigned char)segment[0]) == (unsigned char)'l' &&
          dluma_custom_menu_lower((unsigned char)segment[1]) == (unsigned char)'p' &&
          dluma_custom_menu_lower((unsigned char)segment[2]) == (unsigned char)'t')) &&
        segment[3] >= '1' && segment[3] <= '9')
        return 1;
    return 0;
}

int dluma_custom_main_menu_relative_path_valid(const char *path)
{
    unsigned int index = 0;
    unsigned int segment_start = 0;
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' ||
        (path[0] && path[1] == ':'))
        return 0;
    while (path[index])
    {
        unsigned char character = (unsigned char)path[index];
        if (character < 0x20U || character == 0x7FU || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|')
            return 0;
        if (character == '/' || character == '\\')
        {
            unsigned int length = index - segment_start;
            if (!length || (length == 1U && path[segment_start] == '.') ||
                (length == 2U && path[segment_start] == '.' &&
                 path[segment_start + 1U] == '.') ||
                path[index - 1U] == '.' || path[index - 1U] == ' ' ||
                dluma_custom_menu_device_segment(path + segment_start, length))
                return 0;
            segment_start = index + 1U;
        }
        ++index;
        if (index >= DLUMA_CUSTOM_MENU_PATH_CAPACITY)
            return 0;
    }
    if (segment_start == index || path[index - 1U] == '.' ||
        path[index - 1U] == ' ' ||
        (index - segment_start == 1U && path[segment_start] == '.') ||
        (index - segment_start == 2U && path[segment_start] == '.' &&
         path[segment_start + 1U] == '.') ||
        dluma_custom_menu_device_segment(path + segment_start, index - segment_start))
        return 0;
    return 1;
}

static int dluma_custom_menu_read_key(
    DlumaJsonReader *reader, char *key, unsigned int capacity)
{
    return dluma_json_string(reader, key, capacity) &&
           dluma_json_take(reader, ':');
}

static int dluma_custom_menu_take_unique(
    unsigned int *mask, unsigned int bit)
{
    if (!mask || (*mask & bit) != 0U)
        return 0;
    *mask |= bit;
    return 1;
}

static int dluma_custom_menu_parse_meta(
    DlumaJsonReader *reader, DlumaCustomMainMenuManifest *manifest)
{
    int first = 1;
    unsigned int fields = 0;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (dluma_custom_menu_equal(key, "id"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_json_string(reader, manifest->id, DLUMA_ARRAY_COUNT(manifest->id)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "author"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_json_string(reader, manifest->author, DLUMA_ARRAY_COUNT(manifest->author)))
                return 0;
        }
        else
            return 0;
    }
    return dluma_json_object_end(reader) && fields == 3U;
}

static int dluma_custom_menu_parse_general(
    DlumaJsonReader *reader, DlumaCustomMainMenuManifest *manifest)
{
    int first = 1;
    unsigned int fields = 0;
    int value;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[48];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (dluma_custom_menu_equal(key, "name"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_json_string(reader, manifest->name, DLUMA_ARRAY_COUNT(manifest->name)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "description"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_json_string(reader, manifest->description,
                                 DLUMA_ARRAY_COUNT(manifest->description)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "version"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 4U) ||
                !dluma_json_string(reader, manifest->version,
                                 DLUMA_ARRAY_COUNT(manifest->version)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "is3DMenuEnabled"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 8U) ||
                !dluma_json_boolean(reader, &value))
                return 0;
            manifest->is_3d_menu_enabled = value ? 1U : 0U;
        }
        else if (dluma_custom_menu_equal(key, "useCustomBanks"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 16U) ||
                !dluma_json_boolean(reader, &value))
                return 0;
            manifest->use_custom_banks = value ? 1U : 0U;
        }
        else if (dluma_custom_menu_equal(key, "doesUse3DCameraMovement"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 32U) ||
                !dluma_json_boolean(reader, &value))
                return 0;
            manifest->does_use_3d_camera_movement = value ? 1U : 0U;
        }
        else if (dluma_custom_menu_equal(key, "useCustomTextures"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 64U) ||
                !dluma_json_boolean(reader, &value))
                return 0;
            manifest->use_custom_textures = value ? 1U : 0U;
        }
        else
            return 0;
    }
    return dluma_json_object_end(reader) &&
           (fields & 31U) == 31U && (fields & ~127U) == 0U;
}

static int dluma_custom_menu_parse_scene(
    DlumaJsonReader *reader, DlumaCustomMainMenuManifest *manifest)
{
    int first = 1;
    unsigned int fields = 0;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (!dluma_custom_menu_equal(key, "map") ||
            !dluma_custom_menu_take_unique(&fields, 1U) ||
            !dluma_json_string(reader, manifest->map, DLUMA_ARRAY_COUNT(manifest->map)))
            return 0;
    }
    return dluma_json_object_end(reader) && fields == 1U;
}

static int dluma_custom_menu_parse_cameras(
    DlumaJsonReader *reader, DlumaCustomMainMenuManifest *manifest)
{
    int first = 1;
    int next;
    unsigned int count = 0;
    if (!dluma_json_array_begin(reader))
        return 0;
    for (;;)
    {
        int object_first = 1;
        unsigned int object_fields = 0;
        next = dluma_json_array_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || ++count != 1U || !dluma_json_object_begin(reader))
            return 0;
        for (;;)
        {
            char key[32];
            int object_next = dluma_json_object_next(reader, &object_first);
            if (object_next == 0)
                break;
            if (object_next < 0 ||
                !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)) ||
                !dluma_custom_menu_equal(key, "gameObject") ||
                !dluma_custom_menu_take_unique(&object_fields, 1U) ||
                !dluma_json_array_begin(reader))
                return 0;
            {
                int item_first = 1;
                int item_next = dluma_json_array_next(reader, &item_first);
                if (item_next != 1 ||
                    !dluma_json_string(reader, manifest->camera_name,
                                     DLUMA_ARRAY_COUNT(manifest->camera_name)))
                    return 0;
                item_next = dluma_json_array_next(reader, &item_first);
                if (item_next != 1 || !dluma_json_i32(reader, &manifest->camera_id))
                    return 0;
                if (dluma_json_array_next(reader, &item_first) != 0 ||
                    !dluma_json_array_end(reader))
                    return 0;
            }
        }
        if (!dluma_json_object_end(reader) || object_fields != 1U)
            return 0;
    }
    return dluma_json_array_end(reader) && count == 1U;
}

static int dluma_custom_menu_parse_bank(
    DlumaJsonReader *reader, DlumaCustomMainMenuBank *bank)
{
    int first = 1;
    unsigned int fields = 0;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (dluma_custom_menu_equal(key, "name"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_json_string(reader, bank->name,
                                 DLUMA_ARRAY_COUNT(bank->name)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "path"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_json_string(reader, bank->path,
                                 DLUMA_ARRAY_COUNT(bank->path)))
                return 0;
        }
        else
            return 0;
    }
    return dluma_json_object_end(reader) && fields == 3U;
}

static int dluma_custom_menu_parse_music_item(
    DlumaJsonReader *reader, DlumaCustomMainMenuMusicOverride *item)
{
    int first = 1;
    unsigned int fields = 0;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (dluma_custom_menu_equal(key, "name"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_json_string(reader, item->name,
                                 DLUMA_ARRAY_COUNT(item->name)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "overrides"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_json_string(reader, item->overrides,
                                 DLUMA_ARRAY_COUNT(item->overrides)))
                return 0;
        }
        else
            return 0;
    }
    return dluma_json_object_end(reader) && fields == 3U;
}

static int dluma_custom_menu_parse_music_overrides(
    DlumaJsonReader *reader, DlumaCustomMainMenuManifest *manifest)
{
    int first = 1;
    unsigned int count = 0;
    if (!reader || !manifest)
        return 0;
    dluma_json_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == '{')
    {
        if (!dluma_custom_menu_parse_music_item(
                reader, &manifest->music_overrides[0]))
            return 0;
        manifest->music_override_count = 1U;
        return 1;
    }
    if (!dluma_json_array_begin(reader))
        return 0;
    for (;;)
    {
        int next = dluma_json_array_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || count >= DLUMA_CUSTOM_MENU_MAX_MUSIC_OVERRIDES ||
            !dluma_custom_menu_parse_music_item(
                reader, &manifest->music_overrides[count]))
            return 0;
        ++count;
    }
    if (!dluma_json_array_end(reader) || count == 0U)
        return 0;
    manifest->music_override_count = count;
    return 1;
}

static int dluma_custom_menu_parse_audio(
    DlumaJsonReader *reader, DlumaCustomMainMenuManifest *manifest)
{
    int first = 1;
    unsigned int fields = 0;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (dluma_custom_menu_equal(key, "banks"))
        {
            int array_first = 1;
            unsigned int count = 0;
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_json_array_begin(reader))
                return 0;
            for (;;)
            {
                int array_next = dluma_json_array_next(reader, &array_first);
                if (array_next == 0)
                    break;
                if (array_next < 0 ||
                    count >= DLUMA_CUSTOM_MENU_MAX_BANKS ||
                    !dluma_custom_menu_parse_bank(
                        reader, &manifest->banks[count]))
                    return 0;
                ++count;
            }
            if (!dluma_json_array_end(reader) || count == 0U)
                return 0;
            manifest->bank_count = count;
            dluma_custom_menu_copy(
                manifest->bank_name,
                DLUMA_ARRAY_COUNT(manifest->bank_name),
                manifest->banks[0].name);
            dluma_custom_menu_copy(
                manifest->bank_path,
                DLUMA_ARRAY_COUNT(manifest->bank_path),
                manifest->banks[0].path);
        }
        else if (dluma_custom_menu_equal(key, "music_override") ||
                 dluma_custom_menu_equal(key, "music_overrides"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_custom_menu_parse_music_overrides(reader, manifest))
                return 0;
        }
        else
            return 0;
    }
    return dluma_json_object_end(reader) &&
           (fields == 1U || fields == 3U);
}

static unsigned int dluma_custom_menu_music_target(const char *value)
{
    if (dluma_custom_menu_equal(value, "dw_menu"))
        return DLUMA_CUSTOM_MENU_MUSIC_MAIN;
    if (dluma_custom_menu_equal(value, "dl_menu_end_credits"))
        return DLUMA_CUSTOM_MENU_MUSIC_CREDITS;
    return DLUMA_CUSTOM_MENU_MUSIC_INVALID;
}

static int dluma_custom_menu_music_contract_valid(
    DlumaCustomMainMenuManifest *manifest)
{
    unsigned int targets = 0;
    unsigned int index;
    if (!manifest ||
        manifest->music_override_count > DLUMA_CUSTOM_MENU_MAX_MUSIC_OVERRIDES)
        return 0;
    for (index = 0; index < manifest->music_override_count; ++index)
    {
        DlumaCustomMainMenuMusicOverride *item =
            &manifest->music_overrides[index];
        unsigned int target = dluma_custom_menu_music_target(item->overrides);
        if (!dluma_custom_main_menu_asset_token_valid(item->name) ||
            target == DLUMA_CUSTOM_MENU_MUSIC_INVALID || target >= 32U ||
            (targets & (1U << target)) != 0U)
            return 0;
        item->target = target;
        targets |= 1U << target;
    }
    return 1;
}

static int dluma_custom_menu_xui_stem_safe(const char *value)
{
    unsigned int length;
    if (!dluma_custom_main_menu_asset_token_valid(value))
        return 0;
    length = (unsigned int)strlen(value);
    return length != 0U &&
           !dluma_custom_menu_device_segment(value, length);
}

int dluma_custom_main_menu_resolve_override_destination(
    const char *target,
    char *destination,
    unsigned int destination_capacity)
{
    const char *alias = 0;
    unsigned int target_length;
    if (destination && destination_capacity)
        destination[0] = 0;
    if (!target || !destination || !destination_capacity ||
        !dluma_custom_menu_xui_stem_safe(target) ||
        dluma_custom_menu_equal(target, "MenuDLUMACustomMainMenu"))
        return 0;
    if (dluma_custom_menu_equal(
            target, "MenuMain_PC_workshop_with_B_Mods"))
        alias = "DLM_Main_PC_workshop.xui";
    else if (dluma_custom_menu_equal(target, "MenuSteamWorkshop"))
        alias = "DLM_SteamWorkshop.xui";
    else if (dluma_custom_menu_equal(target, "MenuExtras"))
        alias = "MenuExtras.xui";
    if (alias)
    {
        if (strlen(alias) + 1U > destination_capacity)
            return 0;
        dluma_custom_menu_copy(destination, destination_capacity, alias);
        return 1;
    }
    target_length = (unsigned int)strlen(target);
    if (destination_capacity < 5U ||
        target_length > destination_capacity - 5U)
        return 0;
    dluma_custom_menu_copy(destination, destination_capacity, target);
    dluma_str_append(destination, destination_capacity, ".xui");
    return strlen(destination) == target_length + 4U;
}

int dluma_custom_main_menu_xui_contract_valid(
    const unsigned char *bytes,
    unsigned int size)
{
    /* The manifest parser validates the path-safe destination, while staging
     * validates canonical source identity, byte count, and content hash.
     * Treat the payload as opaque engine-authored data: official and custom
     * editors may legitimately change hierarchy, controller classes,
     * comments, or encoding without changing the mounted destination. */
    return bytes != 0 && size != 0U;
}

static int dluma_custom_menu_xml_name_equal(
    const unsigned char *bytes, unsigned int length, const char *expected)
{
    return bytes && expected && strlen(expected) == length &&
           memcmp(bytes, expected, length) == 0;
}

static unsigned int dluma_custom_menu_xml_find(
    const unsigned char *bytes, unsigned int size, unsigned int begin,
    const char *needle)
{
    unsigned int needle_length = (unsigned int)strlen(needle);
    unsigned int index;
    if (!bytes || !needle_length || begin > size || needle_length > size)
        return size;
    for (index = begin; index + needle_length <= size; ++index)
        if (memcmp(bytes + index, needle, needle_length) == 0)
            return index;
    return size;
}

int dluma_custom_main_menu_xui_music_override(
    const unsigned char *bytes,
    unsigned int size,
    char *cue_out,
    unsigned int cue_capacity)
{
    unsigned int cursor = 0;
    unsigned int depth = 0;
    unsigned int properties_depth = 0;
    unsigned int root_depth = 0;
    unsigned int found = 0;
    if (cue_out && cue_capacity)
        cue_out[0] = 0;
    if (!bytes || !size || !cue_out || !cue_capacity)
        return 0;
    while (cursor < size)
    {
        unsigned int tag;
        unsigned int name_begin;
        unsigned int name_end;
        unsigned int close;
        int closing = 0;
        int self_closing = 0;
        tag = dluma_custom_menu_xml_find(bytes, size, cursor, "<");
        if (tag == size)
            break;
        if (tag + 4U <= size && memcmp(bytes + tag, "<!--", 4U) == 0)
        {
            close = dluma_custom_menu_xml_find(bytes, size, tag + 4U, "-->");
            if (close == size)
                return dluma_custom_menu_xml_find(
                    bytes, size, tag, "MusicOverride") == size;
            cursor = close + 3U;
            continue;
        }
        if (tag + 2U <= size && bytes[tag + 1U] == '?')
        {
            close = dluma_custom_menu_xml_find(bytes, size, tag + 2U, "?>");
            if (close == size)
                return 0;
            cursor = close + 2U;
            continue;
        }
        if (tag + 9U <= size && memcmp(bytes + tag, "<![CDATA[", 9U) == 0)
        {
            close = dluma_custom_menu_xml_find(bytes, size, tag + 9U, "]]>");
            if (close == size)
                return 0;
            cursor = close + 3U;
            continue;
        }
        close = dluma_custom_menu_xml_find(bytes, size, tag + 1U, ">");
        if (close == size)
            return dluma_custom_menu_xml_find(
                bytes, size, tag, "MusicOverride") == size;
        name_begin = tag + 1U;
        while (name_begin < close &&
               (bytes[name_begin] == ' ' || bytes[name_begin] == '\t' ||
                bytes[name_begin] == '\r' || bytes[name_begin] == '\n'))
            ++name_begin;
        if (name_begin < close && bytes[name_begin] == '/')
        {
            closing = 1;
            ++name_begin;
        }
        while (name_begin < close &&
               (bytes[name_begin] == ' ' || bytes[name_begin] == '\t'))
            ++name_begin;
        name_end = name_begin;
        while (name_end < close &&
               ((bytes[name_end] >= 'A' && bytes[name_end] <= 'Z') ||
                (bytes[name_end] >= 'a' && bytes[name_end] <= 'z') ||
                (bytes[name_end] >= '0' && bytes[name_end] <= '9') ||
                bytes[name_end] == '_' || bytes[name_end] == ':' ||
                bytes[name_end] == '-'))
            ++name_end;
        if (name_end == name_begin)
        {
            cursor = close + 1U;
            continue;
        }
        {
            unsigned int tail = close;
            while (tail > name_end &&
                   (bytes[tail - 1U] == ' ' || bytes[tail - 1U] == '\t' ||
                    bytes[tail - 1U] == '\r' || bytes[tail - 1U] == '\n'))
                --tail;
            self_closing = !closing && tail > name_end &&
                           bytes[tail - 1U] == '/';
        }
        if (closing)
        {
            if (depth)
            {
                if (properties_depth == depth &&
                    dluma_custom_menu_xml_name_equal(
                        bytes + name_begin, name_end - name_begin,
                        "Properties"))
                    properties_depth = 0;
                if (root_depth == depth &&
                    dluma_custom_menu_xml_name_equal(
                        bytes + name_begin, name_end - name_begin,
                        "XuiCanvas"))
                    root_depth = 0;
                --depth;
            }
            cursor = close + 1U;
            continue;
        }
        if (root_depth == 1U && properties_depth == 2U && depth == 2U &&
            dluma_custom_menu_xml_name_equal(
                bytes + name_begin, name_end - name_begin,
                "MusicOverride"))
        {
            unsigned int value_begin;
            unsigned int value_end;
            unsigned int closing_tag;
            unsigned int tail = name_end;
            while (tail < close &&
                   (bytes[tail] == ' ' || bytes[tail] == '\t' ||
                    bytes[tail] == '\r' || bytes[tail] == '\n'))
                ++tail;
            if (found || self_closing || tail != close)
                return 0;
            closing_tag = dluma_custom_menu_xml_find(
                bytes, size, close + 1U, "</MusicOverride>");
            if (closing_tag == size ||
                dluma_custom_menu_xml_find(
                    bytes, size, close + 1U, "<") != closing_tag)
                return 0;
            value_begin = close + 1U;
            value_end = closing_tag;
            while (value_begin < value_end &&
                   (bytes[value_begin] == ' ' || bytes[value_begin] == '\t' ||
                    bytes[value_begin] == '\r' || bytes[value_begin] == '\n'))
                ++value_begin;
            while (value_end > value_begin &&
                   (bytes[value_end - 1U] == ' ' ||
                    bytes[value_end - 1U] == '\t' ||
                    bytes[value_end - 1U] == '\r' ||
                    bytes[value_end - 1U] == '\n'))
                --value_end;
            if (value_end == value_begin ||
                value_end - value_begin + 1U > cue_capacity)
                return 0;
            memcpy(cue_out, bytes + value_begin, value_end - value_begin);
            cue_out[value_end - value_begin] = 0;
            if (!dluma_custom_main_menu_asset_token_valid(cue_out))
                return 0;
            found = 1U;
            cursor = closing_tag + sizeof("</MusicOverride>") - 1U;
            continue;
        }
        if (depth == 0U &&
            dluma_custom_menu_xml_name_equal(
                bytes + name_begin, name_end - name_begin, "XuiCanvas"))
            root_depth = 1U;
        else if (root_depth == 1U && depth == 1U &&
                 dluma_custom_menu_xml_name_equal(
                     bytes + name_begin, name_end - name_begin,
                     "Properties"))
            properties_depth = 2U;
        if (!self_closing)
            ++depth;
        cursor = close + 1U;
    }
    return 1;
}

static int dluma_custom_menu_parse_menu_item(
    DlumaJsonReader *reader,
    DlumaCustomMainMenuOverride *item,
    int *is_main_override,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    int first = 1;
    unsigned int fields = 0;
    char target[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    target[0] = 0;
    if (is_main_override)
        *is_main_override = 0;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(reader, key, DLUMA_ARRAY_COUNT(key)))
            return 0;
        if (dluma_custom_menu_equal(key, "name"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_json_string(reader, item->name, DLUMA_ARRAY_COUNT(item->name)))
                return 0;
        }
        else if (dluma_custom_menu_equal(key, "overrides"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_json_string(reader, target, DLUMA_ARRAY_COUNT(target)))
                return 0;
        }
        else
            return 0;
    }
    if (!dluma_json_object_end(reader) || fields != 3U)
        return 0;
    if (!dluma_custom_menu_xui_stem_safe(item->name))
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "menu_override_source_name_invalid_or_unsafe");
        return 0;
    }
    if (!dluma_custom_menu_xui_stem_safe(target))
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "menu_override_target_invalid_or_unsafe");
        return 0;
    }
    if (dluma_custom_menu_equal(target, "MenuDLUMACustomMainMenu"))
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "menu_override_target_reserved_for_recovery");
        return 0;
    }
    if (!dluma_custom_main_menu_resolve_override_destination(
            target, item->destination,
            DLUMA_ARRAY_COUNT(item->destination)))
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "menu_override_destination_length_invalid");
        return 0;
    }
    if (is_main_override)
        *is_main_override = dluma_custom_menu_equal(
            target, "MenuMain_PC_workshop_with_B_Mods");
    dluma_custom_menu_copy(item->source_path,
                         DLUMA_ARRAY_COUNT(item->source_path), "data/menu/dluma/");
    if (strlen(item->source_path) + strlen(item->name) + 5U >=
        DLUMA_ARRAY_COUNT(item->source_path))
        return 0;
    dluma_str_append(item->source_path, DLUMA_ARRAY_COUNT(item->source_path), item->name);
    dluma_str_append(item->source_path, DLUMA_ARRAY_COUNT(item->source_path), ".xui");
    return 1;
}

static int dluma_custom_menu_parse_menus(
    DlumaJsonReader *reader,
    DlumaCustomMainMenuManifest *manifest,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    int first = 1;
    int has_main_override = 0;
    if (!dluma_json_array_begin(reader))
        return 0;
    for (;;)
    {
        int next = dluma_json_array_next(reader, &first);
        DlumaCustomMainMenuOverride *item;
        int is_main_override = 0;
        unsigned int prior_index;
        if (next == 0)
            break;
        if (next < 0)
            return 0;
        if (manifest->override_count >= DLUMA_CUSTOM_MENU_MAX_OVERRIDES)
        {
            dluma_custom_menu_diagnostic(
                diagnostic, diagnostic_capacity,
                "menu_override_count_limit_exceeded");
            return 0;
        }
        item = &manifest->overrides[manifest->override_count];
        if (!dluma_custom_menu_parse_menu_item(
                reader, item, &is_main_override,
                diagnostic, diagnostic_capacity))
            return 0;
        for (prior_index = 0;
             prior_index < manifest->override_count; ++prior_index)
        {
            if (dluma_custom_menu_equal(
                    manifest->overrides[prior_index].destination,
                    item->destination))
            {
                dluma_custom_menu_diagnostic(
                    diagnostic, diagnostic_capacity,
                    "menu_override_destination_duplicate");
                return 0;
            }
        }
        if (is_main_override)
            has_main_override = 1;
        ++manifest->override_count;
    }
    if (!dluma_json_array_end(reader))
        return 0;
    if (!manifest->override_count)
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "menu_override_list_empty");
        return 0;
    }
    if (!has_main_override)
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "main_menu_override_missing");
        return 0;
    }
    return 1;
}

static int dluma_custom_menu_texture_def_name_valid(const char *name)
{
    unsigned int index;
    unsigned int length;
    if (!name || !name[0] ||
        !dluma_custom_main_menu_relative_path_valid(name))
        return 0;
    for (index = 0; name[index]; ++index)
        if (name[index] == '/' || name[index] == '\\')
            return 0;
    length = (unsigned int)strlen(name);
    return length > 4U &&
           length < DLUMA_CUSTOM_MENU_TEXTURE_DEF_NAME_CAPACITY &&
           name[length - 4U] == '.' &&
           dluma_custom_menu_lower((unsigned char)name[length - 3U]) == 'd' &&
           dluma_custom_menu_lower((unsigned char)name[length - 2U]) == 'e' &&
           dluma_custom_menu_lower((unsigned char)name[length - 1U]) == 'f';
}

static int dluma_custom_menu_parse_textures(
    DlumaJsonReader *reader,
    DlumaCustomMainMenuManifest *manifest,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    int first = 1;
    if (!dluma_json_array_begin(reader))
        return 0;
    for (;;)
    {
        DlumaCustomMainMenuTextureDef *item;
        int object_first = 1;
        unsigned int object_fields = 0;
        unsigned int prior_index;
        int next = dluma_json_array_next(reader, &first);
        if (next == 0)
            break;
        if (next < 0)
            return 0;
        if (manifest->texture_def_count >= DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS)
        {
            dluma_custom_menu_diagnostic(
                diagnostic, diagnostic_capacity,
                "custom_texture_definition_count_exceeded");
            return 0;
        }
        item = &manifest->texture_defs[manifest->texture_def_count];
        if (!dluma_json_object_begin(reader))
            return 0;
        for (;;)
        {
            char key[16];
            int object_next = dluma_json_object_next(reader, &object_first);
            if (object_next == 0)
                break;
            if (object_next < 0 ||
                !dluma_custom_menu_read_key(
                    reader, key, DLUMA_ARRAY_COUNT(key)) ||
                !dluma_custom_menu_equal(key, "def") ||
                !dluma_custom_menu_take_unique(&object_fields, 1U) ||
                !dluma_json_string(
                    reader, item->name, DLUMA_ARRAY_COUNT(item->name)))
                return 0;
        }
        if (!dluma_json_object_end(reader) || object_fields != 1U)
            return 0;
        if (!dluma_custom_menu_texture_def_name_valid(item->name))
        {
            dluma_custom_menu_diagnostic(
                diagnostic, diagnostic_capacity,
                "custom_texture_definition_unsafe");
            return 0;
        }
        for (prior_index = 0;
             prior_index < manifest->texture_def_count; ++prior_index)
        {
            if (dluma_custom_menu_equal(
                    manifest->texture_defs[prior_index].name, item->name))
            {
                dluma_custom_menu_diagnostic(
                    diagnostic, diagnostic_capacity,
                    "custom_texture_definition_duplicate");
                return 0;
            }
        }
        dluma_custom_menu_copy(
            item->source_path, DLUMA_ARRAY_COUNT(item->source_path),
            "data/menu/texturedefs/");
        if (strlen(item->source_path) + strlen(item->name) + 1U >
            DLUMA_ARRAY_COUNT(item->source_path))
            return 0;
        dluma_str_append(
            item->source_path, DLUMA_ARRAY_COUNT(item->source_path),
            item->name);
        ++manifest->texture_def_count;
    }
    return dluma_json_array_end(reader);
}

static int dluma_custom_menu_has_nul_escape(
    const unsigned char *bytes, unsigned int size)
{
    unsigned int index;
    for (index = 0; index + 5U < size; ++index)
    {
        if (bytes[index] == '\\' && bytes[index + 1U] == 'u' &&
            bytes[index + 2U] == '0' && bytes[index + 3U] == '0' &&
            bytes[index + 4U] == '0' && bytes[index + 5U] == '0')
            return 1;
    }
    return 0;
}

static int dluma_custom_menu_path_has_csb_extension(const char *path)
{
    unsigned int length = path ? (unsigned int)strlen(path) : 0;
    return length > 4U && path[length - 4U] == '.' &&
           dluma_custom_menu_lower((unsigned char)path[length - 3U]) == 'c' &&
           dluma_custom_menu_lower((unsigned char)path[length - 2U]) == 's' &&
           dluma_custom_menu_lower((unsigned char)path[length - 1U]) == 'b';
}

int dluma_custom_main_menu_manifest_parse(
    const void *bytes,
    unsigned int size,
    DlumaCustomMainMenuManifest *manifest,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    DlumaJsonReader reader;
    int first = 1;
    unsigned int fields = 0;
    if (manifest)
        memset(manifest, 0, sizeof(*manifest));
    if (diagnostic && diagnostic_capacity)
        diagnostic[0] = 0;
    if (!bytes || !manifest || !size || size > DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES)
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "manifest_size_invalid");
        return 0;
    }
    if (dluma_custom_menu_has_nul_escape((const unsigned char *)bytes, size))
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "manifest_embedded_nul_rejected");
        return 0;
    }
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader))
        goto parse_failed;
    for (;;)
    {
        char key[32];
        int next = dluma_json_object_next(&reader, &first);
        if (next == 0)
            break;
        if (next < 0 || !dluma_custom_menu_read_key(&reader, key, DLUMA_ARRAY_COUNT(key)))
            goto parse_failed;
        if (dluma_custom_menu_equal(key, "meta"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 1U) ||
                !dluma_custom_menu_parse_meta(&reader, manifest))
                goto parse_failed;
        }
        else if (dluma_custom_menu_equal(key, "general"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 2U) ||
                !dluma_custom_menu_parse_general(&reader, manifest))
                goto parse_failed;
        }
        else if (dluma_custom_menu_equal(key, "scene"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 4U) ||
                !dluma_custom_menu_parse_scene(&reader, manifest))
                goto parse_failed;
        }
        else if (dluma_custom_menu_equal(key, "cameras"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 8U) ||
                !dluma_custom_menu_parse_cameras(&reader, manifest))
                goto parse_failed;
        }
        else if (dluma_custom_menu_equal(key, "audio"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 16U) ||
                !dluma_custom_menu_parse_audio(&reader, manifest))
                goto parse_failed;
        }
        else if (dluma_custom_menu_equal(key, "menus"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 32U) ||
                !dluma_custom_menu_parse_menus(
                    &reader, manifest, diagnostic,
                    diagnostic_capacity))
                goto parse_failed;
        }
        else if (dluma_custom_menu_equal(key, "textures"))
        {
            if (!dluma_custom_menu_take_unique(&fields, 64U) ||
                !dluma_custom_menu_parse_textures(
                    &reader, manifest, diagnostic,
                    diagnostic_capacity))
                goto parse_failed;
        }
        else
            goto parse_failed;
    }
    if (!dluma_json_object_end(&reader) || !dluma_json_finished(&reader) ||
        (fields & 47U) != 47U || (fields & ~127U) != 0U)
        goto parse_failed;
    if (!dluma_custom_main_menu_id_valid(manifest->id))
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "meta_id_invalid_or_reserved");
        return 0;
    }
    if (!dluma_custom_menu_has_text(manifest->author) ||
        !dluma_custom_menu_has_text(manifest->name) ||
        !dluma_custom_menu_has_text(manifest->description) ||
        !dluma_custom_menu_has_text(manifest->version) ||
        !dluma_custom_main_menu_asset_token_valid(manifest->map) ||
        !dluma_custom_main_menu_asset_token_valid(manifest->camera_name) ||
        manifest->camera_id < 0)
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "required_manifest_value_invalid");
        return 0;
    }
    if (manifest->is_3d_menu_enabled)
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "three_dimensional_xui_unsupported");
        return 0;
    }
    if (manifest->use_custom_banks && (fields & 16U) == 0U)
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "custom_audio_manifest_missing");
        return 0;
    }
    if (manifest->use_custom_textures &&
        ((fields & 64U) == 0U || manifest->texture_def_count == 0U))
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "custom_texture_definitions_missing");
        return 0;
    }
    if (!manifest->use_custom_textures && manifest->texture_def_count != 0U)
    {
        dluma_custom_menu_diagnostic(
            diagnostic, diagnostic_capacity,
            "custom_texture_definitions_disabled");
        return 0;
    }
    if ((fields & 16U) != 0U)
    {
        unsigned int bank_index;
        unsigned int prior_index;
        if (manifest->bank_count == 0U ||
            manifest->bank_count > DLUMA_CUSTOM_MENU_MAX_BANKS ||
            !dluma_custom_menu_music_contract_valid(manifest))
            goto audio_invalid;
        for (bank_index = 0; bank_index < manifest->bank_count; ++bank_index)
        {
            const DlumaCustomMainMenuBank *bank =
                &manifest->banks[bank_index];
            if (!dluma_custom_main_menu_asset_token_valid(bank->name) ||
                !dluma_custom_main_menu_relative_path_valid(bank->path) ||
                !dluma_custom_menu_path_has_csb_extension(bank->path))
                goto audio_invalid;
            for (prior_index = 0; prior_index < bank_index; ++prior_index)
                if (dluma_custom_menu_equal(
                        manifest->banks[prior_index].name, bank->name) ||
                    dluma_custom_menu_equal(
                        manifest->banks[prior_index].path, bank->path))
                    goto audio_invalid;
        }
    }
    return 1;

audio_invalid:
    dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                               "custom_audio_contract_invalid");
    return 0;

parse_failed:
    if (!diagnostic || !diagnostic_capacity || !diagnostic[0])
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "manifest_schema_or_type_invalid");
    return 0;
}

static unsigned int dluma_custom_menu_read_u16(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8U);
}

static unsigned int dluma_custom_menu_read_u32(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8U) |
           ((unsigned int)bytes[2] << 16U) | ((unsigned int)bytes[3] << 24U);
}

static void dluma_custom_menu_write_u16(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8U);
}

static void dluma_custom_menu_write_u32(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8U);
    bytes[2] = (unsigned char)(value >> 16U);
    bytes[3] = (unsigned char)(value >> 24U);
}

int dluma_custom_main_menu_promote_root_record(
    const unsigned char *input,
    unsigned int input_size,
    unsigned char *output,
    unsigned int output_capacity,
    unsigned int *output_size,
    int *already_promoted,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    const unsigned int level_length =
        (unsigned int)(sizeof(DLUMA_CUSTOM_MENU_LEVEL_CLASS) - 1U);
    const unsigned int background_length =
        (unsigned int)(sizeof(DLUMA_CUSTOM_MENU_BACKGROUND_CLASS) - 1U);
    unsigned int outer_length;
    unsigned int record_length;
    unsigned int class_length;
    unsigned int new_size;
    unsigned int delta;
    if (output_size)
        *output_size = 0;
    if (already_promoted)
        *already_promoted = 0;
    if (!input || !output || !output_size || input_size < 15U)
        goto malformed;
    outer_length = dluma_custom_menu_read_u32(input + 2U);
    record_length = dluma_custom_menu_read_u32(input + 8U);
    class_length = dluma_custom_menu_read_u16(input + 12U);
    if (outer_length != input_size - 6U ||
        dluma_custom_menu_read_u16(input + 6U) != DLUMA_CUSTOM_MENU_ROOT_RECORD_TAG ||
        record_length != class_length + 2U ||
        class_length > input_size - 14U ||
        14U + class_length >= input_size)
        goto malformed;
    if (class_length == background_length &&
        memcmp(input + 14U, DLUMA_CUSTOM_MENU_BACKGROUND_CLASS,
               background_length) == 0)
    {
        if (output_capacity < input_size)
            goto insufficient;
        memcpy(output, input, input_size);
        *output_size = input_size;
        if (already_promoted)
            *already_promoted = 1;
        return 1;
    }
    if (class_length != level_length ||
        memcmp(input + 14U, DLUMA_CUSTOM_MENU_LEVEL_CLASS, level_length) != 0)
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "root_class_not_leveldi");
        return 0;
    }
    delta = background_length - level_length;
    if (input_size > 0xFFFFFFFFU - delta ||
        outer_length > 0xFFFFFFFFU - delta ||
        record_length > 0xFFFFFFFFU - delta)
    {
        dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                                   "root_record_length_overflow");
        return 0;
    }
    new_size = input_size + delta;
    if (output_capacity < new_size)
        goto insufficient;
    memcpy(output, input, 14U);
    dluma_custom_menu_write_u32(output + 2U, outer_length + delta);
    dluma_custom_menu_write_u32(output + 8U, record_length + delta);
    dluma_custom_menu_write_u16(output + 12U, background_length);
    memcpy(output + 14U, DLUMA_CUSTOM_MENU_BACKGROUND_CLASS, background_length);
    memcpy(output + 14U + background_length,
           input + 14U + level_length,
           input_size - 14U - level_length);
    *output_size = new_size;
    return 1;

insufficient:
    dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                               "promotion_output_too_small");
    return 0;
malformed:
    dluma_custom_menu_diagnostic(diagnostic, diagnostic_capacity,
                               "root_record_malformed_or_truncated");
    return 0;
}

int dluma_custom_main_menu_boot_receipt_blocks(
    const char *selected_id,
    const char *selected_fingerprint,
    const char *receipt_id,
    const char *receipt_fingerprint,
    const char *receipt_state)
{
    return selected_id && selected_fingerprint && receipt_id &&
           receipt_fingerprint && receipt_state &&
           selected_id[0] && selected_fingerprint[0] &&
           dluma_custom_menu_equal(selected_id, receipt_id) &&
           dluma_custom_menu_equal(selected_fingerprint, receipt_fingerprint) &&
           dluma_custom_menu_equal(receipt_state, "pending");
}
