#ifndef DLUMA_FIXES_AUDIO_BASELINE_WINDOWS_SNAPSHOT_H
#define DLUMA_FIXES_AUDIO_BASELINE_WINDOWS_SNAPSHOT_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot {

/* Exact supported Dying Light Player engine-image facts.  The caller must
 * validate the complete PE identity before setting exact_engine_image_validated.
 * This module only reads through injected callbacks and never invokes an
 * audio, filesystem, or content owner. */
constexpr std::uint32_t kWindowsAudioEngineTimeDateStamp = 0x623395D0U;
constexpr std::uint32_t kWindowsAudioEngineSizeOfImage = 0x00E8D000U;
constexpr char kWindowsAudioEngineSha256[] =
    "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C";
constexpr std::uintptr_t kWindowsAudioSingletonGlobalRva = 0x00AC8D78U;
constexpr std::uintptr_t kWindowsAudioFmodVtableRva = 0x008A9628U;
constexpr std::uintptr_t kWindowsAudioNoSoundVtableRva = 0x008AD6C8U;
constexpr std::uintptr_t kWindowsAudioFmodBankManagerOffset = 0x1E28U;
constexpr std::uintptr_t kWindowsAudioCriticalSectionRva = 0x00AD5160U;
constexpr std::uintptr_t kWindowsSpeechManagerGlobalRva = 0x00ACCD38U;
constexpr std::uintptr_t kWindowsSpeechBankVectorOffset = 0x0038U;
constexpr std::uintptr_t kWindowsSpeechBankVectorTagOffset = 0x0048U;
constexpr std::uint32_t kWindowsSpeechBankVectorTag = 16U;
constexpr std::uintptr_t kWindowsSpeechBankLocalisedOffset = 0x0010U;
constexpr std::uintptr_t kWindowsSpeechBankHashOffset = 0x0000U;
constexpr std::size_t kWindowsSpeechBankDescriptorSize = 0x0018U;

constexpr std::uintptr_t kWindowsGameGlobalRva = 0x00ACA818U;
constexpr std::uintptr_t kWindowsGameVtableRva = 0x008D4D78U;
constexpr std::uintptr_t kWindowsGameAudioSettingsOffset = 0x03D8U;
constexpr std::uintptr_t kWindowsAudioVolumeGrainRva = 0x00A91E60U;
constexpr std::size_t kWindowsAudioSettingsPodSize = 0x0058U;
constexpr std::uintptr_t kWindowsAudioSettingsFxVolumeOffset = 0x0034U;
constexpr std::uintptr_t kWindowsAudioSettingsWaveVolumeOffset = 0x0038U;
constexpr std::uintptr_t kWindowsAudioSettingsSpeechVolumeOffset = 0x003CU;
constexpr std::uintptr_t kWindowsAudioSettingsMovieVolumeOffset = 0x0040U;
constexpr std::uintptr_t
    kWindowsAudioSettingsMovieVolumeSourceOffset = 0x0044U;
constexpr std::uintptr_t kWindowsAudioSettingsGlobalSelectorOffset = 0x0048U;
constexpr std::size_t kWindowsAudioSettingsGlobalSelectorCapacity = 256U;
constexpr std::uint32_t
    kWindowsAudioSettingsGlobalSelectorAllocationLimit = 4096U;

constexpr std::uintptr_t kWindowsAudioScriptPathVectorRva = 0x00AD52E0U;
constexpr std::size_t kWindowsAudioScriptStringStride = 0x10U;

/* SoundBankManagerFMOD stores its bank count beside an intrusive ordered
 * tree.  Tree pointers address the link subobject at allocation +0x48. */
