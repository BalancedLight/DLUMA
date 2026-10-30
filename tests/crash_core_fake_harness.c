#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../src/dlw_crash_core.h"

typedef struct FakeWriter {
    char output[128];
    unsigned long used;
    unsigned long chunk;
    unsigned int fail_after;
    unsigned int calls;
} FakeWriter;

static int fake_write(
    void* context,
    const char* data,
    unsigned long length,
    unsigned long* written)
{
    FakeWriter* writer = (FakeWriter*)context;
    unsigned long count;
    ++writer->calls;
    if (writer->fail_after && writer->calls > writer->fail_after) {
        *written = 0;
        return 0;
    }
    count = length < writer->chunk ? length : writer->chunk;
    if (count > sizeof(writer->output) - writer->used)
        count = (unsigned long)sizeof(writer->output) - writer->used;
    memcpy(writer->output + writer->used, data, count);
    writer->used += count;
    *written = count;
    return count != 0;
}

static int expect(int condition, const char* name)
{
    if (!condition) {
        fprintf(stderr, "crash core assertion failed: %s\n", name);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const DlumaCrashCoreGameModuleProfile game_profiles[] = {
        {
            L"gamedll_x64_rwdi.dll",
            0x623397AAUL,
            0x02082000UL,
            "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95",
            "dying_light_1_49_gamedll"
        }
    };
    char hex[32];
    char frame0[512];
    char frame1[512];
    char frames[1024];
    FakeWriter primary;
    FakeWriter secondary;
    const char payload[] = "DLW_CRASH_REPORT_V1";
    unsigned long primary_written;
    unsigned long secondary_written;
    DlumaCrashCoreModuleIdentity module_identity;
    DlumaCrashCoreModuleClassification module_classification;
    int ok = 1;
    const unsigned int game_profile_count =
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0]));

    ok &= expect(
        dluma_crash_core_game_profile_basename_matches(
            L"GAMEDLL_X64_RWDI.DLL", game_profiles, game_profile_count),
        "game profile basename matches case-insensitively");
    ok &= expect(
        !dluma_crash_core_game_profile_basename_matches(
            L"engine_x64_rwdi.dll", game_profiles, game_profile_count) &&
        !dluma_crash_core_game_profile_basename_matches(
            L"gamedll_x64_rwdi.dll.bak", game_profiles, game_profile_count) &&
        !dluma_crash_core_game_profile_basename_matches(
            L"C:\\Game\\gamedll_x64_rwdi.dll", game_profiles, game_profile_count),
        "game profile basename rejects unknown names, suffixes, and paths");
    ok &= expect(
        !dluma_crash_core_game_profile_basename_matches(
            0, game_profiles, game_profile_count) &&
        !dluma_crash_core_game_profile_basename_matches(
            L"gamedll_x64_rwdi.dll", 0, game_profile_count) &&
        !dluma_crash_core_game_profile_basename_matches(
            L"gamedll_x64_rwdi.dll", game_profiles, 0),
        "game profile basename rejects null inputs and empty profiles");
    memset(&primary, 0, sizeof(primary));
    memset(&secondary, 0, sizeof(secondary));
    primary.chunk = 5;
    secondary.chunk = 4;
    secondary.fail_after = 2;

    ok &= expect(
        strcmp(dluma_crash_core_exception_name(0xC0000005UL),
               "access_violation") == 0,
        "access violation name");
    ok &= expect(
        strcmp(dluma_crash_core_exception_name(0xC0000006UL),
               "in_page_error") == 0,
        "in-page name");
    ok &= expect(
        strcmp(dluma_crash_core_exception_name(0xC00000FDUL),
               "stack_overflow") == 0,
        "stack overflow name");
    ok &= expect(
        strcmp(dluma_crash_core_exception_name(7UL), "unknown") == 0,
        "unknown exception name");
    ok &= expect(
        strcmp(dluma_crash_core_access_operation(0), "read") == 0 &&
        strcmp(dluma_crash_core_access_operation(1), "write") == 0 &&
        strcmp(dluma_crash_core_access_operation(8), "execute") == 0 &&
        strcmp(dluma_crash_core_access_operation(2), "unknown") == 0,
        "access operations");
    ok &= expect(
        !dluma_crash_core_exception_is_reportable(0x80000003UL) &&
        !dluma_crash_core_exception_is_reportable(0x80000004UL) &&
        !dluma_crash_core_exception_is_reportable(0x40010006UL) &&
        !dluma_crash_core_exception_is_reportable(0x4001000AUL) &&
        !dluma_crash_core_exception_is_reportable(0x406D1388UL) &&
        dluma_crash_core_exception_is_reportable(0xC0000005UL) &&
        dluma_crash_core_exception_is_reportable(0xC00000FDUL),
        "filtered debug exceptions");
    ok &= expect(
        dluma_crash_core_report_name_matches(
            L"dluma_crash_session_20260728T190102003Z_pid-42.log") &&
        dluma_crash_core_report_name_matches(
            L"dluma_crash_session_20260728T190102003Z_pid-42-1.log") &&
        !dluma_crash_core_report_name_matches(
            L"DLUMA_CRASH_SESSION_20260728T190102003Z_pid-42.log") &&
        !dluma_crash_core_report_name_matches(
            L"dluma_crash_session_20260728T190102003Z_pid-.log") &&
        !dluma_crash_core_report_name_matches(L"unrelated.log"),
        "exact report names");
    ok &= expect(
        dluma_crash_core_address_in_module(
            0x1010ULL, 0x1000ULL, 0x100UL) &&
        !dluma_crash_core_address_in_module(
            0x1100ULL, 0x1000ULL, 0x100UL) &&
        !dluma_crash_core_address_in_module(
            0ULL, ~0ULL - 4ULL, 16UL),
        "module range mapping");
    memset(&module_identity, 0, sizeof(module_identity));
    module_identity.mapped = 1;
    module_identity.basename = L"gamedll_x64_rwdi.dll";
    module_identity.pe_timestamp = 0x623397AAUL;
    module_identity.image_size = 0x02082000UL;
    module_identity.sha256 =
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
    module_identity.sha256_available = 1;
    module_classification = dluma_crash_core_classify_module(
        &module_identity, game_profiles,
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0])));
    ok &= expect(
        module_classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_GAME_MODULE &&
        module_classification.first_party &&
        module_classification.identity_validated &&
        strcmp(module_classification.profile,
               "dying_light_1_49_gamedll") == 0,
        "exact game DLL identity");
    module_identity.sha256 =
        "0BC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
    module_classification = dluma_crash_core_classify_module(
        &module_identity, game_profiles,
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0])));
    ok &= expect(
        module_classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_UNKNOWN_LOADED_MODULE &&
        !module_classification.first_party &&
        !module_classification.identity_validated,
        "same filename with wrong hash is unknown");
    memset(&module_identity, 0, sizeof(module_identity));
    module_identity.mapped = 1;
    module_identity.loader_module = 1;
    module_identity.basename = L"dluma.mod";
    module_classification = dluma_crash_core_classify_module(
        &module_identity, game_profiles,
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0])));
    ok &= expect(
        module_classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_LOADER_MODULE &&
        module_classification.identity_validated &&
        !module_classification.first_party,
        "loader module classification");
    memset(&module_identity, 0, sizeof(module_identity));
    module_identity.mapped = 1;
    module_identity.windows_system_module = 1;
    module_identity.basename = L"kernel32.dll";
    module_classification = dluma_crash_core_classify_module(
        &module_identity, game_profiles,
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0])));
    ok &= expect(
        module_classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_WINDOWS_SYSTEM_MODULE &&
        module_classification.identity_validated &&
        !module_classification.first_party,
        "Windows system module classification");
    memset(&module_identity, 0, sizeof(module_identity));
    module_identity.mapped = 1;
    module_identity.known_third_party_module = 1;
    module_identity.basename = L"gameoverlayrenderer64.dll";
    module_classification = dluma_crash_core_classify_module(
        &module_identity, game_profiles,
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0])));
    ok &= expect(
        module_classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_KNOWN_THIRD_PARTY_MODULE &&
        !module_classification.identity_validated &&
        strcmp(dluma_crash_core_module_class_text(
                   module_classification.module_class),
               "KnownThirdPartyModule") == 0,
        "known third-party module classification");
    module_classification = dluma_crash_core_classify_module(
        0, game_profiles,
        (unsigned int)(sizeof(game_profiles) / sizeof(game_profiles[0])));
    ok &= expect(
        module_classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_UNMAPPED_ADDRESS &&
        strcmp(dluma_crash_core_module_class_text(
                   module_classification.module_class),
               "UnmappedAddress") == 0,
        "unmapped address classification");
    ok &= expect(
        dluma_crash_core_unwind_step(1, 10, 2, 18) ==
            DLUMA_CRASH_CORE_UNWIND_PROGRESS &&
        dluma_crash_core_unwind_step(1, 10, 0, 0) ==
            DLUMA_CRASH_CORE_UNWIND_COMPLETE &&
        dluma_crash_core_unwind_step(1, 10, 1, 10) ==
            DLUMA_CRASH_CORE_UNWIND_NON_PROGRESS &&
        dluma_crash_core_unwind_step(1, 10, 2, 9) ==
            DLUMA_CRASH_CORE_UNWIND_STACK_REGRESSED,
        "unwind progress");
    ok &= expect(
        dluma_crash_core_format_hex(
            0x1234ABCDEFULL, 16, hex, sizeof(hex)) &&
        strcmp(hex, "0x0000001234ABCDEF") == 0 &&
        !dluma_crash_core_format_hex(
            0, 16, hex, 8),
        "register hex formatting");
    ok &= expect(
        strcmp(dluma_crash_core_value_or_unavailable(0),
               "<unavailable>") == 0 &&
        strcmp(dluma_crash_core_value_or_unavailable(""),
               "<unavailable>") == 0 &&
        strcmp(dluma_crash_core_value_or_unavailable("ready"),
               "ready") == 0,
        "unavailable values");
    ok &= expect(
        dluma_crash_core_report_can_append(0, 80, 100, 20) &&
        !dluma_crash_core_report_can_append(1, 80, 100, 20) &&
        !dluma_crash_core_report_can_append(101, 1, 100, 20),
        "report truncation boundary");
    ok &= expect(
        dluma_crash_core_format_frame_line(
            "stack_frame", 0, 0x7FF612341010ULL,
            "C:\\Mods\\\xE6\xB5\x8B\xE8\xAF\x95\\loader.mod",
            0x7FF612340000ULL, frame0, sizeof(frame0)) &&
        dluma_crash_core_format_frame_line(
            "stack_frame", 1, 0x7FF700001234ULL,
            "X:\\Dying Light\\engine_x64_rwdi.dll",
            0x7FF700000000ULL, frame1, sizeof(frame1)),
        "full Unicode frame paths");
    frames[0] = 0;
    ok &= expect(
        dluma_crash_core_append(frames, sizeof(frames), frame0) &&
        dluma_crash_core_append(frames, sizeof(frames), frame1) &&
        strstr(frames, "stack_frame index=0") <
            strstr(frames, "stack_frame index=1") &&
        strstr(frames, "\xE6\xB5\x8B\xE8\xAF\x95") != 0,
        "frame order and Unicode preservation");

    primary_written = dluma_crash_core_write_all(
        fake_write, &primary, payload,
        (unsigned long)strlen(payload), 8);
    secondary_written = dluma_crash_core_write_all(
        fake_write, &secondary, payload,
        (unsigned long)strlen(payload), 8);
    ok &= expect(
        primary_written == strlen(payload) &&
        primary.used == strlen(payload) &&
        memcmp(primary.output, payload, strlen(payload)) == 0,
        "partial writes complete");
    ok &= expect(
        secondary_written == 8UL &&
        secondary.used == 8UL &&
        primary.used == strlen(payload),
        "one mirror failing does not stop the other");

    if (!ok) return 1;
    puts("crash core fake harness passed");
    return 0;
}
