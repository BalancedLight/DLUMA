#ifndef DLUMA_AUDIO_SEMANTIC_WINDOWS_SNAPSHOT_H
#define DLUMA_AUDIO_SEMANTIC_WINDOWS_SNAPSHOT_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot {

/*
 * Exact Dying Light Developer Tools engine_x64_rwdi.dll profile:
 * SHA-256 A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C.
 * These registries are protected by the engine sound critical section at
 * RVA 0x00AD5160. Callers must own that lock for the complete snapshot.
 */
constexpr std::uintptr_t kAudioScriptPathVectorRva = 0x00AD52E0U;
constexpr std::uintptr_t kAudioScriptOnStartRva = 0x00AD52F8U;
constexpr std::uintptr_t kAudioScriptDefinitionsRva = 0x00AD5308U;
constexpr std::uintptr_t kAudioScriptDefaultProcRva = 0x00AD5320U;
constexpr std::uintptr_t kAudioScriptDefaultDefinitionsRva = 0x00AD5330U;
constexpr std::uintptr_t kAudioScriptProceduresRva = 0x00AD5348U;
constexpr std::uintptr_t kAudioScriptPropertyTreeRva = 0x00AD5360U;
constexpr std::uintptr_t kAudioScriptPropertyTreeAllocatorTagRva =
    0x00AD5370U;

constexpr std::uintptr_t kAudioPropertyNodeKeyOffset = 0x0000U;
constexpr std::uintptr_t kAudioPropertyNodeValueVectorOffset = 0x0010U;
constexpr std::uintptr_t kAudioPropertyNodeLinkOffset = 0x0028U;
constexpr std::uintptr_t kAudioPropertyNodeLeftOffset = 0x0028U;
constexpr std::uintptr_t kAudioPropertyNodeParentOffset = 0x0030U;
constexpr std::uintptr_t kAudioPropertyNodeRightOffset = 0x0038U;
constexpr std::uintptr_t kAudioPropertyNodeColorOffset = 0x0040U;
constexpr std::size_t kAudioPropertyNodeReadableSize = 0x0048U;

constexpr std::uintptr_t kAudioBankTreeRootOffset = 0x0000U;
constexpr std::uintptr_t kAudioBankTreeCountOffset = 0x0008U;
constexpr std::uintptr_t kAudioBankNodeLinkOffset = 0x0048U;
constexpr std::uintptr_t kAudioBankNodeHashOffset = 0x0000U;
constexpr std::uintptr_t kAudioBankNodeNameOffset = 0x0008U;
constexpr std::uintptr_t kAudioBankNodeNativeFileOffset = 0x0018U;
constexpr std::uintptr_t kAudioBankNodeRecordHashOffset = 0x0020U;
constexpr std::uintptr_t kAudioBankNodeLocalisedOffset = 0x0044U;
constexpr std::uintptr_t kAudioBankNodeLeftOffset = 0x0048U;
constexpr std::uintptr_t kAudioBankNodeParentOffset = 0x0050U;
constexpr std::uintptr_t kAudioBankNodeRightOffset = 0x0058U;
constexpr std::uintptr_t kAudioBankNodeColorOffset = 0x0060U;
constexpr std::size_t kAudioBankNodeReadableSize = 0x0068U;

constexpr std::uint32_t kAudioScriptVectorTag = 10U;
constexpr std::size_t kAudioSemanticStringCapacity = 512U;
constexpr std::size_t kAudioScriptVectorCapacity = 4096U;
constexpr std::size_t kAudioScriptPropertyTreeCapacity = 4096U;
constexpr std::size_t kAudioBankTreeCapacity = 192U;
constexpr std::size_t kAudioBankNodeDiagnosticCapacity = 32U;
constexpr std::size_t kAudioBankNodeDiagnosticNameCapacity = 128U;

enum AudioSemanticSnapshotReason : std::uint64_t {
    AudioSemanticReasonNone = 0,
    AudioSemanticReasonInvalidArgument = 1ULL << 0,
    AudioSemanticReasonUnsupportedImage = 1ULL << 1,
    AudioSemanticReasonAddressOverflow = 1ULL << 2,
    AudioSemanticReasonMemoryUnreadable = 1ULL << 3,
    AudioSemanticReasonHeaderInvalid = 1ULL << 4,
    AudioSemanticReasonCountOutOfRange = 1ULL << 5,
    AudioSemanticReasonStringInvalid = 1ULL << 6,
    AudioSemanticReasonValueTypeInvalid = 1ULL << 7,
    AudioSemanticReasonTreeNodeInvalid = 1ULL << 8,
    AudioSemanticReasonTreeCycle = 1ULL << 9,
    AudioSemanticReasonTreeCountMismatch = 1ULL << 10,
    AudioSemanticReasonTreeOrderInvalid = 1ULL << 11,
    AudioSemanticReasonBankHashMismatch = 1ULL << 12,
    AudioSemanticReasonAudioLockUnavailable = 1ULL << 13,
};

