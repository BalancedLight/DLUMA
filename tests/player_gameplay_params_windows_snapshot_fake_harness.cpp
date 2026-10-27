#include "fixes/custom_map_asset_isolation/PlayerGameplayParamsWindowsSnapshot.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace player =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart;

namespace {

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,  \
                         #expression);                                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

constexpr std::uintptr_t kGamedllBase = 0x0000000180000000ULL;
constexpr std::uintptr_t kState = 0x0000000280000000ULL;
constexpr std::uintptr_t kRows = 0x0000000280010000ULL;
constexpr std::uintptr_t kPlayerVariables = 0x0000000280020000ULL;
constexpr std::uintptr_t kProgression = 0x0000000280030000ULL;
constexpr std::uintptr_t kRegularBuffs = 0x0000000280040000ULL;
constexpr std::uintptr_t kTimedBuffs = 0x0000000280050000ULL;
constexpr std::uintptr_t kSkillNode1Storage = 0x0000000280060000ULL;
constexpr std::uintptr_t kSkillNode2Storage = 0x0000000280060100ULL;
constexpr std::uintptr_t kSkillNode1 = kSkillNode1Storage + 16U;
constexpr std::uintptr_t kSkillNode2 = kSkillNode2Storage + 16U;
constexpr std::uintptr_t kDefinitionHeaders = 0x0000000280070000ULL;
constexpr std::uintptr_t kDefinitionData = 0x0000000280080000ULL;
constexpr std::uintptr_t kDefinitionProperties = 0x0000000280088000ULL;
constexpr std::uintptr_t kGameplayContainer = 0x0000000280090000ULL;
constexpr std::uintptr_t kGameDi = 0x00000002800A0000ULL;
constexpr std::uintptr_t kSession = 0x00000002800B0000ULL;
constexpr std::uintptr_t kActiveLevel = 0x00000002800C0000ULL;
constexpr std::size_t kFixtureDefinitionLevelCapacity = 512U;

struct FakeMemory {
    unsigned char state[player::kWindowsPlayerStateSize];
    unsigned char rows[player::kWindowsPlayerLogicalTypeCount *
                       player::kWindowsPlayerRowStride];
    unsigned char regular_buffs[8];
    unsigned char timed_buffs[48];
    unsigned char skill_node1[40];
    unsigned char skill_node2[40];
    unsigned char definition_headers[
        player::kWindowsPlayerLogicalTypeCount][16];
    unsigned char definition_levels[
        kFixtureDefinitionLevelCapacity *
        player::kWindowsPlayerDefinitionLevelStride];
    unsigned char definition_properties[
        kFixtureDefinitionLevelCapacity *
        player::kWindowsPlayerDefinitionPropertyStride];
    unsigned char game_di[0x510];
    unsigned char session[0xB8];
    char progression[32];
    std::uintptr_t gameplay_container_storage;
    std::uintptr_t gameplay_container_global;
    std::uintptr_t game_di_global;
    std::uintptr_t local_state_global;
    std::uintptr_t character_state_global;
    unsigned char player_variables_byte;
    std::uintptr_t unreadable_address;
    std::uintptr_t changing_header_address;
    std::uint32_t direct_skill_count_reads;
    std::uint32_t changing_header_reads;
    bool change_skill_count;
    bool change_definition_header;
    bool overflow_read_attempted;
};

template <typename T>
void Store(unsigned char* bytes, std::size_t offset, T value) noexcept
{
    std::memcpy(bytes + offset, &value, sizeof(value));
}

template <typename T>
T Load(const unsigned char* bytes, std::size_t offset) noexcept
{
    T value = {};
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

bool CopyRegion(std::uintptr_t address, void* output, std::size_t size,
                std::uintptr_t base, const void* region,
                std::size_t region_size) noexcept
{
    if (address < base || address > static_cast<std::uintptr_t>(-1) - size)
        return false;
    const std::uintptr_t end = address + size;
    if (end > base + region_size) return false;
    std::memcpy(output,
                static_cast<const unsigned char*>(region) +
                    static_cast<std::size_t>(address - base),
                size);
    return true;
}

bool ReadFakeMemory(void* user, std::uintptr_t address, void* output,
                    std::size_t size) noexcept
{
    FakeMemory* memory = static_cast<FakeMemory*>(user);
    if (!memory || !output || !size)
        return false;
    if (memory->unreadable_address &&
        address <= memory->unreadable_address &&
        size > memory->unreadable_address - address)
        return false;
    if (address > static_cast<std::uintptr_t>(-1) - (size - 1U)) {
        memory->overflow_read_attempted = true;
        return false;
    }
    if (memory->change_skill_count &&
        address == kState + player::kWindowsPlayerStateSkillCountOffset &&
        size == sizeof(std::uint32_t)) {
        std::uint32_t count = Load<std::uint32_t>(
            memory->state, player::kWindowsPlayerStateSkillCountOffset);
        if (memory->direct_skill_count_reads++ != 0U) ++count;
        std::memcpy(output, &count, sizeof(count));
        return true;
    }
    if (memory->change_definition_header &&
        address == memory->changing_header_address &&
        size == 16U) {
        unsigned char header[16] = {};
        if (!CopyRegion(
                address, header, sizeof(header), kDefinitionHeaders,
                memory->definition_headers,
                sizeof(memory->definition_headers)) &&
            !CopyRegion(
                address, header, sizeof(header), kDefinitionData,
                memory->definition_levels,
                sizeof(memory->definition_levels)))
            return false;
        if (memory->changing_header_reads++ != 0U) {
            const std::uint32_t capacity =
                Load<std::uint32_t>(header, 12U) + 1U;
            Store(header, 12U, capacity);
        }
        std::memcpy(output, header, sizeof(header));
        return true;
    }
    if (CopyRegion(address, output, size, kState, memory->state,
                   sizeof(memory->state)) ||
        CopyRegion(address, output, size, kRows, memory->rows,
                   sizeof(memory->rows)) ||
        CopyRegion(address, output, size, kRegularBuffs,
                   memory->regular_buffs,
                   sizeof(memory->regular_buffs)) ||
        CopyRegion(address, output, size, kTimedBuffs,
                   memory->timed_buffs,
                   sizeof(memory->timed_buffs)) ||
        CopyRegion(address, output, size, kSkillNode1Storage,
                   memory->skill_node1, sizeof(memory->skill_node1)) ||
        CopyRegion(address, output, size, kSkillNode2Storage,
                   memory->skill_node2, sizeof(memory->skill_node2)) ||
        CopyRegion(address, output, size, kDefinitionHeaders,
                    memory->definition_headers,
                    sizeof(memory->definition_headers)) ||
        CopyRegion(address, output, size, kDefinitionData,
                    memory->definition_levels,
                    sizeof(memory->definition_levels)) ||
        CopyRegion(address, output, size, kDefinitionProperties,
                    memory->definition_properties,
                    sizeof(memory->definition_properties)) ||
        CopyRegion(address, output, size, kGameDi, memory->game_di,
                   sizeof(memory->game_di)) ||
        CopyRegion(address, output, size, kSession, memory->session,
                   sizeof(memory->session)) ||
        CopyRegion(address, output, size, kProgression,
                   memory->progression, sizeof(memory->progression)) ||
        CopyRegion(address, output, size, kGameplayContainer,
                   &memory->gameplay_container_storage,
                   sizeof(memory->gameplay_container_storage)) ||
        CopyRegion(address, output, size, kPlayerVariables,
                   &memory->player_variables_byte,
                   sizeof(memory->player_variables_byte)))
        return true;

    const std::uintptr_t* global = nullptr;
    if (address == kGamedllBase +
            player::kWindowsGameplayParamsContainerGlobalRva)
        global = &memory->gameplay_container_global;
    else if (address == kGamedllBase + player::kWindowsGameDiGlobalRva)
        global = &memory->game_di_global;
    else if (address == kGamedllBase +
                 player::kWindowsLocalPlayerStateGlobalRva)
        global = &memory->local_state_global;
    else if (address == kGamedllBase +
                 player::kWindowsCharacterPlayerStateGlobalRva)
        global = &memory->character_state_global;
    if (!global || size != sizeof(*global)) return false;
    std::memcpy(output, global, size);
    return true;
}

void ConfigureDefinitionLevelCounts(
    FakeMemory* memory,
    const std::uint32_t counts[
        player::kWindowsPlayerLogicalTypeCount]) noexcept
{
    std::memset(
        memory->definition_levels, 0,
        sizeof(memory->definition_levels));
    std::memset(
        memory->definition_properties, 0,
        sizeof(memory->definition_properties));
    std::uint32_t level_ordinal = 0;
    for (std::size_t row_index = 0;
         row_index < player::kWindowsPlayerLogicalTypeCount;
         ++row_index) {
        const std::uint32_t count = counts[row_index];
        const std::uintptr_t level_data =
            kDefinitionData +
            static_cast<std::uintptr_t>(level_ordinal) *
                player::kWindowsPlayerDefinitionLevelStride;
        Store(memory->definition_headers[row_index], 0U, level_data);
        Store(memory->definition_headers[row_index], 8U, count);
        Store(memory->definition_headers[row_index], 12U, count);
        for (std::uint32_t level_index = 0;
             level_index < count; ++level_index) {
            const std::uint32_t ordinal = level_ordinal + level_index;
            unsigned char* level =
                memory->definition_levels +
                static_cast<std::size_t>(ordinal) *
                    player::kWindowsPlayerDefinitionLevelStride;
            const std::uintptr_t property_data =
                kDefinitionProperties +
                static_cast<std::uintptr_t>(ordinal) *
                    player::kWindowsPlayerDefinitionPropertyStride;
            Store(level, 0U, property_data);
            Store(level, 8U, static_cast<std::uint32_t>(1U));
            Store(level, 12U, static_cast<std::uint32_t>(1U));
            unsigned char* property =
                memory->definition_properties +
                static_cast<std::size_t>(ordinal) *
                    player::kWindowsPlayerDefinitionPropertyStride;
            Store(
                property, 0U,
                static_cast<std::uint32_t>(
                    700U + row_index * 100U + level_index));
            Store(
                property, 4U,
                static_cast<std::uint32_t>(
                    0x3F800000U + ordinal));
            Store(property, 8U, static_cast<std::uint8_t>(0U));
        }
        level_ordinal += count;
    }
}

void InitializeSkillNode(unsigned char* node, std::uint16_t key,
                         std::uintptr_t left, std::uintptr_t parent,
                         std::uintptr_t right, std::uint32_t value) noexcept
{
    std::memset(node, 0, 40);
    Store(node, 0U, key);
    Store(node, 4U, key);
    Store(node, 6U, static_cast<std::uint16_t>(1U));
    Store(node, 8U, value);
    Store(node, 12U, value + 1U);
    Store(node, 16U, left);
    Store(node, 24U, parent);
    Store(node, 32U, right);
}

void InitializeFake(FakeMemory* memory) noexcept
{
    std::memset(memory, 0, sizeof(*memory));
    Store(memory->state, 0x00U,
          kGamedllBase + player::kWindowsPlayerStatePrimaryVtableRva);
    Store(memory->state, 0x08U,
          kGamedllBase + player::kWindowsPlayerStateVtable8Rva);
    Store(memory->state, 0x10U,
          kGamedllBase + player::kWindowsPlayerStateVtable16Rva);
    Store(memory->state, 0x30U,
          kGamedllBase + player::kWindowsPlayerStateVtable48Rva);
    Store(memory->state, player::kWindowsPlayerStateRowTableOffset, kRows);

    std::strcpy(memory->progression, "default_levels.xml");
    const std::uint32_t progression_length =
        static_cast<std::uint32_t>(std::strlen(memory->progression));
    Store(memory->state,
          player::kWindowsPlayerStateProgressionSelectorOffset,
          kProgression);
    Store(memory->state,
          player::kWindowsPlayerStateProgressionSelectorOffset + 8U,
          progression_length);
    Store(memory->state,
          player::kWindowsPlayerStateProgressionSelectorOffset + 12U,
          progression_length);
    Store(memory->state,
          player::kWindowsPlayerStateClassSelectorOffset,
          static_cast<std::uintptr_t>(0));
    Store(memory->state,
          player::kWindowsPlayerStateClassSelectorOffset + 8U,
          static_cast<std::uint32_t>(0));
    Store(memory->state,
          player::kWindowsPlayerStateClassSelectorOffset + 12U,
          static_cast<std::uint32_t>(0));
    Store(memory->state, player::kWindowsPlayerStatePlayerVariablesOffset,
          kPlayerVariables);

    Store(memory->state, player::kWindowsPlayerStateSkillRootOffset,
          kSkillNode1);
    Store(memory->state, player::kWindowsPlayerStateSkillCountOffset,
          static_cast<std::uint32_t>(2));
    InitializeSkillNode(memory->skill_node1, 10U, kSkillNode2, 0, 0,
                        0x11111111U);
    InitializeSkillNode(memory->skill_node2, 5U, 0, kSkillNode1, 0,
                        0x22222222U);

    Store(memory->state, player::kWindowsPlayerStateRegularBuffOffset,
          kRegularBuffs);
    Store(memory->state,
          player::kWindowsPlayerStateRegularBuffOffset + 8U,
          static_cast<std::uint32_t>(2));
    Store(memory->state,
          player::kWindowsPlayerStateRegularBuffOffset + 12U,
          static_cast<std::uint32_t>(2));
    Store(memory->regular_buffs, 0U, static_cast<std::uint16_t>(1));
    Store(memory->regular_buffs, 2U, static_cast<std::uint16_t>(2));
    Store(memory->regular_buffs, 4U, static_cast<std::uint16_t>(3));
    Store(memory->regular_buffs, 6U, static_cast<std::uint16_t>(4));

    Store(memory->state, player::kWindowsPlayerStateTimedBuffOffset,
          kTimedBuffs);
    Store(memory->state,
          player::kWindowsPlayerStateTimedBuffOffset + 8U,
          static_cast<std::uint32_t>(2));
    Store(memory->state,
          player::kWindowsPlayerStateTimedBuffOffset + 12U,
          static_cast<std::uint32_t>(2));
    Store(memory->timed_buffs, 0U, static_cast<std::uint16_t>(5));
    Store(memory->timed_buffs, 2U, static_cast<std::uint16_t>(1));
    Store(memory->timed_buffs, 8U,
          static_cast<std::uintptr_t>(0x0000000281000000ULL));
    Store(memory->timed_buffs, 0x10U, 0x3FC00000U);
    Store(memory->timed_buffs, 0x18U,
          static_cast<std::uint16_t>(6));
    Store(memory->timed_buffs, 0x1AU,
          static_cast<std::uint16_t>(2));
    Store(memory->timed_buffs, 0x20U,
          static_cast<std::uintptr_t>(0));
    Store(memory->timed_buffs, 0x28U, 0x40200000U);

    const std::uint32_t definition_counts[
        player::kWindowsPlayerLogicalTypeCount] = {
            3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    ConfigureDefinitionLevelCounts(memory, definition_counts);
    for (std::size_t index = 0;
         index < player::kWindowsPlayerLogicalTypeCount; ++index) {
        unsigned char* row =
            memory->rows + index * player::kWindowsPlayerRowStride;
        const std::uintptr_t definition =
            kDefinitionHeaders + index * 16U;
        Store(row, 0x00U, definition);
        Store(row, 0x08U,
              static_cast<std::uint64_t>(1000U + index));
        Store(row, 0x10U,
              static_cast<std::uint32_t>(100U));
        Store(row, 0x14U,
              static_cast<std::uint16_t>(index + 1U));
        Store(row, 0x16U,
              static_cast<std::uint16_t>(20U + index));
        Store(row, 0x18U,
              static_cast<std::uint64_t>(0U));
    }

    for (std::size_t offset = 0x470U; offset <= 0x484U; offset += 4U)
        Store(memory->state, offset,
              static_cast<std::uint32_t>(offset));
    for (std::size_t index = 0;
         index < player::kWindowsPlayerLogicalTypeCount; ++index)
        Store(memory->state, 0x48CU + index * 4U,
              static_cast<std::uint32_t>(0x3F800000U + index));

    memory->gameplay_container_storage = kDefinitionHeaders;
    memory->gameplay_container_global = kGameplayContainer;
    memory->game_di_global = kGameDi;
    memory->local_state_global = kState;
    memory->character_state_global = kState + 0x1000U;
    memory->player_variables_byte = 0x42U;
    Store(memory->game_di, 0U,
          kGamedllBase + player::kWindowsGameDiVtableRva);
    Store(memory->game_di, player::kWindowsGameDiCurrentSessionOffset,
          static_cast<std::uintptr_t>(0));
    Store(memory->session, player::kWindowsSessionActiveLevelOffset,
          kActiveLevel);
}

player::PlayerGameplayParamsWindowsReadContext Context(
    FakeMemory* memory) noexcept
{
    player::PlayerGameplayParamsWindowsReadContext context = {};
    context.gamedll_base = kGamedllBase;
    context.read_memory = ReadFakeMemory;
    context.read_memory_user = memory;
    context.exact_gamedll_image_validated = true;
    return context;
}

bool HasReason(std::uint64_t mask,
               player::PlayerGameplayParamsWindowsSnapshotReason reason)
    noexcept
{
    return (mask & static_cast<std::uint64_t>(reason)) != 0;
}

bool TestEnvironmentSnapshot() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    auto context = Context(&memory);
    player::PlayerGameplayParamsWindowsEnvironmentSnapshot result = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsEnvironment(
        &context, &result));
    CHECK(result.observation_complete);
    CHECK(result.no_session_environment_ready);
    CHECK(result.gameplay_params_container == kGameplayContainer);
    CHECK(result.game_di == kGameDi);
    CHECK(result.current_session == 0);
    CHECK(result.active_level == 0);
    CHECK(result.local_process_global_state == kState);
    CHECK((result.complete_fields &
           player::kWindowsEnvironmentRequiredFields) ==
          player::kWindowsEnvironmentRequiredFields);

    Store(memory.game_di, player::kWindowsGameDiCurrentSessionOffset,
          kSession);
    CHECK(player::SnapshotPlayerGameplayParamsWindowsEnvironment(
        &context, &result));
    CHECK(!result.no_session_environment_ready);
    CHECK(result.current_session == kSession);
    CHECK(result.active_level == kActiveLevel);
    CHECK(HasReason(result.reason_mask,
                    player::WindowsSnapshotReasonActiveSessionPresent));
    CHECK(HasReason(result.reason_mask,
                    player::WindowsSnapshotReasonActiveLevelPresent));
    return true;
}

bool TestPostDestroyEnvironmentDoesNotDereferenceSession() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    auto context = Context(&memory);
    Store(memory.game_di, player::kWindowsGameDiCurrentSessionOffset,
          kSession);
    memory.unreadable_address =
        kSession + player::kWindowsSessionActiveLevelOffset;

