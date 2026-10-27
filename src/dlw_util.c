#include "dlw_common.h"

void* memmove(void* dst, const void* src, SIZE_T count);

void* memcpy(void* dst, const void* src, SIZE_T count)
{
    BYTE* d = (BYTE*)dst;
    const BYTE* s = (const BYTE*)src;
    SIZE_T i;
    for (i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int value, SIZE_T count)
{
    BYTE* d = (BYTE*)dst;
    SIZE_T i;
    for (i = 0; i < count; ++i) d[i] = (BYTE)value;
    return dst;
}

int memcmp(const void* a, const void* b, SIZE_T count)
{
    const BYTE* x = (const BYTE*)a;
    const BYTE* y = (const BYTE*)b;
    SIZE_T i;
    for (i = 0; i < count; ++i) {
        if (x[i] < y[i]) return -1;
        if (x[i] > y[i]) return 1;
    }
    return 0;
}

SIZE_T strlen(const char* text)
{
    SIZE_T n = 0;
    if (!text) return 0;
    while (text[n]) ++n;
    return n;
}

SIZE_T wcslen(const WCHAR* text)
{
    SIZE_T n = 0;
    if (!text) return 0;
    while (text[n]) ++n;
    return n;
}

char dluma_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

WCHAR dluma_wascii_lower(WCHAR c)
{
    if (c >= L'A' && c <= L'Z') return (WCHAR)(c + (L'a' - L'A'));
    return c;
}

int dluma_stricmp(const char* a, const char* b)
{
    unsigned int i = 0;
    char ca, cb;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (;;) {
        ca = dluma_ascii_lower(a[i]);
        cb = dluma_ascii_lower(b[i]);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
        if (!ca) return 0;
        ++i;
    }
}

int dluma_wstricmp(const WCHAR* a, const WCHAR* b)
{
    unsigned int i = 0;
    WCHAR ca, cb;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (;;) {
        ca = dluma_wascii_lower(a[i]);
        cb = dluma_wascii_lower(b[i]);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
        if (!ca) return 0;
        ++i;
    }
}

int dluma_contains_icase(const char* haystack, const char* needle)
{
    unsigned int i, j;
    if (!haystack || !needle) return 0;
    if (!needle[0]) return 1;
    for (i = 0; haystack[i]; ++i) {
        for (j = 0; needle[j] && haystack[i + j]; ++j) {
            if (dluma_ascii_lower(haystack[i + j]) != dluma_ascii_lower(needle[j])) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

int dluma_wcontains_icase(const WCHAR* value, const WCHAR* needle)
{
    unsigned int i, j;
    if (!value || !needle) return 0;
    if (!needle[0]) return 1;
    for (i = 0; value[i]; ++i) {
        for (j = 0; needle[j] && value[i + j]; ++j) {
            if (dluma_wascii_lower(value[i + j]) != dluma_wascii_lower(needle[j])) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

int dluma_wstarts_with_icase(const WCHAR* value, const WCHAR* prefix)
{
    unsigned int i = 0;
    if (!value || !prefix) return 0;
    while (prefix[i]) {
        if (!value[i]) return 0;
        if (dluma_wascii_lower(value[i]) != dluma_wascii_lower(prefix[i])) return 0;
        ++i;
    }
    return 1;
}

int dluma_wends_with_icase(const WCHAR* value, const WCHAR* suffix)
{
    SIZE_T a;
    SIZE_T b;
    if (!value || !suffix) return 0;
    a = wcslen(value);
    b = wcslen(suffix);
    if (b > a) return 0;
    return dluma_wstricmp(value + (a - b), suffix) == 0;
}

int dluma_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int dluma_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

unsigned int dluma_parse_u32(const char* text, unsigned int fallback)
{
    unsigned int value = 0;
    unsigned int base = 10;
    unsigned int any = 0;
    const char* p = dluma_skip_spaces(text);
    if (!p) return fallback;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    while (*p) {
        unsigned int digit;
        if (*p >= '0' && *p <= '9') digit = (unsigned int)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = 10u + (unsigned int)(*p - 'a');
        else if (*p >= 'A' && *p <= 'F') digit = 10u + (unsigned int)(*p - 'A');
        else break;
        if (digit >= base) break;
        if (value > (0xFFFFFFFFUL - digit) / base) return fallback;
        value = value * base + digit;
        any = 1;
        ++p;
    }
    while (*p && dluma_is_space(*p)) ++p;
    return any && !*p ? value : fallback;
}

unsigned int dluma_u64_to_text(ULONGLONG value, char* out, unsigned int capacity)
{
    char reverse[32];
    unsigned int n = 0, i;
    if (!out || capacity == 0) return 0;
    if (value == 0) reverse[n++] = '0';
    while (value && n < DLUMA_ARRAY_COUNT(reverse)) {
        reverse[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    if (n + 1 > capacity) n = capacity - 1;
    for (i = 0; i < n; ++i) out[i] = reverse[n - 1 - i];
    out[n] = 0;
    return n;
}

unsigned int dluma_u32_hex(DWORD value, char* out, unsigned int capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int i;
    if (!out || capacity < 11) return 0;
    out[0] = '0'; out[1] = 'x';
    for (i = 0; i < 8; ++i) out[2 + i] = hex[(value >> ((7 - i) * 4)) & 0xF];
    out[10] = 0;
    return 10;
}

void dluma_str_copy(char* dst, unsigned int cap, const char* src)
{
    unsigned int i = 0;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

void dluma_wstr_copy(WCHAR* dst, unsigned int cap, const WCHAR* src)
{
    unsigned int i = 0;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

void dluma_str_append(char* dst, unsigned int cap, const char* src)
{
    unsigned int i, j = 0;
    if (!dst || !src || cap == 0) return;
    i = (unsigned int)strlen(dst);
    if (i >= cap) { dst[cap - 1] = 0; return; }
    while (src[j] && i + 1 < cap) dst[i++] = src[j++];
    dst[i] = 0;
}

void dluma_wstr_append(WCHAR* dst, unsigned int cap, const WCHAR* src)
{
    unsigned int i, j = 0;
    if (!dst || !src || cap == 0) return;
    i = (unsigned int)wcslen(dst);
    if (i >= cap) { dst[cap - 1] = 0; return; }
    while (src[j] && i + 1 < cap) dst[i++] = src[j++];
    dst[i] = 0;
}

void dluma_path_dirname(WCHAR* path)
{
    SIZE_T n;
    if (!path) return;
    n = wcslen(path);
    while (n > 0) {
        WCHAR c = path[n - 1];
        if (c == L'\\' || c == L'/') { path[n - 1] = 0; return; }
        --n;
    }
    path[0] = 0;
}

void dluma_path_parent(WCHAR* path)
{
    SIZE_T n;
    if (!path) return;
    n = wcslen(path);
    while (n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/')) path[--n] = 0;
    dluma_path_dirname(path);
}

void dluma_path_join(WCHAR* out, unsigned int cap, const WCHAR* left, const WCHAR* right)
{
    SIZE_T n;
    const WCHAR* r = right;
    if (!out || cap == 0) return;
    dluma_wstr_copy(out, cap, left ? left : L"");
    n = wcslen(out);
    while (r && (*r == L'\\' || *r == L'/')) ++r;
    if (n && out[n - 1] != L'\\' && out[n - 1] != L'/') dluma_wstr_append(out, cap, L"\\");
    dluma_wstr_append(out, cap, r ? r : L"");
}

int dluma_path_exists(const WCHAR* path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES;
}

int dluma_dir_exists(const WCHAR* path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void* dluma_alloc(SIZE_T bytes)
{
    if (!bytes) bytes = 1;
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

void dluma_free(void* memory)
{
    if (memory) HeapFree(GetProcessHeap(), 0, memory);
}

int dluma_read_file_utf8(const WCHAR* path, char** out_data, DWORD* out_size)
{
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0;
    char* data;
    DWORD wanted;
    if (!out_data || !out_size) return 0;
    *out_data = 0;
    *out_size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || (ULONGLONG)size.QuadPart > DLUMA_MAX_SCRIPT_FILE_BYTES) {
        CloseHandle(file);
        return 0;
    }
    wanted = (DWORD)size.QuadPart;
    data = (char*)dluma_alloc((SIZE_T)wanted + 1);
    if (!data) { CloseHandle(file); return 0; }
    if (wanted && (!ReadFile(file, data, wanted, &read, 0) || read != wanted)) {
        dluma_free(data);
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    data[wanted] = 0;
    if (wanted >= 3 && (BYTE)data[0] == 0xEF && (BYTE)data[1] == 0xBB && (BYTE)data[2] == 0xBF) {
        memmove(data, data + 3, wanted - 2);
        wanted -= 3;
    }
    *out_data = data;
    *out_size = wanted;
    return 1;
}

/* Local overlap-safe move; kept private to avoid another CRT symbol. */
void* memmove(void* dst, const void* src, SIZE_T count)
{
    BYTE* d = (BYTE*)dst;
    const BYTE* s = (const BYTE*)src;
    SIZE_T i;
    if (d < s) {
        for (i = 0; i < count; ++i) d[i] = s[i];
    } else if (d > s) {
        for (i = count; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

int dluma_utf8_to_wide(const char* source, WCHAR* target, unsigned int target_count)
{
    int result;
    if (!source || !target || target_count == 0) return 0;
    result = MultiByteToWideChar(CP_UTF8, 0, source, -1, target, (int)target_count);
    if (!result) target[0] = 0;
    return result != 0;
}

int dluma_wide_to_utf8(const WCHAR* source, char* target, unsigned int target_count)
{
    int result;
    if (!source || !target || target_count == 0) return 0;
    result = WideCharToMultiByte(CP_UTF8, 0, source, -1, target, (int)target_count, 0, 0);
    if (!result) target[0] = 0;
    return result != 0;
}

int dluma_is_readable(const void* address, SIZE_T bytes)
{
    const BYTE* p = (const BYTE*)address;
    SIZE_T remaining = bytes;
    MEMORY_BASIC_INFORMATION mbi;
    if (!address || !bytes ||
        (ULONG_PTR)address > ~(ULONG_PTR)0 - (bytes - 1U)) return 0;
    while (remaining) {
        SIZE_T offset;
        SIZE_T available;
        DWORD protection;
        if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
        protection = mbi.Protect & 0xFFUL;
        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(protection == PAGE_READONLY || protection == PAGE_READWRITE ||
              protection == PAGE_WRITECOPY ||
              protection == PAGE_EXECUTE_READ ||
              protection == PAGE_EXECUTE_READWRITE ||
              protection == PAGE_EXECUTE_WRITECOPY))
            return 0;
        offset = (SIZE_T)(p - (const BYTE*)mbi.BaseAddress);
        if (offset >= mbi.RegionSize) return 0;
        available = mbi.RegionSize - offset;
        if (available >= remaining) return 1;
        if ((ULONG_PTR)p > ~(ULONG_PTR)0 - available) return 0;
        p += available;
        remaining -= available;
    }
    return 1;
}

void dluma_sanitize_filename(char* text)
{
    unsigned int i;
    if (!text) return;
    for (i = 0; text[i]; ++i) {
        char c = text[i];
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') text[i] = '_';
    }
}

const char* dluma_skip_spaces(const char* p)
{
    if (!p) return 0;
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

void dluma_trim_in_place(char* text)
{
    SIZE_T len;
    SIZE_T start = 0;
    if (!text) return;
    while (text[start] && dluma_is_space(text[start])) ++start;
    if (start) memmove(text, text + start, strlen(text + start) + 1);
    len = strlen(text);
    while (len && dluma_is_space(text[len - 1])) text[--len] = 0;
}

int dluma_extract_quoted(const char* line, unsigned int* cursor, char* out, unsigned int out_cap)
{
    unsigned int i, n = 0;
    if (!line || !cursor || !out || out_cap == 0) return 0;
    i = *cursor;
    while (line[i] == ' ' || line[i] == '\t') ++i;
    if (line[i] != '"') return 0;
    ++i;
    while (line[i] && line[i] != '"') {
        char c = line[i++];
        if (c == '\\' && line[i]) {
            char e = line[i++];
            if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 't') c = '\t';
            else if (e == '"') c = '"';
            else if (e == '\\') c = '\\';
            else { if (n + 1 < out_cap) out[n++] = '\\'; c = e; }
        }
        if (n + 1 < out_cap) out[n++] = c;
    }
    if (line[i] != '"') { out[0] = 0; return 0; }
    out[n] = 0;
    *cursor = i + 1;
    return 1;
}

int dluma_mkdirs(const WCHAR* path)
{
    WCHAR temp[DLUMA_MAX_WPATH];
    unsigned int i;
    if (!path || !path[0]) return 0;
    dluma_wstr_copy(temp, DLUMA_ARRAY_COUNT(temp), path);
    for (i = 0; temp[i]; ++i) {
        if (temp[i] == L'/') temp[i] = L'\\';
        if (temp[i] == L'\\') {
            WCHAR saved;
            if (i == 0 || (i == 2 && temp[1] == L':')) continue;
            saved = temp[i];
            temp[i] = 0;
            if (temp[0] && !dluma_dir_exists(temp)) CreateDirectoryW(temp, 0);
            temp[i] = saved;
        }
    }
    if (!dluma_dir_exists(temp)) CreateDirectoryW(temp, 0);
    return dluma_dir_exists(temp);
}

int dluma_write_utf8_file(const WCHAR* path, const char* text, int append)
{
    HANDLE file;
    DWORD written = 0;
    LARGE_INTEGER move;
    WCHAR dir[DLUMA_MAX_WPATH];
    DWORD len;
    if (!path || !text) return 0;
    dluma_wstr_copy(dir, DLUMA_ARRAY_COUNT(dir), path);
    dluma_path_dirname(dir);
    if (dir[0]) dluma_mkdirs(dir);
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (append) {
        move.QuadPart = 0;
        SetFilePointerEx(file, move, 0, FILE_END);
    }
    len = (DWORD)strlen(text);
    if (len && (!WriteFile(file, text, len, &written, 0) || written != len)) {
        CloseHandle(file);
        return 0;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    return 1;
}

void dluma_normalize_map_name(const char* source, char* out, unsigned int cap)
{
    const char* start;
    unsigned int n = 0;
    unsigned int i;
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!source) return;
    start = source;
    for (i = 0; source[i]; ++i) {
        if (source[i] == '/' || source[i] == '\\') start = source + i + 1;
    }
    while (start[n] && n + 1 < cap) {
        char c = start[n];
        if (c == '?' || c == '#') break;
        out[n] = dluma_ascii_lower(c);
        ++n;
    }
    out[n] = 0;
    if (n > 4 && dluma_stricmp(out + n - 4, ".map") == 0) out[n - 4] = 0;
    else if (n > 6 && dluma_stricmp(out + n - 6, ".level") == 0) out[n - 6] = 0;
    dluma_trim_in_place(out);
}
