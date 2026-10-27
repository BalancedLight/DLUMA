#include "dlw_companion_archive.h"

static int dluma_native_companion_identity_unchanged(
    HANDLE file, const BY_HANDLE_FILE_INFORMATION* before);
#include "dlw_native_mod_config.h"
#include "dlw_path_security.h"
#include "dlw_process_lifetime_sources.h"
#include "dlw_sha256_internal.h"

typedef unsigned char (__cdecl *DlumaNativeFsAddSourceFn)(
    const char* path, unsigned int flags);
typedef unsigned char (__cdecl *DlumaNativeFsExistsFn)(const char* path);
typedef void* (__cdecl *DlumaNativeFsFindSourceFn)(const char* path);
typedef void* (__fastcall *DlumaNativeFsPathCtorFn)(
    void* path, const char* value, unsigned char normalize);
typedef void (__fastcall *DlumaNativeFsPathDtorFn)(void* path);
typedef unsigned char (__fastcall *DlumaNativeFsSourceEntryExistsFn)(
    void* source, const void* path);

typedef struct DlumaNativeFsPathStorage {
    ULONG_PTR words[9];
} DlumaNativeFsPathStorage;

#define DLUMA_NATIVE_ZIP_LOCAL_SIGNATURE 0x04034b50UL
#define DLUMA_NATIVE_ZIP_CENTRAL_SIGNATURE 0x02014b50UL
#define DLUMA_NATIVE_ZIP_EOCD_SIGNATURE 0x06054b50UL
#define DLUMA_NATIVE_FILESYSTEM_TIMESTAMP 0x623394CDUL
#define DLUMA_NATIVE_FILESYSTEM_IMAGE_SIZE 0x00060000UL
#define DLUMA_NATIVE_READ_BUFFER_TOO_SMALL (-4)
#define DLUMA_COMPANION_ARCHIVE_JSON_MAX_DEPTH 16U
#define DLUMA_NATIVE_CREATE_NEW 1UL
static const char g_dluma_native_filesystem_sha256[] =
    "CB6FF4B0C9AA62C8A35742D463AE3C34FE803767E8E2AC9B33E4604886B9846D";

static WORD dluma_native_companion_u16(const BYTE* value)
{
    return (WORD)((WORD)value[0] | ((WORD)value[1] << 8));
}

static DWORD dluma_native_companion_u32(const BYTE* value)
{
    return (DWORD)value[0] |
           ((DWORD)value[1] << 8) |
           ((DWORD)value[2] << 16) |
           ((DWORD)value[3] << 24);
}

static int dluma_native_companion_add_u32(
    DWORD left, DWORD right, DWORD* result)
{
    if (!result || right > 0xffffffffUL - left) return 0;
    *result = left + right;
    return 1;
}

static int dluma_native_companion_read_at(
    HANDLE file, DWORD offset, void* bytes, DWORD size)
{
    LARGE_INTEGER position;
    DWORD read = 0;
    if (file == INVALID_HANDLE_VALUE || (!bytes && size)) return 0;
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file, position, 0, FILE_BEGIN)) return 0;
    if (!size) return 1;
    return ReadFile(file, bytes, size, &read, 0) && read == size;
}

static int dluma_native_companion_wstr_copy_checked(
    WCHAR* out, unsigned int capacity, const WCHAR* value)
{
    unsigned int i = 0;
    if (!out || !capacity || !value) return 0;
    while (value[i] && i + 1U < capacity) {
        out[i] = value[i];
        ++i;
    }
    out[i] = 0;
    return value[i] == 0;
}

static int dluma_native_companion_str_append_checked(
    char* out, unsigned int capacity, const char* value)
{
    unsigned int cursor = 0;
    unsigned int value_length = 0;
    if (!out || !capacity || !value) return 0;
    while (cursor < capacity && out[cursor]) ++cursor;
    if (cursor >= capacity) return 0;
    while (value[value_length]) {
        if (value_length >= capacity - cursor - 1U) return 0;
        ++value_length;
    }
    memcpy(out + cursor, value, value_length + 1U);
    return 1;
}

static unsigned char dluma_native_companion_ascii_lower(
    unsigned char value)
{
    if (value >= 'A' && value <= 'Z')
        return (unsigned char)(value + ('a' - 'A'));
    return value;
}

static int dluma_native_companion_bytes_equal(
    const char* left, const char* right, unsigned int length)
{
    unsigned int i;
    if (!left || !right) return 0;
    for (i = 0; i < length; ++i)
        if (left[i] != right[i]) return 0;
    return 1;
}

static int dluma_native_companion_str_equal(
    const char* left, const char* right)
{
    unsigned int i = 0;
    if (!left || !right) return 0;
    while (left[i] && right[i]) {
        if (left[i] != right[i]) return 0;
        ++i;
    }
    return left[i] == 0 && right[i] == 0;
}

static int dluma_native_companion_segment_equals(
    const char* value, unsigned int length, const char* expected)
{
    unsigned int i = 0;
    if (!value || !expected) return 0;
    while (expected[i]) {
        if (i >= length ||
            dluma_native_companion_ascii_lower((unsigned char)value[i]) !=
                dluma_native_companion_ascii_lower(
                    (unsigned char)expected[i]))
            return 0;
        ++i;
    }
    return i == length;
}

static int dluma_native_companion_segment_valid(
    const char* value, unsigned int length)
{
    unsigned int base_length = 0;
    if (!value || !length ||
        (length == 1U && value[0] == '.') ||
        (length == 2U && value[0] == '.' && value[1] == '.') ||
        value[length - 1U] == '.' || value[length - 1U] == ' ')
        return 0;
    while (base_length < length && value[base_length] != '.')
        ++base_length;
    if (dluma_native_companion_segment_equals(
            value, base_length, "CON") ||
        dluma_native_companion_segment_equals(
            value, base_length, "PRN") ||
        dluma_native_companion_segment_equals(
            value, base_length, "AUX") ||
        dluma_native_companion_segment_equals(
            value, base_length, "NUL") ||
        dluma_native_companion_segment_equals(
            value, base_length, "CLOCK$") ||
        dluma_native_companion_segment_equals(
            value, base_length, "CONIN$") ||
        dluma_native_companion_segment_equals(
            value, base_length, "CONOUT$"))
        return 0;
    if (base_length == 4U &&
        value[3] >= '1' && value[3] <= '9' &&
        ((dluma_native_companion_ascii_lower((unsigned char)value[0]) == 'c' &&
          dluma_native_companion_ascii_lower((unsigned char)value[1]) == 'o' &&
          dluma_native_companion_ascii_lower((unsigned char)value[2]) == 'm') ||
         (dluma_native_companion_ascii_lower((unsigned char)value[0]) == 'l' &&
          dluma_native_companion_ascii_lower((unsigned char)value[1]) == 'p' &&
          dluma_native_companion_ascii_lower((unsigned char)value[2]) == 't')))
        return 0;
    return 1;
}

static int dluma_native_companion_path_valid(const char* name)
{
    unsigned int i;
    unsigned int length;
    unsigned int segment_start = 0;
    if (!name || !name[0] ||
        (length = (unsigned int)strlen(name)) >=
            DLUMA_COMPANION_ARCHIVE_MAX_NAME ||
        name[0] == '/' || name[0] == '\\')
        return 0;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x21U || c > 0x7eU || c == '\\' || c == ':')
            return 0;
        if (c == '/') {
            if (!dluma_native_companion_segment_valid(
                    name + segment_start, i - segment_start))
                return 0;
            segment_start = i + 1U;
        }
    }
    return dluma_native_companion_segment_valid(
        name + segment_start, length - segment_start);
}

static int dluma_native_companion_ascii_basename(
    const WCHAR* value, char* utf8, unsigned int capacity)
{
    unsigned int i;
    unsigned int dot = 0;
    char stem[16];
    if (!value || !value[0] || !utf8 || capacity < 8U ||
        wcslen(value) >= capacity ||
        !dluma_wide_to_utf8(value, utf8, capacity))
        return 0;
    for (i = 0; utf8[i]; ++i) {
        char c = utf8[i];
        if (c == '.') dot = i;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            return 0;
    }
    if (!dot || dluma_stricmp(utf8 + dot, ".pak") != 0 ||
        dot >= DLUMA_ARRAY_COUNT(stem))
        return 0;
    memcpy(stem, utf8, dot);
    stem[dot] = 0;
    if (dluma_stricmp(stem, "CON") == 0 ||
        dluma_stricmp(stem, "PRN") == 0 ||
        dluma_stricmp(stem, "AUX") == 0 ||
        dluma_stricmp(stem, "NUL") == 0)
        return 0;
    if (dot == 4U &&
        ((stem[3] >= '1' && stem[3] <= '9') &&
         ((stem[0] == 'C' || stem[0] == 'c' ||
           stem[0] == 'L' || stem[0] == 'l') &&
          (stem[1] == 'O' || stem[1] == 'o' ||
           stem[1] == 'P' || stem[1] == 'p') &&
          (stem[2] == 'M' || stem[2] == 'm' ||
           stem[2] == 'T' || stem[2] == 't'))))
        return 0;
    return 1;
}

static int dluma_native_companion_open_regular(
    const WCHAR* path, HANDLE* file_out, WCHAR* final_path,
    unsigned int final_capacity, BY_HANDLE_FILE_INFORMATION* info_out)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    WCHAR resolved[DLUMA_MAX_WPATH];
    if (file_out) *file_out = INVALID_HANDLE_VALUE;
    if (!path || !file_out || !final_path || !final_capacity || !info_out)
        return 0;
    file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    memset(&info, 0, sizeof(info));
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1U ||
        info.nFileSizeHigh ||
        info.nFileSizeLow == 0 ||
        (ULONGLONG)info.nFileSizeLow > DLUMA_COMPANION_ARCHIVE_MAX_BYTES ||
        !dluma_get_final_path_checked(
            file, resolved, DLUMA_ARRAY_COUNT(resolved))) {
        CloseHandle(file);
        return 0;
    }
    if (!dluma_native_companion_wstr_copy_checked(
            final_path, final_capacity, resolved)) {
        CloseHandle(file);
        return 0;
    }
    memcpy(info_out, &info, sizeof(info));
    *file_out = file;
    return 1;
}


static int dluma_native_companion_verify_entry_bytes(
    HANDLE file, const DlumaCompanionArchiveEntry* entry,
    char sha256_out[DLUMA_SHA256_HEX_CAPACITY])
{
    static const DWORD crc_table[16] = {
        0x00000000UL, 0x1db71064UL, 0x3b6e20c8UL, 0x26d930acUL,
        0x76dc4190UL, 0x6b6b51f4UL, 0x4db26158UL, 0x5005713cUL,
        0xedb88320UL, 0xf00f9344UL, 0xd6d6a3e8UL, 0xcb61b38cUL,
        0x9b64c2b0UL, 0x86d3d2d4UL, 0xa00ae278UL, 0xbdbdf21cUL
    };
    BYTE buffer[4096];
    BYTE digest[32];
    DlumaSha256Context sha256;
    DWORD crc = 0xffffffffUL;
    DWORD offset;
    DWORD end;
    DWORD remaining;
    int hash_entry = sha256_out != 0;
    if (sha256_out) sha256_out[0] = 0;
    if (!entry ||
        !dluma_native_companion_add_u32(
            entry->data_offset, entry->size, &end))
        return 0;
    offset = entry->data_offset;
    remaining = entry->size;
    if (hash_entry) dluma_sha256_init(&sha256);
    while (remaining) {
        DWORD chunk = remaining > sizeof(buffer)
            ? (DWORD)sizeof(buffer) : remaining;
        DWORD i;
        if (!dluma_native_companion_read_at(file, offset, buffer, chunk))
            return 0;
        for (i = 0; i < chunk; ++i) {
            crc ^= buffer[i];
            crc = (crc >> 4) ^ crc_table[crc & 0x0fU];
            crc = (crc >> 4) ^ crc_table[crc & 0x0fU];
        }
        if (hash_entry) dluma_sha256_update(&sha256, buffer, chunk);
        if (!dluma_native_companion_add_u32(offset, chunk, &offset))
            return 0;
        remaining -= chunk;
    }
    if ((crc ^ 0xffffffffUL) != entry->crc32) return 0;
    if (hash_entry) {
        dluma_sha256_final(&sha256, digest);
        dluma_sha256_hex(digest, sha256_out);
    }
    return 1;
}

