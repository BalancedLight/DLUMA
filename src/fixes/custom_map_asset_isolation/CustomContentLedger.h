#ifndef DLUMA_FIXES_CUSTOM_CONTENT_LEDGER_H
#define DLUMA_FIXES_CUSTOM_CONTENT_LEDGER_H

#include "../../dlw_win32.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::content {

constexpr std::size_t kProjectRootCapacity = 1024;
constexpr std::size_t kSourcePathCapacity = 2048;
constexpr std::size_t kLogicalPathCapacity = 1024;
constexpr std::size_t kCallerModuleCapacity = 64;
constexpr std::size_t kAdapterNameCapacity = 64;
constexpr std::size_t kSelectedContentNameCapacity = 256;
constexpr std::size_t kMapNameCapacity = 128;
constexpr std::size_t kMaximumEpochs = 16;
constexpr std::size_t kMaximumSources = 128;
constexpr std::size_t kMaximumFiles = 384;
constexpr std::size_t kMaximumPacks = 128;
constexpr std::size_t kMaximumDiagnostics = 256;
constexpr std::size_t kMaximumRetirementSummaries = 16;

enum class CustomSourceKind : std::uint8_t {
    ProjectRoot,
    DataDirectory,
    DataWorkshopPak,
    LocaleDirectory,
    LocalePak,
    SpeechDirectory,
    SpeechPak,
    AssetsPc,
    UnknownProjectSource,
    Other,
};

enum class SourceAddRecordResult : std::uint8_t {
    Failed,
    Recorded,
    DuplicateActiveKindRecorded,
};

enum class ContentLifetimeClass : std::uint8_t {
    SourceOnly,
    LevelOwnedResource,
    GlobalDataManager,
    AudioBank,
    ProcessGlobalAllowed,
    Unknown,
};

enum class BaselineProviderDisposition : std::uint8_t {
    Unresolved,
    BaselineOverride,
    CustomOnly,
    UnknownFallback,
};

enum class ContentOpenStagingPhase : std::uint8_t {
    Unknown,
    CustomContentStaging,
    TransitionActive,
    MapActive,
    Frontend,
};

enum class WatchedParserOutcome : std::uint8_t {
    Unknown,
    ParsedNew,
    ParsedReplaced,
    ParsedMerged,
    CacheHitSameEpoch,
    CacheHitPriorEpoch,
    RegistrationConflict,
    ParseFailed,
    CorrelationUnknown,
};

enum class LedgerDiagnosticType : std::uint16_t {
    StartupBaselineCaptured,
    EpochBegin,
    EpochEnd,
    SourceTracked,
    SourceRemovedNaturally,
    SourceRemovedByReset,
    SourceResetFailed,
    FileTracked,
    FileClassified,
    FileUnclassified,
    PackTracked,
    PackReleased,
    FrontendResetBegin,
    FrontendResetComplete,
    FrontendResetPartial,
    BaselineMatch,
    BaselineMismatch,
    SessionOnDestroyEnter,
    FrontendEpochBindAttempt,
    FrontendEpochBindSuccess,
    FrontendEpochBindFailed,
    FrontendOnDestroyUnloadObserved,
    FrontendOnDestroyUnloadMismatch,
    FrontendReleasePacksObserved,
    FrontendReleasePacksMissing,
    SessionOnDestroyOriginalReturned,
    FrontendSourceResetBegin,
    FrontendSourceResetResult,
    FrontendSourceResetComplete,
    FrontendSourceResetPartial,
    FrontendSourceResetFailed,
    FrontendLookupBeforeSourceReset,
    FrontendLookupAfterSourceReset,
    SameProjectTransitionNoSourceReset,
    LazyEpochCreateAttempt,
    LazyEpochCreated,
    LazyEpochReused,
    LazyEpochProjectRootConflict,
    LoaderSourceExcludedExact,
    CustomProjectSourceAllowedSameLoaderItem,
    OwnedFrontendUnloadEnter,
    OwnedFrontendUnloadRejected,
    OwnedFrontendUnloadCompleted,
    OwnedReleasePacksEnter,
    OwnedReleasePacksCompleted,
    OwnedReleaseReceiverAdded,
    OwnedReleaseReceiverOverflow,
    OwnedReleaseCountMismatch,
    FrontendLookupAfterUnboundOnDestroy,
    OwnershipMutationLost,
    DiagnosticObservationLost,
    IrrelevantObservationWithoutEpoch,
    Overflow,
    EpochRetiredVerified,
};