constexpr std::uintptr_t kWindowsAudioBankTreeRootOffset = 0x0000U;
constexpr std::uintptr_t kWindowsAudioBankTreeCountOffset = 0x0008U;
constexpr std::uintptr_t kWindowsAudioBankNodeLinkOffset = 0x0048U;
constexpr std::uintptr_t kWindowsAudioBankNodeHashOffset = 0x0000U;
constexpr std::uintptr_t kWindowsAudioBankNodeNameOffset = 0x0008U;
constexpr std::uintptr_t kWindowsAudioBankNodeNameLengthOffset = 0x0010U;
constexpr std::uintptr_t kWindowsAudioBankNodeNameCapacityOffset = 0x0014U;
constexpr std::uintptr_t kWindowsAudioBankNodeNativeFileOffset = 0x0018U;
constexpr std::uintptr_t kWindowsAudioBankNodeRecordHashOffset = 0x0020U;
constexpr std::uintptr_t kWindowsAudioBankNodeLocalisedOffset = 0x0044U;
constexpr std::uintptr_t kWindowsAudioBankNodeLeftOffset = 0x0048U;
constexpr std::uintptr_t kWindowsAudioBankNodeParentOffset = 0x0050U;
constexpr std::uintptr_t kWindowsAudioBankNodeRightOffset = 0x0058U;
constexpr std::uintptr_t kWindowsAudioBankNodeColorOffset = 0x0060U;
constexpr std::size_t kWindowsAudioBankNodeReadableSize = 0x0068U;

constexpr std::size_t kWindowsAudioBankCapacity = 192U;
constexpr std::size_t kWindowsAudioScriptCapacity = 64U;
constexpr std::size_t kWindowsSpeechBankCapacity = 192U;
constexpr std::size_t kWindowsAudioBankNameCapacity = 256U;
constexpr std::size_t kWindowsAudioScriptPathCapacity = 512U;
constexpr std::uint32_t kWindowsAudioScriptAllocationLimit = 4096U;

enum class AudioBaselineWindowsBackendKind : std::uint8_t {
    Unknown,
    Fmod,
    NoSound,
};

enum AudioBaselineWindowsSnapshotField : std::uint64_t {
    AudioWindowsFieldExactImage = 1ULL << 0,
    AudioWindowsFieldObserverFence = 1ULL << 1,
    AudioWindowsFieldAudioLock = 1ULL << 2,
    AudioWindowsFieldAudioSingleton = 1ULL << 3,
    AudioWindowsFieldBackendIdentity = 1ULL << 4,
    AudioWindowsFieldBankManager = 1ULL << 5,
    AudioWindowsFieldBankTree = 1ULL << 6,
    AudioWindowsFieldAudioSettings = 1ULL << 7,
    AudioWindowsFieldAudioScriptPaths = 1ULL << 8,
    AudioWindowsFieldSpeechManager = 1ULL << 9,
    AudioWindowsFieldSpeechBankVectorObserved = 1ULL << 10,
};

constexpr std::uint64_t kAudioWindowsRequiredRawFields =
    AudioWindowsFieldExactImage |
    AudioWindowsFieldObserverFence |
    AudioWindowsFieldAudioLock |
    AudioWindowsFieldAudioSingleton |
    AudioWindowsFieldBackendIdentity |
    AudioWindowsFieldBankManager |
    AudioWindowsFieldBankTree |
    AudioWindowsFieldAudioSettings |
    AudioWindowsFieldAudioScriptPaths |
    AudioWindowsFieldSpeechManager |
    AudioWindowsFieldSpeechBankVectorObserved;

/* These fields are all protected by the validated engine audio lock and the
 * observer fence. SpeechMgr has a separate mutation surface: callers without
 * the exact writer-admission gate may safely capture this subset without
 * touching any speech-registry address. */
constexpr std::uint64_t kAudioWindowsRequiredSoundLockFields =
    AudioWindowsFieldExactImage |
    AudioWindowsFieldObserverFence |
    AudioWindowsFieldAudioLock |
    AudioWindowsFieldAudioSingleton |
    AudioWindowsFieldBackendIdentity |
    AudioWindowsFieldBankManager |
    AudioWindowsFieldBankTree |
    AudioWindowsFieldAudioSettings |
    AudioWindowsFieldAudioScriptPaths;

/* The raw Windows layout does not contain these authoritative restore facts.
 * Callers must correlate them with natural load/open observations before
 * publishing Audio B0. */
