#ifndef DLUMA_FIXES_AUDIO_BANK_PROVIDER_CORRELATION_H
#define DLUMA_FIXES_AUDIO_BANK_PROVIDER_CORRELATION_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio {

/* Exact Dying Light Player evidence for the provider-bearing native open.
 * The hook which feeds this helper must independently validate the supported
 * engine/filesystem images and the call-site bytes before using these RVAs. */
constexpr std::uint32_t kSoundBankManagerNativeOpenCallRva = 0x00100C43U;
constexpr std::uint32_t kSoundBankManagerNativeOpenReturnRva = 0x00100C49U;
constexpr std::uint32_t kSpeechBankNativeOpenCallRva = 0x0032A8F2U;
constexpr std::uint32_t kSpeechBankNativeOpenReturnRva = 0x0032A8F8U;
constexpr std::uint32_t kFilesystemOpenNativeRva = 0x00003C80U;
constexpr std::uint32_t kFilesystemFullPathRva = 0x00014D80U;
constexpr std::uint32_t kFilesystemFullPathSelectedSourceJoinRva =
    0x00014FABU;
constexpr std::uint32_t kFilesystemFullPathProviderProbeRva =
    0x00014FC0U;
constexpr std::size_t kFilesystemFullPathProviderProbeStolenSize = 18U;
constexpr std::int32_t kSoundBankNativeOpenMode = 0;
constexpr std::uint32_t kSoundBankNativeOpenFlags = 0x40U;
constexpr std::uint32_t kSpeechBankNativeOpenFlags = 0x00U;

constexpr std::size_t kAudioProviderFrameCapacity = 8;
constexpr std::size_t kAudioProviderLogicalDepthCapacity = 32;
constexpr std::size_t kAudioProviderCompletedOpenCapacity = 4;
constexpr std::size_t kAudioProviderBankNameCapacity = 256;
constexpr std::size_t kAudioProviderPathCapacity = 1024;
constexpr std::uint16_t kAudioProviderInvalidFrame = UINT16_MAX;

/* The interior fs::full_path probe cannot call C++ safely: it is entered by
 * a jump from the middle of another module's function.  A fixed slot indexed
 * by the Windows thread id lets the assembly seam publish one pointer without
 * allocating, taking a lock, changing the engine stack, or requiring a
 * callback.  Hash collisions and nested same-thread scopes fail closed. */
constexpr std::size_t kAudioFullPathProbeSlotCapacity = 256U;
constexpr std::uint32_t kAudioFullPathProbeSlotMask = 0xFFU;

enum class AudioFullPathProbeOwner : std::uint32_t {
    None,
    Fmod,
    Speech,
    Resource,
};

enum class AudioFullPathProbeArmResult : std::uint8_t {
    Armed,
    InvalidArgument,
    SlotCollision,
    NestedSameThread,
};

enum class AudioFullPathProbeDrainResult : std::uint8_t {
    NoObservation,
    ExactSource,
    MultipleSources,
    InvalidToken,
    SourceUnavailable,
};

struct AudioFullPathProbeSlot {
    volatile std::int32_t thread_id;
    volatile std::int32_t armed;
    volatile std::int32_t owner;
    volatile std::int32_t observation_count;
    volatile std::int64_t scope_generation;
    volatile std::int64_t selected_source;
};

struct AudioFullPathProbeState {
    volatile std::int32_t active_slot_count;
    std::int32_t reserved;
    AudioFullPathProbeSlot slots[kAudioFullPathProbeSlotCapacity];
};

struct AudioFullPathProbeToken {
    std::uint64_t scope_generation;
    std::uint32_t thread_id;
    std::uint16_t slot_index;
    AudioFullPathProbeOwner owner;
    bool armed;
};

static_assert(sizeof(AudioFullPathProbeSlot) == 32U,
              "assembly probe slot stride changed");
static_assert(offsetof(AudioFullPathProbeState, slots) == 8U,
              "assembly probe slot offset changed");

enum class AudioNativeOpenBeginResult : std::uint8_t {
    Armed,
    NoActiveBankLoad,
    DifferentCallSite,
    Suppressed,
    ArgumentContractMismatch,
    PathUnavailable,
    GenerationExhausted,
};