enum class LedgerLossReason : std::uint8_t {
    None,
    GenericOwnershipContention,
    SourceAddContention,
    SourceAddCapacity,
    SourceIdentityIncomplete,
    SourceRemovalContention,
    EpochContention,
    EpochCapacity,
    EpochConflict,
    ReleaseContention,
    ReleaseCapacity,
    BindingContention,
    ConsumptionContention,
    DiagnosticQueueOverflow,
    OptionalObservationContention,
    BroadFileObservationContention,
    BroadFileCapacity,
    IrrelevantWithoutEpoch,
};

struct LedgerLossState {
    std::uint32_t ownership_mutation_loss;
    std::uint32_t diagnostic_observation_loss;
    std::uint32_t broad_file_observation_loss;
    std::uint32_t irrelevant_without_epoch;
    LedgerLossReason last_ownership_reason;
    LedgerLossReason last_diagnostic_reason;
    LedgerLossReason last_broad_file_reason;
    LedgerLossReason last_irrelevant_reason;
    std::uint64_t broad_file_first_loss_sequence;
    std::uint64_t broad_file_last_loss_sequence;
};

enum class FrontendEpochSelectionStatus : std::uint8_t {
    SelectedByContentName,
    SelectedSingleActiveFallback,
    NoActiveEpoch,
    AmbiguousEpochs,
    NoActiveSources,
};

enum class ExactMapOwnerProofStatus : std::uint8_t {
    Pending,
    Ready,
    Rejected,
};

enum class ExactMapOwnerProofReason : std::uint8_t {
    None,
    NoEvidence,
    MapMismatch,
    TransitionMismatch,
    Consumed,
    AmbiguousEvidence,
    NoActiveEpoch,
    OwnershipIncomplete,
    SourceInvalid,
    InvalidPath,
    LedgerBusy,
};

enum class PresentationSourceSetSnapshotResult : std::uint8_t {
    InvalidArgument,
    Succeeded,
    LedgerBusy,
    EpochMissing,
    ProjectRootInvalid,
    ProjectRootMismatch,
    OwnershipIncomplete,
    ExactHandleIncomplete,
    DuplicateSourceKind,
    RequiredSourceKindMissing,
};

struct CustomContentEpoch {
    std::uint32_t id;
    std::uint64_t begin_sequence;

    char selected_content_name[kSelectedContentNameCapacity];
    WCHAR project_root[kProjectRootCapacity];
    char normalized_project_root[kSourcePathCapacity];

    std::uint32_t successful_source_count;

    bool active;
    bool accepting_records;
    bool frontend_reset_pending;
    bool outgoing_level_release_observed;
    bool audio_quiesced;
    bool sources_removed;
    bool global_data_restored;
    bool audio_restored;
    bool selected_content_name_available;
    bool ownership_complete;
    bool ownership_mutation_lost;
    bool project_root_conflict;
    bool consumed;
};

struct CustomSourceRecord {
    void* source;

    char original_path[kSourcePathCapacity];
    char normalized_path[kSourcePathCapacity];

    std::uint32_t flags;
    std::uint32_t epoch;
    std::uint32_t successful_add_ordinal;

    CustomSourceKind kind;

    bool add_succeeded;
    bool active;
    bool removed_naturally;
    bool removed_by_reset;
    bool exact_handle_available;
    bool loader_owned_custom_content;
    bool path_created_ownership;
    bool reset_failed;
};