    player::PlayerGameplayParamsWindowsEnvironmentSnapshot result = {};
    CHECK(!player::SnapshotPlayerGameplayParamsWindowsEnvironment(
        &context, &result));

    CHECK(player::SnapshotPlayerGameplayParamsWindowsEnvironment(
        &context, &result,
        player::PlayerGameplayParamsWindowsEnvironmentReadMode::
            PostDestroyProcessGlobals));
    CHECK(result.observation_complete);
    CHECK(!result.no_session_environment_ready);
    CHECK(result.current_session == kSession);
    CHECK(result.active_level == 0);
    CHECK(result.gameplay_params_container == kGameplayContainer);
    CHECK(result.game_di == kGameDi);
    CHECK(result.local_process_global_state == kState);
    CHECK(result.character_manager_global_state == kState + 0x1000U);
    CHECK(HasReason(result.reason_mask,
                    player::WindowsSnapshotReasonActiveSessionPresent));
    return true;
}

bool TestCompleteProvenStateSnapshot() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    auto context = Context(&memory);
    player::PlayerGameplayParamsWindowsStateSnapshot first = {};
    player::PlayerGameplayParamsWindowsStateSnapshot second = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 17U, &first));
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 17U, &second));
    CHECK(first.proven_fields_complete);
    CHECK(first.player_semantic_snapshot_complete);
    CHECK(!first.restart_snapshot_complete);
    CHECK(!first.player_restart_snapshot_complete);
    CHECK(first.unresolved_fields ==
          player::kWindowsPlayerStateUnresolvedFields);
    CHECK(HasReason(first.reason_mask,
                    player::WindowsSnapshotReasonInventoryOwnerUnresolved));
    CHECK(HasReason(first.reason_mask,
                    player::WindowsSnapshotReasonEquipmentOwnerUnresolved));
    CHECK(HasReason(first.reason_mask,
                    player::WindowsSnapshotReasonQuestOwnerUnresolved));
    CHECK(HasReason(first.reason_mask,
                    player::WindowsSnapshotReasonProviderIdentityUnresolved));
    CHECK(first.state == kState);
    CHECK(first.state_generation == 17U);
    CHECK(first.player_variables == kPlayerVariables);
    CHECK(first.progression_selector.valid);
    CHECK(first.progression_selector.length ==
          std::strlen("default_levels.xml"));
    CHECK(first.class_selector.valid);
    CHECK(first.class_selector.length == 0U);
    CHECK(first.skills.valid && first.skills.count == 2U);
    CHECK(first.regular_buffs.valid && first.regular_buffs.count == 2U);
    CHECK(first.timed_buffs.valid && first.timed_buffs.count == 2U);
    CHECK(first.timed_buffs.auxiliary_count == 1U);
    CHECK(first.appearance_values_fingerprint != 0);
    CHECK(first.skills.fingerprint == second.skills.fingerprint);
    CHECK(first.regular_buffs.fingerprint ==
          second.regular_buffs.fingerprint);
    CHECK(first.timed_buffs.fingerprint ==
          second.timed_buffs.fingerprint);
    for (std::size_t index = 0;
         index < player::kWindowsPlayerLogicalTypeCount; ++index) {
        CHECK(first.rows[index].preset ==
              kDefinitionHeaders + index * 16U);
        CHECK(first.rows[index].row_readable);
        CHECK(first.rows[index].definition_vector_header_readable);
        CHECK(first.rows[index].semantic_complete);
        CHECK(!first.rows[index].preset_absent);
        CHECK(first.rows[index].semantic_kind ==
              player::PlayerGameplayParamsWindowsRowSemanticKind::
                  PresetRow);
        CHECK(first.rows[index].header_read_result ==
              player::PlayerGameplayParamsWindowsRowHeaderReadResult::
                  Complete);
        CHECK(first.rows[index].header_validation_result ==
              player::PlayerGameplayParamsWindowsRowHeaderValidationResult::
                  Valid);
        CHECK(first.rows[index].earned_experience == 1000U + index);
        CHECK(first.rows[index].unknown_100 == 100U);
        CHECK(first.rows[index].current_level == index + 1U);
        CHECK(first.rows[index].maximum_level == 20U + index);
        CHECK(first.rows[index].trailing_qword == 0U);
        CHECK(first.rows[index].definition_level_count == 3U + index);
    }
    player::PlayerGameplayParamsWindowsStateSnapshot stable_first = {};
    player::PlayerGameplayParamsWindowsStateSnapshot stable_second = {};
    CHECK(player::dluma_capture_stable_main_menu_player_semantic_snapshot(
        &context, kState, 17U, &stable_first, &stable_second));
    CHECK(stable_first.player_semantic_snapshot_complete);
    CHECK(stable_second.player_semantic_snapshot_complete);
    return true;
}