static int dluma_native_companion_csb_name_valid(const BYTE* name)
{
    unsigned int i;
    unsigned int end = 64U;
    if (!name) return 0;
    for (i = 0; i < 64U; ++i) {
        if (!name[i]) {
            end = i;
            break;
        }
    }
    if (!end || end == 64U ||
        !((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= '0' && name[0] <= '9')))
        return 0;
    for (i = 0; i < end; ++i)
        if (!((name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_' || name[i] == '-' || name[i] == '.'))
            return 0;
    for (i = end + 1U; i < 64U; ++i)
        if (name[i]) return 0;
    return 1;
}

static int dluma_native_companion_validate_csb_inventory(
    HANDLE file, const DlumaCompanionArchiveEntry* entry,
    DWORD* entry_count_out,
    DlumaCompanionArchiveAudioManifest* audio_bank)
{
    BYTE header[76];
    BYTE rows[DLUMA_COMPANION_ARCHIVE_MAX_CUES][88];
    DWORD starts[DLUMA_COMPANION_ARCHIVE_MAX_CUES];
    DWORD ends[DLUMA_COMPANION_ARCHIVE_MAX_CUES];
    DWORD header_size;
    DWORD entry_count;
    DWORD table_size;
    DWORD table_end;
    DWORD archive_offset;
    unsigned int i;
    unsigned int j;
    if (entry_count_out) *entry_count_out = 0;
    if (audio_bank)
        memset(audio_bank, 0, sizeof(*audio_bank));
    if (!entry || !entry_count_out || entry->size < sizeof(header) ||
        !dluma_native_companion_read_at(
            file, entry->data_offset, header, sizeof(header)))
        return 0;
    header_size = dluma_native_companion_u32(header + 64);
    entry_count = dluma_native_companion_u32(header + 68);
    if (header_size != sizeof(header) || !entry_count ||
        entry_count > DLUMA_COMPANION_ARCHIVE_MAX_CUES ||
        dluma_native_companion_u32(header + 72) != 0x08000008UL)
        return 0;
    table_size = entry_count * 88U;
    if (!dluma_native_companion_add_u32(header_size, table_size, &table_end) ||
        table_end > entry->size ||
        !dluma_native_companion_add_u32(
            entry->data_offset, header_size, &archive_offset) ||
        !dluma_native_companion_read_at(
            file, archive_offset, rows, table_size))
        return 0;
    for (i = 0; i < entry_count; ++i) {
        const BYTE* row = rows[i];
        BYTE fsb[60];
        DWORD payload_offset = dluma_native_companion_u32(row + 64);
        DWORD payload_size = dluma_native_companion_u32(row + 68);
        DWORD sample_count = dluma_native_companion_u32(row + 76);
        DWORD duration_ms = dluma_native_companion_u32(row + 80);
        DWORD payload_end;
        DWORD sample_headers_size;
        DWORD name_table_size;
        DWORD sample_data_size;
        DWORD expected_size;
        if (!dluma_native_companion_csb_name_valid(row) ||
            payload_size < sizeof(fsb) || !sample_count || !duration_ms ||
            payload_offset < table_end ||
            !dluma_native_companion_add_u32(
                payload_offset, payload_size, &payload_end) ||
            payload_end > entry->size)
            return 0;
        starts[i] = payload_offset;
        ends[i] = payload_end;
        for (j = 0; j < i; ++j) {
            if (!(ends[j] <= starts[i] || ends[i] <= starts[j]) ||
                dluma_stricmp((const char*)rows[j], (const char*)row) == 0)
                return 0;
        }
        if (audio_bank) {
            dluma_str_copy(
                audio_bank->cues[i].name,
                DLUMA_ARRAY_COUNT(audio_bank->cues[i].name),
                (const char*)row);
            audio_bank->cues[i].duration_ms = duration_ms;
        }
        if (!dluma_native_companion_add_u32(
                entry->data_offset, payload_offset, &archive_offset) ||
            !dluma_native_companion_read_at(
                file, archive_offset, fsb, sizeof(fsb)) ||
            !dluma_native_companion_bytes_equal(
                (const char*)fsb, "FSB5", 4U) ||
            dluma_native_companion_u32(fsb + 4) > 1U ||
            dluma_native_companion_u32(fsb + 8) != 1U)
            return 0;
        sample_headers_size = dluma_native_companion_u32(fsb + 12);
        name_table_size = dluma_native_companion_u32(fsb + 16);
        sample_data_size = dluma_native_companion_u32(fsb + 20);
        if (sample_headers_size < 8U ||
            !dluma_native_companion_add_u32(
                (DWORD)sizeof(fsb), sample_headers_size, &expected_size) ||
            !dluma_native_companion_add_u32(
                expected_size, name_table_size, &expected_size) ||
            !dluma_native_companion_add_u32(
                expected_size, sample_data_size, &expected_size) ||
            expected_size != payload_size)
            return 0;
    }
    if (audio_bank)
        audio_bank->cue_count = entry_count;
    *entry_count_out = entry_count;
    return 1;
}

int dluma_companion_archive_catalog_locked_bank(
    HANDLE source_file,
    const BY_HANDLE_FILE_INFORMATION* source_identity,
    DWORD source_size,
    DlumaCompanionArchiveAudioManifest* catalog_out)
{
    DlumaCompanionArchiveEntry entry;
    DWORD cue_count = 0;
    if (catalog_out)
        memset(catalog_out, 0, sizeof(*catalog_out));
    if (source_file == INVALID_HANDLE_VALUE || !source_identity ||
        !source_size || !catalog_out || source_identity->nFileSizeHigh != 0U ||
        source_identity->nFileSizeLow != source_size)
        return 0;
    memset(&entry, 0, sizeof(entry));
    entry.size = source_size;
    if (!dluma_native_companion_validate_csb_inventory(
            source_file, &entry, &cue_count, catalog_out) ||
        !cue_count || !dluma_native_companion_identity_unchanged(
            source_file, source_identity))
    {
        memset(catalog_out, 0, sizeof(*catalog_out));
        return 0;
    }
    catalog_out->bank_size = source_size;
    catalog_out->valid = 1U;
    return 1;
}

typedef struct DlumaNativeCompanionJson {
    const char* bytes;
    unsigned int size;
    unsigned int cursor;
} DlumaNativeCompanionJson;

static void dluma_native_companion_json_space(DlumaNativeCompanionJson* json)
{
    while (json && json->cursor < json->size) {
        char c = json->bytes[json->cursor];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++json->cursor;
    }
}

static int dluma_native_companion_json_take(
    DlumaNativeCompanionJson* json, char wanted)
{
    dluma_native_companion_json_space(json);
    if (!json || json->cursor >= json->size ||
        json->bytes[json->cursor] != wanted)
        return 0;
    ++json->cursor;
    return 1;
}

static int dluma_native_companion_json_hex(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static int dluma_native_companion_json_string(
    DlumaNativeCompanionJson* json, char* out, unsigned int capacity,
    int allow_escapes)
{
    unsigned int used = 0;
    if (out && !capacity) return 0;
    if (out) out[0] = 0;
    if (!dluma_native_companion_json_take(json, '"')) return 0;
    while (json->cursor < json->size) {
        unsigned char value = (unsigned char)json->bytes[json->cursor++];
        if (value == '"') {
            if (out) out[used] = 0;
            return 1;
        }
        if (value < 0x20U) return 0;
        if (value == 0x5cU) {
            unsigned int i;
            unsigned char escaped;
            if (!allow_escapes || json->cursor >= json->size) return 0;
            escaped = (unsigned char)json->bytes[json->cursor++];
            if (escaped == 'u') {
                if (json->size - json->cursor < 4U) return 0;
                for (i = 0; i < 4U; ++i)
                    if (!dluma_native_companion_json_hex(
                            json->bytes[json->cursor + i]))
                        return 0;
                json->cursor += 4U;
            } else if (!(escaped == '"' || escaped == 0x5cU ||
                         escaped == '/' || escaped == 'b' ||
                         escaped == 'f' || escaped == 'n' ||
                         escaped == 'r' || escaped == 't')) {
                return 0;
            }
            continue;
        }
        if (out) {
            if (used + 1U >= capacity) return 0;
            out[used++] = (char)value;
        }
    }
    return 0;
}

static int dluma_native_companion_json_skip_value(
    DlumaNativeCompanionJson* json, unsigned int depth);

static int dluma_native_companion_json_number(
    DlumaNativeCompanionJson* json)
{
    unsigned int start;
    dluma_native_companion_json_space(json);
    if (!json || json->cursor >= json->size) return 0;
    start = json->cursor;
    if (json->bytes[json->cursor] == '-') ++json->cursor;
    if (json->cursor >= json->size) return 0;
    if (json->bytes[json->cursor] == '0') {
        ++json->cursor;
        if (json->cursor < json->size &&
            json->bytes[json->cursor] >= '0' &&
            json->bytes[json->cursor] <= '9')
            return 0;
    } else {
        if (json->bytes[json->cursor] < '1' ||
            json->bytes[json->cursor] > '9')
            return 0;
        while (json->cursor < json->size &&
               json->bytes[json->cursor] >= '0' &&
               json->bytes[json->cursor] <= '9')
            ++json->cursor;
    }
    if (json->cursor < json->size && json->bytes[json->cursor] == '.') {
        ++json->cursor;
        if (json->cursor >= json->size ||
            json->bytes[json->cursor] < '0' ||
            json->bytes[json->cursor] > '9')
            return 0;
        while (json->cursor < json->size &&
               json->bytes[json->cursor] >= '0' &&
               json->bytes[json->cursor] <= '9')
            ++json->cursor;
    }
    if (json->cursor < json->size &&
        (json->bytes[json->cursor] == 'e' ||
         json->bytes[json->cursor] == 'E')) {
        ++json->cursor;
        if (json->cursor < json->size &&
            (json->bytes[json->cursor] == '+' ||
             json->bytes[json->cursor] == '-'))
            ++json->cursor;
        if (json->cursor >= json->size ||
            json->bytes[json->cursor] < '0' ||
            json->bytes[json->cursor] > '9')
            return 0;
        while (json->cursor < json->size &&
               json->bytes[json->cursor] >= '0' &&
               json->bytes[json->cursor] <= '9')
            ++json->cursor;
    }
    return json->cursor > start;
}

static int dluma_native_companion_json_skip_value(
    DlumaNativeCompanionJson* json, unsigned int depth)
{
    char value;
    if (!json || depth >= DLUMA_COMPANION_ARCHIVE_JSON_MAX_DEPTH) return 0;
    dluma_native_companion_json_space(json);
    if (json->cursor >= json->size) return 0;
    value = json->bytes[json->cursor];
    if (value == '"')
        return dluma_native_companion_json_string(json, 0, 0, 1);
    if (value == '{') {
        ++json->cursor;
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size && json->bytes[json->cursor] == '}') {
            ++json->cursor;
            return 1;
        }
        for (;;) {
            if (!dluma_native_companion_json_string(json, 0, 0, 1) ||
                !dluma_native_companion_json_take(json, ':') ||
                !dluma_native_companion_json_skip_value(json, depth + 1U))
                return 0;
            dluma_native_companion_json_space(json);
            if (json->cursor < json->size &&
                json->bytes[json->cursor] == '}') {
                ++json->cursor;
                return 1;
            }
            if (!dluma_native_companion_json_take(json, ',')) return 0;
        }
    }
    if (value == '[') {
        ++json->cursor;
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size && json->bytes[json->cursor] == ']') {
            ++json->cursor;
            return 1;
        }
        for (;;) {
            if (!dluma_native_companion_json_skip_value(json, depth + 1U))
                return 0;
            dluma_native_companion_json_space(json);
            if (json->cursor < json->size &&
                json->bytes[json->cursor] == ']') {
                ++json->cursor;
                return 1;
            }
            if (!dluma_native_companion_json_take(json, ',')) return 0;
        }
    }
    if (value == 't' && json->size - json->cursor >= 4U &&
        dluma_native_companion_bytes_equal(
            json->bytes + json->cursor, "true", 4U)) {
        json->cursor += 4U;
        return 1;
    }
    if (value == 'f' && json->size - json->cursor >= 5U &&
        dluma_native_companion_bytes_equal(
            json->bytes + json->cursor, "false", 5U)) {
        json->cursor += 5U;
        return 1;
    }
    if (value == 'n' && json->size - json->cursor >= 4U &&
        dluma_native_companion_bytes_equal(
            json->bytes + json->cursor, "null", 4U)) {
        json->cursor += 4U;
        return 1;
    }
    return dluma_native_companion_json_number(json);
}

static int dluma_native_companion_json_u32(
    DlumaNativeCompanionJson* json, DWORD* out)
{
    DWORD value = 0;
    unsigned int digits = 0;
    if (out) *out = 0;
    dluma_native_companion_json_space(json);
    if (!json || !out || json->cursor >= json->size ||
        json->bytes[json->cursor] < '0' ||
        json->bytes[json->cursor] > '9')
        return 0;
    if (json->bytes[json->cursor] == '0' &&
        json->cursor + 1U < json->size &&
        json->bytes[json->cursor + 1U] >= '0' &&
        json->bytes[json->cursor + 1U] <= '9')
        return 0;
    while (json->cursor < json->size &&
           json->bytes[json->cursor] >= '0' &&
           json->bytes[json->cursor] <= '9') {
        DWORD digit = (DWORD)(json->bytes[json->cursor] - '0');
        if (value > (0xffffffffUL - digit) / 10U) return 0;
        value = value * 10U + digit;
        ++json->cursor;
        ++digits;
    }
    if (!digits) return 0;
    *out = value;
    return 1;
}

static int dluma_native_companion_id_valid(
    const char* value, unsigned int capacity)
{
    unsigned int i;
    if (!value || !value[0] || capacity < 2U) return 0;
    for (i = 0; i < capacity; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!c) return i > 0U;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 0;
}

static int dluma_native_companion_cue_index(
    const DlumaCompanionArchiveAudioManifest* manifest,
    const char* cue_name, unsigned int* cue_index_out)
{
    unsigned int i;
    if (cue_index_out) *cue_index_out = 0;
    if (!manifest || !cue_name || !cue_index_out ||
        !dluma_native_companion_id_valid(
            cue_name, DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME))
        return 0;
    for (i = 0; i < manifest->cue_count; ++i) {
        if (dluma_native_companion_str_equal(
                manifest->cues[i].name, cue_name)) {
            *cue_index_out = i;
            return 1;
        }
    }
    return 0;
}

static int dluma_companion_audio_parse_cue(
    DlumaNativeCompanionJson* json,
    DlumaCompanionArchiveCue* cue)
{
    unsigned int fields = 0;
    if (!json || !cue ||
        !dluma_native_companion_json_take(json, '{'))
        return 0;
    memset(cue, 0, sizeof(*cue));
    for (;;) {
        char key[64];
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size &&
            json->bytes[json->cursor] == '}') {
            ++json->cursor;
            break;
        }
        if (!dluma_native_companion_json_string(
                json, key, DLUMA_ARRAY_COUNT(key), 0) ||
            !dluma_native_companion_json_take(json, ':'))
            return 0;
        if (dluma_native_companion_str_equal(key, "name")) {
            if ((fields & 1U) ||
                !dluma_native_companion_json_string(
                    json, cue->name,
                    DLUMA_ARRAY_COUNT(cue->name), 0))
                return 0;
            fields |= 1U;
        } else if (dluma_native_companion_str_equal(
                       key, "duration_ms")) {
            if ((fields & 2U) ||
                !dluma_native_companion_json_u32(
                    json, &cue->duration_ms))
                return 0;
            fields |= 2U;
        } else {
            return 0;
        }
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size &&
            json->bytes[json->cursor] == '}') {
            ++json->cursor;
            break;
        }
        if (!dluma_native_companion_json_take(json, ','))
            return 0;
    }
    return fields == 3U && cue->duration_ms &&
           dluma_native_companion_id_valid(
               cue->name, DLUMA_ARRAY_COUNT(cue->name));
}

static int dluma_companion_audio_parse_cues(
    DlumaNativeCompanionJson* json,
    DlumaCompanionArchiveAudioManifest* bank)
{
    if (!json || !bank ||
        !dluma_native_companion_json_take(json, '['))
        return 0;
    dluma_native_companion_json_space(json);
    if (json->cursor < json->size &&
        json->bytes[json->cursor] == ']')
        return 0;
    for (;;) {
        unsigned int index;
        if (bank->cue_count >= DLUMA_COMPANION_ARCHIVE_MAX_CUES ||
            !dluma_companion_audio_parse_cue(
                json, &bank->cues[bank->cue_count]))
            return 0;
        for (index = 0; index < bank->cue_count; ++index)
            if (dluma_stricmp(
                    bank->cues[index].name,
                    bank->cues[bank->cue_count].name) == 0)
                return 0;
        ++bank->cue_count;
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size &&
            json->bytes[json->cursor] == ']') {
            ++json->cursor;
            return 1;
        }
        if (!dluma_native_companion_json_take(json, ','))
            return 0;
    }
}

static int dluma_companion_audio_parse_bank(
    DlumaNativeCompanionJson* json,
    DlumaCompanionArchiveAudioManifest* bank)
{
    unsigned int fields = 0;
    if (!json || !bank ||
        !dluma_native_companion_json_take(json, '{'))
        return 0;
    memset(bank, 0, sizeof(*bank));
    for (;;) {
        char key[64];
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size &&
            json->bytes[json->cursor] == '}') {
            ++json->cursor;
            break;
        }
        if (!dluma_native_companion_json_string(
                json, key, DLUMA_ARRAY_COUNT(key), 0) ||
            !dluma_native_companion_json_take(json, ':'))
            return 0;
        if (dluma_native_companion_str_equal(key, "id")) {
            if ((fields & 1U) ||
                !dluma_native_companion_json_string(
                    json, bank->bank_identifier,
                    DLUMA_ARRAY_COUNT(bank->bank_identifier), 0))
                return 0;
            fields |= 1U;
        } else if (dluma_native_companion_str_equal(key, "entry")) {
            if ((fields & 2U) ||
                !dluma_native_companion_json_string(
                    json, bank->bank_path,
                    DLUMA_ARRAY_COUNT(bank->bank_path), 0))
                return 0;
            fields |= 2U;
        } else if (dluma_native_companion_str_equal(key, "route")) {
            if ((fields & 4U) ||
                !dluma_native_companion_json_string(
                    json, bank->route,
                    DLUMA_ARRAY_COUNT(bank->route), 0))
                return 0;
            fields |= 4U;
        } else if (dluma_native_companion_str_equal(key, "cues")) {
            if ((fields & 8U) ||
                !dluma_companion_audio_parse_cues(json, bank))
                return 0;
            fields |= 8U;
        } else {
            return 0;
        }
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size &&
            json->bytes[json->cursor] == '}') {
            ++json->cursor;
            break;
        }
        if (!dluma_native_companion_json_take(json, ','))
            return 0;
    }
    return fields == 15U && bank->cue_count &&
           dluma_native_companion_id_valid(
               bank->bank_identifier,
               DLUMA_ARRAY_COUNT(bank->bank_identifier)) &&
           dluma_native_companion_path_valid(bank->bank_path) &&
           dluma_native_companion_str_equal(bank->route, "music");
}

static int dluma_companion_audio_parse_banks(
    DlumaNativeCompanionJson* json,
    DlumaCompanionArchiveAudioCatalog* catalog)
{
    if (!json || !catalog ||
        !dluma_native_companion_json_take(json, '['))
        return 0;
    dluma_native_companion_json_space(json);
    if (json->cursor < json->size &&
        json->bytes[json->cursor] == ']')
        return 0;
    for (;;) {
        unsigned int index;
        DlumaCompanionArchiveAudioManifest* bank;
        if (catalog->bank_count >= DLUMA_COMPANION_ARCHIVE_MAX_BANKS)
            return 0;
        bank = &catalog->banks[catalog->bank_count];
        if (!dluma_companion_audio_parse_bank(json, bank))
            return 0;
        for (index = 0; index < catalog->bank_count; ++index)
            if (dluma_stricmp(
                    catalog->banks[index].bank_identifier,
                    bank->bank_identifier) == 0 ||
                dluma_stricmp(
                    catalog->banks[index].bank_path,
                    bank->bank_path) == 0)
                return 0;
        ++catalog->bank_count;
        dluma_native_companion_json_space(json);
        if (json->cursor < json->size &&
            json->bytes[json->cursor] == ']') {
            ++json->cursor;
            return 1;
        }
        if (!dluma_native_companion_json_take(json, ','))
            return 0;
    }
}

static int dluma_companion_audio_parse_manifest(
    const char* bytes,
    unsigned int size,
    DlumaCompanionArchiveAudioCatalog* catalog)
{
    DlumaNativeCompanionJson json;
    unsigned int fields = 0;
    if (!bytes || !size || !catalog) return 0;
    memset(catalog, 0, sizeof(*catalog));
    json.bytes = bytes;
    json.size = size;
    json.cursor = 0;
    if (!dluma_native_companion_json_take(&json, '{')) return 0;
    for (;;) {
        char key[64];
        dluma_native_companion_json_space(&json);
        if (json.cursor < json.size && json.bytes[json.cursor] == '}') {
            ++json.cursor;
            break;
        }
        if (!dluma_native_companion_json_string(
                &json, key, DLUMA_ARRAY_COUNT(key), 0) ||
            !dluma_native_companion_json_take(&json, ':'))
            return 0;
        if (dluma_native_companion_str_equal(key, "schema")) {
            if ((fields & 1U) ||
                !dluma_native_companion_json_string(
                    &json, catalog->schema,
                    DLUMA_ARRAY_COUNT(catalog->schema), 0))
                return 0;
            fields |= 1U;
        } else if (dluma_native_companion_str_equal(key, "banks")) {
            if ((fields & 2U) ||
                !dluma_companion_audio_parse_banks(&json, catalog))
                return 0;
            fields |= 2U;
        } else {
            return 0;
        }
        dluma_native_companion_json_space(&json);
        if (json.cursor < json.size && json.bytes[json.cursor] == '}') {
            ++json.cursor;
            break;
        }
        if (!dluma_native_companion_json_take(&json, ',')) return 0;
    }
    dluma_native_companion_json_space(&json);
    if (fields != 3U || !catalog->bank_count ||
        !dluma_native_companion_str_equal(
            catalog->schema, "dluma.companion.audio.v1") ||
        json.cursor != json.size)
        return 0;
    catalog->valid = 1U;
    return 1;
}

static const DlumaCompanionArchiveEntry*
dluma_companion_audio_find_entry(
    const DlumaCompanionArchiveEntry* entries,
    unsigned int entry_count,
    const char* path)
{
    unsigned int index;
    const DlumaCompanionArchiveEntry* found = 0;
    for (index = 0; index < entry_count; ++index) {
        if (!dluma_native_companion_str_equal(
                entries[index].virtual_path, path))
            continue;
        if (found) return 0;
        found = &entries[index];
    }
    return found;
}

