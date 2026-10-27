#ifndef DLUMA_FIXES_FRONTEND_BASELINE_H
#define DLUMA_FIXES_FRONTEND_BASELINE_H

#include "ContentDomainRestartRegistry.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {

constexpr std::size_t kBaselineSourceCapacity = 192;
constexpr std::size_t kBaselinePlayerStateCapacity = 64;
/* The complete required effective-property set for every bounded
 * PlayerState owner. */
constexpr std::size_t kBaselineRequiredPlayerPropertyCount = 15;
constexpr std::size_t kBaselinePlayerPropertyCapacity =
    kBaselinePlayerStateCapacity * kBaselineRequiredPlayerPropertyCount;
constexpr std::size_t kBaselineAudioBankCapacity = 192;
constexpr std::size_t kBaselineAudioScriptCapacity = 32;
constexpr std::size_t kBaselineSpeechBankCapacity = 192;
constexpr std::size_t kBaselineGlobalDomainCapacity = 24;
constexpr std::size_t kBaselineTextCapacity = 192;
constexpr std::size_t kPlayerLogicalTypeCount = 8;

/* Exact Creepy Run fixture diff: base 6.0f, custom 10.5f. */
constexpr std::uint32_t kMoveSprintSpeedProperty = 625;
constexpr std::uint32_t kCameraDefaultFovProperty = 779;
constexpr std::uint32_t kCameraCarFovProperty = 780;
constexpr std::uint32_t kFovSprintModifierProperty = 817;
constexpr std::uint32_t kFovAmortizerModifierProperty = 818;

enum class BaselineSourceKind : std::uint8_t {
    Unknown,
    Base,
    Dlc,
    LocaleSpeech,
    LoaderPak,
    ApprovedGlobal,
};

enum class BaselineAudioBankRole : std::uint8_t {
    Unknown,
    Menu,
    Speech,
    Base,
    Dlc,
};

/* Keep the baseline owner taxonomy numerically aligned with the bounded
 * Player restart inventory without coupling this immutable evidence format
 * to the mutation adapter. */
enum class BaselinePlayerOwnerKind : std::uint8_t {
    Unknown,
    LocalProcessGlobal,
    CharacterManagerGlobal,
    PlayerDi,
    TutorialPlayerDi,
    OtherRttiObserved,
};

enum class BaselineMismatch : std::uint32_t {
    None = 0,
    Filesystem = 1U << 0,
    PlayerOwners = 1U << 1,
    PlayerRows = 1U << 2,
    PlayerProperties = 1U << 3,
    AudioSettings = 1U << 4,
    AudioBanks = 1U << 5,
    GlobalDomains = 1U << 6,
    FrontendResources = 1U << 7,
    LoaderState = 1U << 8,
};

struct StartupBaselineAdmission {
    bool stable_frontend;
    bool first_startup_frontend;
    bool returned_frontend;
    bool game_thread_owner;
    std::uint32_t custom_epoch_count;
    std::uint32_t active_custom_source_count;
    std::uint32_t local_project_lua_count;
};

struct BaselineSourceIdentity {
    std::uint64_t identity_fingerprint;
    std::uint64_t provider_fingerprint;
    BaselineSourceKind kind;
    bool active;
    char name[kBaselineTextCapacity];
};

struct BaselinePlayerProviderIdentity {
    std::uintptr_t source;
    std::uint64_t normalized_path_fingerprint;
    std::uint64_t source_registry_generation;
    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_inventory_ordinal;
    bool observed;
    bool exact_source;
    bool base_provider;
    /* Generation zero is valid; this bit distinguishes a fenced sample from
     * an unknown generation. */
    bool source_registry_generation_observed;
};

struct BaselinePlayerRow {
    std::uintptr_t definition_vector;
    std::uint64_t definition_fingerprint;
    std::uint64_t earned_experience;
    std::uint64_t cache_generation;
    std::uint32_t definition_count;
    std::uint16_t current_level;
    std::uint16_t maximum_level;
    BaselinePlayerProviderIdentity provider;
    bool member_of_live_cache;
};

struct BaselinePlayerState {
    std::uintptr_t state;
    std::uintptr_t owner_object;
    std::uintptr_t player_variables;
    std::uint64_t state_generation;
    std::uint64_t progression_selector_fingerprint;
    std::uint64_t class_selector_fingerprint;
    std::uint64_t skill_membership_fingerprint;
    std::uint64_t regular_buff_fingerprint;
    std::uint64_t timed_buff_fingerprint;
    std::uint64_t inventory_fingerprint;
    std::uint64_t equipment_fingerprint;
    std::uint64_t quest_fingerprint;
    std::uint64_t appearance_fingerprint;
    BaselinePlayerProviderIdentity appearance_provider;
    BaselinePlayerOwnerKind owner_kind;
    BaselinePlayerRow rows[kPlayerLogicalTypeCount];
};

struct BaselinePlayerProperty {
    std::uint64_t state_generation;
    std::uint64_t cache_generation;
    std::uint32_t property_id;
    std::uint32_t value_bits;
    BaselinePlayerProviderIdentity provider;
    bool available;
};