bool TestExactWindowsRowContractAndAbsentPreset() noexcept
{
    CHECK(player::kWindowsPlayerRowStride == 0x20U);
    CHECK(player::IsSupportedWindowsPlayerRowStride(0x20U));
    CHECK(!player::IsSupportedWindowsPlayerRowStride(0x30U));

    FakeMemory memory;
    InitializeFake(&memory);
    constexpr std::size_t kAbsentRow = 3U;
    unsigned char* row =
        memory.rows + kAbsentRow * player::kWindowsPlayerRowStride;
    Store(row, player::kWindowsPlayerRowPresetOffset,
          static_cast<std::uintptr_t>(0));
    auto context = Context(&memory);
    player::PlayerGameplayParamsWindowsStateSnapshot result = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 22U, &result));
    CHECK(result.first_failed_row_index == UINT32_MAX);
    CHECK(result.rows[kAbsentRow].preset == 0U);
    CHECK(result.rows[kAbsentRow].preset_absent);
    CHECK(result.rows[kAbsentRow].semantic_complete);
    CHECK(result.rows[kAbsentRow].definition_vector_header_readable);
    CHECK(result.rows[kAbsentRow].semantic_kind ==
          player::PlayerGameplayParamsWindowsRowSemanticKind::
              AbsentPresetRow);
    CHECK(result.rows[kAbsentRow].header_read_result ==
          player::PlayerGameplayParamsWindowsRowHeaderReadResult::
              AbsentPresetNotApplicable);
    CHECK(result.rows[kAbsentRow].header_validation_result ==
          player::PlayerGameplayParamsWindowsRowHeaderValidationResult::
              AbsentPresetAccepted);
    CHECK(std::strcmp(
              player::PlayerGameplayParamsWindowsRowSemanticKindName(
                  result.rows[kAbsentRow].semantic_kind),
              "absent_preset_row") == 0);
    CHECK(std::strcmp(
              player::PlayerGameplayParamsWindowsRowHeaderReadResultName(
                  result.rows[kAbsentRow].header_read_result),
              "absent_preset_not_applicable") == 0);
    CHECK(std::strcmp(
              player::
                  PlayerGameplayParamsWindowsRowHeaderValidationResultName(
                      result.rows[kAbsentRow].
                          header_validation_result),
              "absent_preset_accepted") == 0);
    return true;
}