static int dluma_companion_audio_reconcile(
    HANDLE file,
    const char* manifest_path,
    const DlumaCompanionArchiveEntry* entries,
    unsigned int entry_count,
    DlumaCompanionArchiveAudioCatalog* catalog_out)
{
    const DlumaCompanionArchiveEntry* manifest_entry;
    DlumaCompanionArchiveAudioCatalog catalog;
    char* bytes = 0;
    unsigned int bank_index;
    int valid = 0;
    if (catalog_out) memset(catalog_out, 0, sizeof(*catalog_out));
    if (!file || !manifest_path || !entries || !entry_count)
        return 0;
    manifest_entry = dluma_companion_audio_find_entry(
        entries, entry_count, manifest_path);
    if (!manifest_entry || !manifest_entry->is_json ||
        !manifest_entry->size ||
        manifest_entry->size > DLUMA_COMPANION_ARCHIVE_MAX_JSON_BYTES)
        return 0;
    bytes = (char*)dluma_alloc((SIZE_T)manifest_entry->size + 1U);
    if (!bytes || !dluma_native_companion_read_at(
            file, manifest_entry->data_offset,
            bytes, manifest_entry->size))
        goto done;
    bytes[manifest_entry->size] = 0;
    if (!dluma_companion_audio_parse_manifest(
            bytes, manifest_entry->size, &catalog))
        goto done;
    for (bank_index = 0; bank_index < catalog.bank_count; ++bank_index) {
        DlumaCompanionArchiveAudioManifest declared =
            catalog.banks[bank_index];
        DlumaCompanionArchiveAudioManifest actual;
        const DlumaCompanionArchiveEntry* entry =
            dluma_companion_audio_find_entry(
                entries, entry_count, declared.bank_path);
        DWORD cue_count = 0;
        char sha256[DLUMA_SHA256_HEX_CAPACITY];
        unsigned int cue_index;
        if (!entry || !entry->is_csb ||
            !dluma_native_companion_verify_entry_bytes(
                file, entry, sha256) ||
            !dluma_native_companion_validate_csb_inventory(
                file, entry, &cue_count, &actual) ||
            cue_count != declared.cue_count)
            goto done;
        for (cue_index = 0; cue_index < declared.cue_count; ++cue_index) {
            unsigned int actual_index;
            int matched = 0;
            for (actual_index = 0;
                 actual_index < actual.cue_count;
                 ++actual_index) {
                if (dluma_stricmp(
                        declared.cues[cue_index].name,
                        actual.cues[actual_index].name) == 0 &&
                    declared.cues[cue_index].duration_ms ==
                        actual.cues[actual_index].duration_ms) {
                    matched = 1;
                    break;
                }
            }
            if (!matched) goto done;
        }
        actual.valid = 1U;
        actual.bank_size = entry->size;
        dluma_str_copy(
            actual.bank_identifier,
            DLUMA_ARRAY_COUNT(actual.bank_identifier),
            declared.bank_identifier);
        dluma_str_copy(
            actual.bank_name,
            DLUMA_ARRAY_COUNT(actual.bank_name),
            declared.bank_identifier);
        dluma_str_copy(
            actual.route, DLUMA_ARRAY_COUNT(actual.route),
            declared.route);
        dluma_str_copy(
            actual.bank_path,
            DLUMA_ARRAY_COUNT(actual.bank_path),
            declared.bank_path);
        dluma_str_copy(
            actual.bank_sha256,
            DLUMA_ARRAY_COUNT(actual.bank_sha256), sha256);
        catalog.banks[bank_index] = actual;
    }
    catalog.valid = 1U;
    if (catalog_out) *catalog_out = catalog;
    valid = 1;
done:
    dluma_free(bytes);
    return valid;
}

static int dluma_native_companion_allowed_entry(
    const char* name, const char* manifest_path,
    const char* config_manifest_path,
    unsigned char* json_out, unsigned char* csb_out)
{
    if (json_out) *json_out = 0;
    if (csb_out) *csb_out = 0;
    SIZE_T length;
    if (!dluma_native_companion_path_valid(name)) return 0;
    length = strlen(name);
    if (length > 4U && dluma_stricmp(name + length - 4U, ".csb") == 0) {
        if (csb_out) *csb_out = 1;
        return 1;
    }
    if (length > 5U && dluma_stricmp(name + length - 5U, ".json") == 0) {
        if (json_out) *json_out = 1;
        return 1;
    }
    (void)manifest_path;
    (void)config_manifest_path;
    return 1;
}