struct CustomSourceIdentitySnapshot {
    std::uint32_t epoch;
    std::uint32_t source_record_index;
    std::uint32_t successful_add_ordinal;
    CustomSourceKind kind;
    bool source_active;
    bool epoch_active;
    bool epoch_accepting_records;
    bool epoch_consumed;
    char normalized_source_path[kSourcePathCapacity];
    char normalized_project_root[kSourcePathCapacity];
};

struct CustomFileLoadRecord {
    std::uint32_t epoch;
    std::uint32_t open_count;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    CustomSourceKind source_kind;

    char logical_path[kLogicalPathCapacity];
    char resolved_source_path[kSourcePathCapacity];
    char last_resolved_source_path[kSourcePathCapacity];
    char normalized_project_root[kSourcePathCapacity];

    std::uintptr_t source;
    std::uintptr_t last_source;
    std::uintptr_t caller_address;
    std::uint32_t caller_rva;
    std::uint32_t thread_id;
    std::int32_t open_mode;
    std::uint32_t open_flags;

    char caller_module[kCallerModuleCapacity];

    ContentLifetimeClass lifetime;
    std::uint32_t reset_adapter_id;

    std::uint64_t first_open_sequence;
    std::uint64_t last_open_sequence;
    std::uint32_t provider_change_count;
    std::uint64_t first_transition_generation;
    std::uint64_t last_transition_generation;
    ContentOpenStagingPhase first_staging_phase;
    ContentOpenStagingPhase last_staging_phase;
    std::uint16_t first_update_dlc_depth;
    std::uint16_t last_update_dlc_depth;

    WatchedParserOutcome parser_outcome;
    std::uint64_t parser_registration_generation;
    std::uint32_t parsed_object_epoch;
    std::uint64_t parser_input_size;
    std::uintptr_t parser_existing_object;
    std::uintptr_t parser_result_object;
    char parser_input_hash[65];
    char parser_registry_key[kLogicalPathCapacity];

    BaselineProviderDisposition provider_disposition;
    bool fallback_provider_exists;
    bool overrides_baseline_provider;
    bool classification_complete;
    bool baseline_probe_complete;
};

struct CustomPackRecord {
    std::uint32_t epoch;
    std::uintptr_t pack;
    std::uint64_t first_sequence;
    std::uint64_t release_sequence;
    char pack_path[kSourcePathCapacity];
    bool level_owned;
    bool released_naturally;
    bool observed_after_frontend_reset;
};

struct LedgerDiagnosticEvent {
    std::uint64_t sequence;
    LedgerDiagnosticType type;
    std::uint32_t epoch;
    std::uint32_t result;
    std::uintptr_t object;
    std::uint64_t value0;
    std::uint64_t value1;
    char text0[256];
    char text1[256];
};

struct FrontendEpochSelection {
    FrontendEpochSelectionStatus status;
    CustomContentEpoch epoch;
    std::uint32_t active_epoch_count;
    std::uint32_t active_source_count;
    std::uint32_t active_exact_owned_source_count;
    bool content_name_available;
};

struct ExactMapOwnerEvidence {
    std::uint32_t epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uint64_t transition_generation;
    std::uint64_t open_sequence;
    char map_name[kMapNameCapacity];
    char normalized_project_root[kSourcePathCapacity];
    bool present;
    bool consumed;
    bool ambiguous;
};

struct ExactMapOwnerProofSnapshot {
    ExactMapOwnerProofStatus status;
    ExactMapOwnerProofReason reason;
    std::uint32_t epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uint64_t transition_generation;
    char map_name[kMapNameCapacity];
    WCHAR project_root[kProjectRootCapacity];
    char normalized_project_root[kSourcePathCapacity];
};

struct SourceResetSnapshot {
    std::uint32_t record_index;
    std::uint32_t epoch;
    std::uint32_t successful_add_ordinal;
    void* source;
    std::uint32_t flags;
    CustomSourceKind kind;
    bool exact_handle_available;
    bool path_created_ownership;
    char original_path[kSourcePathCapacity];
    char normalized_path[kSourcePathCapacity];
};

