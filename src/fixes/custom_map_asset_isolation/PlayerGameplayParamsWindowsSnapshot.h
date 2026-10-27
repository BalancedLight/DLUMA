#ifndef DLUMA_FIXES_PLAYER_GAMEPLAY_PARAMS_WINDOWS_SNAPSHOT_H
#define DLUMA_FIXES_PLAYER_GAMEPLAY_PARAMS_WINDOWS_SNAPSHOT_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart {

/* Exact supported-Player layout facts.  The caller must still prove the full
 * gamedll image identity before using either reader. */
constexpr std::uintptr_t kWindowsGameplayParamsContainerGlobalRva =
    0x01D07210U;
constexpr std::uintptr_t kWindowsLocalPlayerStateGlobalRva = 0x01D07698U;
constexpr std::uintptr_t kWindowsCharacterPlayerStateGlobalRva =
    0x01D078F0U;
constexpr std::uintptr_t kWindowsGameDiGlobalRva = 0x01D07F88U;
constexpr std::uintptr_t kWindowsGameDiVtableRva = 0x0157D5F8U;
constexpr std::uintptr_t kWindowsGameDiCurrentSessionOffset = 0x0508U;
constexpr std::uintptr_t kWindowsSessionActiveLevelOffset = 0x00B0U;

constexpr std::size_t kWindowsPlayerStateSize = 0x04B0U;
constexpr std::uintptr_t kWindowsPlayerStatePrimaryVtableRva = 0x017E2FF8U;
constexpr std::uintptr_t kWindowsPlayerStateVtable8Rva = 0x017E3788U;
constexpr std::uintptr_t kWindowsPlayerStateVtable16Rva = 0x017E37B0U;
constexpr std::uintptr_t kWindowsPlayerStateVtable48Rva = 0x017E37C0U;
constexpr std::size_t kWindowsPlayerStateRowTableOffset = 0x0040U;
constexpr std::size_t kWindowsPlayerStateProgressionSelectorOffset = 0x00B0U;
constexpr std::size_t kWindowsPlayerStateClassSelectorOffset = 0x00C0U;
constexpr std::size_t kWindowsPlayerStatePlayerVariablesOffset = 0x00D0U;
constexpr std::size_t kWindowsPlayerStateSkillRootOffset = 0x00D8U;
constexpr std::size_t kWindowsPlayerStateSkillCountOffset = 0x00E0U;
constexpr std::size_t kWindowsPlayerStateRegularBuffOffset = 0x00F0U;
constexpr std::size_t kWindowsPlayerStateTimedBuffOffset = 0x0108U;
constexpr std::size_t kWindowsPlayerStateAppearanceOffset = 0x0470U;
constexpr std::size_t kWindowsPlayerStateAppearanceSize = 0x003CU;

constexpr std::size_t kWindowsPlayerLogicalTypeCount = 8U;
constexpr std::size_t kWindowsPlayerRowStride = 0x20U;
constexpr std::size_t kWindowsPlayerRowPresetOffset = 0x00U;
constexpr std::size_t kWindowsPlayerRowEarnedExperienceOffset = 0x08U;
constexpr std::size_t kWindowsPlayerRowUnknown100Offset = 0x10U;
constexpr std::size_t kWindowsPlayerRowCurrentLevelOffset = 0x14U;
constexpr std::size_t kWindowsPlayerRowMaximumLevelOffset = 0x16U;
constexpr std::size_t kWindowsPlayerRowTrailingQwordOffset = 0x18U;
constexpr std::size_t kWindowsSelectorByteCapacity = 255U;
constexpr std::size_t kWindowsSkillNodeCapacity = 1024U;
constexpr std::size_t kWindowsBuffCapacity = 4096U;
constexpr std::uint32_t kWindowsDefinitionLevelCapacity = 512U;
constexpr std::size_t kWindowsPlayerDefinitionLevelStride = 0x28U;
constexpr std::size_t kWindowsPlayerDefinitionPropertyStride = 0x20U;
constexpr std::size_t kWindowsPlayerDefinitionLevelVectorOffset = 0x00U;
constexpr std::size_t kWindowsPlayerDefinitionLevelCountOffset = 0x08U;
constexpr std::size_t kWindowsPlayerDefinitionLevelCapacityOffset = 0x0CU;
constexpr std::size_t kWindowsPlayerDefinitionPropertyVectorOffset = 0x00U;
constexpr std::size_t kWindowsPlayerDefinitionPropertyCountOffset = 0x08U;
constexpr std::size_t kWindowsPlayerDefinitionPropertyCapacityOffset = 0x0CU;
constexpr std::size_t kWindowsPlayerDefinitionPropertyIdOffset = 0x00U;
constexpr std::size_t kWindowsPlayerDefinitionPropertyScalarBitsOffset =
    0x04U;