static int dluma_native_companion_validate_zip(
    HANDLE file, DWORD file_size, const char* manifest_path,
    const char* config_manifest_path,
    DlumaCompanionArchiveEntry* entries, unsigned int* entry_count_out,
    DlumaCompanionArchiveAudioCatalog* audio_catalog_out)
{
    BYTE eocd[22];
    BYTE central[46];
    BYTE local[30];
    DWORD eocd_offset;
    DWORD central_offset;
    DWORD central_size;
    DWORD cursor;
    WORD count;
    unsigned int i;
    unsigned int json_total = 0;
    unsigned int json_count = 0;
    unsigned int csb_count = 0;
    unsigned int manifest_count = 0;
    unsigned int config_manifest_count = 0;
    if (entry_count_out) *entry_count_out = 0;
    if (audio_catalog_out)
        memset(audio_catalog_out, 0, sizeof(*audio_catalog_out));
    if (!entries || !entry_count_out || file_size < sizeof(eocd))
        return 0;
    eocd_offset = file_size - (DWORD)sizeof(eocd);
    if (!dluma_native_companion_read_at(
            file, eocd_offset, eocd, sizeof(eocd)) ||
        dluma_native_companion_u32(eocd) !=
            DLUMA_NATIVE_ZIP_EOCD_SIGNATURE ||
        dluma_native_companion_u16(eocd + 4) != 0 ||
        dluma_native_companion_u16(eocd + 6) != 0 ||
        dluma_native_companion_u16(eocd + 8) !=
            dluma_native_companion_u16(eocd + 10) ||
        dluma_native_companion_u16(eocd + 20) != 0)
        return 0;
    count = dluma_native_companion_u16(eocd + 10);
    central_size = dluma_native_companion_u32(eocd + 12);
    central_offset = dluma_native_companion_u32(eocd + 16);
    if (!count || count > DLUMA_COMPANION_ARCHIVE_MAX_ENTRIES ||
        central_offset > eocd_offset ||
        central_size != eocd_offset - central_offset)
        return 0;
    cursor = central_offset;
    for (i = 0; i < count; ++i) {
        WORD flags;
        WORD method;
        WORD name_length;
        WORD extra_length;
        WORD comment_length;
        DWORD compressed_size;
        DWORD uncompressed_size;
        DWORD local_offset;
        DWORD next;
        char name[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
        unsigned int j;
        unsigned char is_json = 0;
        unsigned char is_csb = 0;
        if (cursor > eocd_offset ||
            sizeof(central) > eocd_offset - cursor ||
            !dluma_native_companion_read_at(
                file, cursor, central, sizeof(central)) ||
            dluma_native_companion_u32(central) !=
                DLUMA_NATIVE_ZIP_CENTRAL_SIGNATURE)
            return 0;
        flags = dluma_native_companion_u16(central + 8);
        method = dluma_native_companion_u16(central + 10);
        compressed_size = dluma_native_companion_u32(central + 20);
        uncompressed_size = dluma_native_companion_u32(central + 24);
        name_length = dluma_native_companion_u16(central + 28);
        extra_length = dluma_native_companion_u16(central + 30);
        comment_length = dluma_native_companion_u16(central + 32);
        local_offset = dluma_native_companion_u32(central + 42);
        if (flags || method || !name_length ||
            name_length >= sizeof(name) ||
            extra_length || comment_length ||
            compressed_size != uncompressed_size ||
            uncompressed_size > DLUMA_COMPANION_ARCHIVE_MAX_BYTES ||
            !dluma_native_companion_add_u32(
                cursor, (DWORD)sizeof(central), &next) ||
            !dluma_native_companion_add_u32(next, name_length, &next) ||
            next > eocd_offset ||
            !dluma_native_companion_read_at(
                file, cursor + (DWORD)sizeof(central),
                name, name_length))
            return 0;
        name[name_length] = 0;
        if (!dluma_native_companion_allowed_entry(
                name, manifest_path, config_manifest_path,
                &is_json, &is_csb))
            return 0;
        for (j = 0; j < i; ++j)
            if (dluma_stricmp(entries[j].virtual_path, name) == 0)
                return 0;
        if (is_json) {
            if (!uncompressed_size ||
                uncompressed_size > DLUMA_COMPANION_ARCHIVE_MAX_JSON_BYTES ||
                uncompressed_size >
                    DLUMA_COMPANION_ARCHIVE_MAX_TOTAL_JSON_BYTES - json_total)
                return 0;
            json_total += uncompressed_size;
            ++json_count;
            if (manifest_path && manifest_path[0] &&
                dluma_native_companion_str_equal(name, manifest_path))
                ++manifest_count;
            if (config_manifest_path &&
                config_manifest_path[0] &&
                dluma_native_companion_str_equal(
                    name, config_manifest_path))
                ++config_manifest_count;
        }
        if (is_csb && !uncompressed_size) return 0;
        if (is_csb) ++csb_count;
        if (local_offset >= central_offset ||
            sizeof(local) > central_offset - local_offset ||
            !dluma_native_companion_read_at(
                file, local_offset, local, sizeof(local)) ||
            dluma_native_companion_u32(local) !=
                DLUMA_NATIVE_ZIP_LOCAL_SIGNATURE ||
            dluma_native_companion_u16(local + 6) != flags ||
            dluma_native_companion_u16(local + 8) != method ||
            dluma_native_companion_u32(local + 14) !=
                dluma_native_companion_u32(central + 16) ||
            dluma_native_companion_u32(local + 18) != compressed_size ||
            dluma_native_companion_u32(local + 22) != uncompressed_size ||
            dluma_native_companion_u16(local + 26) != name_length ||
            dluma_native_companion_u16(local + 28) != 0)
            return 0;
        {
            char local_name[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
            DWORD data_offset;
            DWORD data_end;
            if (!dluma_native_companion_read_at(
                    file, local_offset + (DWORD)sizeof(local),
                    local_name, name_length))
                return 0;
            local_name[name_length] = 0;
            if (!dluma_native_companion_str_equal(local_name, name) ||
                !dluma_native_companion_add_u32(
                    local_offset, (DWORD)sizeof(local), &data_offset) ||
                !dluma_native_companion_add_u32(
                    data_offset, name_length, &data_offset) ||
                !dluma_native_companion_add_u32(
                    data_offset, uncompressed_size, &data_end) ||
                data_end > central_offset)
                return 0;
            dluma_str_copy(
                entries[i].virtual_path,
                DLUMA_ARRAY_COUNT(entries[i].virtual_path), name);
            entries[i].local_header_offset = local_offset;
            entries[i].data_offset = data_offset;
            entries[i].size = uncompressed_size;
            entries[i].crc32 =
                dluma_native_companion_u32(central + 16);
            entries[i].is_json = is_json;
            entries[i].is_csb = is_csb;
        }
        cursor = next;
    }
    if (cursor != eocd_offset ||
        (manifest_path && manifest_path[0]
             ? (!csb_count || manifest_count != 1U || !json_count)
             : manifest_count != 0U) ||
        (config_manifest_path && config_manifest_path[0]
             ? config_manifest_count != 1U
             : config_manifest_count != 0U))
        return 0;
    for (i = 0; i < count; ++i) {
        unsigned int j;
        DWORD first_end;
        if (!dluma_native_companion_add_u32(
                entries[i].data_offset, entries[i].size, &first_end))
            return 0;
        for (j = i + 1U; j < count; ++j) {
            DWORD second_end;
            if (!dluma_native_companion_add_u32(
                    entries[j].data_offset, entries[j].size, &second_end))
                return 0;
            if (!(first_end <= entries[j].local_header_offset ||
                  second_end <= entries[i].local_header_offset))
                return 0;
        }
    }
    for (i = 0; i < count; ++i) {
        if (!dluma_native_companion_verify_entry_bytes(
                file, &entries[i], 0))
            return 0;
    }
    if (manifest_path && manifest_path[0] &&
        !dluma_companion_audio_reconcile(
            file, manifest_path, entries, count,
            audio_catalog_out))
        return 0;
    *entry_count_out = count;
    return 1;
}

static int dluma_native_companion_parse_config_schema(
    HANDLE file,
    const DlumaCompanionArchiveEntry* entries,
    unsigned int entry_count,
    const char* config_manifest_path,
    DlumaLuaConfigSchema* schema,
    char* diagnostic,
    unsigned int diagnostic_capacity)
{
    const DlumaCompanionArchiveEntry* entry = 0;
    char* bytes = 0;
    unsigned int index;
    int result = 0;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = 0;
    if (!schema) return 0;
    memset(schema, 0, sizeof(*schema));
    if (file == INVALID_HANDLE_VALUE || !entries || !entry_count ||
        !config_manifest_path || !config_manifest_path[0]) {
        dluma_str_copy(
            diagnostic, diagnostic_capacity,
            "config_manifest_missing");
        return 0;
    }
    for (index = 0; index < entry_count; ++index) {
        if (dluma_native_companion_str_equal(
                entries[index].virtual_path,
                config_manifest_path)) {
            if (entry) {
                dluma_str_copy(
                    diagnostic, diagnostic_capacity,
                    "duplicate_identifier");
                return 0;
            }
            entry = &entries[index];
        }
    }
    if (!entry || !entry->is_json || !entry->size ||
        entry->size > DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES) {
        dluma_str_copy(
            diagnostic, diagnostic_capacity,
            "config_manifest_missing");
        return 0;
    }
    bytes = (char*)dluma_alloc((SIZE_T)entry->size);
    if (!bytes) {
        dluma_str_copy(
            diagnostic, diagnostic_capacity,
            "config_schema_allocation_failed");
        return 0;
    }
    if (dluma_native_companion_read_at(
            file, entry->data_offset, bytes, entry->size))
        result = dluma_native_mod_config_parse_json(
            bytes, entry->size, schema,
            diagnostic, diagnostic_capacity);
    else
        dluma_str_copy(
            diagnostic, diagnostic_capacity,
            "config_manifest_read_failed");
    dluma_free(bytes);
    return result;
}

static int dluma_native_companion_identity_unchanged(
    HANDLE file, const BY_HANDLE_FILE_INFORMATION* before)
{
    BY_HANDLE_FILE_INFORMATION after;
    memset(&after, 0, sizeof(after));
    return before && GetFileInformationByHandle(file, &after) &&
           before->dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
           before->nFileIndexHigh == after.nFileIndexHigh &&
           before->nFileIndexLow == after.nFileIndexLow &&
           before->nFileSizeHigh == after.nFileSizeHigh &&
           before->nFileSizeLow == after.nFileSizeLow &&
           before->ftLastWriteTime.dwHighDateTime ==
               after.ftLastWriteTime.dwHighDateTime &&
           before->ftLastWriteTime.dwLowDateTime ==
               after.ftLastWriteTime.dwLowDateTime;
}

int dluma_companion_archive_discover_file(
    const WCHAR* final_path,
    const char* content_manifest_path,
    DlumaCompanionArchiveSnapshot* snapshot)
{
    const WCHAR* filename;
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    DlumaCompanionArchiveEntry entries[DLUMA_COMPANION_ARCHIVE_MAX_ENTRIES];
    unsigned int entry_count = 0;
    char filename_utf8[128];
    char manifest_utf8[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    if (!snapshot) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->declared = 1;
    manifest_utf8[0] = 0;
    if (!final_path || !final_path[0] || !content_manifest_path ||
        (content_manifest_path[0] &&
         (!dluma_native_companion_path_valid(content_manifest_path) ||
          strlen(content_manifest_path) >= sizeof(manifest_utf8)))) {
        dluma_str_copy(
            snapshot->error, DLUMA_ARRAY_COUNT(snapshot->error),
            "invalid manifest-declared companion audio path");
        return 0;
    }
    if (content_manifest_path[0])
        dluma_str_copy(
            manifest_utf8, DLUMA_ARRAY_COUNT(manifest_utf8),
            content_manifest_path);
    filename = final_path;
    {
        unsigned int index;
        for (index = 0; final_path[index]; ++index)
            if (final_path[index] == L'\\' ||
                final_path[index] == L'/')
                filename = final_path + index + 1U;
    }
    if (!dluma_native_companion_ascii_basename(
            filename, filename_utf8,
            DLUMA_ARRAY_COUNT(filename_utf8))) {
        dluma_str_copy(
            snapshot->error, DLUMA_ARRAY_COUNT(snapshot->error),
            "invalid manifest-declared companion filename");
        return 0;
    }
    memset(&info, 0, sizeof(info));
    if (!dluma_native_companion_open_regular(
            final_path, &file, snapshot->final_path,
            DLUMA_ARRAY_COUNT(snapshot->final_path), &info) ||
        dluma_wstricmp(snapshot->final_path, final_path) != 0) {
        dluma_str_copy(
            snapshot->error, DLUMA_ARRAY_COUNT(snapshot->error),
            "companion pack is missing, redirected, or not a regular file");
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    snapshot->volume_serial = info.dwVolumeSerialNumber;
    snapshot->file_index_high = info.nFileIndexHigh;
    snapshot->file_index_low = info.nFileIndexLow;
    snapshot->file_size_high = info.nFileSizeHigh;
    snapshot->file_size_low = info.nFileSizeLow;
    dluma_str_copy(
        snapshot->filename, DLUMA_ARRAY_COUNT(snapshot->filename),
        filename_utf8);
    dluma_str_copy(
        snapshot->manifest_path,
        DLUMA_ARRAY_COUNT(snapshot->manifest_path), manifest_utf8);
    snapshot->config_manifest_path[0] = 0;
    if (!dluma_sha256_handle_hex(
            file, DLUMA_COMPANION_ARCHIVE_MAX_BYTES, snapshot->sha256) ||
        !dluma_native_companion_identity_unchanged(file, &info) ||
        !dluma_native_companion_validate_zip(
            file, info.nFileSizeLow, manifest_utf8,
            snapshot->config_manifest_path,
            entries, &entry_count, 0)) {
        dluma_str_copy(
            snapshot->error, DLUMA_ARRAY_COUNT(snapshot->error),
            "companion pack hash, identity, or stored-ZIP contract is invalid");
        CloseHandle(file);
        return 0;
    }
    snapshot->valid = 1;
    CloseHandle(file);
    return 1;
}

int dluma_companion_archive_lock(
    const DlumaCompanionArchiveSnapshot* snapshot,
    DlumaCompanionArchive* pack)
{
    BY_HANDLE_FILE_INFORMATION info;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaLuaConfigSchema* config_schema = 0;
    char config_diagnostic[160];
    unsigned int index;
    int result = 0;
    if (!snapshot || !pack || !snapshot->declared || !snapshot->valid)
        return 0;
    memset(pack, 0, sizeof(*pack));
    pack->file = INVALID_HANDLE_VALUE;
    for (index = 0; index < DLUMA_COMPANION_ARCHIVE_MAX_BANKS; ++index) {
        pack->materialized_banks[index].runtime_dir = INVALID_HANDLE_VALUE;
        pack->materialized_banks[index].cache_dir = INVALID_HANDLE_VALUE;
        pack->materialized_banks[index].bank_dir = INVALID_HANDLE_VALUE;
        pack->materialized_banks[index].bank_file = INVALID_HANDLE_VALUE;
    }
    memset(&info, 0, sizeof(info));
    if (snapshot->config_available) {
        config_schema = (DlumaLuaConfigSchema*)dluma_alloc(
            sizeof(*config_schema));
        if (!config_schema) return 0;
        memset(config_schema, 0, sizeof(*config_schema));
    }
    if (!dluma_native_companion_open_regular(
            snapshot->final_path, &pack->file, pack->final_path,
            DLUMA_ARRAY_COUNT(pack->final_path), &info) ||
        dluma_wstricmp(pack->final_path, snapshot->final_path) != 0 ||
        info.dwVolumeSerialNumber != snapshot->volume_serial ||
        info.nFileIndexHigh != snapshot->file_index_high ||
        info.nFileIndexLow != snapshot->file_index_low ||
        info.nFileSizeHigh != snapshot->file_size_high ||
        info.nFileSizeLow != snapshot->file_size_low ||
        !dluma_sha256_handle_hex(
            pack->file, DLUMA_COMPANION_ARCHIVE_MAX_BYTES, sha256) ||
        dluma_stricmp(sha256, snapshot->sha256) != 0 ||
        !dluma_native_companion_identity_unchanged(pack->file, &info) ||
        !dluma_native_companion_validate_zip(
            pack->file, info.nFileSizeLow, snapshot->manifest_path,
            snapshot->config_manifest_path,
            pack->entries, &pack->entry_count,
            &pack->audio_catalog) ||
        !dluma_wide_to_utf8(
            pack->final_path, pack->final_path_utf8,
            DLUMA_ARRAY_COUNT(pack->final_path_utf8))) {
        goto done;
    }
    config_diagnostic[0] = 0;
    if (snapshot->config_available &&
        (!dluma_native_companion_parse_config_schema(
             pack->file, pack->entries, pack->entry_count,
             snapshot->config_manifest_path, config_schema,
             config_diagnostic,
             DLUMA_ARRAY_COUNT(config_diagnostic)) ||
         dluma_stricmp(
             config_schema->fingerprint,
             snapshot->config_schema_fingerprint) != 0)) {
        goto done;
    }
    memcpy(&pack->identity, &info, sizeof(info));
    if (snapshot->manifest_path[0]) {
        if (!pack->audio_catalog.valid ||
            !pack->audio_catalog.bank_count)
            goto done;
        pack->audio_manifest = pack->audio_catalog.banks[0];
    }
    dluma_str_copy(
        pack->manifest_path, DLUMA_ARRAY_COUNT(pack->manifest_path),
        snapshot->manifest_path);
    dluma_str_copy(
        pack->config_manifest_path,
        DLUMA_ARRAY_COUNT(pack->config_manifest_path),
        snapshot->config_manifest_path);
    dluma_str_copy(
        pack->sha256, DLUMA_ARRAY_COUNT(pack->sha256), sha256);
    dluma_str_copy(
        pack->config_schema_fingerprint,
        DLUMA_ARRAY_COUNT(pack->config_schema_fingerprint),
        snapshot->config_schema_fingerprint);
    pack->validated = 1;
    result = 1;
done:
    dluma_free(config_schema);
    if (!result) dluma_companion_archive_close(pack);
    return result;
}

static int dluma_native_companion_module_identity(
    HMODULE module, DWORD* timestamp, DWORD* image_size)
{
    BYTE* base = (BYTE*)module;
    DWORD nt;
    if (timestamp) *timestamp = 0;
    if (image_size) *image_size = 0;
    if (!base || !dluma_is_readable(base, 0x100) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = dluma_native_companion_u32(base + 0x3c);
    if (!nt || nt > 0x100000UL ||
        !dluma_is_readable(base + nt, 0x58) ||
        memcmp(base + nt, "PE\0\0", 4) != 0)
        return 0;
    if (timestamp)
        *timestamp = dluma_native_companion_u32(base + nt + 8);
    if (image_size)
        *image_size = dluma_native_companion_u32(base + nt + 0x50);
    return timestamp && image_size && *timestamp && *image_size;
}

enum DlumaNativeCompanionVfsDiagnostic {
    DLUMA_COMPANION_ARCHIVE_VFS_NONE = 0,
    DLUMA_COMPANION_ARCHIVE_VFS_VISIBLE,
    DLUMA_COMPANION_ARCHIVE_VFS_BINDING_INVALID,
    DLUMA_COMPANION_ARCHIVE_VFS_UNAPPROVED_PROVIDER,
    DLUMA_COMPANION_ARCHIVE_VFS_REGISTERED_ENTRY_MISSING,
    DLUMA_COMPANION_ARCHIVE_VFS_REGISTERED_ENTRY_NOT_VISIBLE,
    DLUMA_COMPANION_ARCHIVE_VFS_PATH_REVALIDATION_FAILED,
    DLUMA_COMPANION_ARCHIVE_VFS_SOURCE_READD_FAILED,
    DLUMA_COMPANION_ARCHIVE_VFS_READD_VERIFICATION_FAILED,
    DLUMA_COMPANION_ARCHIVE_VFS_REMOUNTED
};

enum DlumaNativeCompanionBankDiagnostic {
    DLUMA_COMPANION_ARCHIVE_BANK_NONE = 0,
    DLUMA_COMPANION_ARCHIVE_BANK_READY,
    DLUMA_COMPANION_ARCHIVE_BANK_PATH_INVALID,
    DLUMA_COMPANION_ARCHIVE_BANK_CACHE_INVALID,
    DLUMA_COMPANION_ARCHIVE_BANK_WRITE_FAILED,
    DLUMA_COMPANION_ARCHIVE_BANK_BYTES_INVALID,
    DLUMA_COMPANION_ARCHIVE_BANK_BINDING_INVALID,
    DLUMA_COMPANION_ARCHIVE_BANK_ALIAS_COLLISION,
    DLUMA_COMPANION_ARCHIVE_BANK_SOURCE_FAILED
};

static void dluma_native_companion_report_vfs(
    DlumaCompanionArchive* pack, unsigned int diagnostic)
{
    const char* level = "ERROR";
    const char* value = 0;
    if (!pack || pack->vfs_diagnostic == diagnostic) return;
    switch (diagnostic) {
    case DLUMA_COMPANION_ARCHIVE_VFS_VISIBLE:
        level = "INFO";
        value = "approved_pack_entry_visible";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_BINDING_INVALID:
        value = "filesystem_visibility_binding_invalid";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_UNAPPROVED_PROVIDER:
        value = "entry_visible_from_unapproved_source";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_REGISTERED_ENTRY_MISSING:
        value = "approved_source_registered_but_entry_missing";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_REGISTERED_ENTRY_NOT_VISIBLE:
        value = "approved_source_entry_not_globally_visible";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_PATH_REVALIDATION_FAILED:
        value = "locked_pack_path_revalidation_failed";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_SOURCE_READD_FAILED:
        value = "source_registration_lost_readd_failed";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_READD_VERIFICATION_FAILED:
        value = "source_readded_but_entry_not_visible";
        break;
    case DLUMA_COMPANION_ARCHIVE_VFS_REMOUNTED:
        level = "INFO";
        value = "source_registration_lost_remounted";
        break;
    default:
        return;
    }
    pack->vfs_diagnostic = (unsigned char)diagnostic;
    dluma_log_pair(level, "native_companion_vfs_ready", value);
}

static void dluma_native_companion_report_bank(
    DlumaCompanionArchive* pack, unsigned int diagnostic)
{
    const char* level = "ERROR";
    const char* value = 0;
    if (!pack || pack->bank_diagnostic == diagnostic) return;
    switch (diagnostic) {
    case DLUMA_COMPANION_ARCHIVE_BANK_READY:
        level = "INFO";
        value = "locked_physical_bank_source_ready";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_PATH_INVALID:
        value = "physical_bank_cache_path_invalid";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_CACHE_INVALID:
        value = "physical_bank_cache_file_invalid";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_WRITE_FAILED:
        value = "physical_bank_materialization_failed";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_BYTES_INVALID:
        value = "physical_bank_bytes_failed_validation";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_BINDING_INVALID:
        value = "physical_bank_source_binding_invalid";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_ALIAS_COLLISION:
        value = "physical_bank_alias_owned_by_another_source";
        break;
    case DLUMA_COMPANION_ARCHIVE_BANK_SOURCE_FAILED:
        value = "physical_bank_directory_source_failed";
        break;
    default:
        return;
    }
    pack->bank_diagnostic = (unsigned char)diagnostic;
    dluma_log_pair(level, "native_companion_physical_bank_ready", value);
}

static int dluma_native_companion_join_component(
    WCHAR* out, unsigned int capacity,
    const WCHAR* parent, const WCHAR* component)
{
    unsigned int parent_length = 0;
    unsigned int component_length = 0;
    unsigned int cursor = 0;
    int separator;
    if (!out || capacity < 4U || !parent || !parent[0] ||
        !component || !component[0])
        return 0;
    while (parent[parent_length]) {
        if (parent_length + 1U >= capacity) return 0;
        ++parent_length;
    }
    while (component[component_length]) {
        if (component[component_length] == L'\\' ||
            component[component_length] == L'/')
            return 0;
        ++component_length;
    }
    separator =
        parent[parent_length - 1U] != L'\\' &&
        parent[parent_length - 1U] != L'/';
    if (parent_length + (separator ? 1U : 0U) +
            component_length + 1U > capacity)
        return 0;
    while (cursor < parent_length) {
        out[cursor] = parent[cursor];
        ++cursor;
    }
    if (separator) out[cursor++] = L'\\';
    while (component_length) {
        out[cursor++] = *component++;
        --component_length;
    }
    out[cursor] = 0;
    return 1;
}

static const WCHAR* dluma_native_companion_skip_extended_prefix(
    const WCHAR* path)
{
    if (path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\' &&
        !((path[4] == L'U' || path[4] == L'u') &&
          (path[5] == L'N' || path[5] == L'n') &&
          (path[6] == L'C' || path[6] == L'c') &&
          path[7] == L'\\'))
        return path + 4;
    return path;
}

static int dluma_native_companion_final_path_matches(
    const WCHAR* requested_path, const WCHAR* final_path)
{
    WCHAR absolute[DLUMA_MAX_WPATH];
    DWORD length;
    if (!requested_path || !requested_path[0] ||
        !final_path || !final_path[0])
        return 0;
    length = GetFullPathNameW(
        requested_path, DLUMA_ARRAY_COUNT(absolute), absolute, 0);
    if (!length || length >= DLUMA_ARRAY_COUNT(absolute))
        return 0;
    return dluma_wstricmp(
        dluma_native_companion_skip_extended_prefix(absolute),
        dluma_native_companion_skip_extended_prefix(final_path)) == 0;
}

static int dluma_native_companion_open_plain_directory(
    const WCHAR* path, int create_if_missing, HANDLE* directory_out)
{
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    WCHAR final_path[DLUMA_MAX_WPATH];
    DWORD attributes;
    if (directory_out) *directory_out = INVALID_HANDLE_VALUE;
    if (!path || !path[0] || !directory_out) return 0;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES && create_if_missing) {
        if (!CreateDirectoryW(path, 0) &&
            GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return 0;
        attributes = GetFileAttributesW(path);
    }
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    directory = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (directory == INVALID_HANDLE_VALUE) return 0;
    memset(&info, 0, sizeof(info));
    if (!GetFileInformationByHandle(directory, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            directory, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_native_companion_final_path_matches(path, final_path)) {
        CloseHandle(directory);
        return 0;
    }
    *directory_out = directory;
    return 1;
}

static const DlumaCompanionArchiveEntry*
dluma_native_companion_bank_entry(
    const DlumaCompanionArchive* pack, const char* virtual_path)
{
    unsigned int i;
    if (!pack || !virtual_path) return 0;
    for (i = 0; i < pack->entry_count; ++i)
        if (pack->entries[i].is_csb &&
            dluma_native_companion_str_equal(
                pack->entries[i].virtual_path, virtual_path))
            return &pack->entries[i];
    return 0;
}

static int dluma_native_companion_open_cached_bank(
    const WCHAR* requested_path, DWORD expected_size,
    const char* expected_sha256, HANDLE* file_out,
    BY_HANDLE_FILE_INFORMATION* identity_out)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    if (file_out) *file_out = INVALID_HANDLE_VALUE;
    if (!requested_path || !expected_size || !expected_sha256 ||
        !file_out || !identity_out)
        return 0;
    memset(&info, 0, sizeof(info));
    if (!dluma_native_companion_open_regular(
            requested_path, &file, final_path,
            DLUMA_ARRAY_COUNT(final_path), &info) ||
        !dluma_native_companion_final_path_matches(
            requested_path, final_path) ||
        info.nFileSizeHigh || info.nFileSizeLow != expected_size ||
        !dluma_sha256_handle_hex(
            file, DLUMA_COMPANION_ARCHIVE_MAX_BYTES, sha256) ||
        dluma_stricmp(sha256, expected_sha256) != 0 ||
        !dluma_native_companion_identity_unchanged(file, &info)) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    memcpy(identity_out, &info, sizeof(info));
    *file_out = file;
    return 1;
}

static int dluma_native_companion_write_bank_temp(
    DlumaCompanionArchive* pack,
    const DlumaCompanionArchiveEntry* entry,
    const WCHAR* temporary_path)
{
    static const DWORD crc_table[16] = {
        0x00000000UL, 0x1db71064UL, 0x3b6e20c8UL, 0x26d930acUL,
        0x76dc4190UL, 0x6b6b51f4UL, 0x4db26158UL, 0x5005713cUL,
        0xedb88320UL, 0xf00f9344UL, 0xd6d6a3e8UL, 0xcb61b38cUL,
        0x9b64c2b0UL, 0x86d3d2d4UL, 0xa00ae278UL, 0xbdbdf21cUL
    };
    BYTE buffer[16384];
    BYTE digest[32];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaSha256Context hash;
    HANDLE output = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD offset;
    DWORD remaining;
    DWORD crc = 0xffffffffUL;
    int valid = 0;
    if (!pack || !entry || !temporary_path ||
        pack->file == INVALID_HANDLE_VALUE ||
        !dluma_native_companion_identity_unchanged(
            pack->file, &pack->identity))
        return 0;
    output = CreateFileW(
        temporary_path, GENERIC_WRITE, 0, 0, DLUMA_NATIVE_CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (output == INVALID_HANDLE_VALUE) return 0;
    offset = entry->data_offset;
    remaining = entry->size;
    dluma_sha256_init(&hash);
    while (remaining) {
        DWORD chunk = remaining > sizeof(buffer)
            ? (DWORD)sizeof(buffer) : remaining;
        DWORD written = 0;
        DWORD i;
        if (!dluma_native_companion_read_at(
                pack->file, offset, buffer, chunk) ||
            !WriteFile(output, buffer, chunk, &written, 0) ||
            written != chunk)
            goto done;
        for (i = 0; i < chunk; ++i) {
            crc ^= buffer[i];
            crc = (crc >> 4) ^ crc_table[crc & 0x0fU];
            crc = (crc >> 4) ^ crc_table[crc & 0x0fU];
        }
        dluma_sha256_update(&hash, buffer, chunk);
        if (!dluma_native_companion_add_u32(offset, chunk, &offset))
            goto done;
        remaining -= chunk;
    }
    dluma_sha256_final(&hash, digest);
    dluma_sha256_hex(digest, sha256);
    memset(&info, 0, sizeof(info));
    if ((crc ^ 0xffffffffUL) != entry->crc32 ||
        dluma_stricmp(
            sha256, pack->audio_manifest.bank_sha256) != 0 ||
        !FlushFileBuffers(output) ||
        !GetFileInformationByHandle(output, &info) ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1U ||
        info.nFileSizeHigh ||
        info.nFileSizeLow != entry->size ||
        !dluma_native_companion_identity_unchanged(
            pack->file, &pack->identity))
        goto done;
    valid = 1;
done:
    CloseHandle(output);
    if (!valid) DeleteFileW(temporary_path);
    return valid;
}

static int dluma_companion_archive_locked_path_revalidates(
    DlumaCompanionArchive* pack)
{
    HANDLE path_file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    int valid = 0;
    if (!pack || pack->file == INVALID_HANDLE_VALUE ||
        !dluma_native_companion_identity_unchanged(
            pack->file, &pack->identity))
        return 0;
    memset(&info, 0, sizeof(info));
    if (dluma_native_companion_open_regular(
            pack->final_path, &path_file, final_path,
            DLUMA_ARRAY_COUNT(final_path), &info) &&
        dluma_wstricmp(final_path, pack->final_path) == 0 &&
        info.dwVolumeSerialNumber == pack->identity.dwVolumeSerialNumber &&
        info.nFileIndexHigh == pack->identity.nFileIndexHigh &&
        info.nFileIndexLow == pack->identity.nFileIndexLow &&
        info.nFileSizeHigh == pack->identity.nFileSizeHigh &&
        info.nFileSizeLow == pack->identity.nFileSizeLow &&
        info.ftLastWriteTime.dwHighDateTime ==
            pack->identity.ftLastWriteTime.dwHighDateTime &&
        info.ftLastWriteTime.dwLowDateTime ==
            pack->identity.ftLastWriteTime.dwLowDateTime &&
        dluma_sha256_handle_hex(
            path_file, DLUMA_COMPANION_ARCHIVE_MAX_BYTES, sha256) &&
        dluma_stricmp(sha256, pack->sha256) == 0 &&
        dluma_native_companion_identity_unchanged(path_file, &info))
        valid = 1;
    if (path_file != INVALID_HANDLE_VALUE) CloseHandle(path_file);
    return valid;
}

static int dluma_native_companion_readonly_module_range(
    HMODULE module, DWORD image_size, const void* address, SIZE_T size)
{
    const BYTE* cursor = (const BYTE*)address;
    ULONG_PTR base = (ULONG_PTR)module;
    ULONG_PTR value = (ULONG_PTR)address;
    SIZE_T remaining = size;
    SIZE_T module_offset;
    if (!module || !image_size || !address || !size || value < base)
        return 0;
    module_offset = (SIZE_T)(value - base);
    if (module_offset > (SIZE_T)image_size ||
        size > (SIZE_T)image_size - module_offset ||
        !dluma_is_readable(address, size))
        return 0;
    while (remaining) {
        MEMORY_BASIC_INFORMATION info;
        SIZE_T region_offset;
        SIZE_T available;
        DWORD protection;
        if (VirtualQuery(cursor, &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return 0;
        protection = info.Protect & 0xffU;
        if (protection != PAGE_READONLY &&
            protection != PAGE_EXECUTE_READ)
            return 0;
        region_offset =
            (SIZE_T)(cursor - (const BYTE*)info.BaseAddress);
        if (region_offset >= info.RegionSize) return 0;
        available = info.RegionSize - region_offset;
        if (available >= remaining) return 1;
        cursor += available;
        remaining -= available;
    }
    return 1;
}

static int dluma_native_companion_source_has_entry(
    HMODULE filesystem,
    DWORD image_size,
    void* source,
    const char* virtual_path,
    DlumaNativeFsPathCtorFn path_ctor,
    DlumaNativeFsPathDtorFn path_dtor,
    const void* expected_vtable,
    DlumaNativeFsSourceEntryExistsFn entry_exists)
{
    DlumaNativeFsPathStorage path;
    void* vtable = 0;
    unsigned char result = 0;
    int constructed = 0;
    int delivered = 1;
    if (!filesystem || !source || !virtual_path ||
        !path_ctor || !path_dtor || !expected_vtable || !entry_exists ||
        !dluma_is_readable(source, sizeof(vtable)))
        return 0;
    memcpy(&vtable, source, sizeof(vtable));
    if (vtable != expected_vtable ||
        !dluma_native_companion_readonly_module_range(
            filesystem, image_size, vtable, 11U * sizeof(void*)) ||
        ((void**)vtable)[9] != (void*)entry_exists)
        return 0;
    memset(&path, 0, sizeof(path));
    __try {
        (void)path_ctor(&path, virtual_path, 1U);
        constructed = 1;
        result = entry_exists(source, &path);
    }
    __except(1) {
        delivered = 0;
    }
    if (constructed) {
        __try {
            path_dtor(&path);
        }
        __except(1) {
            delivered = 0;
        }
    }
    return delivered && result;
}

int dluma_companion_archive_ensure_vfs_entry(
    DlumaCompanionArchive* pack, const char* virtual_path)
{
    static const char add_source_export[] =
        "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z";
    static const char exists_export[] =
        "?exists@fs@@YA_NPEBD@Z";
    static const char find_source_export[] =
        "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z";
    static const char path_ctor_export[] =
        "??0path@fs@@QEAA@PEBD_N@Z";
    static const char path_dtor_export[] =
        "??1path@fs@@QEAA@XZ";
    static const BYTE add_source_prefix[32] = {
        0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54,
        0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d,
        0xac, 0x24, 0xf0, 0xfd, 0xff, 0xff, 0x48, 0x81,
        0xec, 0x10, 0x03, 0x00, 0x00, 0x4c, 0x8b, 0xe9
    };
    static const BYTE exists_prefix[24] = {
        0x48, 0x83, 0xec, 0x78, 0x48, 0x8b, 0xd1, 0x48,
        0x8d, 0x4c, 0x24, 0x20, 0x41, 0xb0, 0x01, 0xe8,
        0x4c, 0xf3, 0x00, 0x00, 0x44, 0x8b, 0x5c, 0x24
    };
    static const BYTE find_source_prefix[24] = {
        0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0xac, 0x24,
        0x20, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xe0,
        0x02, 0x00, 0x00, 0x48, 0x8b, 0xd1, 0x48, 0x83
    };
    static const BYTE path_ctor_prefix[24] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81,
        0xec, 0x40, 0x02, 0x00, 0x00, 0x33, 0xff, 0x48,
        0x8b, 0xd9, 0xc7, 0x41, 0x40, 0x08, 0x00, 0x00
    };
    static const BYTE path_dtor_prefix[24] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xec, 0x20, 0x33, 0xff, 0xf6, 0x41, 0x40, 0x01,
        0x48, 0x8b, 0xd9, 0x74, 0x0c, 0x48, 0x8b, 0x09
    };
    static const BYTE source_entry_exists_prefix[20] = {
        0x48, 0x83, 0xec, 0x28, 0xe8, 0xd7, 0xfd, 0xff,
        0xff, 0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48,
        0x83, 0xc4, 0x28, 0xc3
    };
    HMODULE filesystem;
    DlumaNativeFsAddSourceFn add_source;
    DlumaNativeFsExistsFn exists;
    DlumaNativeFsFindSourceFn find_source;
    DlumaNativeFsPathCtorFn path_ctor;
    DlumaNativeFsPathDtorFn path_dtor;
    DlumaNativeFsSourceEntryExistsFn source_entry_exists;
    const BYTE* expected_add_source;
    const BYTE* expected_exists;
    const BYTE* expected_find_source;
    const BYTE* expected_path_ctor;
    const BYTE* expected_path_dtor;
    const BYTE* expected_source_entry_exists;
    void* source;
    WCHAR module_path[DLUMA_MAX_WPATH];
    char module_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD timestamp = 0;
    DWORD image_size = 0;
    if (!pack || !virtual_path || !pack->validated || !pack->mounted ||
        pack->file == INVALID_HANDLE_VALUE ||
        !pack->audio_manifest.valid ||
        !dluma_native_companion_str_equal(
            virtual_path, pack->audio_manifest.bank_path) ||
        !dluma_companion_archive_has_entry(pack, virtual_path))
        return 0;
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!filesystem ||
        !dluma_native_companion_module_identity(
            filesystem, &timestamp, &image_size) ||
        timestamp != DLUMA_NATIVE_FILESYSTEM_TIMESTAMP ||
        image_size != DLUMA_NATIVE_FILESYSTEM_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            filesystem, module_path, DLUMA_ARRAY_COUNT(module_path)) ||
        !dluma_sha256_file_hex(
            module_path, 16ULL * 1024ULL * 1024ULL, module_sha256) ||
        dluma_stricmp(
            module_sha256, g_dluma_native_filesystem_sha256) != 0) {
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_BINDING_INVALID);
        return 0;
    }
    expected_add_source = (const BYTE*)filesystem + 0x00012dd0UL;
    expected_exists = (const BYTE*)filesystem + 0x000148e0UL;
    expected_find_source = (const BYTE*)filesystem + 0x00014430UL;
    expected_path_ctor = (const BYTE*)filesystem + 0x00023c40UL;
    expected_path_dtor = (const BYTE*)filesystem + 0x00023e20UL;
    expected_source_entry_exists =
        (const BYTE*)filesystem + 0x00025680UL;
    add_source = (DlumaNativeFsAddSourceFn)GetProcAddress(
        filesystem, add_source_export);
    exists = (DlumaNativeFsExistsFn)GetProcAddress(
        filesystem, exists_export);
    find_source = (DlumaNativeFsFindSourceFn)GetProcAddress(
        filesystem, find_source_export);
    path_ctor = (DlumaNativeFsPathCtorFn)GetProcAddress(
        filesystem, path_ctor_export);
    path_dtor = (DlumaNativeFsPathDtorFn)GetProcAddress(
        filesystem, path_dtor_export);
    source_entry_exists =
        (DlumaNativeFsSourceEntryExistsFn)expected_source_entry_exists;
    if (!add_source || !exists || !find_source ||
        !path_ctor || !path_dtor ||
        (ULONG_PTR)add_source != (ULONG_PTR)expected_add_source ||
        (ULONG_PTR)exists != (ULONG_PTR)expected_exists ||
        (ULONG_PTR)find_source != (ULONG_PTR)expected_find_source ||
        (ULONG_PTR)path_ctor != (ULONG_PTR)expected_path_ctor ||
        (ULONG_PTR)path_dtor != (ULONG_PTR)expected_path_dtor ||
        !dluma_is_readable(
            expected_add_source, sizeof(add_source_prefix)) ||
        !dluma_is_readable(expected_exists, sizeof(exists_prefix)) ||
        !dluma_is_readable(
            expected_find_source, sizeof(find_source_prefix)) ||
        !dluma_is_readable(
            expected_path_ctor, sizeof(path_ctor_prefix)) ||
        !dluma_is_readable(
            expected_path_dtor, sizeof(path_dtor_prefix)) ||
        !dluma_is_readable(
            expected_source_entry_exists,
            sizeof(source_entry_exists_prefix)) ||
        memcmp(
            expected_add_source, add_source_prefix,
            sizeof(add_source_prefix)) != 0 ||
        memcmp(
            expected_exists, exists_prefix,
            sizeof(exists_prefix)) != 0 ||
        memcmp(
            expected_find_source, find_source_prefix,
            sizeof(find_source_prefix)) != 0 ||
        memcmp(
            expected_path_ctor, path_ctor_prefix,
            sizeof(path_ctor_prefix)) != 0 ||
        memcmp(
            expected_path_dtor, path_dtor_prefix,
            sizeof(path_dtor_prefix)) != 0 ||
        memcmp(
            expected_source_entry_exists,
            source_entry_exists_prefix,
            sizeof(source_entry_exists_prefix)) != 0) {
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_BINDING_INVALID);
        return 0;
    }
    source = find_source(pack->final_path_utf8);
    if (source) {
        if (!dluma_native_companion_source_has_entry(
                filesystem, image_size, source, virtual_path,
                path_ctor, path_dtor,
                (BYTE*)filesystem + 0x00044a48UL,
                source_entry_exists)) {
            dluma_native_companion_report_vfs(
                pack,
                DLUMA_COMPANION_ARCHIVE_VFS_REGISTERED_ENTRY_MISSING);
            return 0;
        }
        if (!exists(virtual_path)) {
            dluma_native_companion_report_vfs(
                pack,
                DLUMA_COMPANION_ARCHIVE_VFS_REGISTERED_ENTRY_NOT_VISIBLE);
            return 0;
        }
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_VISIBLE);
        return 1;
    }
    if (exists(virtual_path)) {
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_UNAPPROVED_PROVIDER);
        return 0;
    }
    if (!dluma_companion_archive_locked_path_revalidates(pack)) {
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_PATH_REVALIDATION_FAILED);
        return 0;
    }
    if (!add_source(pack->final_path_utf8, 9U)) {
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_SOURCE_READD_FAILED);
        return 0;
    }
    source = find_source(pack->final_path_utf8);
    if (!source ||
        !dluma_native_companion_source_has_entry(
            filesystem, image_size, source, virtual_path,
            path_ctor, path_dtor,
            (BYTE*)filesystem + 0x00044a48UL,
            source_entry_exists) ||
        !exists(virtual_path)) {
        dluma_native_companion_report_vfs(
            pack, DLUMA_COMPANION_ARCHIVE_VFS_READD_VERIFICATION_FAILED);
        return 0;
    }
    dluma_native_companion_report_vfs(
        pack, DLUMA_COMPANION_ARCHIVE_VFS_REMOUNTED);
    return 1;
}