struct SourceSnapshotResult {
    std::size_t count;
    std::uint32_t expected_active_count;
    std::uint32_t dropped_mutations;
    bool complete;
};

struct PresentationSourceSetSnapshot {
    PresentationSourceSetSnapshotResult result;
    std::uint32_t epoch;
    std::uint32_t project_root_count;
    std::uint32_t data_directory_count;
    std::uint32_t assets_pc_count;
    std::uint32_t active_source_count;
    bool ownership_complete;
    bool exact_handles_complete;
    bool root_equal;
    bool complete;
    char presenter_root_canonical[kSourcePathCapacity];
    char epoch_root_canonical[kSourcePathCapacity];
};

struct PlayerPresetRepairSourceProof {
    std::uint32_t old_epoch;
    std::uint32_t current_epoch;
    std::uint32_t current_active_exact_source_count;
    bool old_data_source_identified;
    bool old_data_sources_inactive;
    bool current_epoch_active;
    bool current_epoch_sources_ready;
    bool ownership_complete;
};

struct LedgerSnapshot {
    CustomContentEpoch epochs[kMaximumEpochs];
    CustomSourceRecord sources[kMaximumSources];
    CustomFileLoadRecord files[kMaximumFiles];
    CustomPackRecord packs[kMaximumPacks];
    std::uint32_t epoch_count;
    std::uint32_t source_count;
    std::uint32_t file_count;
    std::uint32_t pack_count;
    std::uint32_t current_epoch;
    std::uint32_t dropped_mutations;
    LedgerLossState losses;
    bool baseline_captured;
};

struct CustomFileMutationToken {
    std::uint32_t record_index;
    std::uint32_t epoch;
    std::uint32_t source_add_ordinal;
    std::uintptr_t source;
    std::uint64_t first_open_sequence;
};

struct CustomPackMutationToken {
    std::uint32_t record_index;
    std::uint32_t epoch;
    std::uintptr_t pack;
    std::uint64_t first_sequence;
};

/* Capture the immutable ledger half after observer admission has closed. The
 * coordinator supplies the independently verified transaction and quiescence
 * half only after every native/SDK consumer and provider domain has finished. */
struct VerifiedEpochRetirement {
    std::uint32_t epoch;
    std::uint64_t begin_sequence;
    std::uint64_t records_fingerprint;
    LedgerLossState losses;
    std::uint64_t verified_transaction_token;
    std::uint64_t observer_quiescence_token;
    std::uint32_t pending_observer_callbacks;
    std::uint32_t external_active_references;
    bool transaction_verified;
    bool observer_admission_closed;
    bool quarantined;
};

struct LedgerRetirementSummary {
    std::uint32_t epoch;
    std::uint64_t begin_sequence;
    std::uint64_t retired_sequence;
    std::uint64_t records_fingerprint;
    std::uint64_t verified_transaction_token;
    std::uint64_t observer_quiescence_token;
    std::uint32_t sources;
    std::uint32_t files;
    std::uint32_t packs;
    std::uint64_t file_opens;
    LedgerLossState losses;
};

struct LedgerRetirementHistory {
    LedgerRetirementSummary recent[kMaximumRetirementSummaries];
    std::uint32_t next_recent;
    std::uint64_t epochs;
    std::uint64_t sources;
    std::uint64_t files;
    std::uint64_t packs;
    std::uint64_t file_opens;
    std::uint64_t summaries_overwritten;
};

enum class EpochRetirementResult : std::uint8_t {
    Retired, InvalidReceipt, LedgerBusy, EpochMissing, EpochNotConsumed,
    OwnershipIncomplete, ResourcesLive, GlobalRecordRetained,
    UnclassifiedObservation, FenceChanged, Ready,
};

