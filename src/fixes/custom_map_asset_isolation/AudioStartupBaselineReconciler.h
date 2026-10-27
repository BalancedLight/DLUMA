#ifndef DLUMA_FIXES_AUDIO_STARTUP_BASELINE_RECONCILER_H
#define DLUMA_FIXES_AUDIO_STARTUP_BASELINE_RECONCILER_H

#include "AudioBaselineWindowsSnapshot.h"
#include "AudioStartupObservationLedger.h"
#include "FilesystemSourceInventory.h"
#include "FrontendBaseline.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::
    audio_reconcile {

enum class AudioStartupBaselineReconcileResult : std::uint8_t {
    Rejected,
    ObservationalInventoryReconciled,
};

enum AudioStartupBaselineReconcileReason : std::uint64_t {
    AudioReconcileReasonNone = 0,
    AudioReconcileReasonInvalidArgument = 1ULL << 0,
    AudioReconcileReasonRawSnapshotIncomplete = 1ULL << 1,
    AudioReconcileReasonRawSnapshotFailure = 1ULL << 2,
    AudioReconcileReasonBackendUnsupported = 1ULL << 3,
    AudioReconcileReasonObservationLoss = 1ULL << 4,
    AudioReconcileReasonLateHookCoverage = 1ULL << 5,
    AudioReconcileReasonHookPlanIncomplete = 1ULL << 6,
    AudioReconcileReasonSourceInventoryIncomplete = 1ULL << 7,
    AudioReconcileReasonInputCountOutOfRange = 1ULL << 8,
    AudioReconcileReasonOutputCapacityExceeded = 1ULL << 9,
    AudioReconcileReasonInvalidText = 1ULL << 10,
    /* Bits 11 and 12 were used by the pre-interval prototype.  Keep them
     * reserved so persisted diagnostic masks are never reinterpreted. */
    AudioReconcileReasonDuplicateSourceRoot = 1ULL << 13,
    AudioReconcileReasonUnknownSourceKind = 1ULL << 14,
    AudioReconcileReasonProviderUnknown = 1ULL << 15,
    AudioReconcileReasonProviderPrefixAmbiguous = 1ULL << 16,
    AudioReconcileReasonProviderIdentityMismatch = 1ULL << 17,
    AudioReconcileReasonSettingsIncomplete = 1ULL << 18,
    AudioReconcileReasonScriptCountMismatch = 1ULL << 19,
    AudioReconcileReasonScriptDuplicate = 1ULL << 20,
    AudioReconcileReasonScriptObservationMissing = 1ULL << 21,
    AudioReconcileReasonScriptParseFailed = 1ULL << 22,
    AudioReconcileReasonFmodCountMismatch = 1ULL << 23,
    AudioReconcileReasonFmodDuplicate = 1ULL << 24,
    AudioReconcileReasonFmodObservationMissing = 1ULL << 25,
    AudioReconcileReasonFmodCacheHitWithoutPriorExact = 1ULL << 26,
    AudioReconcileReasonFmodManagerMismatch = 1ULL << 27,
    AudioReconcileReasonSpeechCountMismatch = 1ULL << 28,
    AudioReconcileReasonSpeechDuplicate = 1ULL << 29,
    AudioReconcileReasonSpeechObservationMissing = 1ULL << 30,
    AudioReconcileReasonSpeechHashMismatch = 1ULL << 31,
    AudioReconcileReasonLocalizedMismatch = 1ULL << 32,
    AudioReconcileReasonContentIdentityUnavailable = 1ULL << 33,
    AudioReconcileReasonFmodReloadObservation = 1ULL << 34,
    AudioReconcileReasonSourceRegistryGenerationMismatch = 1ULL << 35,
    AudioReconcileReasonInvalidObservationInterval = 1ULL << 36,
    AudioReconcileReasonDuplicateObservationEndpoint = 1ULL << 37,
};

/* These are deliberate limitations, not reconciliation failures.  The
 * staged inventory remains observational and cannot authorize a reset. */
enum AudioStartupBaselineUnresolvedField : std::uint64_t {
    AudioReconcileUnresolvedAudioSettingsProvider = 1ULL << 0,
    AudioReconcileUnresolvedScriptContentBytes = 1ULL << 1,
    AudioReconcileUnresolvedFmodContentBytes = 1ULL << 2,
    AudioReconcileUnresolvedFmodReferenceCounts = 1ULL << 3,
    AudioReconcileUnresolvedFmodInstanceGeneration = 1ULL << 4,
    AudioReconcileUnresolvedSpeechSynchronization = 1ULL << 5,
    AudioReconcileUnresolvedSpeechContentBytes = 1ULL << 6,
    AudioReconcileUnresolvedSpeechReferenceCounts = 1ULL << 7,
    AudioReconcileUnresolvedSpeechInstanceGeneration = 1ULL << 8,
    AudioReconcileUnresolvedResetOwner = 1ULL << 9,
    AudioReconcileUnresolvedExactSourceCoverage = 1ULL << 10,
    AudioReconcileUnresolvedFmodLocalizedEvidence = 1ULL << 11,
    AudioReconcileUnresolvedSpeechLocalizedEvidence = 1ULL << 12,
    AudioReconcileUnresolvedDependencyOrdering = 1ULL << 13,
};

constexpr std::uint64_t kAudioStartupBaselineUnresolvedFields =
    AudioReconcileUnresolvedAudioSettingsProvider |
    AudioReconcileUnresolvedScriptContentBytes |
    AudioReconcileUnresolvedFmodContentBytes |
    AudioReconcileUnresolvedFmodReferenceCounts |
    AudioReconcileUnresolvedFmodInstanceGeneration |
    AudioReconcileUnresolvedSpeechSynchronization |
    AudioReconcileUnresolvedSpeechContentBytes |
    AudioReconcileUnresolvedSpeechReferenceCounts |
    AudioReconcileUnresolvedSpeechInstanceGeneration |
    AudioReconcileUnresolvedResetOwner;

struct AudioStartupBaselineStaging {
    BaselineAudioSettings settings;
    BaselineAudioScript scripts[kBaselineAudioScriptCapacity];
    BaselineAudioBank fmod_banks[kBaselineAudioBankCapacity];
    BaselineSpeechBank speech_banks[kBaselineSpeechBankCapacity];
    std::uint64_t script_manifest_fingerprint;
    std::uint64_t script_occurrence_manifest_fingerprint;
    std::uint64_t script_provider_fingerprint;
    std::uint64_t reason_mask;
    std::uint64_t unresolved_fields;
    std::uint32_t script_count;
    std::uint32_t fmod_bank_count;
    std::uint32_t speech_bank_count;
    std::uint32_t inherited_fmod_cache_hit_count;
    AudioStartupBaselineReconcileResult result;
    /* Physical provider paths may be complete while CFSSource identity is
     * deliberately unresolved. */
    bool provider_coverage_complete;
    bool exact_source_coverage_complete;
    bool fmod_localized_evidence_complete;
    bool speech_localized_evidence_complete;
    bool dependency_order_complete;
    bool fully_reconciled_observational_inventory;
    bool authoritative_b0_complete;
    bool reset_ready;
    bool reset_owner_proven;
};

/* Produces bounded staging only.  It never publishes to FrontendBaseline,
 * invokes an engine owner, or supplies missing content/reference/generation
 * facts. */
bool ReconcileAudioStartupBaseline(
    const audio_snapshot::AudioBaselineWindowsSnapshot* raw,
    const audio_startup::AudioStartupObservationSnapshot* observations,
    const vfs::FilesystemSourceInventory* sources,
    AudioStartupBaselineStaging* output) noexcept;

const char* AudioStartupBaselineReconcileResultName(
    AudioStartupBaselineReconcileResult result) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_reconcile

#endif