static int dluma_native_companion_materialize_bank(
    DlumaCompanionArchive* pack, const char* virtual_path)
{
    static const char bank_prefix[] = "DLUMAAudio_";
    const DlumaCompanionArchiveEntry* entry;
    const DlumaCompanionArchiveAudioManifest* selected_bank = 0;
    DlumaCompanionArchiveMaterializedBank* materialized = 0;
    HANDLE runtime_handle = INVALID_HANDLE_VALUE;
    HANDLE cache_handle = INVALID_HANDLE_VALUE;
    HANDLE bank_dir_handle = INVALID_HANDLE_VALUE;
    HANDLE bank_file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION bank_identity;
    BY_HANDLE_FILE_INFORMATION directory_identity;
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    WCHAR cache_dir[DLUMA_MAX_WPATH];
    WCHAR bank_dir[DLUMA_MAX_WPATH];
    WCHAR bank_path[DLUMA_MAX_WPATH];
    WCHAR component_wide[DLUMA_MAX_WPATH];
    WCHAR temporary_path[DLUMA_MAX_WPATH];
    char engine_name[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME];
    char bank_filename[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME + 8U];
    char temporary_component[
        DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME + 64U];
    char number[32];
    char bank_dir_utf8[DLUMA_MAX_WPATH * 2];
    DWORD attributes;
    unsigned int prefix_length;
    unsigned int bank_index = 0;
    unsigned int i;
    int created = 0;
    int valid = 0;
    if (!pack || !virtual_path || !pack->validated ||
        pack->file == INVALID_HANDLE_VALUE || !pack->audio_catalog.valid)
        return 0;
    for (i = 0; i < pack->audio_catalog.bank_count; ++i) {
        if (pack->audio_catalog.banks[i].valid &&
            dluma_native_companion_str_equal(
                pack->audio_catalog.banks[i].bank_path,
                virtual_path)) {
            selected_bank = &pack->audio_catalog.banks[i];
            break;
        }
    }
    if (!selected_bank || strlen(selected_bank->bank_sha256) != 64U)
        return 0;
    bank_index = i;
    materialized = &pack->materialized_banks[bank_index];
    pack->audio_manifest = *selected_bank;
    pack->selected_materialized_bank_index = bank_index;
    entry = dluma_native_companion_bank_entry(pack, virtual_path);
    if (!entry || entry->size != pack->audio_manifest.bank_size)
        return 0;
    if (materialized->active) {
        if (!materialized->bank_name[0] ||
            !materialized->bank_dir_utf8[0] ||
            materialized->catalog_index != bank_index ||
            materialized->bank_file == INVALID_HANDLE_VALUE ||
            materialized->bank_file == 0 ||
            !dluma_native_companion_identity_unchanged(
                materialized->bank_file,
                &materialized->bank_identity)) {
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_CACHE_INVALID);
            return 0;
        }
        return 1;
    }
    memset(&bank_identity, 0, sizeof(bank_identity));
    memset(&directory_identity, 0, sizeof(directory_identity));
    memset(engine_name, 0, sizeof(engine_name));
    dluma_str_copy(engine_name, DLUMA_ARRAY_COUNT(engine_name), bank_prefix);
    prefix_length = (unsigned int)strlen(engine_name);
    if (prefix_length + 16U + 1U > DLUMA_ARRAY_COUNT(engine_name))
        return 0;
    for (i = 0; i < 16U; ++i) {
        char value = pack->audio_manifest.bank_sha256[i];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'A' && value <= 'F') ||
              (value >= 'a' && value <= 'f')))
            return 0;
        engine_name[prefix_length + i] = value;
    }
    engine_name[prefix_length + 16U] = 0;
    dluma_str_copy(
        bank_filename, DLUMA_ARRAY_COUNT(bank_filename), engine_name);
    if (!dluma_native_companion_str_append_checked(
            bank_filename, DLUMA_ARRAY_COUNT(bank_filename), ".csb") ||
        !dluma_utf8_to_wide(
            pack->audio_manifest.bank_sha256,
            component_wide, DLUMA_ARRAY_COUNT(component_wide)) ||
        !dluma_native_companion_join_component(
            runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir),
            g_dluma.module_dir, L"dluma_runtime") ||
        !dluma_native_companion_join_component(
            cache_dir, DLUMA_ARRAY_COUNT(cache_dir),
            runtime_dir, L"companion_banks") ||
        !dluma_native_companion_join_component(
            bank_dir, DLUMA_ARRAY_COUNT(bank_dir),
            cache_dir, component_wide) ||
        !dluma_utf8_to_wide(
            bank_filename, component_wide,
            DLUMA_ARRAY_COUNT(component_wide)) ||
        !dluma_native_companion_join_component(
            bank_path, DLUMA_ARRAY_COUNT(bank_path),
            bank_dir, component_wide) ||
        !dluma_native_companion_open_plain_directory(
            runtime_dir, 1, &runtime_handle) ||
        !dluma_native_companion_open_plain_directory(
            cache_dir, 1, &cache_handle) ||
        !dluma_native_companion_open_plain_directory(
            bank_dir, 1, &bank_dir_handle) ||
        !dluma_wide_to_utf8(
            bank_dir, bank_dir_utf8,
            DLUMA_ARRAY_COUNT(bank_dir_utf8))) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_PATH_INVALID);
        goto done;
    }
    attributes = GetFileAttributesW(bank_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        memset(temporary_component, 0, sizeof(temporary_component));
        dluma_str_copy(
            temporary_component,
            DLUMA_ARRAY_COUNT(temporary_component), engine_name);
        if (!dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), ".tmp-") ||
            !dluma_u64_to_text(
                GetTickCount64(), number, DLUMA_ARRAY_COUNT(number))) {
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_WRITE_FAILED);
            goto done;
        }
        if (!dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), number) ||
            !dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), "-") ||
            !dluma_u64_to_text(
                (ULONGLONG)GetCurrentThreadId(),
                number, DLUMA_ARRAY_COUNT(number))) {
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_WRITE_FAILED);
            goto done;
        }
        if (!dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), number) ||
            !dluma_utf8_to_wide(
                temporary_component, component_wide,
                DLUMA_ARRAY_COUNT(component_wide)) ||
            !dluma_native_companion_join_component(
                temporary_path, DLUMA_ARRAY_COUNT(temporary_path),
                bank_dir, component_wide) ||
            !dluma_native_companion_write_bank_temp(
                pack, entry, temporary_path)) {
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_WRITE_FAILED);
            goto done;
        }
        if (!MoveFileExW(
                temporary_path, bank_path, MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary_path);
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_WRITE_FAILED);
            goto done;
        }
        created = 1;
    } else if (attributes &
               (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_CACHE_INVALID);
        goto done;
    }
    if (!dluma_native_companion_open_cached_bank(
            bank_path, entry->size,
            pack->audio_manifest.bank_sha256,
            &bank_file, &bank_identity) ||
        !GetFileInformationByHandle(
            bank_dir_handle, &directory_identity)) {
        dluma_native_companion_report_bank(
            pack,
            created
                ? DLUMA_COMPANION_ARCHIVE_BANK_BYTES_INVALID
                : DLUMA_COMPANION_ARCHIVE_BANK_CACHE_INVALID);
        goto done;
    }
    materialized->runtime_dir = runtime_handle;
    materialized->cache_dir = cache_handle;
    materialized->bank_dir = bank_dir_handle;
    materialized->bank_file = bank_file;
    runtime_handle = INVALID_HANDLE_VALUE;
    cache_handle = INVALID_HANDLE_VALUE;
    bank_dir_handle = INVALID_HANDLE_VALUE;
    bank_file = INVALID_HANDLE_VALUE;
    memcpy(
        &materialized->bank_identity,
        &bank_identity, sizeof(bank_identity));
    memcpy(
        &materialized->directory_identity,
        &directory_identity, sizeof(directory_identity));
    dluma_wstr_copy(
        materialized->bank_dir_path,
        DLUMA_ARRAY_COUNT(materialized->bank_dir_path), bank_dir);
    dluma_wstr_copy(
        materialized->bank_path,
        DLUMA_ARRAY_COUNT(materialized->bank_path), bank_path);
    dluma_str_copy(
        materialized->bank_dir_utf8,
        DLUMA_ARRAY_COUNT(materialized->bank_dir_utf8),
        bank_dir_utf8);
    dluma_str_copy(
        materialized->bank_name,
        DLUMA_ARRAY_COUNT(materialized->bank_name),
        engine_name);
    materialized->catalog_index = bank_index;
    materialized->active = 1U;
    valid = 1;