struct CustomContentLedger {
    volatile LONG lock;
    volatile LONG dropped_mutations;
    volatile LONG ownership_mutation_loss;
    volatile LONG diagnostic_observation_loss;
    volatile LONG broad_file_observation_loss;
    volatile LONG irrelevant_without_epoch;
    volatile LONG last_ownership_loss_reason;
    volatile LONG last_diagnostic_loss_reason;
    volatile LONG last_broad_file_loss_reason;
    volatile LONG last_irrelevant_loss_reason;
    volatile LONGLONG broad_file_first_loss_sequence;
    volatile LONGLONG broad_file_last_loss_sequence;
    volatile LONGLONG sequence;

    CustomContentEpoch epochs[kMaximumEpochs];
    CustomSourceRecord sources[kMaximumSources];
    CustomFileLoadRecord files[kMaximumFiles];
    CustomPackRecord packs[kMaximumPacks];
    LedgerDiagnosticEvent diagnostics[kMaximumDiagnostics];
    ExactMapOwnerEvidence exact_map_owner;
    LedgerRetirementHistory retirement_history;
    bool source_slot_reused[kMaximumSources];
    bool file_slot_reused[kMaximumFiles];

    std::uint32_t epoch_count;
    std::uint32_t source_count;
    std::uint32_t file_count;
    std::uint32_t pack_count;
    std::uint32_t diagnostic_head;
    std::uint32_t diagnostic_tail;
    std::uint32_t current_epoch;
    std::uint32_t next_epoch_id;
    std::uint32_t next_source_ordinal;

    bool baseline_captured;
    bool diagnostic_overflow_latched;
};

void Initialize(CustomContentLedger* ledger) noexcept;
bool MarkBaselineCaptured(CustomContentLedger* ledger,
                          bool captured,
                          std::uint64_t sequence) noexcept;

bool NormalizeLogicalPath(const char* input, char* output,
                          std::size_t capacity) noexcept;
bool NormalizePhysicalPath(const char* input, char* output,
                           std::size_t capacity) noexcept;
bool NormalizeProjectRoot(const char* input, char* output,
                          std::size_t capacity) noexcept;
bool PathsEqual(const char* left, const char* right) noexcept;
bool ProjectRootsEqual(const char* left, const char* right) noexcept;
bool PathWithinRoot(const char* path, const char* root) noexcept;

CustomSourceKind ClassifySourceKind(const char* normalized_path,
                                    const char* normalized_project_root) noexcept;
const char* SourceKindName(CustomSourceKind kind) noexcept;
const char* LifetimeName(ContentLifetimeClass lifetime) noexcept;
const char* ProviderDispositionName(
    BaselineProviderDisposition disposition) noexcept;
const char* ContentOpenStagingPhaseName(
    ContentOpenStagingPhase phase) noexcept;
const char* WatchedParserOutcomeName(
    WatchedParserOutcome outcome) noexcept;

std::uint64_t NextSequence(CustomContentLedger* ledger) noexcept;

std::uint32_t BeginEpoch(CustomContentLedger* ledger,
                         const char* selected_content_name,
                         const WCHAR* project_root,
                         const char* normalized_project_root,
                         std::uint64_t sequence) noexcept;
bool CloseEpochForFrontend(CustomContentLedger* ledger,
                           std::uint64_t sequence) noexcept;
bool CloseEpochForFrontend(CustomContentLedger* ledger,
                           std::uint32_t epoch,
                           std::uint64_t sequence) noexcept;
std::uint32_t CurrentEpochId(CustomContentLedger* ledger) noexcept;
bool TryGetCurrentEpochId(CustomContentLedger* ledger,
                          std::uint32_t* epoch_out) noexcept;
bool SnapshotEpoch(CustomContentLedger* ledger, std::uint32_t epoch,
                   CustomContentEpoch* output) noexcept;
bool MarkEpochComponentResults(CustomContentLedger* ledger,
                               std::uint32_t epoch,
                               bool audio_quiesced,
                               bool sources_removed,
                               bool global_data_restored,
                               bool audio_restored,
                               std::uint64_t sequence) noexcept;