constexpr std::size_t kWindowsPlayerDefinitionPropertyBoolOffset = 0x08U;
constexpr std::size_t kWindowsPlayerDefinitionPropertyStringOffset = 0x10U;
constexpr std::uint32_t kWindowsPlayerDefinitionPropertyCapacity = 4096U;
constexpr std::uint32_t kWindowsPlayerDefinitionTotalPropertyCapacity =
    8192U;
constexpr std::uint32_t kWindowsPlayerDefinitionStringByteCapacity = 4096U;

constexpr bool IsSupportedWindowsPlayerRowStride(
    std::size_t stride) noexcept
{
    return stride == kWindowsPlayerRowStride;
}

constexpr bool IsSupportedWindowsPlayerDefinitionLevelStride(
    std::size_t stride) noexcept
{
    return stride == kWindowsPlayerDefinitionLevelStride;
}

constexpr bool IsSupportedWindowsPlayerDefinitionPropertyStride(
    std::size_t stride) noexcept
{
    return stride == kWindowsPlayerDefinitionPropertyStride;
}

enum PlayerGameplayParamsWindowsSnapshotField : std::uint64_t {
    WindowsFieldExactImage = 1ULL << 0,
    WindowsFieldGameplayParamsContainer = 1ULL << 1,
    WindowsFieldGameDi = 1ULL << 2,
    WindowsFieldCurrentSession = 1ULL << 3,
    WindowsFieldActiveLevel = 1ULL << 4,
    WindowsFieldLocalPlayerStateGlobal = 1ULL << 5,
    WindowsFieldCharacterPlayerStateGlobal = 1ULL << 6,
    WindowsFieldPlayerStateBody = 1ULL << 7,
    WindowsFieldPlayerStateIdentity = 1ULL << 8,
    WindowsFieldPlayerRows = 1ULL << 9,
    WindowsFieldDefinitionVectorHeaders = 1ULL << 10,
    WindowsFieldProgressionSelector = 1ULL << 11,
    WindowsFieldClassSelector = 1ULL << 12,
    WindowsFieldPlayerVariables = 1ULL << 13,
    WindowsFieldSkills = 1ULL << 14,
    WindowsFieldRegularBuffs = 1ULL << 15,
    WindowsFieldTimedBuffs = 1ULL << 16,
    WindowsFieldAppearanceValues = 1ULL << 17,
    WindowsFieldDefinitionSemantics = 1ULL << 18,

    /* These domains deliberately have no value/fingerprint fields below.
     * Their exact live owners are not yet proven at the no-session barrier. */
    WindowsFieldInventory = 1ULL << 24,
    WindowsFieldEquipment = 1ULL << 25,
    WindowsFieldQuests = 1ULL << 26,
    WindowsFieldProviderIdentity = 1ULL << 27,
};

constexpr std::uint64_t kWindowsEnvironmentRequiredFields =
    WindowsFieldExactImage |
    WindowsFieldGameplayParamsContainer |
    WindowsFieldGameDi |
    WindowsFieldCurrentSession |
    WindowsFieldActiveLevel |
    WindowsFieldLocalPlayerStateGlobal |
    WindowsFieldCharacterPlayerStateGlobal;