done:
    if (bank_file != INVALID_HANDLE_VALUE) CloseHandle(bank_file);
    if (bank_dir_handle != INVALID_HANDLE_VALUE)
        CloseHandle(bank_dir_handle);
    if (cache_handle != INVALID_HANDLE_VALUE) CloseHandle(cache_handle);
    if (runtime_handle != INVALID_HANDLE_VALUE) CloseHandle(runtime_handle);
    return valid;
}

static int dluma_companion_archive_register_bank_source(
    DlumaCompanionArchive* pack,
    DlumaCompanionArchiveMaterializedBank* materialized,
    void* source)
{
    static const char owner_prefix[] = "dluma.audio.bank:";
    DlumaProcessLifetimeSourceRecord record;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    const DlumaCompanionArchiveAudioManifest* manifest;
    unsigned int index;
    if (!pack || !materialized || !source ||
        materialized->catalog_index >= pack->audio_catalog.bank_count)
        return 0;
    manifest = &pack->audio_catalog.banks[
        materialized->catalog_index];
    if (!manifest->valid || strlen(manifest->bank_sha256) != 64U)
        return 0;
    memset(&record, 0, sizeof(record));
    record.owner_kind = DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT;
    dluma_str_copy(
        record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id), owner_prefix);
    if (!dluma_native_companion_str_append_checked(
            record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id),
            manifest->bank_sha256))
        return 0;
    record.source_object = source;
    dluma_str_copy(
        record.requested_mount_path,
        DLUMA_ARRAY_COUNT(record.requested_mount_path),
        materialized->bank_dir_utf8);
    record.runtime_source_type = DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record.source_flags = 9U;
    record.volume_serial =
        materialized->directory_identity.dwVolumeSerialNumber;
    record.file_index_high =
        materialized->directory_identity.nFileIndexHigh;
    record.file_index_low =
        materialized->directory_identity.nFileIndexLow;
    dluma_str_copy(
        record.content_sha256, DLUMA_ARRAY_COUNT(record.content_sha256),
        manifest->bank_sha256);
    dluma_str_copy(
        record.approval_fingerprint,
        DLUMA_ARRAY_COUNT(record.approval_fingerprint),
        manifest->bank_sha256);
    dluma_str_copy(
        record.manifest_fingerprint,
        DLUMA_ARRAY_COUNT(record.manifest_fingerprint),
        manifest->bank_sha256);
    record.metadata_complete = 1U;
    if (!dluma_process_lifetime_source_register(&record, 0) ||
        !dluma_process_lifetime_sources_snapshot(&snapshot))
        return 0;
    for (index = 0; index < snapshot.count; ++index) {
        const DlumaProcessLifetimeSourceRecord* mounted =
            &snapshot.records[index];
        if (mounted->owner_kind ==
                DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT &&
            mounted->source_object == source &&
            dluma_stricmp(mounted->owner_id, record.owner_id) == 0) {
            materialized->source_object = source;
            materialized->source_mount_generation =
                mounted->mount_generation;
            materialized->source_registered = 1U;
            return materialized->source_mount_generation != 0U;
        }
    }
    return 0;
}

int dluma_companion_archive_prepare_bank(
    DlumaCompanionArchive* pack, const char* virtual_path,
    char* engine_bank_name_out, unsigned int engine_bank_name_capacity)
{
    static const char add_source_export[] =
        "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z";
    static const char exists_export[] =
        "?exists@fs@@YA_NPEBD@Z";
    static const char find_source_export[] =
        "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z";
    static const char path_ctor_export[] =
        "??0path@fs@@QEAA@PEBD_N@Z";
    static const char path_dtor_export[] =
        "??1path@fs@@QEAA@XZ";
    static const BYTE add_source_prefix[32] = {
        0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54,
        0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d,
        0xac, 0x24, 0xf0, 0xfd, 0xff, 0xff, 0x48, 0x81,
        0xec, 0x10, 0x03, 0x00, 0x00, 0x4c, 0x8b, 0xe9
    };
    static const BYTE exists_prefix[24] = {
        0x48, 0x83, 0xec, 0x78, 0x48, 0x8b, 0xd1, 0x48,
        0x8d, 0x4c, 0x24, 0x20, 0x41, 0xb0, 0x01, 0xe8,
        0x4c, 0xf3, 0x00, 0x00, 0x44, 0x8b, 0x5c, 0x24
    };
    static const BYTE find_source_prefix[24] = {
        0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0xac, 0x24,
        0x20, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xe0,
        0x02, 0x00, 0x00, 0x48, 0x8b, 0xd1, 0x48, 0x83
    };
    static const BYTE path_ctor_prefix[24] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81,
        0xec, 0x40, 0x02, 0x00, 0x00, 0x33, 0xff, 0x48,
        0x8b, 0xd9, 0xc7, 0x41, 0x40, 0x08, 0x00, 0x00
    };
    static const BYTE path_dtor_prefix[24] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xec, 0x20, 0x33, 0xff, 0xf6, 0x41, 0x40, 0x01,
        0x48, 0x8b, 0xd9, 0x74, 0x0c, 0x48, 0x8b, 0x09
    };
    static const BYTE directory_entry_exists_prefix[20] = {
        0x48, 0x83, 0xec, 0x28, 0xe8, 0xe7, 0xdf, 0xff,
        0xff, 0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48,
        0x83, 0xc4, 0x28, 0xc3
    };
    static const BYTE directory_full_path_prefix[32] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81,
        0xec, 0x30, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xda,
        0x49, 0x8b, 0xd0, 0x48, 0x8b, 0xf9, 0xe8, 0xb5,
        0xdf, 0xff, 0xff, 0x48, 0x85, 0xc0, 0x74, 0x22
    };
    HMODULE filesystem;
    DlumaNativeFsAddSourceFn add_source;
    DlumaNativeFsExistsFn exists;
    DlumaNativeFsFindSourceFn find_source;
    DlumaNativeFsPathCtorFn path_ctor;
    DlumaNativeFsPathDtorFn path_dtor;
    DlumaNativeFsSourceEntryExistsFn directory_entry_exists;
    const BYTE* expected_add_source;
    const BYTE* expected_exists;
    const BYTE* expected_find_source;
    const BYTE* expected_path_ctor;
    const BYTE* expected_path_dtor;
    const BYTE* expected_directory_entry_exists;
    const BYTE* expected_directory_full_path;
    void* source;
    WCHAR module_path[DLUMA_MAX_WPATH];
    char module_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char engine_file[
        DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME + 8U];
    DWORD timestamp = 0;
    DWORD image_size = 0;
    SIZE_T engine_name_length;
    DlumaCompanionArchiveMaterializedBank* materialized;
    if (engine_bank_name_out && engine_bank_name_capacity)
        engine_bank_name_out[0] = 0;
    if (!pack || !virtual_path || !engine_bank_name_out ||
        !engine_bank_name_capacity)
        return 0;
    if (!dluma_native_companion_materialize_bank(pack, virtual_path)) {
        if (pack && pack->bank_diagnostic ==
                DLUMA_COMPANION_ARCHIVE_BANK_NONE)
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_BYTES_INVALID);
        return 0;
    }
    if (pack->selected_materialized_bank_index >=
            DLUMA_COMPANION_ARCHIVE_MAX_BANKS)
        return 0;
    materialized = &pack->materialized_banks[
        pack->selected_materialized_bank_index];
    if (!materialized->active)
        return 0;
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!filesystem ||
        !dluma_native_companion_module_identity(
            filesystem, &timestamp, &image_size) ||
        timestamp != DLUMA_NATIVE_FILESYSTEM_TIMESTAMP ||
        image_size != DLUMA_NATIVE_FILESYSTEM_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            filesystem, module_path, DLUMA_ARRAY_COUNT(module_path)) ||
        !dluma_sha256_file_hex(
            module_path, 16ULL * 1024ULL * 1024ULL, module_sha256) ||
        dluma_stricmp(
            module_sha256, g_dluma_native_filesystem_sha256) != 0) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_BINDING_INVALID);
        return 0;
    }
    expected_add_source = (BYTE*)filesystem + 0x00012dd0UL;
    expected_exists = (BYTE*)filesystem + 0x000148e0UL;
    expected_find_source = (BYTE*)filesystem + 0x00014430UL;
    expected_path_ctor = (BYTE*)filesystem + 0x00023c40UL;
    expected_path_dtor = (BYTE*)filesystem + 0x00023e20UL;
    expected_directory_entry_exists =
        (BYTE*)filesystem + 0x00006460UL;
    expected_directory_full_path =
        (BYTE*)filesystem + 0x00006480UL;
    add_source = (DlumaNativeFsAddSourceFn)GetProcAddress(
        filesystem, add_source_export);
    exists = (DlumaNativeFsExistsFn)GetProcAddress(
        filesystem, exists_export);
    find_source = (DlumaNativeFsFindSourceFn)GetProcAddress(
        filesystem, find_source_export);
    path_ctor = (DlumaNativeFsPathCtorFn)GetProcAddress(
        filesystem, path_ctor_export);
    path_dtor = (DlumaNativeFsPathDtorFn)GetProcAddress(
        filesystem, path_dtor_export);
    directory_entry_exists =
        (DlumaNativeFsSourceEntryExistsFn)(
            expected_directory_entry_exists);
    if (!add_source || !exists || !find_source ||
        !path_ctor || !path_dtor ||
        (ULONG_PTR)add_source !=
            (ULONG_PTR)expected_add_source ||
        (ULONG_PTR)exists !=
            (ULONG_PTR)expected_exists ||
        (ULONG_PTR)find_source !=
            (ULONG_PTR)expected_find_source ||
        (ULONG_PTR)path_ctor !=
            (ULONG_PTR)expected_path_ctor ||
        (ULONG_PTR)path_dtor !=
            (ULONG_PTR)expected_path_dtor ||
        !dluma_is_readable(
            expected_add_source, sizeof(add_source_prefix)) ||
        !dluma_is_readable(expected_exists, sizeof(exists_prefix)) ||
        !dluma_is_readable(
            expected_find_source, sizeof(find_source_prefix)) ||
        !dluma_is_readable(
            expected_path_ctor, sizeof(path_ctor_prefix)) ||
        !dluma_is_readable(
            expected_path_dtor, sizeof(path_dtor_prefix)) ||
        !dluma_is_readable(
            expected_directory_entry_exists,
            sizeof(directory_entry_exists_prefix)) ||
        !dluma_is_readable(
            expected_directory_full_path,
            sizeof(directory_full_path_prefix)) ||
        memcmp(
            expected_add_source, add_source_prefix,
            sizeof(add_source_prefix)) != 0 ||
        memcmp(
            expected_exists, exists_prefix,
            sizeof(exists_prefix)) != 0 ||
        memcmp(
            expected_find_source, find_source_prefix,
            sizeof(find_source_prefix)) != 0 ||
        memcmp(
            expected_path_ctor, path_ctor_prefix,
            sizeof(path_ctor_prefix)) != 0 ||
        memcmp(
            expected_path_dtor, path_dtor_prefix,
            sizeof(path_dtor_prefix)) != 0 ||
        memcmp(
            expected_directory_entry_exists,
            directory_entry_exists_prefix,
            sizeof(directory_entry_exists_prefix)) != 0 ||
        memcmp(
            expected_directory_full_path,
            directory_full_path_prefix,
            sizeof(directory_full_path_prefix)) != 0 ||
        !dluma_native_companion_readonly_module_range(
            filesystem, image_size,
            (BYTE*)filesystem + 0x0003f4a8UL,
            11U * sizeof(void*)) ||
        ((void**)((BYTE*)filesystem + 0x0003f4a8UL))[10] !=
            (void*)expected_directory_full_path ||
        !dluma_native_companion_identity_unchanged(
            materialized->bank_file,
            &materialized->bank_identity)) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_BINDING_INVALID);
        return 0;
    }
    dluma_str_copy(
        engine_file, DLUMA_ARRAY_COUNT(engine_file),
        materialized->bank_name);
    if (!dluma_native_companion_str_append_checked(
            engine_file, DLUMA_ARRAY_COUNT(engine_file), ".csb")) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_PATH_INVALID);
        return 0;
    }
    source = find_source(materialized->bank_dir_utf8);
    if (!source) {
        if (exists(engine_file)) {
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_ALIAS_COLLISION);
            return 0;
        }
        if (!add_source(materialized->bank_dir_utf8, 9U)) {
            dluma_native_companion_report_bank(
                pack, DLUMA_COMPANION_ARCHIVE_BANK_SOURCE_FAILED);
            return 0;
        }
        source = find_source(materialized->bank_dir_utf8);
    }
    if (!source ||
        !dluma_native_companion_source_has_entry(
            filesystem, image_size, source, engine_file,
            path_ctor, path_dtor,
            (BYTE*)filesystem + 0x0003f4a8UL,
            directory_entry_exists) ||
        !exists(engine_file) ||
        !dluma_native_companion_identity_unchanged(
            materialized->bank_file,
            &materialized->bank_identity)) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_SOURCE_FAILED);
        return 0;
    }
    if ((materialized->source_registered &&
         materialized->source_object != source) ||
        (!materialized->source_registered &&
         !dluma_companion_archive_register_bank_source(
             pack, materialized, source))) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_SOURCE_FAILED);
        return 0;
    }
    engine_name_length = strlen(materialized->bank_name);
    if (!engine_name_length ||
        engine_name_length + 1U > engine_bank_name_capacity) {
        dluma_native_companion_report_bank(
            pack, DLUMA_COMPANION_ARCHIVE_BANK_PATH_INVALID);
        return 0;
    }
    dluma_str_copy(
        engine_bank_name_out, engine_bank_name_capacity,
        materialized->bank_name);
    dluma_log_pair(
        "INFO", "native_companion_physical_bank_alias",
        materialized->bank_name);
    dluma_log_pair(
        "INFO", "native_companion_physical_bank_path",
        materialized->bank_dir_utf8);
    dluma_native_companion_report_bank(
        pack, DLUMA_COMPANION_ARCHIVE_BANK_READY);
    return 1;
}

