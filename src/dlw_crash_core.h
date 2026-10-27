#ifndef DLUMA_CRASH_CORE_H
#define DLUMA_CRASH_CORE_H

/*
 * Small allocation-free crash-report helpers shared with the deterministic
 * native harness. Keep this header independent of loader globals and Win32
 * calls so malformed-input behavior can be tested without loading the game.
 */

typedef int (*DlumaCrashCoreWriteFn)(
    void* context,
    const char* data,
    unsigned long length,
    unsigned long* written);

enum DlumaCrashCoreUnwindStep {
    DLUMA_CRASH_CORE_UNWIND_PROGRESS = 0,
    DLUMA_CRASH_CORE_UNWIND_COMPLETE = 1,
    DLUMA_CRASH_CORE_UNWIND_NON_PROGRESS = 2,
    DLUMA_CRASH_CORE_UNWIND_STACK_REGRESSED = 3
};

enum DlumaCrashCoreModuleClass {
    DLUMA_CRASH_CORE_MODULE_UNMAPPED_ADDRESS = 0,
    DLUMA_CRASH_CORE_MODULE_GAME_EXECUTABLE,
    DLUMA_CRASH_CORE_MODULE_GAME_MODULE,
    DLUMA_CRASH_CORE_MODULE_LOADER_MODULE,
    DLUMA_CRASH_CORE_MODULE_WINDOWS_SYSTEM_MODULE,
    DLUMA_CRASH_CORE_MODULE_KNOWN_THIRD_PARTY_MODULE,
    DLUMA_CRASH_CORE_MODULE_UNKNOWN_LOADED_MODULE
};

typedef struct DlumaCrashCoreGameModuleProfile {
    const unsigned short* basename;
    unsigned long pe_timestamp;
    unsigned long image_size;
    const char* sha256;
    const char* profile;
} DlumaCrashCoreGameModuleProfile;

typedef struct DlumaCrashCoreModuleIdentity {
    int mapped;
    int process_executable;
    int loader_module;
    int windows_system_module;
    int known_third_party_module;
    const unsigned short* basename;
    unsigned long pe_timestamp;
    unsigned long image_size;
    const char* sha256;
    int sha256_available;
} DlumaCrashCoreModuleIdentity;

typedef struct DlumaCrashCoreModuleClassification {
    enum DlumaCrashCoreModuleClass module_class;
    int first_party;
    int identity_validated;
    const char* profile;
} DlumaCrashCoreModuleClassification;

static unsigned short dluma_crash_core_wide_ascii_lower(
    unsigned short value)
{
    if (value >= (unsigned short)L'A' &&
        value <= (unsigned short)L'Z')
        return (unsigned short)(
            value + ((unsigned short)L'a' - (unsigned short)L'A'));
    return value;
}