enum AudioBaselineWindowsUnresolvedField : std::uint64_t {
    AudioWindowsUnresolvedScriptProviders = 1ULL << 0,
    AudioWindowsUnresolvedBankProviders = 1ULL << 1,
    AudioWindowsUnresolvedBankContent = 1ULL << 2,
    AudioWindowsUnresolvedBankDependencyOrder = 1ULL << 3,
    AudioWindowsUnresolvedBankReferenceModel = 1ULL << 4,
    AudioWindowsUnresolvedBankInstanceGeneration = 1ULL << 5,
    AudioWindowsUnresolvedSpeechSynchronization = 1ULL << 6,
    AudioWindowsUnresolvedSpeechProviders = 1ULL << 7,
    AudioWindowsUnresolvedSpeechContent = 1ULL << 8,
    AudioWindowsUnresolvedSpeechDependencyOrder = 1ULL << 9,
    AudioWindowsUnresolvedSpeechReferenceModel = 1ULL << 10,
    AudioWindowsUnresolvedSpeechInstanceGeneration = 1ULL << 11,
    AudioWindowsUnresolvedAudioSettingsProvider = 1ULL << 12,
};

enum AudioBaselineWindowsSnapshotReason : std::uint64_t {
    AudioWindowsReasonNone = 0,
    AudioWindowsReasonInvalidArgument = 1ULL << 0,
    AudioWindowsReasonUnsupportedImage = 1ULL << 1,
    AudioWindowsReasonFenceUnavailable = 1ULL << 2,
    AudioWindowsReasonObserverBusy = 1ULL << 3,
    AudioWindowsReasonObservationLoss = 1ULL << 4,
    AudioWindowsReasonAudioLockBusy = 1ULL << 5,
    AudioWindowsReasonAddressOverflow = 1ULL << 6,
    AudioWindowsReasonMemoryUnreadable = 1ULL << 7,
    AudioWindowsReasonAudioSingletonUnavailable = 1ULL << 8,
    AudioWindowsReasonBackendIdentityMismatch = 1ULL << 9,
    AudioWindowsReasonGameUnavailable = 1ULL << 10,
    AudioWindowsReasonGameIdentityMismatch = 1ULL << 11,
    AudioWindowsReasonAudioSettingsUnavailable = 1ULL << 12,
    AudioWindowsReasonScriptHeaderInvalid = 1ULL << 13,
    AudioWindowsReasonScriptCountOutOfRange = 1ULL << 14,
    AudioWindowsReasonScriptPathInvalid = 1ULL << 15,
    AudioWindowsReasonBankManagerUnavailable = 1ULL << 16,
    AudioWindowsReasonBankHeaderInvalid = 1ULL << 17,
    AudioWindowsReasonBankCountOutOfRange = 1ULL << 18,
    AudioWindowsReasonBankNodeInvalid = 1ULL << 19,
    AudioWindowsReasonBankTreeCycle = 1ULL << 20,
    AudioWindowsReasonBankCountMismatch = 1ULL << 21,
    AudioWindowsReasonBankHashMismatch = 1ULL << 22,
    AudioWindowsReasonBankNameInvalid = 1ULL << 23,
    AudioWindowsReasonSnapshotChanged = 1ULL << 24,
    AudioWindowsReasonFenceChanged = 1ULL << 25,
    AudioWindowsReasonSpeechManagerUnavailable = 1ULL << 26,
    AudioWindowsReasonSpeechHeaderInvalid = 1ULL << 27,
    AudioWindowsReasonSpeechCountOutOfRange = 1ULL << 28,
    AudioWindowsReasonSpeechRecordInvalid = 1ULL << 29,
    AudioWindowsReasonSpeechVectorChanged = 1ULL << 30,
    AudioWindowsReasonAudioSettingsStringInvalid = 1ULL << 31,
};

struct AudioBaselineWindowsObservationFence {
    std::uint64_t revision;
    std::uint32_t active_calls;
    bool observation_loss;
};

using ReadAudioBaselineWindowsMemoryFn = bool (*)(
    void* user, std::uintptr_t address, void* output,
    std::size_t size) noexcept;
using ReadAudioBaselineWindowsFenceFn = bool (*)(
    void* user, AudioBaselineWindowsObservationFence* output) noexcept;
using TryEnterAudioBaselineWindowsLockFn = bool (*)(void* user) noexcept;
using LeaveAudioBaselineWindowsLockFn = void (*)(void* user) noexcept;