bool TestExactWindowsDefinitionTraversalAndRuntime11Rows() noexcept
{
    CHECK(player::kWindowsPlayerDefinitionLevelStride == 0x28U);
    CHECK(player::kWindowsPlayerDefinitionPropertyStride == 0x20U);
    CHECK(player::IsSupportedWindowsPlayerDefinitionLevelStride(0x28U));
    CHECK(!player::IsSupportedWindowsPlayerDefinitionLevelStride(0x30U));
    CHECK(player::IsSupportedWindowsPlayerDefinitionPropertyStride(0x20U));
    CHECK(!player::IsSupportedWindowsPlayerDefinitionPropertyStride(0x18U));

    FakeMemory memory;
    InitializeFake(&memory);
    auto context = Context(&memory);
    player::PlayerDefinitionSemanticSnapshot definition = {};
    CHECK(player::CaptureWindowsPlayerDefinitionSemantics(
        &context, kDefinitionHeaders, 0U, &definition));
    CHECK(definition.complete);
    CHECK(definition.level_count == 3U);
    CHECK(definition.property_count == 3U);
    CHECK(definition.semantic_fingerprint != 0U);
    CHECK(definition.evidence.configured_level_stride == 0x28U);
    CHECK(definition.evidence.configured_property_stride == 0x20U);
    CHECK(definition.evidence.first_failure ==
          player::PlayerDefinitionTraversalFailure::None);

    const std::uint32_t runtime11_counts[
        player::kWindowsPlayerLogicalTypeCount] = {
            60U, 24U, 24U, 25U, 6U, 250U, 24U, 13U};
    ConfigureDefinitionLevelCounts(&memory, runtime11_counts);
    player::PlayerGameplayParamsWindowsStateSnapshot snapshot = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 31U, &snapshot));
    CHECK(snapshot.player_semantic_snapshot_complete);
    CHECK(!snapshot.player_restart_snapshot_complete);
    for (std::size_t index = 0;
         index < player::kWindowsPlayerLogicalTypeCount; ++index) {
        CHECK(snapshot.rows[index].definition_level_count ==
              runtime11_counts[index]);
        CHECK(snapshot.rows[index].definition_property_count ==
              runtime11_counts[index]);
        CHECK(snapshot.rows[index].definition_fingerprint != 0U);
        CHECK(snapshot.rows[index].definition_traversal.
                  configured_level_stride == 0x28U);
        CHECK(snapshot.rows[index].definition_traversal.
                  configured_property_stride == 0x20U);
    }

    InitializeFake(&memory);
    unsigned char* first_level = memory.definition_levels;
    Store(first_level, 8U, static_cast<std::uint32_t>(2U));
    Store(first_level, 12U, static_cast<std::uint32_t>(2U));
    CHECK(player::CaptureWindowsPlayerDefinitionSemantics(
        &context, kDefinitionHeaders, 0U, &definition));
    CHECK(definition.property_count == 4U);
    const std::uint64_t before = definition.semantic_fingerprint;
    Store(
        memory.definition_properties,
        player::kWindowsPlayerDefinitionPropertyStride + 4U,
        static_cast<std::uint32_t>(0x40400000U));
    CHECK(player::CaptureWindowsPlayerDefinitionSemantics(
        &context, kDefinitionHeaders, 0U, &definition));
    CHECK(definition.semantic_fingerprint != before);
    return true;
}