bool MarkEpochOwnershipLost(CustomContentLedger* ledger,
                            std::uint32_t epoch,
                            LedgerLossReason reason,
                            bool project_root_conflict,
                            std::uint64_t sequence) noexcept;
bool MarkEpochConsumed(CustomContentLedger* ledger,
                       std::uint32_t epoch,
                       std::uint64_t sequence) noexcept;
bool UpdateEpochOptionalContentName(CustomContentLedger* ledger,
                                    std::uint32_t epoch,
                                    const char* selected_content_name,
                                    std::uint64_t sequence) noexcept;

bool RecordSuccessfulSourceAdd(CustomContentLedger* ledger, void* source,
                               const char* original_path,
                               const char* normalized_path,
                               std::uint32_t flags,
                               CustomSourceKind kind,
                               bool loader_owned_custom_content,
                               std::uint64_t sequence,
                               bool path_created_ownership = false) noexcept;
bool RecordSuccessfulSourceAddForEpoch(
    CustomContentLedger* ledger, std::uint32_t epoch, void* source,
    const char* original_path, const char* normalized_path,
    std::uint32_t flags, CustomSourceKind kind,
    bool loader_owned_custom_content, std::uint64_t sequence,
    bool path_created_ownership = false) noexcept;
SourceAddRecordResult RecordSuccessfulSourceAddForEpochDetailed(
    CustomContentLedger* ledger, std::uint32_t epoch, void* source,
    const char* original_path, const char* normalized_path,
    std::uint32_t flags, CustomSourceKind kind,
    bool loader_owned_custom_content, std::uint64_t sequence,
    bool path_created_ownership = false) noexcept;
bool RecordNaturalSourceRemoval(CustomContentLedger* ledger, void* source,
                                const char* path, bool succeeded,
                                std::uint64_t sequence) noexcept;

std::size_t SnapshotActiveSourcesForReset(
    CustomContentLedger* ledger, SourceResetSnapshot* output,
    std::size_t capacity, std::uint64_t sequence) noexcept;
std::size_t SnapshotActiveSourcesThroughEpoch(
    CustomContentLedger* ledger, std::uint32_t maximum_epoch,
    SourceResetSnapshot* output, std::size_t capacity,
    std::uint64_t sequence) noexcept;
SourceSnapshotResult SnapshotActiveSourcesThroughEpochComplete(
    CustomContentLedger* ledger, std::uint32_t maximum_epoch,
    SourceResetSnapshot* output, std::size_t capacity,
    std::uint64_t sequence) noexcept;
SourceSnapshotResult SnapshotActiveSourcesForEpochComplete(
    CustomContentLedger* ledger, std::uint32_t epoch,
    SourceResetSnapshot* output, std::size_t capacity,
    std::uint64_t sequence) noexcept;
PresentationSourceSetSnapshotResult SnapshotPresentationSourceSet(
    CustomContentLedger* ledger, std::uint32_t epoch,
    const char* normalized_project_root,
    PresentationSourceSetSnapshot* output) noexcept;
const char* PresentationSourceSetSnapshotResultName(
    PresentationSourceSetSnapshotResult result) noexcept;
bool MarkSourceResetResult(CustomContentLedger* ledger,
                           std::uint32_t record_index,
                           bool removed,
                           std::uint64_t sequence,
                           const SourceResetSnapshot* expected = nullptr) noexcept;
bool RecordNaturalSourceRemovalExact(CustomContentLedger* ledger,
                                     const SourceResetSnapshot* expected,
                                     bool succeeded,
                                     std::uint64_t sequence) noexcept;
bool SnapshotSourceMutationToken(CustomContentLedger* ledger,
                                 std::uint32_t record_index,
                                 std::uint32_t expected_epoch,
                                 SourceResetSnapshot* output) noexcept;
/* Capture before calling a natural removal. A path selector must identify one
 * active custom source; the captured incarnation is never rediscovered later. */