enum class AudioNativeOpenCompleteResult : std::uint8_t {
    Correlated,
    CorrelatedOpenFailed,
    CorrelatedAdditionalOpen,
    InvalidAttempt,
    Suppressed,
    GenerationMismatch,
    DuplicateCompletion,
    CompletionCapacityExceeded,
};

enum class AudioBankProviderEndResult : std::uint8_t {
    Completed,
    Suppressed,
    InvalidToken,
    GenerationMismatch,
};

enum class AudioResolvedSourceObservationResult : std::uint8_t {
    Correlated,
    NoActiveBankLoad,
    Suppressed,
    InvalidSource,
    MultipleResolutions,
    GenerationMismatch,
};

enum class AudioBankProviderOutcome : std::uint8_t {
    ExactProvider,
    CacheHitNoOpen,
    LoadFailed,
    MissingNativeOpen,
    UnexpectedNativeOpen,
    ArgumentContractMismatch,
    PathUnavailable,
    MultipleNativeOpens,
    SourceIdentityUnavailable,
    PendingNativeOpen,
    StackOverflow,
    GenerationMismatch,
};

struct AudioBankLoadToken {
    std::uint64_t generation;
    std::uint32_t logical_depth;
    std::uint16_t frame_index;
    bool tracked;
    bool suppressed;
};

/* This object is intended to live on the fs::open_native hook's stack across
 * its one original call.  frame_index + scope_generation are revalidated on
 * return because the original may re-enter SoundBankManager::LoadBank. */
struct AudioNativeOpenAttempt {
    std::uint64_t attempt_generation;
    std::uint64_t scope_generation;
    std::uint64_t enter_sequence;
    std::uint32_t caller_return_rva;
    std::uint16_t frame_index;
    std::uintptr_t selected_source;
    bool armed;
    bool callsite_matched;
    bool argument_contract_matched;
    bool path_copy_complete;
    bool exact_source;
    std::uint64_t native_file_instance_generation;
    std::uint64_t provider_instance_generation;
    char provider_path[kAudioProviderPathCapacity];
};

struct AudioNativeOpenObservation {
    std::uint64_t attempt_generation;
    std::uint64_t enter_sequence;
    std::uint64_t exit_sequence;
    std::uintptr_t native_file;
    std::uintptr_t selected_source;
    std::uint32_t caller_return_rva;
    bool callsite_matched;
    bool argument_contract_matched;
    bool path_copy_complete;
    bool open_succeeded;
    bool exact_source;
    std::uint64_t native_file_instance_generation;
    std::uint64_t provider_instance_generation;
    char provider_path[kAudioProviderPathCapacity];
};

struct AudioBankProviderFrame {
    std::uint64_t generation;
    std::uint64_t enter_sequence;
    std::uintptr_t manager;
    std::uint32_t logical_depth;
    std::uint16_t completed_generation_count;
    std::uint16_t native_open_count;
    std::uint16_t pending_open_count;
    std::uint16_t resolved_source_count;
    std::uintptr_t selected_source;
    bool reload;
    bool localised;
    bool bank_name_copy_complete;
    bool argument_contract_loss;
    bool path_copy_loss;
    bool multiple_open_loss;
    bool completion_overflow;
    bool nested_stack_overflow;
    bool generation_mismatch_loss;
    bool first_open_available;
    bool resolved_source_observation_required;
    bool source_identity_loss;
    std::uint64_t completed_generations[
        kAudioProviderCompletedOpenCapacity];
    char bank_name[kAudioProviderBankNameCapacity];
    AudioNativeOpenObservation first_open;
};

/* One context is owned by one thread (normally a thread_local instance).  It
 * deliberately contains no lock, heap owner, STL container, or engine object.
 * Different engine threads must use different contexts. */