bool TestDefinitionTraversalFailureEvidence() noexcept
{
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(
            memory.definition_headers[2], 8U,
            static_cast<std::uint32_t>(5U));
        Store(
            memory.definition_headers[2], 12U,
            static_cast<std::uint32_t>(4U));
        auto context = Context(&memory);
        player::PlayerDefinitionSemanticSnapshot result = {};
        CHECK(!player::CaptureWindowsPlayerDefinitionSemantics(
            &context, kDefinitionHeaders + 2U * 16U, 2U, &result));
        CHECK(result.evidence.row_index == 2U);
        CHECK(result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  PresetHeaderInvalid);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(
            memory.definition_headers[1], 8U,
            player::kWindowsDefinitionLevelCapacity + 1U);
        Store(
            memory.definition_headers[1], 12U,
            player::kWindowsDefinitionLevelCapacity + 1U);
        auto context = Context(&memory);
        player::PlayerDefinitionSemanticSnapshot result = {};
        CHECK(!player::CaptureWindowsPlayerDefinitionSemantics(
            &context, kDefinitionHeaders + 16U, 1U, &result));
        CHECK(result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  LevelCountOutOfRange);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        const std::uintptr_t row4_levels =
            Load<std::uintptr_t>(memory.definition_headers[4], 0U);
        memory.unreadable_address =
            row4_levels +
            3U * player::kWindowsPlayerDefinitionLevelStride;
        auto context = Context(&memory);
        player::PlayerDefinitionSemanticSnapshot result = {};
        CHECK(!player::CaptureWindowsPlayerDefinitionSemantics(
            &context, kDefinitionHeaders + 4U * 16U, 4U, &result));
        CHECK(result.evidence.row_index == 4U);
        CHECK(result.evidence.failed_level_index == 3U);
        CHECK(result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  LevelSpanUnreadable);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        const std::uintptr_t row2_levels =
            Load<std::uintptr_t>(memory.definition_headers[2], 0U);
        const std::size_t level_offset =
            static_cast<std::size_t>(
                row2_levels - kDefinitionData) +
            player::kWindowsPlayerDefinitionLevelStride;
        Store(
            memory.definition_levels, level_offset + 8U,
            static_cast<std::uint32_t>(2U));
        Store(
            memory.definition_levels, level_offset + 12U,
            static_cast<std::uint32_t>(1U));
        auto context = Context(&memory);
        player::PlayerDefinitionSemanticSnapshot result = {};
        CHECK(!player::CaptureWindowsPlayerDefinitionSemantics(
            &context, kDefinitionHeaders + 2U * 16U, 2U, &result));
        CHECK(result.evidence.row_index == 2U);
        CHECK(result.evidence.failed_level_index == 1U);
        CHECK(result.evidence.failed_property_index == 0U);
        CHECK(result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  LevelPropertyHeaderInvalid);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        const std::uintptr_t row1_levels =
            Load<std::uintptr_t>(memory.definition_headers[1], 0U);
        memory.change_definition_header = true;
        memory.changing_header_address = row1_levels;
        auto context = Context(&memory);
        player::PlayerDefinitionSemanticSnapshot result = {};
        CHECK(!player::CaptureWindowsPlayerDefinitionSemantics(
            &context, kDefinitionHeaders + 16U, 1U, &result));
        CHECK(result.evidence.row_index == 1U);
        CHECK(result.evidence.failed_level_index == 0U);
        CHECK(result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  PropertyHeaderChanged);
        memory.changing_header_reads = 0U;
        player::PlayerGameplayParamsWindowsStateSnapshot state = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &state));
        CHECK(state.first_failed_row_index == 1U);
        CHECK(HasReason(
            state.reason_mask,
            player::WindowsSnapshotReasonSnapshotChanged));
        CHECK(state.rows[1].definition_traversal.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  PropertyHeaderChanged);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        memory.change_definition_header = true;
        memory.changing_header_address = kDefinitionHeaders;
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(result.first_failed_row_index == 0U);
        CHECK(HasReason(
            result.reason_mask,
            player::WindowsSnapshotReasonDefinitionTraversalIncomplete));
        CHECK(HasReason(
            result.reason_mask,
            player::WindowsSnapshotReasonSnapshotChanged));
        CHECK(result.rows[0].definition_traversal.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  LevelHeaderChanged);
    }
    return true;
}