enum class AudioScriptSemanticStage : std::uint8_t {
    NotAttempted,
    ContextValidation,
    PathVector,
    OnStartProcedure,
    MainDefinitionVector,
    DefaultProcedure,
    DefaultDefinitionVector,
    ProcedureVector,
    PropertyTree,
    FinalSemanticFingerprint,
    Complete,
};

enum class AudioBankSemanticStage : std::uint8_t {
    NotAttempted,
    ContextValidation,
    TreeHeader,
    TreeTraversal,
    FinalSemanticFingerprint,
    Complete,
};

enum class AudioPropertyTreeHeaderValidationResult : std::uint8_t {
    NotAttempted,
    Valid,
    RootCountMismatch,
    CountOutOfRange,
    PaddingInvalid,
    AllocatorTagInvalid,
};

using ReadAudioSemanticMemoryFn = bool (*)(
    void* user, std::uintptr_t address, void* output,
    std::size_t size) noexcept;

struct AudioSemanticReadContext {
    std::uintptr_t engine_base;
    ReadAudioSemanticMemoryFn read_memory;
    void* callback_user;
    bool exact_engine_image_validated;
    bool caller_owns_audio_lock;
};

struct AudioScriptRegistrySemanticSnapshot {
    std::uintptr_t registry_owner;
    std::uint64_t semantic_fingerprint;
    std::uint64_t reason_mask;
    std::uint32_t definition_count;
    std::uint32_t path_count;
    std::uint32_t procedure_count;
    std::uint32_t property_group_count;
    std::uintptr_t property_tree_raw_root;
    std::uint32_t property_tree_raw_count;
    std::uint32_t property_tree_raw_padding;
    std::uint32_t property_tree_raw_allocator_tag;
    std::uint32_t property_tree_configured_bound;
    AudioPropertyTreeHeaderValidationResult
        property_tree_header_validation_result;
    AudioScriptSemanticStage failure_stage;
    bool attempted;
    bool skipped;
    bool complete;
};

struct AudioBankNodeSemanticDiagnostic {
    std::uint32_t in_order_ordinal;
    std::uintptr_t node;
    std::uintptr_t link;
    std::uintptr_t left;
    std::uintptr_t parent;
    std::uintptr_t right;
    std::uintptr_t native_file;
    std::uint32_t color;
    std::uint32_t raw_key_bits;
    std::int32_t signed_key_value;
    std::uint32_t recomputed_hash_bits;
    std::uint32_t record_hash;
    std::uint32_t name_length;
    std::uint8_t localised;
    char name[kAudioBankNodeDiagnosticNameCapacity];
};

struct AudioBankTreeSemanticSnapshot {
    std::uintptr_t manager;
    std::uintptr_t tree_root;
    std::uintptr_t matching_node;
    std::uintptr_t matching_native_file;
    std::uint64_t semantic_fingerprint;
    std::uint64_t identity_fingerprint;
    std::uint64_t reason_mask;
    std::uint32_t bank_count;
    std::uint32_t matching_node_count;
    std::uint32_t diagnostic_node_count;
    AudioBankNodeSemanticDiagnostic
        node_diagnostics[kAudioBankNodeDiagnosticCapacity];
    AudioBankSemanticStage failure_stage;
    bool node_diagnostics_truncated;
    bool attempted;
    bool skipped;
    bool complete;
};

bool SnapshotAudioScriptRegistrySemanticUnderLock(
    const AudioSemanticReadContext* context,
    AudioScriptRegistrySemanticSnapshot* output) noexcept;

bool SnapshotAudioBankTreeSemanticUnderLock(
    const AudioSemanticReadContext* context,
    std::uintptr_t manager,
    const char* requested_bank_name,
    AudioBankTreeSemanticSnapshot* output) noexcept;

bool dluma_compare_fmod_bank_key_in_engine_order(
    std::uint32_t left_bits, std::uint32_t right_bits) noexcept;

const char* AudioScriptSemanticStageName(
    AudioScriptSemanticStage stage) noexcept;
const char* AudioBankSemanticStageName(
    AudioBankSemanticStage stage) noexcept;
const char* AudioPropertyTreeHeaderValidationResultName(
    AudioPropertyTreeHeaderValidationResult result) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot

#endif