constexpr std::uint64_t kWindowsPlayerStateRequiredProvenFields =
    WindowsFieldExactImage |
    WindowsFieldPlayerStateBody |
    WindowsFieldPlayerStateIdentity |
    WindowsFieldPlayerRows |
    WindowsFieldDefinitionVectorHeaders |
    WindowsFieldDefinitionSemantics |
    WindowsFieldProgressionSelector |
    WindowsFieldClassSelector |
    WindowsFieldPlayerVariables |
    WindowsFieldSkills |
    WindowsFieldRegularBuffs |
    WindowsFieldTimedBuffs |
    WindowsFieldAppearanceValues;

constexpr std::uint64_t kWindowsPlayerStateUnresolvedFields =
    WindowsFieldInventory |
    WindowsFieldEquipment |
    WindowsFieldQuests |
    WindowsFieldProviderIdentity;

enum PlayerGameplayParamsWindowsSnapshotReason : std::uint64_t {
    WindowsSnapshotReasonNone = 0,
    WindowsSnapshotReasonInvalidArgument = 1ULL << 0,
    WindowsSnapshotReasonUnsupportedImage = 1ULL << 1,
    WindowsSnapshotReasonAddressOverflow = 1ULL << 2,
    WindowsSnapshotReasonMemoryUnreadable = 1ULL << 3,
    WindowsSnapshotReasonGameplayParamsContainerUnavailable = 1ULL << 4,
    WindowsSnapshotReasonGameDiUnavailable = 1ULL << 5,
    WindowsSnapshotReasonGameDiIdentityMismatch = 1ULL << 6,
    WindowsSnapshotReasonLocalPlayerStateUnavailable = 1ULL << 7,
    WindowsSnapshotReasonCharacterPlayerStateUnavailable = 1ULL << 8,
    WindowsSnapshotReasonActiveSessionPresent = 1ULL << 9,
    WindowsSnapshotReasonActiveLevelPresent = 1ULL << 10,
    WindowsSnapshotReasonPlayerStateIdentityMismatch = 1ULL << 11,
    WindowsSnapshotReasonRowTableUnavailable = 1ULL << 12,
    WindowsSnapshotReasonDefinitionVectorHeaderUnavailable = 1ULL << 13,
    WindowsSnapshotReasonSelectorInvalid = 1ULL << 14,
    WindowsSnapshotReasonPlayerVariablesUnavailable = 1ULL << 15,
    WindowsSnapshotReasonHeaderInvalid = 1ULL << 16,
    WindowsSnapshotReasonHeaderChanged = 1ULL << 17,
    WindowsSnapshotReasonCountOutOfRange = 1ULL << 18,
    WindowsSnapshotReasonPointerInvalid = 1ULL << 19,
    WindowsSnapshotReasonSkillTreeCycle = 1ULL << 20,
    WindowsSnapshotReasonSkillTreeCountMismatch = 1ULL << 21,
    WindowsSnapshotReasonSkillNodeInvalid = 1ULL << 22,
    WindowsSnapshotReasonSnapshotChanged = 1ULL << 23,
    WindowsSnapshotReasonDefinitionTraversalIncomplete = 1ULL << 24,
    WindowsSnapshotReasonInventoryOwnerUnresolved = 1ULL << 32,
    WindowsSnapshotReasonEquipmentOwnerUnresolved = 1ULL << 33,
    WindowsSnapshotReasonQuestOwnerUnresolved = 1ULL << 34,
    WindowsSnapshotReasonProviderIdentityUnresolved = 1ULL << 35,
};

using ReadPlayerGameplayParamsWindowsMemoryFn = bool (*)(
    void* user, std::uintptr_t address, void* output,
    std::size_t size) noexcept;

struct PlayerGameplayParamsWindowsReadContext {
    std::uintptr_t gamedll_base;
    ReadPlayerGameplayParamsWindowsMemoryFn read_memory;
    void* read_memory_user;
    bool exact_gamedll_image_validated;
};

struct PlayerGameplayParamsWindowsEnvironmentSnapshot {
    std::uintptr_t gameplay_params_container;
    std::uintptr_t game_di;
    std::uintptr_t current_session;
    std::uintptr_t active_level;
    std::uintptr_t local_process_global_state;
    std::uintptr_t character_manager_global_state;
    std::uint64_t complete_fields;
    std::uint64_t reason_mask;
    bool observation_complete;
    /* Read-only environmental fact, never restart authorization: this does
     * not prove local PlayerDI/representation absence or unresolved domains. */
    bool no_session_environment_ready;
};