struct AudioBankProviderCorrelationContext {
    std::uint64_t next_scope_generation;
    std::uint64_t next_open_generation;
    std::uint64_t active_generation[
        kAudioProviderLogicalDepthCapacity];
    std::uint32_t logical_depth;
    std::uint32_t suppression_depth;
    std::uint32_t dropped_scope_count;
    std::uint32_t dropped_open_count;
    std::uint32_t generation_mismatch_count;
    bool hard_overflow_latched;
    bool integrity_loss_latched;
    bool generation_exhausted;
    bool resolved_source_observation_required;
    AudioBankProviderFrame frames[kAudioProviderFrameCapacity];
};

struct AudioBankProviderRecord {
    std::uint64_t generation;
    std::uint64_t enter_sequence;
    std::uint64_t exit_sequence;
    std::uintptr_t manager;
    std::uint32_t logical_depth;
    std::int32_t manager_result;
    std::uint16_t native_open_count;
    std::uint16_t resolved_source_count;
    std::uintptr_t selected_source;
    bool reload;
    bool localised;
    bool bank_name_copy_complete;
    bool callsite_matched;
    bool argument_contract_matched;
    bool path_copy_complete;
    bool open_succeeded;
    bool provider_identity_available;
    bool exact_source_identity_available;
    bool observation_complete;
    bool nested_stack_overflow;
    bool multiple_native_opens;
    bool generation_mismatch;
    bool source_identity_loss;
    AudioBankProviderOutcome outcome;
    char bank_name[kAudioProviderBankNameCapacity];
    AudioNativeOpenObservation first_open;
};

void InitializeAudioBankProviderCorrelation(
    AudioBankProviderCorrelationContext* context) noexcept;

void InitializeAudioFullPathProbeState(
    AudioFullPathProbeState* state) noexcept;

AudioFullPathProbeArmResult ArmAudioFullPathProbe(
    AudioFullPathProbeState* state, std::uint32_t thread_id,
    AudioFullPathProbeOwner owner, std::uint64_t scope_generation,
    AudioFullPathProbeToken* token) noexcept;

AudioFullPathProbeDrainResult DrainAudioFullPathProbe(
    AudioFullPathProbeState* state,
    const AudioFullPathProbeToken& token,
    std::uintptr_t* selected_source) noexcept;

bool DisarmAudioFullPathProbe(
    AudioFullPathProbeState* state,
    AudioFullPathProbeToken* token) noexcept;

/* Enables the exact fs::full_path interior-source seam for subsequently
 * opened scopes. It never changes an already-active scope's contract. */
bool SetAudioResolvedSourceObservationRequired(
    AudioBankProviderCorrelationContext* context,
    bool required) noexcept;

AudioBankLoadToken BeginAudioBankLoad(
    AudioBankProviderCorrelationContext* context,
    std::uintptr_t manager, const char* bank_name,
    bool bank_name_copy_complete, bool reload, bool localised,
    std::uint64_t enter_sequence) noexcept;

AudioNativeOpenBeginResult BeginAudioNativeOpen(
    AudioBankProviderCorrelationContext* context,
    std::uint32_t caller_return_rva, std::uintptr_t explicit_source,
    std::int32_t mode, std::uint32_t flags,
    const char* full_path, bool path_copy_complete,
    std::uint64_t enter_sequence,
    AudioNativeOpenAttempt* attempt) noexcept;

AudioNativeOpenCompleteResult CompleteAudioNativeOpen(
    AudioBankProviderCorrelationContext* context,
    AudioNativeOpenAttempt* attempt, std::uintptr_t native_file,
    std::uint64_t exit_sequence) noexcept;

/* Called only by the exact 0x14FC0 assembly probe while fs::full_path still
 * owns the shared source-list lock. Stores the pointer in TLS/POD only. */
AudioResolvedSourceObservationResult ObserveAudioResolvedSource(
    AudioBankProviderCorrelationContext* context,
    std::uintptr_t selected_source) noexcept;

AudioBankProviderEndResult EndAudioBankLoad(
    AudioBankProviderCorrelationContext* context,
    const AudioBankLoadToken& token, std::int32_t manager_result,
    std::uint64_t exit_sequence,
    AudioBankProviderRecord* output) noexcept;

bool AudioBankProviderCorrelationIdle(
    const AudioBankProviderCorrelationContext* context) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio

#endif
