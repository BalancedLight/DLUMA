#ifndef DLUMA_FIXES_PLAYER_PRESET_TARGETED_REBUILD_H
#define DLUMA_FIXES_PLAYER_PRESET_TARGETED_REBUILD_H

#include "../../dlw_win32.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::playerpreset {

constexpr std::size_t kLogicalTypeCount = 8;
constexpr std::size_t kSelectorCapacity = 192;
constexpr std::size_t kProviderCapacity = 1024;
constexpr std::size_t kPresetRecordSize = 0x40;
constexpr std::size_t kPresetDefinitionVectorOffset = 0x20;

enum class PreflightFailure : std::uint8_t {
    None,
    FeatureDisabled,
    UnsupportedImage,
    NoPendingToken,
    TokenAlreadyConsumed,
    StateMismatch,
    StateGenerationMismatch,
    ThreadMismatch,
    SelectorMismatch,
    ContentEpochMismatch,
    CacheGenerationMismatch,
    OldDataSourceStillActive,
    CurrentEpochSourcesIncomplete,
    WrongLoadCaller,
    Shutdown,
    MutationScopeActive,
    RowUnreadable,
    CacheContainerUnreadable,
    CacheVectorInvalid,
    RecordMembershipMissing,
    RecordMembershipAmbiguous,
    RecordTypeMismatch,
    RecordSelectorMismatch,
    LiveStateMembershipIncomplete,
    TraceEvidenceUnavailable,
};

struct PendingPlayerPresetProviderRepair {
    std::uint64_t token_generation;
    std::uint64_t created_sequence;
    std::uint32_t old_content_epoch;
    std::uint32_t current_content_epoch;
    std::uint64_t cache_generation;
    std::uintptr_t player_state;
    std::uint64_t player_state_generation;
    std::uintptr_t player_variables;
    std::uintptr_t old_data_source;
    std::uintptr_t old_type0_definition_vector;
    std::uint64_t old_type0_fingerprint;
    std::uint32_t old_type0_definition_count;
    std::uint32_t owner_thread_id;
    char progression_selector[kSelectorCapacity];
    char class_selector[kSelectorCapacity];
    char old_provider[kProviderCapacity];
    bool selector_equal;
    bool content_identity_changed;
    bool old_data_source_inactive;
    bool current_epoch_sources_ready;
    bool consumed;
};

struct PendingRepairStore {
    volatile LONG lock;
    std::uint64_t next_generation;
    PendingPlayerPresetProviderRepair token;
    bool available;
};

struct CacheVectorBounds {
    std::uintptr_t records;
    std::uint32_t count;
    std::uint32_t capacity;
    std::uint32_t logical_type;
    std::uint64_t cache_generation;
    bool descriptor_readable;
};

struct TargetRecord {
    std::uintptr_t record;
    std::uintptr_t definition_vector;
    std::uint32_t logical_type;
};

struct TargetedRebuildPlan {
    TargetRecord records[kLogicalTypeCount];
    std::uint8_t row_to_record[kLogicalTypeCount];
    std::uint32_t distinct_count;
};

void Initialize(PendingRepairStore* store) noexcept;
bool PublishPendingRepair(
    PendingRepairStore* store,
    PendingPlayerPresetProviderRepair* token) noexcept;
bool PeekPendingRepair(
    PendingRepairStore* store,
    PendingPlayerPresetProviderRepair* output) noexcept;
bool ConsumePendingRepair(
    PendingRepairStore* store, std::uint64_t token_generation,
    PendingPlayerPresetProviderRepair* output) noexcept;
void ExpirePendingRepair(PendingRepairStore* store) noexcept;

bool TokenMatchesLoad(
    const PendingPlayerPresetProviderRepair& token,
    std::uintptr_t player_state, std::uint64_t player_state_generation,
    std::uintptr_t player_variables, std::uint32_t content_epoch,
    std::uint64_t cache_generation, std::uint32_t thread_id,
    const char* progression_selector, const char* class_selector,
    PreflightFailure* failure = nullptr) noexcept;

bool MapDefinitionVectorToValidatedCacheRecord(
    std::uintptr_t definition_vector, std::uint32_t logical_type,
    const CacheVectorBounds& bounds, std::uintptr_t* record,
    PreflightFailure* failure = nullptr) noexcept;

bool AppendValidatedRecord(
    TargetedRebuildPlan* plan, std::uint32_t row_index,
    std::uintptr_t record, std::uintptr_t definition_vector,
    std::uint32_t logical_type,
    PreflightFailure* failure = nullptr) noexcept;

const char* PreflightFailureName(PreflightFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::playerpreset

#endif