enum class PlayerGameplayParamsWindowsEnvironmentReadMode : std::uint8_t {
    /* Startup and mutation diagnostics may inspect the active level only
     * while the current SessionDI is still a live, validated owner. */
    SessionAware,
    /* The post-OnDestroy semantic owner must never dereference the outgoing
     * SessionDI. A nonzero GameDI current-session observation is sufficient
     * to report the exact active-session blocker. */
    PostDestroyProcessGlobals,
};

struct PlayerGameplayParamsWindowsSelectorSnapshot {
    std::uint64_t fingerprint;
    std::uint32_t length;
    bool valid;
};

enum class PlayerGameplayParamsWindowsRowSemanticKind : std::uint8_t {
    Unavailable,
    AbsentPresetRow,
    PresetRow,
};

enum class PlayerGameplayParamsWindowsRowHeaderReadResult : std::uint8_t {
    NotAttempted,
    AbsentPresetNotApplicable,
    Complete,
    AddressOverflow,
    MemoryUnreadable,
    ChangedBetweenReads,
};

enum class PlayerGameplayParamsWindowsRowHeaderValidationResult
    : std::uint8_t {
    NotAttempted,
    AbsentPresetAccepted,
    Valid,
    SizeExceedsCapacity,
    CountOutOfRange,
    DataPointerUnavailable,
};

