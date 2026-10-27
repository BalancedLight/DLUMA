#include "../src/dlw_common.h"
#include "../src/dlw_animation_pack_runtime.h"
#include "../src/dlw_editor_animation_bridge.h"

#include <string.h>

char dluma_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

WCHAR dluma_wascii_lower(WCHAR value)
{
    return value >= L'A' && value <= L'Z'
        ? (WCHAR)(value + (L'a' - L'A')) : value;
}

int dluma_stricmp(const char* left, const char* right)
{
    unsigned int index = 0;
    if (left == right) return 0;
    if (!left) return -1;
    if (!right) return 1;
    for (;;) {
        char a = dluma_ascii_lower(left[index]);
        char b = dluma_ascii_lower(right[index]);
        if (a != b) return a < b ? -1 : 1;
        if (!a) return 0;
        ++index;
    }
}

int dluma_wstricmp(const WCHAR* left, const WCHAR* right)
{
    unsigned int index = 0;
    if (left == right) return 0;
    if (!left) return -1;
    if (!right) return 1;
    for (;;) {
        WCHAR a = dluma_wascii_lower(left[index]);
        WCHAR b = dluma_wascii_lower(right[index]);
        if (a != b) return a < b ? -1 : 1;
        if (!a) return 0;
        ++index;
    }
}

int dluma_wcontains_icase(const WCHAR* value, const WCHAR* needle)
{
    unsigned int outer;
    unsigned int inner;
    if (!value || !needle) return 0;
    if (!needle[0]) return 1;
    for (outer = 0; value[outer]; ++outer) {
        for (inner = 0; needle[inner] && value[outer + inner]; ++inner)
            if (dluma_wascii_lower(value[outer + inner]) !=
                dluma_wascii_lower(needle[inner]))
                break;
        if (!needle[inner]) return 1;
    }
    return 0;
}

unsigned int dluma_parse_u32(const char* text, unsigned int fallback)
{
    unsigned int value = 0;
    unsigned int any = 0;
    if (!text) return fallback;
    while (*text == ' ' || *text == '\t') ++text;
    while (*text >= '0' && *text <= '9') {
        unsigned int digit = (unsigned int)(*text - '0');
        if (value > (0xFFFFFFFFUL - digit) / 10U) return fallback;
        value = value * 10U + digit;
        any = 1U;
        ++text;
    }
    while (*text == ' ' || *text == '\t' ||
           *text == '\r' || *text == '\n') ++text;
    return any && !*text ? value : fallback;
}

unsigned int dluma_u64_to_text(
    ULONGLONG value, char* output, unsigned int capacity)
{
    char reverse[32];
    unsigned int count = 0;
    unsigned int index;
    if (!output || !capacity) return 0;
    if (!value) reverse[count++] = '0';
    while (value && count < DLUMA_ARRAY_COUNT(reverse)) {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    if (count + 1U > capacity) count = capacity - 1U;
    for (index = 0; index < count; ++index)
        output[index] = reverse[count - index - 1U];
    output[count] = 0;
    return count;
}

void dluma_str_copy(char* output, unsigned int capacity, const char* source)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    if (!source) {
        output[0] = 0;
        return;
    }
    while (source[index] && index + 1U < capacity) {
        output[index] = source[index];
        ++index;
    }
    output[index] = 0;
}

void dluma_wstr_copy(
    WCHAR* output, unsigned int capacity, const WCHAR* source)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    if (!source) {
        output[0] = 0;
        return;
    }
    while (source[index] && index + 1U < capacity) {
        output[index] = source[index];
        ++index;
    }
    output[index] = 0;
}

void dluma_str_append(char* output, unsigned int capacity, const char* source)
{
    unsigned int offset = 0;
    unsigned int index = 0;
    if (!output || !source || !capacity) return;
    while (offset < capacity && output[offset]) ++offset;
    if (offset >= capacity) {
        output[capacity - 1U] = 0;
        return;
    }
    while (source[index] && offset + 1U < capacity)
        output[offset++] = source[index++];
    output[offset] = 0;
}