static int dluma_crash_core_wide_ascii_equal_icase(
    const unsigned short* left,
    const unsigned short* right)
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (dluma_crash_core_wide_ascii_lower(*left) !=
            dluma_crash_core_wide_ascii_lower(*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

static char dluma_crash_core_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z')
        return (char)(value + ('a' - 'A'));
    return value;
}

static int dluma_crash_core_ascii_equal_icase(
    const char* left,
    const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (dluma_crash_core_ascii_lower(*left) !=
            dluma_crash_core_ascii_lower(*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

static int dluma_crash_core_game_profile_basename_matches(
    const unsigned short* basename,
    const DlumaCrashCoreGameModuleProfile* profiles,
    unsigned int profile_count)
{
    unsigned int index;
    if (!basename || !profiles) return 0;
    for (index = 0; index < profile_count; ++index)
        if (dluma_crash_core_wide_ascii_equal_icase(
                basename, profiles[index].basename))
            return 1;
    return 0;
}

static DlumaCrashCoreModuleClassification
dluma_crash_core_classify_module(
    const DlumaCrashCoreModuleIdentity* identity,
    const DlumaCrashCoreGameModuleProfile* profiles,
    unsigned int profile_count)
{
    DlumaCrashCoreModuleClassification result;
    unsigned int index;
    result.module_class = DLUMA_CRASH_CORE_MODULE_UNMAPPED_ADDRESS;
    result.first_party = 0;
    result.identity_validated = 0;
    result.profile = "unmapped";
    if (!identity || !identity->mapped)
        return result;
    if (identity->loader_module) {
        result.module_class = DLUMA_CRASH_CORE_MODULE_LOADER_MODULE;
        result.identity_validated = 1;
        result.profile = "dluma";
        return result;
    }
    if (identity->process_executable) {
        result.module_class = DLUMA_CRASH_CORE_MODULE_GAME_EXECUTABLE;
        result.first_party = 1;
        result.identity_validated = 1;
        result.profile = "process_executable";
        return result;
    }
    if (identity->windows_system_module) {
        result.module_class =
            DLUMA_CRASH_CORE_MODULE_WINDOWS_SYSTEM_MODULE;
        result.identity_validated = 1;
        result.profile = "windows_system_path";
        return result;
    }
    if (profiles) {
        for (index = 0; index < profile_count; ++index) {
            const DlumaCrashCoreGameModuleProfile* profile =
                &profiles[index];
            if (!dluma_crash_core_wide_ascii_equal_icase(
                    identity->basename, profile->basename))
                continue;
            if (identity->pe_timestamp == profile->pe_timestamp &&
                identity->image_size == profile->image_size &&
                identity->sha256_available &&
                dluma_crash_core_ascii_equal_icase(
                    identity->sha256, profile->sha256)) {
                result.module_class =
                    DLUMA_CRASH_CORE_MODULE_GAME_MODULE;
                result.first_party = 1;
                result.identity_validated = 1;
                result.profile = profile->profile;
                return result;
            }
            result.module_class =
                DLUMA_CRASH_CORE_MODULE_UNKNOWN_LOADED_MODULE;
            result.profile = "unsupported_game_module_identity";
            return result;
        }
    }
    if (identity->known_third_party_module) {
        result.module_class =
            DLUMA_CRASH_CORE_MODULE_KNOWN_THIRD_PARTY_MODULE;
        result.profile = "known_third_party_basename";
        return result;
    }
    result.module_class =
        DLUMA_CRASH_CORE_MODULE_UNKNOWN_LOADED_MODULE;
    result.profile = "unvalidated_loaded_module";
    return result;
}

static const char* dluma_crash_core_module_class_text(
    enum DlumaCrashCoreModuleClass module_class)
{
    switch (module_class) {
    case DLUMA_CRASH_CORE_MODULE_GAME_EXECUTABLE:
        return "GameExecutable";
    case DLUMA_CRASH_CORE_MODULE_GAME_MODULE:
        return "GameModule";
    case DLUMA_CRASH_CORE_MODULE_LOADER_MODULE:
        return "LoaderModule";
    case DLUMA_CRASH_CORE_MODULE_WINDOWS_SYSTEM_MODULE:
        return "WindowsSystemModule";
    case DLUMA_CRASH_CORE_MODULE_KNOWN_THIRD_PARTY_MODULE:
        return "KnownThirdPartyModule";
    case DLUMA_CRASH_CORE_MODULE_UNKNOWN_LOADED_MODULE:
        return "UnknownLoadedModule";
    default:
        return "UnmappedAddress";
    }
}

static const char* dluma_crash_core_exception_name(unsigned long code)
{
    switch (code) {
    case 0xC0000005UL: return "access_violation";
    case 0xC0000006UL: return "in_page_error";
    case 0xC000001DUL: return "illegal_instruction";
    case 0xC00000FDUL: return "stack_overflow";
    case 0xC0000374UL: return "heap_corruption";
    case 0xC0000409UL: return "fail_fast";
    case 0xE06D7363UL: return "cpp_exception";
    default: return "unknown";
    }
}

static const char* dluma_crash_core_access_operation(
    unsigned long long operation)
{
    if (operation == 0ULL) return "read";
    if (operation == 1ULL) return "write";
    if (operation == 8ULL) return "execute";
    return "unknown";
}

static int dluma_crash_core_exception_is_reportable(unsigned long code)
{
    return code != 0UL &&
           code != 0x80000003UL &&
           code != 0x80000004UL &&
           code != 0x40010006UL &&
           code != 0x4001000AUL &&
           code != 0x406D1388UL;
}

static int dluma_crash_core_report_name_matches(const unsigned short* name)
{
    static const unsigned short prefix[] = L"dluma_crash_session_";
    unsigned int index = 0;
    unsigned int digits;
    if (!name) return 0;
    while (prefix[index]) {
        if (name[index] != prefix[index]) return 0;
        ++index;
    }
    for (digits = 0; digits < 8U; ++digits, ++index)
        if (name[index] < L'0' || name[index] > L'9') return 0;
    if (name[index++] != L'T') return 0;
    for (digits = 0; digits < 9U; ++digits, ++index)
        if (name[index] < L'0' || name[index] > L'9') return 0;
    if (name[index++] != L'Z' || name[index++] != L'_' ||
        name[index++] != L'p' || name[index++] != L'i' ||
        name[index++] != L'd' || name[index++] != L'-')
        return 0;
    digits = 0;
    while (name[index] >= L'0' && name[index] <= L'9') {
        ++digits;
        ++index;
    }
    if (!digits) return 0;
    if (name[index] == L'-') {
        ++index;
        digits = 0;
        while (name[index] >= L'0' && name[index] <= L'9') {
            ++digits;
            ++index;
        }
        if (!digits) return 0;
    }
    return name[index++] == L'.' && name[index++] == L'l' &&
           name[index++] == L'o' && name[index++] == L'g' &&
           !name[index];
}

static int dluma_crash_core_address_in_module(
    unsigned long long address,
    unsigned long long base,
    unsigned long image_size)
{
    unsigned long long end;
    if (!base || !image_size) return 0;
    end = base + (unsigned long long)image_size;
    return end >= base && address >= base && address < end;
}

static int dluma_crash_core_unwind_step(
    unsigned long long old_rip,
    unsigned long long old_rsp,
    unsigned long long new_rip,
    unsigned long long new_rsp)
{
    if (!new_rip && !new_rsp)
        return DLUMA_CRASH_CORE_UNWIND_COMPLETE;
    if (new_rip == old_rip && new_rsp == old_rsp)
        return DLUMA_CRASH_CORE_UNWIND_NON_PROGRESS;
    if (new_rsp < old_rsp)
        return DLUMA_CRASH_CORE_UNWIND_STACK_REGRESSED;
    return DLUMA_CRASH_CORE_UNWIND_PROGRESS;
}

static int dluma_crash_core_format_hex(
    unsigned long long value,
    unsigned int digits,
    char* output,
    unsigned int capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int index;
    if (!output || digits > 16U || capacity < digits + 3U)
        return 0;
    output[0] = '0';
    output[1] = 'x';
    for (index = 0; index < digits; ++index)
        output[2U + index] =
            hex[(value >> ((digits - index - 1U) * 4U)) & 0xFU];
    output[2U + digits] = 0;
    return 1;
}

static const char* dluma_crash_core_value_or_unavailable(const char* value)
{
    return value && value[0] ? value : "<unavailable>";
}

static int dluma_crash_core_report_can_append(
    unsigned long current_bytes,
    unsigned long next_bytes,
    unsigned long maximum_bytes,
    unsigned long reserve_bytes)
{
    if (reserve_bytes > maximum_bytes ||
        current_bytes > maximum_bytes - reserve_bytes)
        return 0;
    return next_bytes <=
        maximum_bytes - reserve_bytes - current_bytes;
}

static int dluma_crash_core_append(
    char* output,
    unsigned int capacity,
    const char* text)
{
    unsigned int used = 0;
    unsigned int index = 0;
    if (!output || !capacity || !text) return 0;
    while (used < capacity && output[used]) ++used;
    if (used >= capacity) return 0;
    while (text[index]) {
        if (used + 1U >= capacity) return 0;
        output[used++] = text[index++];
    }
    output[used] = 0;
    return 1;
}

static int dluma_crash_core_append_decimal(
    char* output,
    unsigned int capacity,
    unsigned long long value)
{
    char reverse[32];
    char number[32];
    unsigned int count = 0;
    unsigned int index;
    do {
        reverse[count++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value && count < sizeof(reverse));
    for (index = 0; index < count; ++index)
        number[index] = reverse[count - index - 1U];
    number[count] = 0;
    return dluma_crash_core_append(output, capacity, number);
}

static int dluma_crash_core_format_frame_line(
    const char* kind,
    unsigned int index,
    unsigned long long address,
    const char* full_module_path,
    unsigned long long module_base,
    char* output,
    unsigned int capacity)
{
    char hex[19];
    int mapped = full_module_path && full_module_path[0] && module_base &&
                 address >= module_base;
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!dluma_crash_core_append(
            output, capacity, "[CRASH] [INFO] ") ||
        !dluma_crash_core_append(
            output, capacity, kind ? kind : "stack_frame") ||
        !dluma_crash_core_append(output, capacity, " index=") ||
        !dluma_crash_core_append_decimal(output, capacity, index) ||
        !dluma_crash_core_append(output, capacity, " address=") ||
        !dluma_crash_core_format_hex(
            address, 16, hex, sizeof(hex)) ||
        !dluma_crash_core_append(output, capacity, hex))
        return 0;
    if (!mapped)
        return dluma_crash_core_append(
                   output, capacity, " module=<unmapped>\r\n");
    if (!dluma_crash_core_append(output, capacity, " module=") ||
        !dluma_crash_core_append(
            output, capacity, full_module_path) ||
        !dluma_crash_core_append(output, capacity, " offset=") ||
        !dluma_crash_core_format_hex(
            address - module_base, 16, hex, sizeof(hex)) ||
        !dluma_crash_core_append(output, capacity, hex) ||
        !dluma_crash_core_append(output, capacity, "\r\n"))
        return 0;
    return 1;
}

static unsigned long dluma_crash_core_write_all(
    DlumaCrashCoreWriteFn writer,
    void* context,
    const char* data,
    unsigned long length,
    unsigned int max_attempts)
{
    unsigned long offset = 0;
    unsigned int attempts = 0;
    if (!writer || !data) return 0;
    while (offset < length && attempts < max_attempts) {
        unsigned long written = 0;
        unsigned long remaining = length - offset;
        if (!writer(
                context, data + offset, remaining, &written) ||
            !written || written > remaining)
            break;
        offset += written;
        ++attempts;
    }
    return offset;
}

#endif