struct PlayerDefinitionVectorHeaderSnapshot {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

enum class PlayerDefinitionTraversalFailure : std::uint8_t {
    None,
    InvalidArgument,
    PresetHeaderUnreadable,
    PresetHeaderInvalid,
    LevelCountOutOfRange,
    LevelSpanUnreadable,
    LevelHeaderChanged,
    LevelPropertyHeaderUnreadable,
    LevelPropertyHeaderInvalid,
    PropertyCountOutOfRange,
    PropertySpanUnreadable,
    PropertyValueInvalid,
    StringValueInvalid,
    PropertyHeaderChanged,
    SemanticFingerprintChanged,
};

struct PlayerDefinitionTraversalEvidence {
    std::uint32_t row_index;
    std::uintptr_t preset;
    std::uintptr_t level_data;
    std::uint32_t level_count;
    std::uint32_t level_capacity;
    std::size_t configured_level_stride;
    std::uint32_t failed_level_index;
    std::uintptr_t property_data;
    std::uint32_t property_count;
    std::uint32_t property_capacity;
    std::size_t configured_property_stride;
    std::uint32_t failed_property_index;
    std::uint32_t failed_property_id;
    PlayerDefinitionTraversalFailure first_failure;
    PlayerDefinitionVectorHeaderSnapshot level_header_first;
    PlayerDefinitionVectorHeaderSnapshot level_header_second;
    PlayerDefinitionVectorHeaderSnapshot property_header_first;
    PlayerDefinitionVectorHeaderSnapshot property_header_second;
};

struct PlayerDefinitionSemanticSnapshot {
    std::uint64_t semantic_fingerprint;
    std::uint32_t level_count;
    std::uint32_t property_count;
    std::uint32_t fov_value_bits[4];
    std::uint8_t fov_present_mask;
    bool complete;
    PlayerDefinitionTraversalEvidence evidence;
};

struct PlayerGameplayParamsWindowsRowSnapshot {
    /* PlayerState's +0x40 table stores PlayerLevelPreset/cache-record
     * pointers. The first field of a non-null record is the level-definition
     * vector header. Keep definition_vector as a compatibility alias for the
     * exact preset pointer consumed by the existing observation ledger. */
    std::uintptr_t preset;
    std::uintptr_t definition_vector;
    std::uintptr_t definition_header_data;
    std::uint64_t definition_fingerprint;
    std::uint64_t earned_experience;
    std::uint64_t trailing_qword;
    /* Number of level entries in the vector header at the first field of the
     * returned preset/cache record. This is not the summed parsed
     * property/definition count (379/385). */
    std::uint32_t definition_level_count;
    std::uint32_t definition_level_capacity;
    std::uint32_t definition_property_count;
    std::uint32_t unknown_100;
    std::uint16_t current_level;
    std::uint16_t maximum_level;
    std::uint64_t failure_reason;
    PlayerGameplayParamsWindowsRowSemanticKind semantic_kind;
    PlayerGameplayParamsWindowsRowHeaderReadResult header_read_result;
    PlayerGameplayParamsWindowsRowHeaderValidationResult
        header_validation_result;
    PlayerDefinitionTraversalEvidence definition_traversal;
    bool row_readable;
    bool definition_vector_header_readable;
    bool semantic_complete;
    bool preset_absent;
};

struct PlayerGameplayParamsWindowsFingerprint {
    std::uint64_t fingerprint;
    std::uint32_t count;
    std::uint32_t auxiliary_count;
    bool valid;
};

struct PlayerGameplayParamsWindowsStateSnapshot {
    std::uintptr_t state;
    std::uint64_t state_generation;
    std::uintptr_t player_variables;
    PlayerGameplayParamsWindowsSelectorSnapshot progression_selector;
    PlayerGameplayParamsWindowsSelectorSnapshot class_selector;
    PlayerGameplayParamsWindowsRowSnapshot rows[
        kWindowsPlayerLogicalTypeCount];
    PlayerGameplayParamsWindowsFingerprint skills;
    PlayerGameplayParamsWindowsFingerprint regular_buffs;
    PlayerGameplayParamsWindowsFingerprint timed_buffs;
    std::uint64_t appearance_values_fingerprint;
    std::uint64_t complete_fields;
    std::uint64_t unresolved_fields;
    std::uint64_t reason_mask;
    std::uint64_t first_failed_row_reason;
    std::uint32_t first_failed_row_index;
    bool player_semantic_snapshot_complete;
    bool player_restart_snapshot_complete;
    /* Compatibility names retained for existing diagnostics/tests. */
    bool proven_fields_complete;
    bool restart_snapshot_complete;
};

/* These routines only read through the supplied callback.  They allocate no
 * memory, call no game function, and mutate no game state. */
bool SnapshotPlayerGameplayParamsWindowsEnvironment(
    const PlayerGameplayParamsWindowsReadContext* context,
    PlayerGameplayParamsWindowsEnvironmentSnapshot* output,
    PlayerGameplayParamsWindowsEnvironmentReadMode mode =
        PlayerGameplayParamsWindowsEnvironmentReadMode::
            SessionAware) noexcept;

bool SnapshotPlayerGameplayParamsWindowsState(
    const PlayerGameplayParamsWindowsReadContext* context,
    std::uintptr_t player_state, std::uint64_t state_generation,
    PlayerGameplayParamsWindowsStateSnapshot* output) noexcept;

bool CaptureWindowsPlayerDefinitionSemantics(
    const PlayerGameplayParamsWindowsReadContext* context,
    std::uintptr_t preset, std::uint32_t row_index,
    PlayerDefinitionSemanticSnapshot* output) noexcept;

/* Captures two complete semantic passes for one exact main-menu PlayerState.
 * This is read-only and deliberately does not claim restart/mutation
 * readiness for unresolved inventory/equipment/quest/provider owners. */
bool dluma_capture_stable_main_menu_player_semantic_snapshot(
    const PlayerGameplayParamsWindowsReadContext* context,
    std::uintptr_t player_state, std::uint64_t state_generation,
    PlayerGameplayParamsWindowsStateSnapshot* first,
    PlayerGameplayParamsWindowsStateSnapshot* second) noexcept;

const char* PlayerGameplayParamsWindowsRowSemanticKindName(
    PlayerGameplayParamsWindowsRowSemanticKind kind) noexcept;
const char* PlayerGameplayParamsWindowsRowHeaderReadResultName(
    PlayerGameplayParamsWindowsRowHeaderReadResult result) noexcept;
const char* PlayerGameplayParamsWindowsRowHeaderValidationResultName(
    PlayerGameplayParamsWindowsRowHeaderValidationResult result) noexcept;
const char* PlayerDefinitionTraversalFailureName(
    PlayerDefinitionTraversalFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart

#endif