struct BaselineAudioSettings {
    std::uint64_t fingerprint;
    std::uint64_t global_settings_selector_fingerprint;
    std::uint32_t requested_api;
    std::uint32_t applied_api;
    std::uint32_t requested_channels;
    std::uint32_t applied_channels;
    std::uint32_t requested_mix_frequency;
    std::uint32_t applied_mix_frequency;
    std::uint32_t requested_sound_mode;
    std::uint32_t applied_sound_mode;
    std::uint32_t requested_quality_3d;
    std::uint32_t applied_quality_3d;
    std::uint32_t requested_mixer_blocks;
    std::uint32_t applied_mixer_blocks;
    std::uint32_t fx_volume_bits;
    std::uint32_t wave_volume_bits;
    std::uint32_t speech_volume_bits;
    std::uint32_t movie_volume_bits;
    std::uint32_t movie_source_volume_bits;
    std::uint8_t requested_flags;
    std::uint8_t applied_flags;
    bool selector_available;
    bool available;
};

struct BaselineAudioProviderIdentity {
    std::uintptr_t source;
    std::uint64_t normalized_path_fingerprint;
    std::uint64_t source_registry_generation;
    std::uint32_t source_record_index;
    std::uint32_t source_inventory_ordinal;
    BaselineSourceKind source_kind;
    bool observed;
    bool exact_source;
    /* Generation zero is valid when sampled under the registry fence. */
    bool source_registry_generation_observed;
};

struct BaselineAudioScript {
    std::uint64_t logical_path_fingerprint;
    std::uint64_t content_identity_fingerprint;
    std::uint32_t load_order;
    BaselineAudioProviderIdentity provider_identity;
    bool natural_load_observed;
    bool parse_succeeded;
    char logical_path[kBaselineTextCapacity];
    char provider[kBaselineTextCapacity];
};

struct BaselineAudioBank {
    std::uint64_t logical_bank_id;
    std::uint64_t provider_fingerprint;
    std::uint64_t content_fingerprint;
    std::uint32_t dependency_order;
    std::uint32_t reference_count;
    std::uint64_t instance_generation;
    std::uintptr_t manager_record;
    std::uintptr_t retained_native_file;
    BaselineAudioProviderIdentity provider_identity;
    BaselineAudioBankRole role;
    bool membership_observed;
    bool natural_load_observed;
    bool dependency_order_observed;
    bool reference_count_observed;
    bool localized;
    char name[kBaselineTextCapacity];
    char provider[kBaselineTextCapacity];
};

struct BaselineSpeechBank {
    std::uint64_t logical_bank_id;
    std::uint64_t provider_fingerprint;
    std::uint64_t content_identity_fingerprint;
    std::uint64_t instance_generation;
    std::uintptr_t bank_record;
    std::uint32_t load_order;
    BaselineAudioProviderIdentity provider_identity;
    bool membership_observed;
    bool natural_load_observed;
    bool load_order_observed;
    bool localized;
    char name[kBaselineTextCapacity];
    char provider[kBaselineTextCapacity];
};

struct BaselineGlobalDomain {
    ContentDomainId id;
    std::uint64_t fingerprint;
    std::uint64_t provider_fingerprint;
    std::uint32_t entry_count;
    std::uint64_t generation;
    bool available;
};

struct BaselineFrontendState {
    std::uint64_t menu_pack_fingerprint;
    std::uint64_t material_provider_fingerprint;
    std::uint64_t localized_text_generation;
    std::uint32_t custom_assets_pc_resource_count;
    bool available;
};

struct BaselineLoaderState {
    std::uint64_t loader_pak_identity;
    std::uint64_t global_lua_fingerprint;
    std::uint32_t approved_global_lua_count;
    std::uint32_t local_project_lua_count;
    bool loader_pak_state_known;
    bool loader_pak_present;
    bool loader_xui_state_known;
    bool loader_xui_operational;
};

struct FrontendBaseline {
    volatile LONG lock;
    std::uint64_t generation;
    std::uint64_t capture_sequence;
    std::uint64_t capture_qpc;
    std::uint32_t owner_thread_id;
    std::uint32_t custom_source_count;
    std::uint64_t gameplay_cache_generation;
    std::uint64_t audio_script_fingerprint;
    std::uint64_t audio_script_provider_fingerprint;
    std::uint32_t required_property_ids[
        kBaselineRequiredPlayerPropertyCount];
    std::uint32_t required_property_count;
    BaselineSourceIdentity sources[kBaselineSourceCapacity];
    BaselinePlayerState player_states[kBaselinePlayerStateCapacity];
    BaselinePlayerProperty player_properties[kBaselinePlayerPropertyCapacity];
    BaselineAudioScript audio_scripts[kBaselineAudioScriptCapacity];
    BaselineAudioBank audio_banks[kBaselineAudioBankCapacity];
    BaselineSpeechBank speech_banks[kBaselineSpeechBankCapacity];
    BaselineGlobalDomain global_domains[kBaselineGlobalDomainCapacity];
    std::uint32_t source_count;
    std::uint32_t player_state_count;
    std::uint32_t player_property_count;
    std::uint32_t audio_script_count;
    std::uint32_t audio_bank_count;
    std::uint32_t speech_bank_count;
    std::uint32_t global_domain_count;
    BaselineAudioSettings audio_settings;
    BaselineFrontendState frontend;
    BaselineLoaderState loader;
    bool source_inventory_complete;
    bool player_inventory_complete;
    bool audio_settings_complete;
    bool audio_script_inventory_complete;
    bool audio_fmod_inventory_complete;
    bool audio_speech_inventory_complete;
    bool audio_provider_coverage_complete;
    bool audio_replay_owner_complete;
    bool audio_inventory_complete;
    bool global_domain_inventory_complete;
    bool capturing;
    bool captured;
    bool capture_failed;
};

