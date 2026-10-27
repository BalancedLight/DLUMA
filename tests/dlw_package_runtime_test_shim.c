#include "../src/dlw_common.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

char dluma_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + 32) : value;
}

WCHAR dluma_wascii_lower(WCHAR value)
{
    return value >= L'A' && value <= L'Z' ? (WCHAR)(value + 32) : value;
}

int dluma_stricmp(const char* left, const char* right)
{
    while (*left && dluma_ascii_lower(*left) == dluma_ascii_lower(*right)) {
        ++left;
        ++right;
    }
    return (unsigned char)dluma_ascii_lower(*left) -
        (unsigned char)dluma_ascii_lower(*right);
}

int dluma_wstricmp(const WCHAR* left, const WCHAR* right)
{
    while (*left &&
           dluma_wascii_lower(*left) == dluma_wascii_lower(*right)) {
        ++left;
        ++right;
    }
    return (int)dluma_wascii_lower(*left) -
        (int)dluma_wascii_lower(*right);
}

void dluma_str_copy(char* output, unsigned int capacity, const char* value)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    while (value && value[index] && index + 1U < capacity) {
        output[index] = value[index];
        ++index;
    }
    output[index] = 0;
}

void dluma_wstr_copy(
    WCHAR* output,
    unsigned int capacity,
    const WCHAR* value)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    while (value && value[index] && index + 1U < capacity) {
        output[index] = value[index];
        ++index;
    }
    output[index] = 0;
}

void dluma_wstr_append(
    WCHAR* output,
    unsigned int capacity,
    const WCHAR* value)
{
    unsigned int length = (unsigned int)wcslen(output);
    if (length < capacity)
        dluma_wstr_copy(output + length, capacity - length, value);
}

void dluma_path_join(
    WCHAR* output,
    unsigned int capacity,
    const WCHAR* left,
    const WCHAR* right)
{
    dluma_wstr_copy(output, capacity, left);
    if (*output && output[wcslen(output) - 1U] != L'\\')
        dluma_wstr_append(output, capacity, L"\\");
    dluma_wstr_append(output, capacity, right);
}

void* dluma_alloc(SIZE_T size)
{
    return malloc(size);
}

void dluma_free(void* memory)
{
    free(memory);
}

int dluma_utf8_to_wide(
    const char* source,
    WCHAR* target,
    unsigned int target_count)
{
    int result = MultiByteToWideChar(
        CP_UTF8, 0, source, -1,
        target, (int)target_count);
    return result > 0 && (unsigned int)result <= target_count;
}

int dluma_wide_to_utf8(
    const WCHAR* source,
    char* target,
    unsigned int target_count)
{
    int result = WideCharToMultiByte(
        CP_UTF8, 0, source, -1,
        target, (int)target_count, 0, 0);
    return result > 0 && (unsigned int)result <= target_count;
}
