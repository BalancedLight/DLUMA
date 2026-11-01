#include "dlw_menu_animation_cache.h"

#define MENU_CACHE_ARCHIVE_LIMIT (512ULL * 1024ULL * 1024ULL)
#define MENU_CACHE_PACK_LIMIT (16U * 1024U * 1024U)
#define MENU_CACHE_MAX_ENTRIES 8192U
#define MENU_CACHE_BLOCK 16384U

static const char g_menu_cache_entry[] = "Anims/dluma_thriller_ee.rpack";
static WCHAR g_menu_cache_path[DLUMA_MAX_WPATH];

static WORD menu_cache_u16(const BYTE *p)
{
    return (WORD)((WORD)p[0] | ((WORD)p[1] << 8));
}

static DWORD menu_cache_u32(const BYTE *p)
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8) |
        ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static int menu_cache_read_at(HANDLE file, DWORD offset, void *out, DWORD size)
{
    LARGE_INTEGER position;
    DWORD read = 0;
    position.QuadPart = offset;
    return SetFilePointerEx(file, position, 0, FILE_BEGIN) &&
        ReadFile(file, out, size, &read, 0) && read == size;
}

static DWORD menu_cache_crc32(DWORD crc, const BYTE *data, DWORD size)
{
    DWORD i;
    for (i = 0; i < size; ++i) {
        unsigned int bit;
        crc ^= data[i];
        for (bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
    return crc;
}

static int menu_cache_find(HANDLE file, DWORD size, DWORD *offset_out,
                           DWORD *length_out, DWORD *crc_out)
{
    BYTE end[22];
    BYTE central[46];
    BYTE local[30];
    DWORD end_offset, central_offset, central_size, cursor;
    DWORD found_offset = 0, found_size = 0, found_crc = 0;
    unsigned int i, found = 0;
    WORD count;
    if (size < sizeof(end))
        return 0;
    end_offset = size - (DWORD)sizeof(end);
    if (!menu_cache_read_at(file, end_offset, end, sizeof(end)) ||
        menu_cache_u32(end) != 0x06054b50UL ||
        menu_cache_u16(end + 4) != 0 || menu_cache_u16(end + 6) != 0 ||
        menu_cache_u16(end + 8) != menu_cache_u16(end + 10) ||
        menu_cache_u16(end + 20) != 0)
        return 0;
    count = menu_cache_u16(end + 10);
    central_size = menu_cache_u32(end + 12);
    central_offset = menu_cache_u32(end + 16);
    if (!count || count > MENU_CACHE_MAX_ENTRIES ||
        central_offset > end_offset || central_size != end_offset - central_offset)
        return 0;
    cursor = central_offset;
    for (i = 0; i < count; ++i) {
        WORD flags, method, name_length, extra_length, comment_length;
        DWORD compressed, raw, local_offset, next;
        char name[sizeof(g_menu_cache_entry)];
        if (cursor > end_offset || sizeof(central) > end_offset - cursor ||
            !menu_cache_read_at(file, cursor, central, sizeof(central)) ||
            menu_cache_u32(central) != 0x02014b50UL)
            return 0;
        flags = menu_cache_u16(central + 8);
        method = menu_cache_u16(central + 10);
        compressed = menu_cache_u32(central + 20);
        raw = menu_cache_u32(central + 24);
        name_length = menu_cache_u16(central + 28);
        extra_length = menu_cache_u16(central + 30);
        comment_length = menu_cache_u16(central + 32);
        local_offset = menu_cache_u32(central + 42);
        next = cursor + (DWORD)sizeof(central);
        if (next > end_offset || name_length > end_offset - next)
            return 0;
        next += name_length;
        if (extra_length > end_offset - next)
            return 0;
        next += extra_length;
        if (comment_length > end_offset - next)
            return 0;
        next += comment_length;
        if (name_length == sizeof(g_menu_cache_entry) - 1U) {
            if (!menu_cache_read_at(file, cursor + (DWORD)sizeof(central),
                                    name, name_length))
                return 0;
            name[name_length] = 0;
            if (memcmp(name, g_menu_cache_entry, sizeof(g_menu_cache_entry)) == 0) {
                DWORD data_offset;
                char local_name[sizeof(g_menu_cache_entry)];
                if (++found != 1U || flags || method || !raw ||
                    raw > MENU_CACHE_PACK_LIMIT || compressed != raw ||
                    local_offset > central_offset ||
                    sizeof(local) > central_offset - local_offset ||
                    !menu_cache_read_at(file, local_offset, local, sizeof(local)) ||
                    menu_cache_u32(local) != 0x04034b50UL ||
                    menu_cache_u16(local + 6) != flags ||
                    menu_cache_u16(local + 8) != method ||
                    menu_cache_u32(local + 14) != menu_cache_u32(central + 16) ||
                    menu_cache_u32(local + 18) != compressed ||
                    menu_cache_u32(local + 22) != raw ||
                    menu_cache_u16(local + 26) != name_length ||
                    menu_cache_u16(local + 28) != 0)
                    return 0;
                data_offset = local_offset + (DWORD)sizeof(local);
                if (name_length > central_offset - data_offset ||
                    !menu_cache_read_at(file, data_offset, local_name, name_length))
                    return 0;
                local_name[name_length] = 0;
                if (memcmp(local_name, g_menu_cache_entry,
                           sizeof(g_menu_cache_entry)) != 0)
                    return 0;
                data_offset += name_length;
                if (raw > central_offset - data_offset)
                    return 0;
                found_offset = data_offset;
                found_size = raw;
                found_crc = menu_cache_u32(central + 16);
            }
        }
        cursor = next;
    }
    if (cursor != end_offset || found != 1U)
        return 0;
    *offset_out = found_offset;
    *length_out = found_size;
    *crc_out = found_crc;
    return 1;
}

static int menu_cache_verify(HANDLE file, DWORD offset, DWORD size,
                             DWORD expected_crc, HANDLE output)
{
    BYTE block[MENU_CACHE_BLOCK];
    DWORD consumed = 0, crc = 0xffffffffUL;
    while (consumed < size) {
        DWORD amount = size - consumed;
        DWORD written = 0;
        if (amount > sizeof(block))
            amount = sizeof(block);
        if (!menu_cache_read_at(file, offset + consumed, block, amount) ||
            (!consumed && (amount < 4U || memcmp(block, "RP6L", 4U) != 0)))
            return 0;
        crc = menu_cache_crc32(crc, block, amount);
        if (output != INVALID_HANDLE_VALUE &&
            (!WriteFile(output, block, amount, &written, 0) || written != amount))
            return 0;
        consumed += amount;
    }
    return (crc ^ 0xffffffffUL) == expected_crc;
}

static int menu_cache_existing(const WCHAR *path, DWORD size, DWORD crc)
{
    LARGE_INTEGER length;
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_DELETE, 0,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    BY_HANDLE_FILE_INFORMATION info;
    int valid;
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    valid = GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        GetFileSizeEx(file, &length) && length.QuadPart == size &&
        menu_cache_verify(file, 0, size, crc, INVALID_HANDLE_VALUE);
    CloseHandle(file);
    return valid;
}

static int menu_cache_directory(const WCHAR *path)
{
    DWORD attributes;
    if (!dluma_mkdirs(path))
        return 0;
    attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static int menu_cache_source_unchanged(
    HANDLE file, const BY_HANDLE_FILE_INFORMATION *before)
{
    BY_HANDLE_FILE_INFORMATION after;
    return GetFileInformationByHandle(file, &after) &&
        after.dwVolumeSerialNumber == before->dwVolumeSerialNumber &&
        after.nFileIndexHigh == before->nFileIndexHigh &&
        after.nFileIndexLow == before->nFileIndexLow &&
        after.nFileSizeHigh == before->nFileSizeHigh &&
        after.nFileSizeLow == before->nFileSizeLow &&
        after.ftLastWriteTime.dwHighDateTime == before->ftLastWriteTime.dwHighDateTime &&
        after.ftLastWriteTime.dwLowDateTime == before->ftLastWriteTime.dwLowDateTime;
}

int dluma_menu_animation_cache_materialize(WCHAR *out, unsigned int capacity)
{
    WCHAR archive[DLUMA_MAX_WPATH], runtime[DLUMA_MAX_WPATH];
    WCHAR cache[DLUMA_MAX_WPATH], folder[DLUMA_MAX_WPATH];
    WCHAR target[DLUMA_MAX_WPATH], temporary[DLUMA_MAX_WPATH];
    WCHAR suffix[24];
    LARGE_INTEGER length;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE source = INVALID_HANDLE_VALUE, dest = INVALID_HANDLE_VALUE;
    DWORD offset, size, crc, process;
    unsigned int index;
    int ok = 0, temporary_created = 0;
    if (!out || !capacity || !g_dluma.module_dir[0] ||
        wcslen(g_dluma.module_dir) + 80U >= DLUMA_MAX_WPATH)
        return 0;
    out[0] = 0;
    dluma_path_join(archive, DLUMA_ARRAY_COUNT(archive), g_dluma.module_dir, L"dluma.pak");
    dluma_path_join(runtime, DLUMA_ARRAY_COUNT(runtime), g_dluma.module_dir, L"dluma_runtime");
    dluma_path_join(cache, DLUMA_ARRAY_COUNT(cache), runtime, L"cache");
    dluma_path_join(folder, DLUMA_ARRAY_COUNT(folder), cache, L"Anims");
    dluma_path_join(target, DLUMA_ARRAY_COUNT(target), folder, L"dluma_thriller_ee.rpack");
    if (wcslen(target) + 20U >= DLUMA_ARRAY_COUNT(target) ||
        wcslen(target) + 1U > capacity ||
        !menu_cache_directory(runtime) || !menu_cache_directory(cache) ||
        !menu_cache_directory(folder))
        return 0;
    source = CreateFileW(archive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                         0, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (source == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileInformationByHandle(source, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(source, &length) || length.QuadPart < 0 ||
        (ULONGLONG)length.QuadPart > MENU_CACHE_ARCHIVE_LIMIT ||
        !menu_cache_find(source, (DWORD)length.QuadPart, &offset, &size, &crc))
        goto done;
    if (menu_cache_existing(target, size, crc)) {
        ok = menu_cache_source_unchanged(source, &info);
        goto done;
    }
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary), target);
    process = GetCurrentProcessId();
    suffix[0] = L'.'; suffix[1] = L't'; suffix[2] = L'm';
    suffix[3] = L'p'; suffix[4] = L'.';
    for (index = 0; index < 8U; ++index) {
        unsigned int nibble = (process >> (28U - index * 4U)) & 15U;
        suffix[5U + index] = (WCHAR)(nibble < 10U ? L'0' + nibble : L'a' + nibble - 10U);
    }
    suffix[13] = 0;
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), suffix);
    dest = CreateFileW(temporary, GENERIC_WRITE, 0, 0, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (dest == INVALID_HANDLE_VALUE)
        goto done;
    temporary_created = 1;
    if (!menu_cache_verify(source, offset, size, crc, dest) ||
        !FlushFileBuffers(dest) || !menu_cache_source_unchanged(source, &info))
        goto done;
    CloseHandle(dest);
    dest = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (!menu_cache_existing(target, size, crc))
            goto done;
        (void)DeleteFileW(temporary);
    }
    temporary_created = 0;
    ok = 1;
done:
    if (dest != INVALID_HANDLE_VALUE)
        CloseHandle(dest);
    if (temporary_created)
        (void)DeleteFileW(temporary);
    if (source != INVALID_HANDLE_VALUE)
        CloseHandle(source);
    if (ok) {
        dluma_wstr_copy(out, capacity, target);
        dluma_wstr_copy(g_menu_cache_path, DLUMA_ARRAY_COUNT(g_menu_cache_path), target);
    }
    return ok;
}

void dluma_menu_animation_cache_cleanup(void)
{
    if (g_menu_cache_path[0]) {
        (void)DeleteFileW(g_menu_cache_path);
        g_menu_cache_path[0] = 0;
    }
}