struct FrontendBaselineComparison {
    std::uint32_t mismatch_mask;
    std::uint32_t missing_required_properties;
    std::uint32_t unexpected_custom_sources;
    std::uint32_t stale_custom_audio_banks;
    std::uint32_t stale_player_rows;
    bool comparable;
    bool match;
};

void InitializeBaseline(FrontendBaseline* baseline) noexcept;
bool ConfigureRequiredProperty(FrontendBaseline* baseline,
                               std::uint32_t property_id) noexcept;
bool BeginStartupBaselineCapture(
    FrontendBaseline* baseline, const StartupBaselineAdmission* admission,
    std::uint64_t generation, std::uint64_t sequence,
    std::uint64_t qpc, std::uint32_t owner_thread_id) noexcept;
bool BeginReturnedFrontendCapture(
    FrontendBaseline* baseline, const StartupBaselineAdmission* admission,
    std::uint64_t generation, std::uint64_t sequence,
    std::uint64_t qpc, std::uint32_t owner_thread_id) noexcept;
bool AddBaselineSource(FrontendBaseline* baseline,
                       const BaselineSourceIdentity* source) noexcept;
/* Atomically installs the complete, prevalidated startup source inventory.
 * A failed call leaves source_count and every source record unchanged, so a
 * transient registry-commit retry cannot poison the immutable B0 session. */
bool SetBaselineSources(FrontendBaseline* baseline,
                        const BaselineSourceIdentity* sources,
                        std::uint32_t source_count) noexcept;
/* Atomically projects one complete, authoritative Player startup snapshot.
 * Validation failure leaves the cache generation, state/property records,
 * counts, and player_inventory_complete flag unchanged. */
bool SetBaselinePlayerSnapshot(
    FrontendBaseline* baseline, std::uint64_t cache_generation,
    const BaselinePlayerState* states, std::uint32_t state_count,
    const BaselinePlayerProperty* properties,
    std::uint32_t property_count) noexcept;
/* Rolls back a not-yet-accepted Player projection when its external
 * inventory/VFS evidence changes during publication.  It is unavailable
 * after the full baseline is captured or the capture has failed. */
bool AbandonBaselinePlayerSnapshot(FrontendBaseline* baseline) noexcept;
/* Atomically installs one fully reconciled, read-only startup audio
 * inventory.  The FMOD and speech registries are separate owners; neither
 * hash-only membership nor a current filesystem lookup may be substituted
 * for the exact provider observed during the natural load. */
bool SetBaselineAudioSnapshot(
    FrontendBaseline* baseline, const BaselineAudioSettings* settings,
    const BaselineAudioScript* scripts, std::uint32_t script_count,
    const BaselineAudioBank* banks, std::uint32_t bank_count,
    const BaselineSpeechBank* speech_banks,
    std::uint32_t speech_bank_count,
    std::uint64_t script_manifest_fingerprint,
    std::uint64_t script_provider_fingerprint) noexcept;
bool AbandonBaselineAudioSnapshot(FrontendBaseline* baseline) noexcept;
bool AddBaselinePlayerState(FrontendBaseline* baseline,
                            const BaselinePlayerState* state) noexcept;
bool AddBaselinePlayerProperty(FrontendBaseline* baseline,
                               const BaselinePlayerProperty* property) noexcept;
bool AddBaselineAudioBank(FrontendBaseline* baseline,
                          const BaselineAudioBank* bank) noexcept;
bool AddBaselineGlobalDomain(FrontendBaseline* baseline,
                             const BaselineGlobalDomain* domain) noexcept;
bool CompleteStartupBaselineCapture(FrontendBaseline* baseline) noexcept;
/* Close a startup capture window after custom staging without turning a
 * partial diagnostic snapshot into a permanent capture failure. */
void CloseIncompleteStartupBaselineCapture(
    FrontendBaseline* baseline) noexcept;
void FailStartupBaselineCapture(FrontendBaseline* baseline) noexcept;
bool SnapshotBaseline(const FrontendBaseline* baseline,
                      FrontendBaseline* output) noexcept;
FrontendBaselineComparison CompareFrontendBaseline(
    const FrontendBaseline* expected,
    const FrontendBaseline* actual) noexcept;

const char* BaselineSourceKindName(BaselineSourceKind kind) noexcept;
const char* BaselineAudioBankRoleName(BaselineAudioBankRole role) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot

#endif