bool TestMacShapedDefinitionIsRejectedByWindowsProfile() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    std::memset(
        memory.definition_levels, 0,
        sizeof(memory.definition_levels));
    Store(memory.definition_headers[0], 0U, kDefinitionData);
    Store(
        memory.definition_headers[0], 8U,
        static_cast<std::uint32_t>(2U));
    Store(
        memory.definition_headers[0], 12U,
        static_cast<std::uint32_t>(2U));
    Store(
        memory.definition_levels, 0U,
        kDefinitionProperties);
    Store(
        memory.definition_levels, 8U,
        static_cast<std::uint32_t>(1U));
    Store(
        memory.definition_levels, 12U,
        static_cast<std::uint32_t>(1U));
    Store(
        memory.definition_levels, 0x30U,
        kDefinitionProperties +
            player::kWindowsPlayerDefinitionPropertyStride);
    Store(
        memory.definition_levels, 0x38U,
        static_cast<std::uint32_t>(1U));
    Store(
        memory.definition_levels, 0x3CU,
        static_cast<std::uint32_t>(1U));
    auto context = Context(&memory);
    player::PlayerDefinitionSemanticSnapshot result = {};
    CHECK(!player::CaptureWindowsPlayerDefinitionSemantics(
        &context, kDefinitionHeaders, 0U, &result));
    CHECK(result.evidence.failed_level_index == 1U);
    CHECK(result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  LevelPropertyHeaderInvalid ||
          result.evidence.first_failure ==
              player::PlayerDefinitionTraversalFailure::
                  PropertyCountOutOfRange);
    CHECK(!player::IsSupportedWindowsPlayerDefinitionLevelStride(0x30U));
    return true;
}

