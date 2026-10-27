#ifndef DLUMA_FIXES_AUDIO_STARTUP_OBSERVATION_LEDGER_H
#define DLUMA_FIXES_AUDIO_STARTUP_OBSERVATION_LEDGER_H

#include "../../dlw_win32.h"
#include "CustomContentLedger.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_startup {

constexpr std::size_t kAudioStartupScriptCapacity = 32;
constexpr std::size_t kAudioStartupBankCapacity = 256;
constexpr std::size_t kAudioStartupLogicalPathCapacity = 256;
constexpr std::size_t kAudioStartupProviderPathCapacity = 1024;

enum class AudioStartupBankOwner : std::uint8_t {
    Unknown,
    Fmod,
    Speech,
};

enum class AudioStartupProviderOutcome : std::uint8_t {
    Unknown,
    /* Exact physical native path; CFSSource ownership remains separate. */
    NaturalOpenResolvedPath,
    CacheHitPriorExact,
    CacheHitProviderMissing,
    LoadFailed,
    ObservationIncomplete,
};

struct AudioStartupProvider {
    std::uintptr_t source;
    std::uint64_t source_registry_generation;
    bool exact_source;
    bool path_complete;
    bool source_registry_generation_observed;
    char path[kAudioStartupProviderPathCapacity];
    // Immutable CFSSource lifetime key from NativeFileHookBridge. This is
    // distinct from the changing source-list publication revision above.
    std::uint64_t instance_generation;
};

struct AudioStartupScriptObservation {
    std::uint32_t epoch;
    std::uint64_t enter_sequence;
    std::uint64_t exit_sequence;
    std::uintptr_t loader;
    AudioStartupProvider provider;
    bool request_complete;
    bool parse_succeeded;
    char logical_path[kAudioStartupLogicalPathCapacity];
    /* Optional exact ENGINE Parser_GetTreeFromMemory observation. These bytes
     * include the generated definition and decoded UTF16 script, not raw SCR
     * file bytes. Only the authenticated AudioScript scope may publish them. */
    unsigned char parser_input_sha256[32];
    std::uint64_t parser_input_bytes;
    std::uint32_t parser_input_observations;
    bool parser_input_complete;
};

struct AudioStartupBankObservation {
    std::uint32_t epoch;
    std::uint64_t enter_sequence;
    std::uint64_t exit_sequence;
    std::uint64_t scope_generation;
    std::uintptr_t manager;
    std::int32_t result;
    AudioStartupBankOwner owner;
    AudioStartupProviderOutcome provider_outcome;
    AudioStartupProvider provider;
    bool bank_name_complete;
    bool localized;
    /* True only when the natural owner supplied the localized argument. */
    bool localized_observed;
    bool reload;
    bool observation_complete;
    char bank_name[kAudioStartupLogicalPathCapacity];
    std::uintptr_t native_file;
    std::uint64_t native_file_instance_generation;
};

struct AudioStartupObservationLoss {
    std::uint32_t lock_contention;
    std::uint32_t script_overflow;
    std::uint32_t bank_overflow;
    std::uint32_t incomplete_observation;
    std::uint32_t prepublication_observation;
    std::uint32_t script_depth_overflow;
};

struct AudioStartupObservationLedger {
    volatile LONG lock;
    volatile LONG contention_mutations;
    volatile LONGLONG revision;
    std::uint32_t script_count;
    std::uint32_t bank_count;
    AudioStartupObservationLoss loss;
    bool coverage_started_before_audio_singletons;
    bool hook_plan_complete;
    AudioStartupScriptObservation scripts[kAudioStartupScriptCapacity];
    AudioStartupBankObservation banks[kAudioStartupBankCapacity];
    AudioStartupObservationLedger* continuation;
    std::uint64_t sealed_revision, retired_transaction;
    std::uint64_t retired_scripts, retired_banks;
    std::uint32_t epoch, retired_epoch;
    bool runtime_occurrences;
};

struct AudioStartupObservationSnapshot {
    std::uint64_t revision;
    std::uint32_t script_count;
    std::uint32_t bank_count;
    AudioStartupObservationLoss loss;
    bool coverage_started_before_audio_singletons;
    bool hook_plan_complete;
    AudioStartupScriptObservation scripts[kAudioStartupScriptCapacity];
    AudioStartupBankObservation banks[kAudioStartupBankCapacity];
};

void InitializeAudioStartupObservationLedger(
    AudioStartupObservationLedger* ledger,
    bool coverage_started_before_audio_singletons) noexcept;
void SetAudioStartupHookPlanComplete(
    AudioStartupObservationLedger* ledger, bool complete) noexcept;
bool RecordAudioStartupScript(
    AudioStartupObservationLedger* ledger,
    const AudioStartupScriptObservation& observation) noexcept;
bool RecordAudioStartupBank(
    AudioStartupObservationLedger* ledger,
    const AudioStartupBankObservation& observation) noexcept;
bool RecordAudioStartupScriptDepthOverflow(
    AudioStartupObservationLedger* ledger) noexcept;
/*
 * Advances the observational fence for a natural mutation whose complete
 * payload is captured by a separate, exact-build owner.  This never invokes
 * an engine mutation and stores no engine pointer or content record.
 */
bool RecordAudioStartupMutationFence(
    AudioStartupObservationLedger* ledger) noexcept;
bool CaptureAudioStartupObservationSnapshot(
    AudioStartupObservationLedger* ledger,
    AudioStartupObservationSnapshot* output) noexcept;
bool AudioStartupObservationLossEmpty(
    const AudioStartupObservationLoss& loss) noexcept;
// Freezes B0 rows permanently; subsequent occurrences go to a separate epoch
// ledger while the primary mutation revision continues to advance.
bool SealAudioStartupObservationLedger(AudioStartupObservationLedger*,
    AudioStartupObservationLedger* runtime,std::uint64_t expected_revision) noexcept;
bool RetireAudioRuntimeObservations(AudioStartupObservationLedger*,
    std::uint64_t expected_revision,const content::VerifiedEpochRetirement&) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_startup

#endif