static int dluma_native_companion_write_locked_bank_temp(
    HANDLE source_file,
    const BY_HANDLE_FILE_INFORMATION* source_identity,
    DWORD source_size,
    const char* source_sha256,
    const WCHAR* temporary_path)
{
    BYTE buffer[16384];
    BYTE digest[32];
    char actual_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaSha256Context hash;
    HANDLE output = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION output_identity;
    LARGE_INTEGER zero;
    DWORD remaining;
    int valid = 0;
    if (source_file == INVALID_HANDLE_VALUE || !source_identity ||
        !source_size || !source_sha256 || !temporary_path ||
        !dluma_native_companion_identity_unchanged(
            source_file, source_identity))
        return 0;
    output = CreateFileW(
        temporary_path, GENERIC_WRITE, 0, 0, DLUMA_NATIVE_CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (output == INVALID_HANDLE_VALUE)
        return 0;
    zero.QuadPart = 0;
    remaining = source_size;
    dluma_sha256_init(&hash);
    if (!SetFilePointerEx(source_file, zero, 0, FILE_BEGIN))
        goto done;
    while (remaining)
    {
        DWORD chunk = remaining > sizeof(buffer)
            ? (DWORD)sizeof(buffer) : remaining;
        DWORD read = 0;
        DWORD written = 0;
        if (!ReadFile(source_file, buffer, chunk, &read, 0) ||
            read != chunk ||
            !WriteFile(output, buffer, chunk, &written, 0) ||
            written != chunk)
            goto done;
        dluma_sha256_update(&hash, buffer, chunk);
        remaining -= chunk;
    }
    dluma_sha256_final(&hash, digest);
    dluma_sha256_hex(digest, actual_sha256);
    memset(&output_identity, 0, sizeof(output_identity));
    if (dluma_stricmp(actual_sha256, source_sha256) != 0 ||
        !FlushFileBuffers(output) ||
        !GetFileInformationByHandle(output, &output_identity) ||
        (output_identity.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        output_identity.nNumberOfLinks != 1U ||
        output_identity.nFileSizeHigh ||
        output_identity.nFileSizeLow != source_size ||
        !dluma_native_companion_identity_unchanged(
            source_file, source_identity))
        goto done;
    valid = 1;
done:
    CloseHandle(output);
    if (!valid)
        DeleteFileW(temporary_path);
    return valid;
}

int dluma_companion_archive_materialize_locked_bank(
    DlumaCompanionArchiveMaterializedBank* materialized,
    HANDLE source_file,
    const BY_HANDLE_FILE_INFORMATION* source_identity,
    DWORD source_size,
    const char* source_sha256)
{
    static const char bank_prefix[] = "DLUMAPrepared_";
    HANDLE runtime_handle = INVALID_HANDLE_VALUE;
    HANDLE cache_handle = INVALID_HANDLE_VALUE;
    HANDLE bank_dir_handle = INVALID_HANDLE_VALUE;
    HANDLE bank_file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION bank_identity;
    BY_HANDLE_FILE_INFORMATION directory_identity;
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    WCHAR cache_dir[DLUMA_MAX_WPATH];
    WCHAR bank_dir[DLUMA_MAX_WPATH];
    WCHAR bank_path[DLUMA_MAX_WPATH];
    WCHAR component_wide[DLUMA_MAX_WPATH];
    WCHAR temporary_path[DLUMA_MAX_WPATH];
    char engine_name[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME];
    char bank_filename[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME + 8U];
    char temporary_component[
        DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME + 64U];
    char bank_dir_utf8[DLUMA_MAX_WPATH * 2U];
    char number[32];
    DWORD attributes;
    unsigned int prefix_length;
    unsigned int index;
    int created = 0;
    int valid = 0;
    if (!materialized || source_file == INVALID_HANDLE_VALUE ||
        !source_identity || !source_size || !source_sha256 ||
        strlen(source_sha256) != 64U ||
        source_identity->nNumberOfLinks != 1U ||
        source_identity->nFileSizeHigh ||
        source_identity->nFileSizeLow != source_size ||
        !dluma_native_companion_identity_unchanged(
            source_file, source_identity))
        return 0;
    memset(engine_name, 0, sizeof(engine_name));
    dluma_str_copy(engine_name, DLUMA_ARRAY_COUNT(engine_name), bank_prefix);
    prefix_length = (unsigned int)strlen(engine_name);
    if (prefix_length + 24U + 1U > DLUMA_ARRAY_COUNT(engine_name))
        return 0;
    for (index = 0; index < 64U; ++index)
        if (!dluma_native_companion_json_hex(source_sha256[index]))
            return 0;
    for (index = 0; index < 24U; ++index)
        engine_name[prefix_length + index] = source_sha256[index];
    engine_name[prefix_length + 24U] = 0;
    if (materialized->active)
        return dluma_stricmp(materialized->bank_name, engine_name) == 0 &&
               materialized->bank_file != INVALID_HANDLE_VALUE &&
               materialized->bank_file != 0 &&
               dluma_native_companion_identity_unchanged(
                   materialized->bank_file,
                   &materialized->bank_identity);
    memset(&bank_identity, 0, sizeof(bank_identity));
    memset(&directory_identity, 0, sizeof(directory_identity));
    dluma_str_copy(
        bank_filename, DLUMA_ARRAY_COUNT(bank_filename), engine_name);
    if (!dluma_native_companion_str_append_checked(
            bank_filename, DLUMA_ARRAY_COUNT(bank_filename), ".csb") ||
        !dluma_utf8_to_wide(
            source_sha256, component_wide,
            DLUMA_ARRAY_COUNT(component_wide)) ||
        !dluma_native_companion_join_component(
            runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir),
            g_dluma.module_dir, L"dluma_runtime") ||
        !dluma_native_companion_join_component(
            cache_dir, DLUMA_ARRAY_COUNT(cache_dir),
            runtime_dir, L"prepared_banks") ||
        !dluma_native_companion_join_component(
            bank_dir, DLUMA_ARRAY_COUNT(bank_dir),
            cache_dir, component_wide) ||
        !dluma_utf8_to_wide(
            bank_filename, component_wide,
            DLUMA_ARRAY_COUNT(component_wide)) ||
        !dluma_native_companion_join_component(
            bank_path, DLUMA_ARRAY_COUNT(bank_path),
            bank_dir, component_wide) ||
        !dluma_native_companion_open_plain_directory(
            runtime_dir, 1, &runtime_handle) ||
        !dluma_native_companion_open_plain_directory(
            cache_dir, 1, &cache_handle) ||
        !dluma_native_companion_open_plain_directory(
            bank_dir, 1, &bank_dir_handle) ||
        !dluma_wide_to_utf8(
            bank_dir, bank_dir_utf8,
            DLUMA_ARRAY_COUNT(bank_dir_utf8)))
        goto done;
    attributes = GetFileAttributesW(bank_path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        memset(temporary_component, 0, sizeof(temporary_component));
        dluma_str_copy(
            temporary_component,
            DLUMA_ARRAY_COUNT(temporary_component), engine_name);
        if (!dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), ".tmp-") ||
            !dluma_u64_to_text(
                GetTickCount64(), number, DLUMA_ARRAY_COUNT(number)) ||
            !dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), number) ||
            !dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), "-") ||
            !dluma_u64_to_text(
                (ULONGLONG)GetCurrentThreadId(), number,
                DLUMA_ARRAY_COUNT(number)) ||
            !dluma_native_companion_str_append_checked(
                temporary_component,
                DLUMA_ARRAY_COUNT(temporary_component), number) ||
            !dluma_utf8_to_wide(
                temporary_component, component_wide,
                DLUMA_ARRAY_COUNT(component_wide)) ||
            !dluma_native_companion_join_component(
                temporary_path, DLUMA_ARRAY_COUNT(temporary_path),
                bank_dir, component_wide) ||
            !dluma_native_companion_write_locked_bank_temp(
                source_file, source_identity, source_size,
                source_sha256, temporary_path))
            goto done;
        if (!MoveFileExW(
                temporary_path, bank_path, MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary_path);
            goto done;
        }
        created = 1;
    }
    else if (attributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        goto done;
    if (!dluma_native_companion_open_cached_bank(
            bank_path, source_size, source_sha256,
            &bank_file, &bank_identity) ||
        !GetFileInformationByHandle(
            bank_dir_handle, &directory_identity) ||
        !dluma_native_companion_identity_unchanged(
            source_file, source_identity))
        goto done;
    materialized->runtime_dir = runtime_handle;
    materialized->cache_dir = cache_handle;
    materialized->bank_dir = bank_dir_handle;
    materialized->bank_file = bank_file;
    runtime_handle = INVALID_HANDLE_VALUE;
    cache_handle = INVALID_HANDLE_VALUE;
    bank_dir_handle = INVALID_HANDLE_VALUE;
    bank_file = INVALID_HANDLE_VALUE;
    memcpy(
        &materialized->bank_identity,
        &bank_identity, sizeof(bank_identity));
    memcpy(
        &materialized->directory_identity,
        &directory_identity, sizeof(directory_identity));
    dluma_wstr_copy(
        materialized->bank_dir_path,
        DLUMA_ARRAY_COUNT(materialized->bank_dir_path), bank_dir);
    dluma_wstr_copy(
        materialized->bank_path,
        DLUMA_ARRAY_COUNT(materialized->bank_path), bank_path);
    dluma_str_copy(
        materialized->bank_dir_utf8,
        DLUMA_ARRAY_COUNT(materialized->bank_dir_utf8), bank_dir_utf8);
    dluma_str_copy(
        materialized->bank_name,
        DLUMA_ARRAY_COUNT(materialized->bank_name), engine_name);
    materialized->active = 1U;
    valid = 1;
done:
    if (bank_file != INVALID_HANDLE_VALUE)
        CloseHandle(bank_file);
    if (bank_dir_handle != INVALID_HANDLE_VALUE)
        CloseHandle(bank_dir_handle);
    if (cache_handle != INVALID_HANDLE_VALUE)
        CloseHandle(cache_handle);
    if (runtime_handle != INVALID_HANDLE_VALUE)
        CloseHandle(runtime_handle);
    if (!valid && created)
        dluma_log_pair(
            "WARN", "prepared_physical_bank",
            "created_cache_failed_revalidation");
    return valid;
}

static int dluma_companion_archive_register_materialized_source(
    DlumaCompanionArchiveMaterializedBank* materialized,
    void* source,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    const char* content_sha256,
    const char* approval_fingerprint,
    const char* manifest_fingerprint)
{
    DlumaProcessLifetimeSourceRecord record;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    unsigned int index;
    if (!materialized || !source || !owner_id || !owner_id[0] ||
        !content_sha256 || strlen(content_sha256) != 64U)
        return 0;
    memset(&record, 0, sizeof(record));
    record.owner_kind = owner_kind;
    dluma_str_copy(
        record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id), owner_id);
    record.source_object = source;
    dluma_str_copy(
        record.requested_mount_path,
        DLUMA_ARRAY_COUNT(record.requested_mount_path),
        materialized->bank_dir_utf8);
    record.runtime_source_type = DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record.source_flags = 9U;
    record.volume_serial =
        materialized->directory_identity.dwVolumeSerialNumber;
    record.file_index_high =
        materialized->directory_identity.nFileIndexHigh;
    record.file_index_low =
        materialized->directory_identity.nFileIndexLow;
    dluma_str_copy(
        record.content_sha256,
        DLUMA_ARRAY_COUNT(record.content_sha256), content_sha256);
    if (approval_fingerprint && approval_fingerprint[0])
        dluma_str_copy(
            record.approval_fingerprint,
            DLUMA_ARRAY_COUNT(record.approval_fingerprint),
            approval_fingerprint);
    if (manifest_fingerprint && manifest_fingerprint[0])
        dluma_str_copy(
            record.manifest_fingerprint,
            DLUMA_ARRAY_COUNT(record.manifest_fingerprint),
            manifest_fingerprint);
    record.metadata_complete = 1U;
    if (!dluma_process_lifetime_source_register(&record, 0) ||
        !dluma_process_lifetime_sources_snapshot(&snapshot))
        return 0;
    for (index = 0; index < snapshot.count; ++index)
    {
        const DlumaProcessLifetimeSourceRecord* mounted =
            &snapshot.records[index];
        if (mounted->owner_kind == owner_kind &&
            mounted->source_object == source &&
            dluma_stricmp(mounted->owner_id, owner_id) == 0)
        {
            materialized->source_object = source;
            materialized->source_mount_generation =
                mounted->mount_generation;
            materialized->source_registered = 1U;
            return materialized->source_mount_generation != 0U;
        }
    }
    return 0;
}

