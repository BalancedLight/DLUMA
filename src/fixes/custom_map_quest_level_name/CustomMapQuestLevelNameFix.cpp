#define _HAS_EXCEPTIONS 0

#include "CustomMapQuestLevelNameFix.h"
#include "../MemoryPatch.h"

extern "C" {
#include "../../dlw_common.h"
}

#include <cstring>
#include <new>

namespace dluma::fixes::custom_map_quest_level_name {
namespace {

constexpr SupportedBuild kSupportedBuilds[] = {
    {0x623397AA, 0x02082000, kPatchRva, kDestinationRva,
     {0xE8, 0x93, 0x05, 0x00, 0x00}},
};

constexpr uint8_t kContextBefore[] = {
    0x48, 0x8B, 0x56, 0x50, 0x48, 0x8D, 0x4E, 0x50, 0x48, 0x8B, 0xD8};
constexpr uint8_t kContextAfter[] = {
    0x48, 0x89, 0x6E, 0x50, 0x89, 0x6E, 0x58, 0x3B, 0xED, 0x75, 0x0D};

alignas(MemoryPatch) unsigned char g_patch_storage[sizeof(MemoryPatch)];
MemoryPatch* g_patch;
FixStatus g_status = {
    kFixId, kFixDisplayName, FixState::UnsupportedBuild, 0, kPatchRva, false};

MemoryPatch& Patch()
{
    if (!g_patch) {
        g_patch = new (g_patch_storage) MemoryPatch{0, {}, {}, false};
    }
    return *g_patch;
}

FixStatus MakeStatus(FixState state, uintptr_t address, bool owned)
{
    FixStatus status = {
        kFixId, kFixDisplayName, state, address, kPatchRva, owned};
    g_status = status;
    return status;
}

const SupportedBuild* FindBuild(const RuntimeContext& context)
{
    for (const SupportedBuild& build : kSupportedBuilds) {
        if (build.timestamp == context.gamedll_timestamp &&
            build.size_of_image == context.gamedll_size_of_image) {
            return &build;
        }
    }
    return nullptr;
}

bool ReadExact(uintptr_t address, const uint8_t* expected, size_t size)
{
    return address && expected && size &&
           dluma_is_readable(reinterpret_cast<const void*>(address), size) &&
           memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
}

void FormatBytes(const uint8_t* bytes, size_t count, char* output,
                 unsigned int capacity)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t index;
    unsigned int cursor = 0;
    if (!output || !capacity) return;
    output[0] = 0;
    for (index = 0; index < count && cursor + 3 < capacity; ++index) {
        if (index) output[cursor++] = ' ';
        output[cursor++] = digits[(bytes[index] >> 4) & 0x0F];
        output[cursor++] = digits[bytes[index] & 0x0F];
    }
    output[cursor] = 0;
}

bool ImageContainsPatch(const RuntimeContext& context,
                        const SupportedBuild& build)
{
    const uintptr_t image_size = context.gamedll_size_of_image;
    return context.gamedll_base &&
           build.patch_rva <= image_size &&
           sizeof(build.original_bytes) <= image_size - build.patch_rva &&
           build.destination_rva < image_size;
}

}  // namespace

std::array<uint8_t, 5> BuildRelativeJump(uintptr_t patch_address,
                                         uintptr_t destination_address)
{
    std::array<uint8_t, 5> replacement = {0xE9, 0, 0, 0, 0};
    const int64_t delta = static_cast<int64_t>(destination_address) -
                          static_cast<int64_t>(patch_address + 5);
    const int32_t displacement = static_cast<int32_t>(delta);
    memcpy(&replacement[1], &displacement, sizeof(displacement));
    return replacement;
}