void dluma_wstr_append(
    WCHAR* output, unsigned int capacity, const WCHAR* source)
{
    unsigned int offset = 0;
    unsigned int index = 0;
    if (!output || !source || !capacity) return;
    while (offset < capacity && output[offset]) ++offset;
    if (offset >= capacity) {
        output[capacity - 1U] = 0;
        return;
    }
    while (source[index] && offset + 1U < capacity)
        output[offset++] = source[index++];
    output[offset] = 0;
}

void* dluma_alloc(SIZE_T size)
{
    if (!size) size = 1U;
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void dluma_free(void* memory)
{
    if (memory) HeapFree(GetProcessHeap(), 0, memory);
}

volatile LONG g_test_animation_pack_swap_calls;
DWORD g_test_animation_pack_swap_thread_id;
int g_test_animation_pack_swap_result;
unsigned int g_test_animation_pack_swap_rollback_attempted;
unsigned int g_test_animation_pack_swap_rollback_succeeded;
WCHAR g_test_animation_pack_swap_path[DLUMA_MAX_WPATH];
HANDLE g_test_animation_pack_swap_started_event;
HANDLE g_test_animation_pack_swap_continue_event;
volatile LONG g_test_animation_pack_shutdown_calls;
DWORD g_test_animation_pack_shutdown_thread_id;
int g_test_animation_pack_retained;

int dluma_is_readable(const void* address, SIZE_T bytes)
{
    return address != 0 && bytes != 0;
}

int dluma_animation_pack_runtime_swap(
    const WCHAR* absolute_pack_path,
    DlumaAnimationPackSwap* swap)
{
    dluma_atomic_increment(&g_test_animation_pack_swap_calls);
    g_test_animation_pack_swap_thread_id = GetCurrentThreadId();
    dluma_wstr_copy(
        g_test_animation_pack_swap_path,
        DLUMA_ARRAY_COUNT(g_test_animation_pack_swap_path),
        absolute_pack_path);
    if (g_test_animation_pack_swap_started_event)
        (void)SetEvent(g_test_animation_pack_swap_started_event);
    if (g_test_animation_pack_swap_continue_event &&
        WaitForSingleObject(
            g_test_animation_pack_swap_continue_event, INFINITE) !=
            WAIT_OBJECT_0)
        return 0;
    if (swap) {
        memset(swap, 0, sizeof(*swap));
        swap->rollback_attempted =
            g_test_animation_pack_swap_rollback_attempted;
        swap->rollback_succeeded =
            g_test_animation_pack_swap_rollback_succeeded;
        if (g_test_animation_pack_swap_result) {
            swap->changed = 1U;
            swap->registered = 1U;
        }
    }
    if (g_test_animation_pack_swap_result)
        g_test_animation_pack_retained = 1;
    return g_test_animation_pack_swap_result;
}

int dluma_animation_pack_runtime_retained(void)
{
    return g_test_animation_pack_retained;
}

void dluma_animation_pack_runtime_shutdown(void)
{
    dluma_atomic_increment(&g_test_animation_pack_shutdown_calls);
    g_test_animation_pack_shutdown_thread_id = GetCurrentThreadId();
    g_test_animation_pack_retained = 0;
}

int dluma_editor_animation_bridge_dispatch(
    const DlumaEditorAnimationReloadRequest* request,
    DlumaAnimationRefreshResult* result)
{
    (void)request;
    (void)result;
    return 0;
}

int dluma_editor_exit_observer_dispatch_command(unsigned int command_id)
{
    (void)command_id;
    return 0;
}

int dluma_utf8_to_wide(
    const char* source, WCHAR* output, unsigned int capacity)
{
    int result;
    if (!source || !output || !capacity) return 0;
    result = MultiByteToWideChar(
        CP_UTF8, 0, source, -1, output, (int)capacity);
    if (!result) output[0] = 0;
    return result != 0;
}

int dluma_wide_to_utf8(
    const WCHAR* source, char* output, unsigned int capacity)
{
    int result;
    if (!source || !output || !capacity) return 0;
    result = WideCharToMultiByte(
        CP_UTF8, 0, source, -1, output, (int)capacity, 0, 0);
    if (!result) output[0] = 0;
    return result != 0;
}