bool TestSemanticFingerprints() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    auto context = Context(&memory);
    player::PlayerGameplayParamsWindowsStateSnapshot baseline = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &baseline));

    Store(memory.regular_buffs, 2U, static_cast<std::uint16_t>(9U));
    player::PlayerGameplayParamsWindowsStateSnapshot changed = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &changed));
    CHECK(changed.regular_buffs.fingerprint !=
          baseline.regular_buffs.fingerprint);

    InitializeFake(&memory);
    Store(memory.timed_buffs, 0x10U, 0x40400000U);
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &changed));
    CHECK(changed.timed_buffs.fingerprint !=
          baseline.timed_buffs.fingerprint);

    InitializeFake(&memory);
    Store(memory.timed_buffs, 4U, 0xDEADBEEFU);
    Store(memory.timed_buffs, 8U,
          static_cast<std::uintptr_t>(0x0000000281001000ULL));
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &changed));
    CHECK(changed.timed_buffs.fingerprint ==
          baseline.timed_buffs.fingerprint);

    InitializeFake(&memory);
    Store(memory.skill_node2, 8U, 0x33333333U);
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &changed));
    CHECK(changed.skills.fingerprint != baseline.skills.fingerprint);

    InitializeFake(&memory);
    Store(memory.state, 0x488U, 0xA5A5A5A5U);
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &changed));
    CHECK(changed.appearance_values_fingerprint !=
          baseline.appearance_values_fingerprint);
    return true;
}

bool TestEmptyCollectionsAreValid() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    Store(memory.state, player::kWindowsPlayerStateSkillRootOffset,
          static_cast<std::uintptr_t>(0));
    Store(memory.state, player::kWindowsPlayerStateSkillCountOffset,
          static_cast<std::uint32_t>(0));
    Store(memory.state,
          player::kWindowsPlayerStateRegularBuffOffset + 8U,
          static_cast<std::uint32_t>(0));
    Store(memory.state,
          player::kWindowsPlayerStateTimedBuffOffset + 8U,
          static_cast<std::uint32_t>(0));
    auto context = Context(&memory);
    player::PlayerGameplayParamsWindowsStateSnapshot result = {};
    CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &result));
    CHECK(result.skills.valid && result.skills.count == 0U &&
          result.skills.fingerprint != 0);
    CHECK(result.regular_buffs.valid &&
          result.regular_buffs.count == 0U &&
          result.regular_buffs.fingerprint != 0);
    CHECK(result.timed_buffs.valid && result.timed_buffs.count == 0U &&
          result.timed_buffs.fingerprint != 0);
    return true;
}

