#ifndef DLUMA_FIXES_CUSTOM_MAP_QUEST_LEVEL_NAME_FIX_H
#define DLUMA_FIXES_CUSTOM_MAP_QUEST_LEVEL_NAME_FIX_H

#include "../FixRegistry.h"

#include <array>
#include <cstdint>

namespace dluma::fixes::custom_map_quest_level_name {

static constexpr const char* kFixId = "custom_map_quest_level_name";
static constexpr const char* kFixDisplayName =
    "Custom Map QUEST LevelName Crash Fix";

static constexpr uintptr_t kFunctionRva = 0x002D2F70;
static constexpr uintptr_t kPatchRva = 0x002D2FC8;
static constexpr uintptr_t kDestinationRva = 0x002D2FE5;

struct SupportedBuild {
    uint32_t timestamp;
    uint32_t size_of_image;
    uintptr_t patch_rva;
    uintptr_t destination_rva;
    std::array<uint8_t, 5> original_bytes;
};

std::array<uint8_t, 5> BuildRelativeJump(uintptr_t patch_address,
                                         uintptr_t destination_address);
FixStatus Initialize(const RuntimeContext& context, bool enabled);
FixStatus Shutdown();

}  // namespace dluma::fixes::custom_map_quest_level_name

#endif