bool SnapshotNaturalSourceMutationToken(CustomContentLedger* ledger,
                                        void* source, const char* path,
                                        SourceResetSnapshot* output) noexcept;
bool CloseCancelledEpochConsumed(CustomContentLedger* ledger,
                                 std::uint32_t epoch,
                                 std::uint64_t sequence) noexcept;

bool IsTrackedCustomSource(CustomContentLedger* ledger, void* source,
                           const char* resolved_source_path,
                           std::uint32_t* epoch_output,
                           bool* rejected_closed_epoch_output = nullptr) noexcept;
bool LookupExactCustomSourceIdentity(
    CustomContentLedger* ledger, void* source,
    const char* resolved_source_path,
    CustomSourceIdentitySnapshot* output) noexcept;
bool EvaluatePlayerPresetRepairSourceProof(
    CustomContentLedger* ledger, std::uint32_t old_epoch,
    std::uint32_t current_epoch, void* old_source,
    const char* old_provider,
    PlayerPresetRepairSourceProof* output) noexcept;
bool RecordCustomFileOpen(CustomContentLedger* ledger,
                          std::uint32_t epoch, const char* logical_path,
                          const char* resolved_source_path, void* source,
                          std::uintptr_t caller_address,
                          std::uint32_t caller_rva,
                          const char* caller_module,
                          std::uint32_t thread_id,
                          std::uint64_t sequence,
                          std::uint32_t* record_index_output = nullptr,
                          const CustomSourceIdentitySnapshot* attribution =
                              nullptr,
                          std::uint64_t transition_generation = 0,
                          ContentOpenStagingPhase staging_phase =
                              ContentOpenStagingPhase::Unknown,
                          std::uint16_t update_dlc_depth = 0) noexcept;
bool RecordExactMapOwnerEvidence(
    CustomContentLedger* ledger, const char* logical_path,
    const CustomSourceIdentitySnapshot* attribution,
    std::uint64_t transition_generation,
    std::uint64_t open_sequence) noexcept;
ExactMapOwnerProofStatus SnapshotExactMapOwnerProof(
    CustomContentLedger* ledger, const char* map_name,
    std::uint64_t transition_generation,
    ExactMapOwnerProofSnapshot* output) noexcept;
bool ConsumeExactMapOwnerProof(
    CustomContentLedger* ledger,
    const ExactMapOwnerProofSnapshot* proof) noexcept;
bool RecordFileParserObservation(
    CustomContentLedger* ledger, std::uint32_t record_index,
    WatchedParserOutcome outcome, const char* input_hash,
    std::uint64_t input_size, const char* registry_key,
    std::uintptr_t existing_object, std::uintptr_t result_object,
    std::uint64_t registration_generation,
    std::uint32_t parsed_object_epoch,
    std::uint64_t sequence,
    const CustomFileMutationToken* expected = nullptr) noexcept;
bool ClassifyFile(CustomContentLedger* ledger, std::uint32_t record_index,
                  ContentLifetimeClass lifetime,
                  std::uint32_t reset_adapter_id,
                  bool complete,
                  std::uint64_t sequence,
                  const CustomFileMutationToken* expected = nullptr) noexcept;
bool RecordFileOpenContract(CustomContentLedger* ledger,
                            std::uint32_t record_index,
                            std::int32_t open_mode,
                            std::uint32_t open_flags,
                            const CustomFileMutationToken* expected = nullptr) noexcept;
bool RecordProviderProbe(CustomContentLedger* ledger,
                         std::uint32_t record_index,
                         BaselineProviderDisposition disposition,
                         bool fallback_exists,
                         bool overrides_baseline,
                         std::uint64_t sequence,
                         const CustomFileMutationToken* expected = nullptr) noexcept;
bool SnapshotFileMutationToken(CustomContentLedger* ledger,
                               std::uint32_t record_index,
                               std::uint32_t expected_epoch,
                               CustomFileMutationToken* output) noexcept;