FixStatus Initialize(const RuntimeContext& context, bool enabled)
{
    const SupportedBuild* build;
    uintptr_t patch_address;
    uintptr_t destination_address;
    std::array<uint8_t, 5> replacement;
    uint8_t actual[5] = {};
    char expected_text[32];
    char actual_text[32];
    MemoryPatch& patch = Patch();

    if (!enabled) {
        dluma_log_line("FIX", "custom_map_quest_level_name: disabled by configuration");
        patch.applied_by_us = false;
        return MakeStatus(FixState::Disabled, 0, false);
    }

    dluma_log_line("FIX", "custom_map_quest_level_name: initialization started");
    if (context.gamedll_timestamp)
        dluma_log_hex("FIX", "gamedll timestamp", context.gamedll_timestamp);
    if (context.gamedll_size_of_image)
        dluma_log_hex("FIX", "gamedll SizeOfImage",
                    context.gamedll_size_of_image);

    build = FindBuild(context);
    if (!context.gamedll_module || !context.gamedll_base || !build ||
        !ImageContainsPatch(context, *build)) {
        dluma_log_line("WARN", "custom_map_quest_level_name: unsupported gamedll build");
        dluma_log_line("WARN", "fix skipped; loader will continue");
        return MakeStatus(FixState::UnsupportedBuild, 0, false);
    }

    patch_address = context.gamedll_base + build->patch_rva;
    destination_address = context.gamedll_base + build->destination_rva;
    dluma_log_hex("FIX", "patch RVA", static_cast<DWORD>(build->patch_rva));
    dluma_log_hex("FIX", "destination RVA",
                static_cast<DWORD>(build->destination_rva));

    /* These neighboring instructions were identical in both supplied DLLs.
     * They make the fingerprint gate stronger without global signature scans. */
    if (!ReadExact(patch_address - sizeof(kContextBefore), kContextBefore,
                   sizeof(kContextBefore)) ||
        !ReadExact(patch_address + build->original_bytes.size(), kContextAfter,
                   sizeof(kContextAfter))) {
        dluma_log_line("WARN", "custom_map_quest_level_name: unsupported gamedll build");
        dluma_log_line("WARN", "fix skipped; loader will continue");
        return MakeStatus(FixState::UnsupportedBuild, patch_address, false);
    }

    replacement = BuildRelativeJump(patch_address, destination_address);
    patch.address = patch_address;
    patch.expected_original.assign(build->original_bytes.begin(),
                                   build->original_bytes.end());
    patch.replacement.assign(replacement.begin(), replacement.end());
    patch.applied_by_us = false;

    if (!dluma_is_readable(reinterpret_cast<const void*>(patch_address),
                         sizeof(actual))) {
        dluma_log_line("WARN", "custom_map_quest_level_name: target bytes were unreadable");
        dluma_log_line("WARN", "fix skipped; loader will continue");
        return MakeStatus(FixState::ApplyFailed, patch_address, false);
    }
    memcpy(actual, reinterpret_cast<const void*>(patch_address), sizeof(actual));

    if (memcmp(actual, build->original_bytes.data(), sizeof(actual)) == 0) {
        dluma_log_line("FIX", "original bytes matched");
    } else if (memcmp(actual, replacement.data(), sizeof(actual)) != 0) {
        FormatBytes(build->original_bytes.data(), build->original_bytes.size(),
                    expected_text, DLUMA_ARRAY_COUNT(expected_text));
        FormatBytes(actual, sizeof(actual), actual_text,
                    DLUMA_ARRAY_COUNT(actual_text));
        dluma_log_line("WARN", "custom_map_quest_level_name: original bytes did not match");
        dluma_log_pair("WARN", "expected", expected_text);
        dluma_log_pair("WARN", "actual  ", actual_text);
        dluma_log_line("WARN", "fix skipped; loader will continue");
        return MakeStatus(FixState::OriginalBytesMismatch, patch_address, false);
    }

    switch (ApplyValidatedPatch(patch)) {
    case PatchResult::Applied:
        dluma_log_line("FIX", "custom-map QUEST LevelName crash fix applied");
        return MakeStatus(FixState::Applied, patch_address, true);
    case PatchResult::AlreadyApplied:
        dluma_log_line("FIX", "custom_map_quest_level_name: replacement bytes already present");
        dluma_log_pair("FIX", "patch ownership", "external");
        return MakeStatus(FixState::AlreadyApplied, patch_address, false);
    case PatchResult::OriginalBytesMismatch:
        dluma_log_line("WARN", "custom_map_quest_level_name: original bytes did not match");
        dluma_log_line("WARN", "fix skipped; loader will continue");
        return MakeStatus(FixState::OriginalBytesMismatch, patch_address, false);
    default:
        dluma_log_line("WARN", "custom_map_quest_level_name: patch application failed");
        dluma_log_line("WARN", "fix skipped; loader will continue");
        return MakeStatus(FixState::ApplyFailed, patch_address,
                          patch.applied_by_us);
    }
}

FixStatus Shutdown()
{
    MemoryPatch& patch = Patch();
    if (!patch.applied_by_us) return g_status;

    switch (RestoreValidatedPatch(patch)) {
    case PatchResult::Restored:
        dluma_log_line("FIX", "custom_map_quest_level_name: original bytes restored");
        return MakeStatus(FixState::Restored, patch.address, false);
    case PatchResult::ModifiedAfterApply:
        dluma_log_line("WARN", "custom_map_quest_level_name: patch bytes changed after application; restoration skipped");
        return MakeStatus(FixState::ApplyFailed, patch.address, false);
    default:
        dluma_log_line("WARN", "custom_map_quest_level_name: original-byte restoration failed");
        return MakeStatus(FixState::ApplyFailed, patch.address,
                          patch.applied_by_us);
    }
}

}  // namespace dluma::fixes::custom_map_quest_level_name