bool TestFailClosedCorruption() noexcept
{
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.skill_node1, 16U, kSkillNode1);
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonSkillTreeCycle));
        CHECK(!result.skills.valid);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.state, player::kWindowsPlayerStateSkillCountOffset,
              static_cast<std::uint32_t>(1));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonSkillTreeCountMismatch));
        CHECK(!HasReason(result.reason_mask,
                         player::WindowsSnapshotReasonSkillTreeCycle));
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.state, player::kWindowsPlayerStateSkillCountOffset,
              static_cast<std::uint32_t>(3));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonSkillTreeCountMismatch));
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.definition_headers[0], 8U,
              static_cast<std::uint32_t>(
                  player::kWindowsDefinitionLevelCapacity + 1U));
        Store(memory.definition_headers[0], 12U,
              static_cast<std::uint32_t>(
                  player::kWindowsDefinitionLevelCapacity + 1U));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonCountOutOfRange));
        CHECK(!result.rows[0].definition_vector_header_readable);
        CHECK(result.first_failed_row_index == 0U);
        CHECK(HasReason(
            result.first_failed_row_reason,
            player::WindowsSnapshotReasonCountOutOfRange));
        CHECK(result.rows[0].header_read_result ==
              player::PlayerGameplayParamsWindowsRowHeaderReadResult::
                  Complete);
        CHECK(result.rows[0].header_validation_result ==
              player::PlayerGameplayParamsWindowsRowHeaderValidationResult::
                  CountOutOfRange);
        CHECK(std::strcmp(
                  player::
                      PlayerGameplayParamsWindowsRowHeaderValidationResultName(
                          result.rows[0].header_validation_result),
                  "count_out_of_range") == 0);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.definition_headers[0], 0U,
              static_cast<std::uintptr_t>(0));
        Store(memory.definition_headers[0], 8U,
              static_cast<std::uint32_t>(0));
        Store(memory.definition_headers[0], 12U,
              static_cast<std::uint32_t>(1));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(
            result.reason_mask,
            player::WindowsSnapshotReasonDefinitionVectorHeaderUnavailable));
        CHECK(!result.rows[0].definition_vector_header_readable);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.state, player::kWindowsPlayerStateClassSelectorOffset,
              static_cast<std::uintptr_t>(0));
        Store(memory.state,
              player::kWindowsPlayerStateClassSelectorOffset + 8U,
              static_cast<std::uint32_t>(0));
        Store(memory.state,
              player::kWindowsPlayerStateClassSelectorOffset + 12U,
              static_cast<std::uint32_t>(1));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonSelectorInvalid));
        CHECK(!result.class_selector.valid);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.state, player::kWindowsPlayerStateRegularBuffOffset,
              static_cast<std::uintptr_t>(0));
        Store(memory.state,
              player::kWindowsPlayerStateRegularBuffOffset + 8U,
              static_cast<std::uint32_t>(0));
        Store(memory.state,
              player::kWindowsPlayerStateRegularBuffOffset + 12U,
              static_cast<std::uint32_t>(1));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonPointerInvalid));
        CHECK(!result.regular_buffs.valid);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.state, player::kWindowsPlayerStateTimedBuffOffset,
              static_cast<std::uintptr_t>(0));
        Store(memory.state,
              player::kWindowsPlayerStateTimedBuffOffset + 8U,
              static_cast<std::uint32_t>(0));
        Store(memory.state,
              player::kWindowsPlayerStateTimedBuffOffset + 12U,
              static_cast<std::uint32_t>(1));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonPointerInvalid));
        CHECK(!result.timed_buffs.valid);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.state, player::kWindowsPlayerStateTimedBuffOffset,
              static_cast<std::uintptr_t>(-9));
        Store(memory.state,
              player::kWindowsPlayerStateTimedBuffOffset + 8U,
              static_cast<std::uint32_t>(1));
        Store(memory.state,
              player::kWindowsPlayerStateTimedBuffOffset + 12U,
              static_cast<std::uint32_t>(1));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonAddressOverflow));
        CHECK(!memory.overflow_read_attempted);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        Store(memory.rows, 0U, static_cast<std::uintptr_t>(0));
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(result.player_semantic_snapshot_complete);
        CHECK(result.rows[0].preset_absent);
        CHECK(result.rows[0].semantic_kind ==
              player::PlayerGameplayParamsWindowsRowSemanticKind::
                  AbsentPresetRow);
        CHECK((result.complete_fields &
               player::WindowsFieldDefinitionVectorHeaders) != 0);
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        memory.progression[std::strlen(memory.progression)] = 'x';
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonSelectorInvalid));
    }
    {
        FakeMemory memory;
        InitializeFake(&memory);
        memory.change_skill_count = true;
        auto context = Context(&memory);
        player::PlayerGameplayParamsWindowsStateSnapshot result = {};
        CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
            &context, kState, 1U, &result));
        CHECK(HasReason(result.reason_mask,
                        player::WindowsSnapshotReasonHeaderChanged));
    }
    return true;
}

bool TestProfileAndIdentityFailures() noexcept
{
    FakeMemory memory;
    InitializeFake(&memory);
    auto context = Context(&memory);
    context.exact_gamedll_image_validated = false;
    player::PlayerGameplayParamsWindowsStateSnapshot state = {};
    CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &state));
    CHECK(HasReason(state.reason_mask,
                    player::WindowsSnapshotReasonUnsupportedImage));

    context.exact_gamedll_image_validated = true;
    Store(memory.state, 0U, static_cast<std::uintptr_t>(0xBADU));
    CHECK(!player::SnapshotPlayerGameplayParamsWindowsState(
        &context, kState, 1U, &state));
    CHECK(HasReason(
        state.reason_mask,
        player::WindowsSnapshotReasonPlayerStateIdentityMismatch));

    InitializeFake(&memory);
    Store(memory.game_di, 0U, static_cast<std::uintptr_t>(0xBADU));
    player::PlayerGameplayParamsWindowsEnvironmentSnapshot environment = {};
    CHECK(!player::SnapshotPlayerGameplayParamsWindowsEnvironment(
        &context, &environment));
    CHECK(HasReason(
        environment.reason_mask,
        player::WindowsSnapshotReasonGameDiIdentityMismatch));
    return true;
}

}  // namespace

int main()
{
    if (!TestEnvironmentSnapshot() ||
        !TestPostDestroyEnvironmentDoesNotDereferenceSession() ||
        !TestCompleteProvenStateSnapshot() ||
        !TestExactWindowsRowContractAndAbsentPreset() ||
        !TestExactWindowsDefinitionTraversalAndRuntime11Rows() ||
        !TestDefinitionTraversalFailureEvidence() ||
        !TestMacShapedDefinitionIsRejectedByWindowsProfile() ||
        !TestSemanticFingerprints() ||
        !TestEmptyCollectionsAreValid() ||
        !TestFailClosedCorruption() ||
        !TestProfileAndIdentityFailures())
        return 1;
    std::puts("player_gameplay_params_windows_snapshot_fake_harness: ok");
    return 0;
}