bool CaptureEpochRetirementCandidate(CustomContentLedger* ledger,
                                     std::uint32_t epoch,
                                       VerifiedEpochRetirement* output) noexcept;
// Same eligibility/fingerprint checks as retirement, without changing slots
// or history. A Ready result must still be revalidated by the actual commit.
EpochRetirementResult CheckEpochRetirement(CustomContentLedger*,const VerifiedEpochRetirement*) noexcept;
EpochRetirementResult RetireVerifiedEpoch(CustomContentLedger* ledger,
                                        const VerifiedEpochRetirement* receipt,
                                        std::uint64_t sequence,
                                        LedgerRetirementSummary* output) noexcept;
bool SnapshotRetirementHistory(CustomContentLedger* ledger,
                                LedgerRetirementHistory* output) noexcept;

bool RecordPackLoad(CustomContentLedger* ledger, void* pack,
                    const char* pack_path, std::uint64_t sequence) noexcept;
bool RecordPackRelease(CustomContentLedger* ledger, void* pack,
                       const char* pack_path,
                       std::uint64_t sequence,
                       const CustomPackMutationToken* expected = nullptr) noexcept;
bool SnapshotPackMutationToken(CustomContentLedger* ledger,
                               void* pack, const char* pack_path,
                               std::uint32_t expected_epoch,
                               CustomPackMutationToken* output) noexcept;
bool MarkOutgoingLevelRelease(CustomContentLedger* ledger,
                              std::uint32_t epoch,
                              std::uint64_t sequence) noexcept;
bool FindUniqueActiveEpochByContentName(
    CustomContentLedger* ledger, const char* selected_content_name,
    CustomContentEpoch* output) noexcept;
FrontendEpochSelection SelectFrontendResetEpoch(
    CustomContentLedger* ledger, const char* selected_content_name,
    bool content_name_available) noexcept;
std::uint32_t DroppedMutationCount(
    const CustomContentLedger* ledger) noexcept;
void RecordOwnershipMutationLoss(CustomContentLedger* ledger,
                                 LedgerLossReason reason) noexcept;
void RecordDiagnosticObservationLoss(CustomContentLedger* ledger,
                                     LedgerLossReason reason) noexcept;
void RecordBroadFileObservationLoss(CustomContentLedger* ledger,
                                    LedgerLossReason reason,
                                    std::uint64_t sequence) noexcept;
void RecordIrrelevantObservationWithoutEpoch(
    CustomContentLedger* ledger, LedgerLossReason reason) noexcept;
LedgerLossState CaptureLossState(
    const CustomContentLedger* ledger) noexcept;
const char* LedgerLossReasonName(LedgerLossReason reason) noexcept;

bool RecordLifecycleDiagnostic(CustomContentLedger* ledger,
                               LedgerDiagnosticType type,
                               std::uint32_t epoch,
                               std::uint32_t result,
                               std::uintptr_t object,
                               std::uint64_t value0,
                               std::uint64_t value1,
                               const char* text0,
                               const char* text1,
                               std::uint64_t sequence) noexcept;

std::uint32_t CountActiveCustomSources(CustomContentLedger* ledger) noexcept;
std::uint32_t CountActiveCustomSourcesThroughEpoch(
    CustomContentLedger* ledger, std::uint32_t maximum_epoch) noexcept;
std::uint32_t CountActiveCustomSourcesForEpoch(
    CustomContentLedger* ledger, std::uint32_t epoch) noexcept;
std::uint32_t CountUnconsumedCustomEpochs(
    CustomContentLedger* ledger) noexcept;
std::uint32_t CountUnknownPersistentFiles(CustomContentLedger* ledger) noexcept;
bool CanReportBaselineMatch(CustomContentLedger* ledger) noexcept;

bool CaptureSnapshot(CustomContentLedger* ledger,
                     LedgerSnapshot* output) noexcept;
std::size_t DrainDiagnostics(CustomContentLedger* ledger,
                             LedgerDiagnosticEvent* output,
                             std::size_t capacity) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::content

#endif