int dluma_companion_archive_mount_materialized_bank(
    DlumaCompanionArchiveMaterializedBank* materialized,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    const char* content_sha256,
    const char* approval_fingerprint,
    const char* manifest_fingerprint,
    char* engine_bank_name_out,
    unsigned int engine_bank_name_capacity)
{
    static const char add_source_export[] =
        "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z";
    static const char exists_export[] =
        "?exists@fs@@YA_NPEBD@Z";
    static const char find_source_export[] =
        "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z";
    static const char path_ctor_export[] =
        "??0path@fs@@QEAA@PEBD_N@Z";
    static const char path_dtor_export[] =
        "??1path@fs@@QEAA@XZ";
    static const BYTE add_source_prefix[32] = {
        0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54,
        0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d,
        0xac, 0x24, 0xf0, 0xfd, 0xff, 0xff, 0x48, 0x81,
        0xec, 0x10, 0x03, 0x00, 0x00, 0x4c, 0x8b, 0xe9
    };
    static const BYTE exists_prefix[24] = {
        0x48, 0x83, 0xec, 0x78, 0x48, 0x8b, 0xd1, 0x48,
        0x8d, 0x4c, 0x24, 0x20, 0x41, 0xb0, 0x01, 0xe8,
        0x4c, 0xf3, 0x00, 0x00, 0x44, 0x8b, 0x5c, 0x24
    };
    static const BYTE find_source_prefix[24] = {
        0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0xac, 0x24,
        0x20, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xe0,
        0x02, 0x00, 0x00, 0x48, 0x8b, 0xd1, 0x48, 0x83
    };
    static const BYTE path_ctor_prefix[24] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81,
        0xec, 0x40, 0x02, 0x00, 0x00, 0x33, 0xff, 0x48,
        0x8b, 0xd9, 0xc7, 0x41, 0x40, 0x08, 0x00, 0x00
    };
    static const BYTE path_dtor_prefix[24] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xec, 0x20, 0x33, 0xff, 0xf6, 0x41, 0x40, 0x01,
        0x48, 0x8b, 0xd9, 0x74, 0x0c, 0x48, 0x8b, 0x09
    };
    static const BYTE directory_entry_exists_prefix[20] = {
        0x48, 0x83, 0xec, 0x28, 0xe8, 0xe7, 0xdf, 0xff,
        0xff, 0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48,
        0x83, 0xc4, 0x28, 0xc3
    };
    static const BYTE directory_full_path_prefix[32] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81,
        0xec, 0x30, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xda,
        0x49, 0x8b, 0xd0, 0x48, 0x8b, 0xf9, 0xe8, 0xb5,
        0xdf, 0xff, 0xff, 0x48, 0x85, 0xc0, 0x74, 0x22
    };
    HMODULE filesystem;
    DlumaNativeFsAddSourceFn add_source;
    DlumaNativeFsExistsFn exists;
    DlumaNativeFsFindSourceFn find_source;
    DlumaNativeFsPathCtorFn path_ctor;
    DlumaNativeFsPathDtorFn path_dtor;
    DlumaNativeFsSourceEntryExistsFn directory_entry_exists;
    const BYTE* expected_add_source;
    const BYTE* expected_exists;
    const BYTE* expected_find_source;
    const BYTE* expected_path_ctor;
    const BYTE* expected_path_dtor;
    const BYTE* expected_directory_entry_exists;
    const BYTE* expected_directory_full_path;
    void* source;
    WCHAR module_path[DLUMA_MAX_WPATH];
    char module_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char engine_file[
        DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME + 8U];
    DWORD timestamp = 0;
    DWORD image_size = 0;
    if (engine_bank_name_out && engine_bank_name_capacity)
        engine_bank_name_out[0] = 0;
    if (!materialized || !materialized->active ||
        !materialized->bank_name[0] ||
        !materialized->bank_dir_utf8[0] ||
        !owner_id || !owner_id[0] ||
        !content_sha256 || strlen(content_sha256) != 64U ||
        !engine_bank_name_out || !engine_bank_name_capacity ||
        materialized->bank_file == INVALID_HANDLE_VALUE ||
        materialized->bank_file == 0 ||
        !dluma_native_companion_identity_unchanged(
            materialized->bank_file, &materialized->bank_identity))
        return 0;
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!filesystem ||
        !dluma_native_companion_module_identity(
            filesystem, &timestamp, &image_size) ||
        timestamp != DLUMA_NATIVE_FILESYSTEM_TIMESTAMP ||
        image_size != DLUMA_NATIVE_FILESYSTEM_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            filesystem, module_path, DLUMA_ARRAY_COUNT(module_path)) ||
        !dluma_sha256_file_hex(
            module_path, 16ULL * 1024ULL * 1024ULL, module_sha256) ||
        dluma_stricmp(
            module_sha256, g_dluma_native_filesystem_sha256) != 0)
        return 0;
    expected_add_source = (BYTE*)filesystem + 0x00012dd0UL;
    expected_exists = (BYTE*)filesystem + 0x000148e0UL;
    expected_find_source = (BYTE*)filesystem + 0x00014430UL;
    expected_path_ctor = (BYTE*)filesystem + 0x00023c40UL;
    expected_path_dtor = (BYTE*)filesystem + 0x00023e20UL;
    expected_directory_entry_exists =
        (BYTE*)filesystem + 0x00006460UL;
    expected_directory_full_path =
        (BYTE*)filesystem + 0x00006480UL;
    add_source = (DlumaNativeFsAddSourceFn)GetProcAddress(
        filesystem, add_source_export);
    exists = (DlumaNativeFsExistsFn)GetProcAddress(
        filesystem, exists_export);
    find_source = (DlumaNativeFsFindSourceFn)GetProcAddress(
        filesystem, find_source_export);
    path_ctor = (DlumaNativeFsPathCtorFn)GetProcAddress(
        filesystem, path_ctor_export);
    path_dtor = (DlumaNativeFsPathDtorFn)GetProcAddress(
        filesystem, path_dtor_export);
    directory_entry_exists =
        (DlumaNativeFsSourceEntryExistsFn)expected_directory_entry_exists;
    if (!add_source || !exists || !find_source || !path_ctor ||
        !path_dtor ||
        (ULONG_PTR)add_source !=
            (ULONG_PTR)expected_add_source ||
        (ULONG_PTR)exists !=
            (ULONG_PTR)expected_exists ||
        (ULONG_PTR)find_source !=
            (ULONG_PTR)expected_find_source ||
        (ULONG_PTR)path_ctor !=
            (ULONG_PTR)expected_path_ctor ||
        (ULONG_PTR)path_dtor !=
            (ULONG_PTR)expected_path_dtor ||
        !dluma_is_readable(
            expected_add_source, sizeof(add_source_prefix)) ||
        !dluma_is_readable(expected_exists, sizeof(exists_prefix)) ||
        !dluma_is_readable(
            expected_find_source, sizeof(find_source_prefix)) ||
        !dluma_is_readable(
            expected_path_ctor, sizeof(path_ctor_prefix)) ||
        !dluma_is_readable(
            expected_path_dtor, sizeof(path_dtor_prefix)) ||
        !dluma_is_readable(
            expected_directory_entry_exists,
            sizeof(directory_entry_exists_prefix)) ||
        !dluma_is_readable(
            expected_directory_full_path,
            sizeof(directory_full_path_prefix)) ||
        memcmp(expected_add_source, add_source_prefix,
               sizeof(add_source_prefix)) != 0 ||
        memcmp(expected_exists, exists_prefix,
               sizeof(exists_prefix)) != 0 ||
        memcmp(expected_find_source, find_source_prefix,
               sizeof(find_source_prefix)) != 0 ||
        memcmp(expected_path_ctor, path_ctor_prefix,
               sizeof(path_ctor_prefix)) != 0 ||
        memcmp(expected_path_dtor, path_dtor_prefix,
               sizeof(path_dtor_prefix)) != 0 ||
        memcmp(expected_directory_entry_exists,
               directory_entry_exists_prefix,
               sizeof(directory_entry_exists_prefix)) != 0 ||
        memcmp(expected_directory_full_path,
               directory_full_path_prefix,
               sizeof(directory_full_path_prefix)) != 0 ||
        !dluma_native_companion_readonly_module_range(
            filesystem, image_size,
            (BYTE*)filesystem + 0x0003f4a8UL,
            11U * sizeof(void*)) ||
        ((void**)((BYTE*)filesystem + 0x0003f4a8UL))[10] !=
            (void*)expected_directory_full_path)
        return 0;
    dluma_str_copy(
        engine_file, DLUMA_ARRAY_COUNT(engine_file),
        materialized->bank_name);
    if (!dluma_native_companion_str_append_checked(
            engine_file, DLUMA_ARRAY_COUNT(engine_file), ".csb"))
        return 0;
    source = find_source(materialized->bank_dir_utf8);
    if (!source)
    {
        if (exists(engine_file) ||
            !add_source(materialized->bank_dir_utf8, 9U))
            return 0;
        source = find_source(materialized->bank_dir_utf8);
    }
    if (!source ||
        !dluma_native_companion_source_has_entry(
            filesystem, image_size, source, engine_file,
            path_ctor, path_dtor,
            (BYTE*)filesystem + 0x0003f4a8UL,
            directory_entry_exists) ||
        !exists(engine_file) ||
        !dluma_native_companion_identity_unchanged(
            materialized->bank_file, &materialized->bank_identity) ||
        (materialized->source_registered &&
         materialized->source_object != source) ||
        (!materialized->source_registered &&
         !dluma_companion_archive_register_materialized_source(
             materialized, source, owner_kind, owner_id,
             content_sha256, approval_fingerprint,
             manifest_fingerprint)))
        return 0;
    if (strlen(materialized->bank_name) + 1U >
        engine_bank_name_capacity)
        return 0;
    dluma_str_copy(
        engine_bank_name_out, engine_bank_name_capacity,
        materialized->bank_name);
    dluma_log_pair(
        "INFO", "prepared_physical_bank_alias",
        materialized->bank_name);
    dluma_log_pair(
        "INFO", "prepared_physical_bank_path",
        materialized->bank_dir_utf8);
    return 1;
}

int dluma_companion_archive_set_source_owner(
    DlumaCompanionArchive* pack,
    const char* owner_id,
    const char* approval_fingerprint,
    const char* manifest_fingerprint)
{
    if (!pack || !pack->validated || pack->mounted ||
        !owner_id || !owner_id[0] ||
        !approval_fingerprint ||
        strlen(approval_fingerprint) != 64U ||
        !manifest_fingerprint ||
        strlen(manifest_fingerprint) != 64U)
        return 0;
    pack->source_owner_id[0] = 0;
    pack->source_approval_fingerprint[0] = 0;
    pack->source_manifest_fingerprint[0] = 0;
    if (!dluma_native_companion_str_append_checked(
            pack->source_owner_id,
            DLUMA_ARRAY_COUNT(pack->source_owner_id),
            owner_id) ||
        !dluma_native_companion_str_append_checked(
            pack->source_approval_fingerprint,
            DLUMA_ARRAY_COUNT(
                pack->source_approval_fingerprint),
            approval_fingerprint) ||
        !dluma_native_companion_str_append_checked(
            pack->source_manifest_fingerprint,
            DLUMA_ARRAY_COUNT(
                pack->source_manifest_fingerprint),
            manifest_fingerprint)) {
        pack->source_owner_id[0] = 0;
        pack->source_approval_fingerprint[0] = 0;
        pack->source_manifest_fingerprint[0] = 0;
        return 0;
    }
    return 1;
}

static int dluma_native_companion_register_source(
    DlumaCompanionArchive* pack, void* source)
{
    DlumaProcessLifetimeSourceRecord record;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    unsigned int index;
    if (!pack || !source || !pack->source_owner_id[0] ||
        !pack->source_approval_fingerprint[0] ||
        !pack->source_manifest_fingerprint[0])
        return 0;
    memset(&record, 0, sizeof(record));
    record.owner_kind =
        DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT;
    dluma_str_copy(
        record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id),
        pack->source_owner_id);
    record.source_object = source;
    dluma_str_copy(
        record.requested_mount_path,
        DLUMA_ARRAY_COUNT(record.requested_mount_path),
        pack->final_path_utf8);
    record.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
    record.source_flags = 9U;
    record.volume_serial =
        pack->identity.dwVolumeSerialNumber;
    record.file_index_high = pack->identity.nFileIndexHigh;
    record.file_index_low = pack->identity.nFileIndexLow;
    record.file_size =
        ((ULONGLONG)pack->identity.nFileSizeHigh << 32U) |
        pack->identity.nFileSizeLow;
    dluma_str_copy(
        record.content_sha256,
        DLUMA_ARRAY_COUNT(record.content_sha256),
        pack->sha256);
    dluma_str_copy(
        record.approval_fingerprint,
        DLUMA_ARRAY_COUNT(record.approval_fingerprint),
        pack->source_approval_fingerprint);
    dluma_str_copy(
        record.manifest_fingerprint,
        DLUMA_ARRAY_COUNT(record.manifest_fingerprint),
        pack->source_manifest_fingerprint);
    record.metadata_complete = 1;
    if (!dluma_process_lifetime_source_register(
            &record, 0) ||
        !dluma_process_lifetime_sources_snapshot(&snapshot))
        return 0;
    for (index = 0; index < snapshot.count; ++index) {
        const DlumaProcessLifetimeSourceRecord* mounted =
            &snapshot.records[index];
        if (mounted->owner_kind ==
                DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT &&
            mounted->source_object == source &&
            dluma_stricmp(
                mounted->owner_id,
                pack->source_owner_id) == 0) {
            pack->source_object = source;
            pack->source_mount_generation =
                mounted->mount_generation;
            pack->source_registered = 1;
            return pack->source_mount_generation != 0;
        }
    }
    return 0;
}

int dluma_companion_archive_mount(DlumaCompanionArchive* pack)
{
    static const char add_source_export[] =
        "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z";
    static const BYTE add_source_prefix[32] = {
        0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54,
        0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d,
        0xac, 0x24, 0xf0, 0xfd, 0xff, 0xff, 0x48, 0x81,
        0xec, 0x10, 0x03, 0x00, 0x00, 0x4c, 0x8b, 0xe9
    };
    static const char find_source_export[] =
        "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z";
    static const BYTE find_source_prefix[24] = {
        0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0xac, 0x24,
        0x20, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xe0,
        0x02, 0x00, 0x00, 0x48, 0x8b, 0xd1, 0x48, 0x83
    };
    HMODULE filesystem;
    DlumaNativeFsAddSourceFn add_source;
    DlumaNativeFsFindSourceFn find_source;
    const BYTE* expected_add_source;
    const BYTE* expected_find_source;
    void* source;
    WCHAR module_path[DLUMA_MAX_WPATH];
    char module_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD timestamp = 0;
    DWORD image_size = 0;
    if (!pack || !pack->validated ||
        pack->file == INVALID_HANDLE_VALUE ||
        !pack->source_owner_id[0] ||
        !pack->source_approval_fingerprint[0] ||
        !pack->source_manifest_fingerprint[0])
        return 0;
    if (pack->mounted)
        return pack->source_registered &&
               pack->source_object &&
               pack->source_mount_generation;
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!filesystem ||
        !dluma_native_companion_module_identity(
            filesystem, &timestamp, &image_size) ||
        timestamp != DLUMA_NATIVE_FILESYSTEM_TIMESTAMP ||
        image_size != DLUMA_NATIVE_FILESYSTEM_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            filesystem, module_path, DLUMA_ARRAY_COUNT(module_path)) ||
        !dluma_sha256_file_hex(
            module_path, 16ULL * 1024ULL * 1024ULL, module_sha256) ||
        dluma_stricmp(
            module_sha256, g_dluma_native_filesystem_sha256) != 0)
        return 0;
    expected_add_source = (const BYTE*)filesystem + 0x00012dd0UL;
    expected_find_source =
        (const BYTE*)filesystem + 0x00014430UL;
    add_source = (DlumaNativeFsAddSourceFn)GetProcAddress(
        filesystem, add_source_export);
    find_source = (DlumaNativeFsFindSourceFn)GetProcAddress(
        filesystem, find_source_export);
    if (!add_source || !find_source ||
        (ULONG_PTR)add_source != (ULONG_PTR)expected_add_source ||
        (ULONG_PTR)find_source !=
            (ULONG_PTR)expected_find_source ||
        !dluma_is_readable(expected_add_source, sizeof(add_source_prefix)) ||
        !dluma_is_readable(
            expected_find_source, sizeof(find_source_prefix)) ||
        memcmp(
            expected_add_source, add_source_prefix,
            sizeof(add_source_prefix)) != 0 ||
        memcmp(
            expected_find_source, find_source_prefix,
            sizeof(find_source_prefix)) != 0)
        return 0;
    source = find_source(pack->final_path_utf8);
    if (!source) {
        if (!add_source(pack->final_path_utf8, 9U))
            return 0;
        source = find_source(pack->final_path_utf8);
    }
    pack->mounted = 1;
    if (!source ||
        !dluma_native_companion_register_source(pack, source)) {
        dluma_log_pair(
            "ERROR", "native_companion_pack_source_ownership",
            "exact_source_registration_failed");
        return 0;
    }
    dluma_log_pair(
        "INFO", "native_companion_pack_mounted",
        pack->final_path_utf8);
    return 1;
}

void dluma_companion_archive_close(DlumaCompanionArchive* pack)
{
    unsigned int index;
    if (!pack) return;
    if (pack->mounted) {
        dluma_log_pair(
            "INFO",
            "native_companion_pack_left_mounted_until_process_exit",
            pack->final_path_utf8);
        return;
    }
    for (index = 0; index < DLUMA_COMPANION_ARCHIVE_MAX_BANKS; ++index) {
        DlumaCompanionArchiveMaterializedBank* materialized =
            &pack->materialized_banks[index];
        if (materialized->bank_file != INVALID_HANDLE_VALUE &&
            materialized->bank_file != 0)
            CloseHandle(materialized->bank_file);
        if (materialized->bank_dir != INVALID_HANDLE_VALUE &&
            materialized->bank_dir != 0)
            CloseHandle(materialized->bank_dir);
        if (materialized->cache_dir != INVALID_HANDLE_VALUE &&
            materialized->cache_dir != 0)
            CloseHandle(materialized->cache_dir);
        if (materialized->runtime_dir != INVALID_HANDLE_VALUE &&
            materialized->runtime_dir != 0)
            CloseHandle(materialized->runtime_dir);
    }
    if (pack->file != INVALID_HANDLE_VALUE)
        CloseHandle(pack->file);
    memset(pack, 0, sizeof(*pack));
    pack->file = INVALID_HANDLE_VALUE;
    for (index = 0; index < DLUMA_COMPANION_ARCHIVE_MAX_BANKS; ++index) {
        pack->materialized_banks[index].runtime_dir = INVALID_HANDLE_VALUE;
        pack->materialized_banks[index].cache_dir = INVALID_HANDLE_VALUE;
        pack->materialized_banks[index].bank_dir = INVALID_HANDLE_VALUE;
        pack->materialized_banks[index].bank_file = INVALID_HANDLE_VALUE;
    }
}

int dluma_companion_archive_has_entry(
    const DlumaCompanionArchive* pack, const char* virtual_path)
{
    unsigned int i;
    if (!pack || !pack->validated || !virtual_path) return 0;
    for (i = 0; i < pack->entry_count; ++i)
        if (dluma_native_companion_str_equal(
                pack->entries[i].virtual_path, virtual_path))
            return 1;
    return 0;
}

int dluma_companion_archive_read_entry(
    DlumaCompanionArchive* pack, const char* virtual_path,
    void* bytes_out, unsigned int bytes_capacity,
    unsigned int* required_size_out)
{
    unsigned int i;
    if (required_size_out) *required_size_out = 0;
    if (!pack || !pack->validated ||
        pack->file == INVALID_HANDLE_VALUE ||
        !virtual_path || !required_size_out ||
        !dluma_native_companion_identity_unchanged(
            pack->file, &pack->identity))
        return 0;
    for (i = 0; i < pack->entry_count; ++i) {
        const DlumaCompanionArchiveEntry* entry = &pack->entries[i];
        if (!dluma_native_companion_str_equal(
                entry->virtual_path, virtual_path)) continue;
        *required_size_out = entry->size;
        if (!bytes_out || bytes_capacity < entry->size)
            return DLUMA_NATIVE_READ_BUFFER_TOO_SMALL;
        return dluma_native_companion_read_at(
            pack->file, entry->data_offset, bytes_out, entry->size);
    }
    return 0;
}

int dluma_companion_archive_manifest_bank_matches(
    const DlumaCompanionArchive* pack, const char* bank_identifier)
{
    return dluma_companion_archive_find_bank(pack, bank_identifier) != 0;
}

const DlumaCompanionArchiveAudioManifest* dluma_companion_archive_find_bank(
    const DlumaCompanionArchive* pack, const char* bank_identifier)
{
    unsigned int index;
    if (!pack || !pack->validated || !bank_identifier ||
        !dluma_native_companion_id_valid(
            bank_identifier, DLUMA_COMPANION_ARCHIVE_MAX_ID))
        return 0;
    if (pack->audio_catalog.valid) {
        for (index = 0; index < pack->audio_catalog.bank_count; ++index) {
            const DlumaCompanionArchiveAudioManifest* bank =
                &pack->audio_catalog.banks[index];
            if (bank->valid && dluma_native_companion_str_equal(
                    bank->bank_identifier, bank_identifier))
                return bank;
        }
        return 0;
    }
    return pack->audio_manifest.valid &&
           dluma_native_companion_str_equal(
               pack->audio_manifest.bank_identifier, bank_identifier)
        ? &pack->audio_manifest : 0;
}

int dluma_companion_archive_manifest_has_cue(
    const DlumaCompanionArchive* pack, const char* bank_identifier,
    const char* cue_name,
    DWORD* duration_ms_out)
{
    const DlumaCompanionArchiveAudioManifest* bank;
    unsigned int cue_index = 0;
    if (duration_ms_out) *duration_ms_out = 0;
    bank = dluma_companion_archive_find_bank(pack, bank_identifier);
    if (!bank || !cue_name || !duration_ms_out ||
        !dluma_native_companion_cue_index(
            bank, cue_name, &cue_index) ||
        cue_index >= bank->cue_count ||
        !bank->cues[cue_index].duration_ms)
        return 0;
    *duration_ms_out = bank->cues[cue_index].duration_ms;
    return 1;
}