struct AudioBaselineWindowsReadContext {
    std::uintptr_t engine_base;
    ReadAudioBaselineWindowsMemoryFn read_memory;
    ReadAudioBaselineWindowsFenceFn read_fence;
    TryEnterAudioBaselineWindowsLockFn try_enter_audio_lock;
    LeaveAudioBaselineWindowsLockFn leave_audio_lock;
    void* callback_user;
    bool exact_engine_image_validated;
    /* Set only after an exact-build owner has proven that the caller's
     * synchronization also protects SpeechMgr+0x38 and every descriptor it
     * contains.  False is fail-closed and forbids all speech-registry reads. */
    bool speech_registry_synchronization_proven;
};

struct AudioBaselineWindowsSettingsSnapshot {
    std::uintptr_t game;
    std::uintptr_t settings;
    unsigned char pod[kWindowsAudioSettingsPodSize];
    std::uint32_t volume_grain_bits;
    std::uint32_t fx_volume_bits;
    std::uint32_t wave_volume_bits;
    std::uint32_t speech_volume_bits;
    std::uint32_t movie_volume_bits;
    std::uint32_t movie_volume_source_bits;
    std::uint32_t global_settings_length;
    std::uint64_t global_settings_fingerprint;
    std::uint64_t fingerprint;
    char global_settings[kWindowsAudioSettingsGlobalSelectorCapacity];
    bool available;
};

struct AudioBaselineWindowsScriptPath {
    std::uint32_t load_order;
    std::uint32_t length;
    std::uint64_t logical_path_fingerprint;
    char logical_path[kWindowsAudioScriptPathCapacity];
};

struct AudioBaselineWindowsBankRecord {
    std::uintptr_t node_link;
    std::uintptr_t record;
    std::uintptr_t native_file;
    std::uint32_t tree_key;
    std::uint32_t record_hash;
    std::uint32_t name_length;
    std::uint32_t tree_color;
    std::uint64_t structural_fingerprint;
    bool localised;
    char name[kWindowsAudioBankNameCapacity];
};

struct AudioBaselineWindowsSpeechBankRecord {
    std::uintptr_t bank;
    std::uint32_t bank_hash;
    std::uint64_t descriptor_fingerprint;
    unsigned char descriptor[kWindowsSpeechBankDescriptorSize];
    bool localised;
};

struct AudioBaselineWindowsSnapshot {
    std::uintptr_t audio_singleton;
    std::uintptr_t audio_vtable;
    std::uintptr_t bank_manager;
    std::uintptr_t bank_tree_root;
    std::uintptr_t speech_manager;
    std::uint64_t observation_revision;
    std::uint64_t script_manifest_fingerprint;
    std::uint64_t bank_manifest_fingerprint;
    std::uint64_t speech_bank_manifest_fingerprint;
    std::uint64_t raw_snapshot_fingerprint;
    std::uint64_t complete_fields;
    std::uint64_t unresolved_fields;
    std::uint64_t reason_mask;
    AudioBaselineWindowsSettingsSnapshot settings;
    AudioBaselineWindowsScriptPath scripts[kWindowsAudioScriptCapacity];
    AudioBaselineWindowsBankRecord banks[kWindowsAudioBankCapacity];
    AudioBaselineWindowsSpeechBankRecord
        speech_banks[kWindowsSpeechBankCapacity];
    std::uint32_t script_count;
    std::uint32_t bank_count;
    std::uint32_t speech_bank_count;
    AudioBaselineWindowsBackendKind backend_kind;
    bool lock_acquired;
    bool sound_lock_snapshot_complete;
    bool raw_snapshot_complete;
    bool authoritative_b0_complete;
    bool speech_vector_observation_stable;
    bool speech_owner_synchronization_proven;
    bool speech_baseline_complete;
};

/* Returns true when every safety-authorized domain was captured stably.  If
 * speech_registry_synchronization_proven is false, this captures only the
 * sound-lock-protected settings/script/FMOD subset; raw_snapshot_complete and
 * speech_baseline_complete remain false.  A true return never implies
 * authoritative Audio B0: unresolved_fields remains nonzero until separate
 * natural provider/content/order evidence is joined. */
bool SnapshotAudioBaselineWindows(
    const AudioBaselineWindowsReadContext* context,
    AudioBaselineWindowsSnapshot* output) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot

#endif
