#define _HAS_EXCEPTIONS 0

#include "FrontendBaseline.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {
namespace {

constexpr std::uint32_t kMandatoryPlayerProperties[
    kBaselineRequiredPlayerPropertyCount] = {
        103U,
        209U,
        496U,
        kMoveSprintSpeedProperty,
        702U,
        706U,
        707U,
        713U,
        714U,
        715U,
        kCameraDefaultFovProperty,
        kCameraCarFovProperty,
        kFovSprintModifierProperty,
        kFovAmortizerModifierProperty,
        823U,
};

class BaselineGuard {
public:
    explicit BaselineGuard(FrontendBaseline* baseline) noexcept
        : baseline_(baseline), locked_(false)
    {
        if (baseline_ &&
            dluma_atomic_compare_exchange(&baseline_->lock, 1, 0) == 0)
            locked_ = true;
    }
    ~BaselineGuard()
    {
        if (locked_) dluma_atomic_exchange(&baseline_->lock, 0);
    }
    bool locked() const noexcept { return locked_; }
private:
    FrontendBaseline* baseline_;
    bool locked_;
};

bool EqualText(const char* left, const char* right) noexcept
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (*left++ != *right++) return false;
    }
    return *left == *right;
}

bool BoundedNonEmptyText(const char* text,
                         std::size_t capacity) noexcept
{
    if (!text || !capacity || !text[0]) return false;
    for (std::size_t index = 1; index < capacity; ++index)
        if (!text[index]) return true;
    return false;
}

bool ValidPlayerOwnerKind(BaselinePlayerOwnerKind kind) noexcept
{
    return kind != BaselinePlayerOwnerKind::Unknown &&
        static_cast<std::uint8_t>(kind) <=
            static_cast<std::uint8_t>(
                BaselinePlayerOwnerKind::OtherRttiObserved);
}

bool CompletePlayerProvider(
    const BaselinePlayerProviderIdentity& provider) noexcept
{
    return provider.observed && provider.exact_source &&
        provider.base_provider && provider.source &&
        provider.normalized_path_fingerprint &&
        provider.source_registry_generation_observed &&
        !provider.content_epoch &&
        provider.source_record_index != UINT32_MAX &&
        provider.source_inventory_ordinal;
}

bool CompleteAudioProvider(
    const BaselineAudioProviderIdentity& provider) noexcept
{
    return provider.observed && provider.exact_source && provider.source &&
        provider.normalized_path_fingerprint &&
        provider.source_registry_generation_observed &&
        provider.source_record_index != UINT32_MAX &&
        provider.source_inventory_ordinal &&
        provider.source_kind != BaselineSourceKind::Unknown &&
        static_cast<std::uint8_t>(provider.source_kind) <=
            static_cast<std::uint8_t>(BaselineSourceKind::ApprovedGlobal);
}

bool EqualAudioProvider(
    const BaselineAudioProviderIdentity& left,
    const BaselineAudioProviderIdentity& right) noexcept
{
    return left.source == right.source &&
        left.normalized_path_fingerprint ==
            right.normalized_path_fingerprint &&
        left.source_registry_generation ==
            right.source_registry_generation &&
        left.source_record_index == right.source_record_index &&
        left.source_inventory_ordinal == right.source_inventory_ordinal &&
        left.source_kind == right.source_kind &&
        left.observed == right.observed &&
        left.exact_source == right.exact_source &&
        left.source_registry_generation_observed ==
            right.source_registry_generation_observed;
}

bool CompleteAudioSettings(
    const BaselineAudioSettings& settings) noexcept
{
    return settings.available && settings.fingerprint &&
        settings.selector_available &&
        settings.global_settings_selector_fingerprint;
}

bool CompleteAudioScript(const BaselineAudioScript& script) noexcept
{
    return script.logical_path_fingerprint &&
        script.content_identity_fingerprint && script.load_order &&
        script.natural_load_observed && script.parse_succeeded &&
        CompleteAudioProvider(script.provider_identity) &&
        BoundedNonEmptyText(script.logical_path,
                            sizeof(script.logical_path)) &&
        BoundedNonEmptyText(script.provider, sizeof(script.provider));
}

bool CompleteAudioBank(const BaselineAudioBank& bank) noexcept
{
    return bank.logical_bank_id && bank.provider_fingerprint &&
        bank.content_fingerprint && bank.instance_generation &&
        bank.manager_record && bank.membership_observed &&
        bank.natural_load_observed && bank.dependency_order_observed &&
        bank.dependency_order && !bank.reference_count_observed &&
        !bank.reference_count && CompleteAudioProvider(
            bank.provider_identity) &&
        bank.role != BaselineAudioBankRole::Unknown &&
        static_cast<std::uint8_t>(bank.role) <=
            static_cast<std::uint8_t>(BaselineAudioBankRole::Dlc) &&
        BoundedNonEmptyText(bank.name, sizeof(bank.name)) &&
        BoundedNonEmptyText(bank.provider, sizeof(bank.provider));
}

bool CompleteSpeechBank(const BaselineSpeechBank& bank) noexcept
{
    return bank.logical_bank_id && bank.provider_fingerprint &&
        bank.content_identity_fingerprint && bank.instance_generation &&
        bank.bank_record && bank.load_order && bank.membership_observed &&
        bank.natural_load_observed && bank.load_order_observed &&
        CompleteAudioProvider(bank.provider_identity) &&
        BoundedNonEmptyText(bank.name, sizeof(bank.name)) &&
        BoundedNonEmptyText(bank.provider, sizeof(bank.provider));
}

bool EqualPlayerProvider(
    const BaselinePlayerProviderIdentity& left,
    const BaselinePlayerProviderIdentity& right) noexcept
{
    return left.source == right.source &&
        left.normalized_path_fingerprint ==
            right.normalized_path_fingerprint &&
        left.source_registry_generation ==
            right.source_registry_generation &&
        left.content_epoch == right.content_epoch &&
        left.source_record_index == right.source_record_index &&
        left.source_inventory_ordinal == right.source_inventory_ordinal &&
        left.observed == right.observed &&
        left.exact_source == right.exact_source &&
        left.base_provider == right.base_provider &&
        left.source_registry_generation_observed ==
            right.source_registry_generation_observed;
}

bool CompletePlayerRow(const BaselinePlayerRow& row) noexcept
{
    return row.definition_vector && row.definition_fingerprint &&
        row.definition_count && row.maximum_level &&
        row.current_level <= row.maximum_level && row.cache_generation &&
        row.member_of_live_cache && CompletePlayerProvider(row.provider);
}

bool CompletePlayerState(const BaselinePlayerState& state) noexcept
{
    if (!state.state || !state.state_generation || !state.owner_object ||
        !ValidPlayerOwnerKind(state.owner_kind) ||
        !state.player_variables ||
        !state.progression_selector_fingerprint ||
        !state.class_selector_fingerprint ||
        !state.skill_membership_fingerprint ||
        !state.regular_buff_fingerprint ||
        !state.timed_buff_fingerprint ||
        !state.inventory_fingerprint ||
        !state.equipment_fingerprint || !state.quest_fingerprint ||
        !state.appearance_fingerprint ||
        !CompletePlayerProvider(state.appearance_provider))
        return false;
    for (std::uint32_t row = 0; row < kPlayerLogicalTypeCount; ++row)
        if (!CompletePlayerRow(state.rows[row])) return false;
    return true;
}

bool HasRequiredProperty(const FrontendBaseline& baseline,
                         std::uint32_t property_id) noexcept
{
    for (std::uint32_t index = 0;
         index < baseline.player_property_count; ++index) {
        const BaselinePlayerProperty& property =
            baseline.player_properties[index];
        if (property.available && property.property_id == property_id)
            return true;
    }
    return false;
}

bool HasConfiguredProperty(const FrontendBaseline& baseline,
                           std::uint32_t property_id) noexcept
{
    for (std::uint32_t index = 0;
         index < baseline.required_property_count; ++index)
        if (baseline.required_property_ids[index] == property_id)
            return true;
    return false;
}

bool CompleteRequiredPropertyConfiguration(
    const FrontendBaseline& baseline) noexcept
{
    if (baseline.required_property_count !=
        kBaselineRequiredPlayerPropertyCount)
        return false;
    for (std::uint32_t property_id : kMandatoryPlayerProperties)
        if (!HasConfiguredProperty(baseline, property_id)) return false;
    return true;
}

bool HasOwnerProperty(const FrontendBaseline& baseline,
                      std::uint64_t state_generation,
                      std::uint32_t property_id) noexcept
{
    for (std::uint32_t index = 0;
         index < baseline.player_property_count; ++index) {
        const BaselinePlayerProperty& property =
            baseline.player_properties[index];
        if (property.available &&
            property.state_generation == state_generation &&
            property.property_id == property_id)
            return true;
    }
    return false;
}

bool HasLoaderPakSource(const FrontendBaseline& baseline) noexcept
{
    if (!baseline.loader.loader_pak_identity) return false;
    for (std::uint32_t index = 0; index < baseline.source_count; ++index) {
        const BaselineSourceIdentity& source = baseline.sources[index];
        if (source.active && source.kind == BaselineSourceKind::LoaderPak &&
            source.identity_fingerprint == baseline.loader.loader_pak_identity)
            return true;
    }
    return false;
}

bool EqualSources(const FrontendBaseline& left,
                  const FrontendBaseline& right) noexcept
{
    if (left.source_count != right.source_count) return false;
    for (std::uint32_t index = 0; index < left.source_count; ++index) {
        const BaselineSourceIdentity& expected = left.sources[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < right.source_count; ++candidate) {
            const BaselineSourceIdentity& actual = right.sources[candidate];
            if (expected.identity_fingerprint ==
                    actual.identity_fingerprint &&
                expected.provider_fingerprint ==
                    actual.provider_fingerprint &&
                expected.kind == actual.kind && expected.active ==
                    actual.active && EqualText(expected.name, actual.name)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool EqualPlayerProperties(const FrontendBaseline& left,
                           const FrontendBaseline& right) noexcept
{
    if (left.required_property_count != right.required_property_count ||
        left.player_property_count != right.player_property_count)
        return false;
    for (std::uint32_t index = 0;
         index < left.required_property_count; ++index)
        if (!HasConfiguredProperty(
                right, left.required_property_ids[index]))
            return false;
    for (std::uint32_t index = 0;
         index < left.player_property_count; ++index) {
        const BaselinePlayerProperty& expected =
            left.player_properties[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < right.player_property_count; ++candidate) {
            const BaselinePlayerProperty& actual =
                right.player_properties[candidate];
            if (expected.state_generation == actual.state_generation &&
                expected.property_id == actual.property_id &&
                expected.value_bits == actual.value_bits &&
                expected.available == actual.available &&
                expected.cache_generation ==
                    left.gameplay_cache_generation &&
                actual.cache_generation ==
                    right.gameplay_cache_generation &&
                EqualPlayerProvider(expected.provider,
                                    actual.provider)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool EqualPlayerOwners(const FrontendBaseline& left,
                       const FrontendBaseline& right) noexcept
{
    if (left.player_state_count != right.player_state_count) return false;
    for (std::uint32_t index = 0; index < left.player_state_count; ++index) {
        const BaselinePlayerState& expected = left.player_states[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < right.player_state_count; ++candidate) {
            const BaselinePlayerState& actual = right.player_states[candidate];
            if (expected.state == actual.state &&
                expected.state_generation == actual.state_generation &&
                expected.owner_kind == actual.owner_kind &&
                expected.owner_object == actual.owner_object) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool EqualPlayerStates(const FrontendBaseline& left,
                       const FrontendBaseline& right,
                       std::uint32_t* stale_rows) noexcept
{
    if (stale_rows) *stale_rows = 0;
    if (left.player_state_count != right.player_state_count) return false;
    bool equal = true;
    for (std::uint32_t index = 0; index < left.player_state_count; ++index) {
        const BaselinePlayerState& expected = left.player_states[index];
        const BaselinePlayerState* actual = nullptr;
        for (std::uint32_t candidate = 0;
             candidate < right.player_state_count; ++candidate) {
            if (right.player_states[candidate].state == expected.state &&
                right.player_states[candidate].state_generation ==
                    expected.state_generation) {
                actual = &right.player_states[candidate];
                break;
            }
        }
        if (!actual ||
            !CompletePlayerState(expected) ||
            !CompletePlayerState(*actual) ||
            expected.progression_selector_fingerprint !=
                actual->progression_selector_fingerprint ||
            expected.class_selector_fingerprint !=
                actual->class_selector_fingerprint ||
            expected.player_variables != actual->player_variables ||
            expected.skill_membership_fingerprint !=
                actual->skill_membership_fingerprint ||
            expected.regular_buff_fingerprint !=
                actual->regular_buff_fingerprint ||
            expected.timed_buff_fingerprint !=
                actual->timed_buff_fingerprint ||
            expected.inventory_fingerprint !=
                actual->inventory_fingerprint ||
            expected.equipment_fingerprint !=
                actual->equipment_fingerprint ||
            expected.quest_fingerprint != actual->quest_fingerprint ||
            expected.appearance_fingerprint !=
                actual->appearance_fingerprint ||
            !EqualPlayerProvider(expected.appearance_provider,
                                 actual->appearance_provider)) {
            equal = false;
            if (stale_rows) *stale_rows += kPlayerLogicalTypeCount;
            continue;
        }
        for (std::uint32_t row = 0; row < kPlayerLogicalTypeCount; ++row) {
            const BaselinePlayerRow& expected_row = expected.rows[row];
            const BaselinePlayerRow& actual_row = actual->rows[row];
            if (expected_row.definition_fingerprint !=
                    actual_row.definition_fingerprint ||
                expected_row.definition_count !=
                    actual_row.definition_count ||
                expected_row.earned_experience !=
                    actual_row.earned_experience ||
                expected_row.current_level != actual_row.current_level ||
                expected_row.maximum_level != actual_row.maximum_level ||
                expected_row.cache_generation !=
                    left.gameplay_cache_generation ||
                actual_row.cache_generation !=
                    right.gameplay_cache_generation ||
                !expected_row.member_of_live_cache ||
                !actual_row.member_of_live_cache ||
                !EqualPlayerProvider(expected_row.provider,
                                     actual_row.provider)) {
                equal = false;
                if (stale_rows) ++*stale_rows;
            }
        }
    }
    return equal;
}

bool EqualAudioBanks(const FrontendBaseline& left,
                     const FrontendBaseline& right) noexcept
{
    if (left.audio_bank_count != right.audio_bank_count) return false;
    bool matched[kBaselineAudioBankCapacity] = {};
    for (std::uint32_t index = 0; index < left.audio_bank_count; ++index) {
        const BaselineAudioBank& expected = left.audio_banks[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < right.audio_bank_count; ++candidate) {
            if (matched[candidate]) continue;
            const BaselineAudioBank& actual = right.audio_banks[candidate];
            if (expected.logical_bank_id == actual.logical_bank_id &&
                expected.provider_fingerprint ==
                    actual.provider_fingerprint &&
                expected.content_fingerprint ==
                    actual.content_fingerprint &&
                expected.reference_count == actual.reference_count &&
                expected.dependency_order == actual.dependency_order &&
                expected.instance_generation != actual.instance_generation &&
                expected.membership_observed ==
                    actual.membership_observed &&
                expected.natural_load_observed ==
                    actual.natural_load_observed &&
                expected.dependency_order_observed ==
                    actual.dependency_order_observed &&
                expected.reference_count_observed ==
                    actual.reference_count_observed &&
                expected.localized == actual.localized &&
                EqualAudioProvider(expected.provider_identity,
                                   actual.provider_identity) &&
                expected.role == actual.role &&
                EqualText(expected.name, actual.name) &&
                EqualText(expected.provider, actual.provider)) {
                found = true;
                matched[candidate] = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool EqualAudioScripts(const FrontendBaseline& left,
                       const FrontendBaseline& right) noexcept
{
    if (left.audio_script_count != right.audio_script_count) return false;
    for (std::uint32_t index = 0;
         index < left.audio_script_count; ++index) {
        const BaselineAudioScript& expected = left.audio_scripts[index];
        const BaselineAudioScript& actual = right.audio_scripts[index];
        if (expected.logical_path_fingerprint !=
                actual.logical_path_fingerprint ||
            expected.content_identity_fingerprint !=
                actual.content_identity_fingerprint ||
            expected.load_order != actual.load_order ||
            expected.natural_load_observed !=
                actual.natural_load_observed ||
            expected.parse_succeeded != actual.parse_succeeded ||
            !EqualAudioProvider(expected.provider_identity,
                                actual.provider_identity) ||
            !EqualText(expected.logical_path, actual.logical_path) ||
            !EqualText(expected.provider, actual.provider))
            return false;
    }
    return true;
}

bool EqualSpeechBanks(const FrontendBaseline& left,
                      const FrontendBaseline& right) noexcept
{
    if (left.speech_bank_count != right.speech_bank_count) return false;
    bool matched[kBaselineSpeechBankCapacity] = {};
    for (std::uint32_t index = 0;
         index < left.speech_bank_count; ++index) {
        const BaselineSpeechBank& expected = left.speech_banks[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < right.speech_bank_count; ++candidate) {
            if (matched[candidate]) continue;
            const BaselineSpeechBank& actual =
                right.speech_banks[candidate];
            if (expected.logical_bank_id == actual.logical_bank_id &&
                expected.provider_fingerprint ==
                    actual.provider_fingerprint &&
                expected.content_identity_fingerprint ==
                    actual.content_identity_fingerprint &&
                expected.instance_generation !=
                    actual.instance_generation &&
                expected.load_order == actual.load_order &&
                expected.membership_observed ==
                    actual.membership_observed &&
                expected.natural_load_observed ==
                    actual.natural_load_observed &&
                expected.load_order_observed ==
                    actual.load_order_observed &&
                expected.localized == actual.localized &&
                EqualAudioProvider(expected.provider_identity,
                                   actual.provider_identity) &&
                EqualText(expected.name, actual.name) &&
                EqualText(expected.provider, actual.provider)) {
                found = true;
                matched[candidate] = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

std::uint32_t CountUnmatchedAudioBanks(
    const FrontendBaseline& expected,
    const FrontendBaseline& actual) noexcept
{
    std::uint32_t unmatched = 0;
    for (std::uint32_t index = 0; index < actual.audio_bank_count; ++index) {
        const BaselineAudioBank& bank = actual.audio_banks[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < expected.audio_bank_count; ++candidate) {
            const BaselineAudioBank& baseline = expected.audio_banks[candidate];
            if (bank.logical_bank_id == baseline.logical_bank_id &&
                bank.provider_fingerprint == baseline.provider_fingerprint &&
                bank.content_fingerprint == baseline.content_fingerprint &&
                bank.role == baseline.role &&
                EqualText(bank.name, baseline.name) &&
                EqualText(bank.provider, baseline.provider)) {
                found = true;
                break;
            }
        }
        if (!found) ++unmatched;
    }
    return unmatched;
}

bool EqualAudioSettings(const BaselineAudioSettings& left,
                        const BaselineAudioSettings& right) noexcept
{
    return left.available == right.available && left.available &&
        left.fingerprint == right.fingerprint &&
        left.global_settings_selector_fingerprint ==
            right.global_settings_selector_fingerprint &&
        left.requested_api == right.requested_api &&
        left.applied_api == right.applied_api &&
        left.requested_channels == right.requested_channels &&
        left.applied_channels == right.applied_channels &&
        left.requested_mix_frequency == right.requested_mix_frequency &&
        left.applied_mix_frequency == right.applied_mix_frequency &&
        left.requested_sound_mode == right.requested_sound_mode &&
        left.applied_sound_mode == right.applied_sound_mode &&
        left.requested_quality_3d == right.requested_quality_3d &&
        left.applied_quality_3d == right.applied_quality_3d &&
        left.requested_mixer_blocks == right.requested_mixer_blocks &&
        left.applied_mixer_blocks == right.applied_mixer_blocks &&
        left.fx_volume_bits == right.fx_volume_bits &&
        left.wave_volume_bits == right.wave_volume_bits &&
        left.speech_volume_bits == right.speech_volume_bits &&
        left.movie_volume_bits == right.movie_volume_bits &&
        left.movie_source_volume_bits ==
            right.movie_source_volume_bits &&
        left.requested_flags == right.requested_flags &&
        left.applied_flags == right.applied_flags &&
        left.selector_available == right.selector_available;
}

bool EqualFrontendState(const BaselineFrontendState& left,
                        const BaselineFrontendState& right) noexcept
{
    return left.available == right.available && left.available &&
        left.menu_pack_fingerprint == right.menu_pack_fingerprint &&
        left.material_provider_fingerprint ==
            right.material_provider_fingerprint &&
        left.custom_assets_pc_resource_count ==
            right.custom_assets_pc_resource_count;
}

bool EqualLoaderState(const BaselineLoaderState& left,
                      const BaselineLoaderState& right) noexcept
{
    return left.loader_pak_identity == right.loader_pak_identity &&
        left.global_lua_fingerprint == right.global_lua_fingerprint &&
        left.approved_global_lua_count == right.approved_global_lua_count &&
        left.local_project_lua_count == right.local_project_lua_count &&
        left.loader_pak_state_known == right.loader_pak_state_known &&
        left.loader_pak_present == right.loader_pak_present &&
        left.loader_xui_state_known == right.loader_xui_state_known &&
        left.loader_xui_operational == right.loader_xui_operational;
}

bool EqualGlobalDomains(const FrontendBaseline& left,
                        const FrontendBaseline& right) noexcept
{
    if (left.global_domain_count != right.global_domain_count) return false;
    for (std::uint32_t index = 0; index < left.global_domain_count; ++index) {
        const BaselineGlobalDomain& expected = left.global_domains[index];
        bool found = false;
        for (std::uint32_t candidate = 0;
             candidate < right.global_domain_count; ++candidate) {
            const BaselineGlobalDomain& actual =
                right.global_domains[candidate];
            if (expected.id == actual.id &&
                expected.fingerprint == actual.fingerprint &&
                expected.provider_fingerprint ==
                    actual.provider_fingerprint &&
                expected.entry_count == actual.entry_count &&
                expected.available == actual.available) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

}  // namespace

void InitializeBaseline(FrontendBaseline* baseline) noexcept
{
    if (baseline) std::memset(baseline, 0, sizeof(*baseline));
}

bool ConfigureRequiredProperty(FrontendBaseline* baseline,
                               std::uint32_t property_id) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || baseline->capturing || baseline->captured ||
        !property_id)
        return false;
    for (std::uint32_t index = 0;
         index < baseline->required_property_count; ++index)
        if (baseline->required_property_ids[index] == property_id)
            return true;
    if (baseline->required_property_count >=
        kBaselineRequiredPlayerPropertyCount)
        return false;
    baseline->required_property_ids[
        baseline->required_property_count++] = property_id;
    return true;
}

bool BeginStartupBaselineCapture(
    FrontendBaseline* baseline, const StartupBaselineAdmission* admission,
    std::uint64_t generation, std::uint64_t sequence,
    std::uint64_t qpc, std::uint32_t owner_thread_id) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !admission || baseline->capturing ||
        baseline->captured || baseline->capture_failed || !generation ||
        !owner_thread_id || !admission->stable_frontend ||
        !admission->first_startup_frontend || admission->returned_frontend ||
        !admission->game_thread_owner || admission->custom_epoch_count ||
        admission->active_custom_source_count ||
        admission->local_project_lua_count)
        return false;
    baseline->generation = generation;
    baseline->capture_sequence = sequence;
    baseline->capture_qpc = qpc;
    baseline->owner_thread_id = owner_thread_id;
    baseline->capturing = true;
    return true;
}

bool BeginReturnedFrontendCapture(
    FrontendBaseline* baseline, const StartupBaselineAdmission* admission,
    std::uint64_t generation, std::uint64_t sequence,
    std::uint64_t qpc, std::uint32_t owner_thread_id) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !admission || baseline->capturing ||
        baseline->captured || baseline->capture_failed || !generation ||
        !owner_thread_id || !admission->stable_frontend ||
        admission->first_startup_frontend || !admission->returned_frontend ||
        !admission->game_thread_owner || admission->custom_epoch_count ||
        admission->active_custom_source_count ||
        admission->local_project_lua_count)
        return false;
    baseline->generation = generation;
    baseline->capture_sequence = sequence;
    baseline->capture_qpc = qpc;
    baseline->owner_thread_id = owner_thread_id;
    baseline->capturing = true;
    return true;
}

bool AddBaselineSource(FrontendBaseline* baseline,
                       const BaselineSourceIdentity* source) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || !source ||
        !source->identity_fingerprint || !source->provider_fingerprint ||
        source->kind == BaselineSourceKind::Unknown ||
        static_cast<std::uint8_t>(source->kind) >
            static_cast<std::uint8_t>(BaselineSourceKind::ApprovedGlobal) ||
        !source->active ||
        !BoundedNonEmptyText(source->name, sizeof(source->name)) ||
        baseline->source_count >= kBaselineSourceCapacity)
        return false;
    for (std::uint32_t index = 0; index < baseline->source_count; ++index)
        if (baseline->sources[index].identity_fingerprint ==
            source->identity_fingerprint)
            return false;
    baseline->sources[baseline->source_count++] = *source;
    return true;
}

bool SetBaselineSources(FrontendBaseline* baseline,
                        const BaselineSourceIdentity* sources,
                        std::uint32_t source_count) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !sources || !source_count ||
        !baseline->capturing || baseline->captured ||
        baseline->capture_failed || baseline->source_count ||
        source_count > kBaselineSourceCapacity)
        return false;
    for (std::uint32_t index = 0; index < source_count; ++index) {
        const BaselineSourceIdentity& source = sources[index];
        if (!source.identity_fingerprint ||
            !source.provider_fingerprint || !source.active ||
            source.kind == BaselineSourceKind::Unknown ||
            !BoundedNonEmptyText(source.name, sizeof(source.name)))
            return false;
        for (std::uint32_t prior = 0; prior < index; ++prior)
            if (sources[prior].identity_fingerprint ==
                    source.identity_fingerprint)
                return false;
    }
    std::memcpy(baseline->sources, sources,
                sizeof(BaselineSourceIdentity) * source_count);
    baseline->source_count = source_count;
    return true;
}

bool SetBaselinePlayerSnapshot(
    FrontendBaseline* baseline, std::uint64_t cache_generation,
    const BaselinePlayerState* states, std::uint32_t state_count,
    const BaselinePlayerProperty* properties,
    std::uint32_t property_count) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || baseline->captured ||
        baseline->capture_failed || !cache_generation || !states ||
        !state_count || state_count > kBaselinePlayerStateCapacity ||
        !properties || property_count !=
            state_count * kBaselineRequiredPlayerPropertyCount ||
        property_count > kBaselinePlayerPropertyCapacity ||
        baseline->gameplay_cache_generation ||
        baseline->player_state_count || baseline->player_property_count ||
        baseline->player_inventory_complete ||
        !CompleteRequiredPropertyConfiguration(*baseline))
        return false;

    for (std::uint32_t state_index = 0;
         state_index < state_count; ++state_index) {
        const BaselinePlayerState& state = states[state_index];
        if (!CompletePlayerState(state)) return false;
        for (std::uint32_t row = 0; row < kPlayerLogicalTypeCount; ++row)
            if (state.rows[row].cache_generation != cache_generation)
                return false;
        for (std::uint32_t prior = 0; prior < state_index; ++prior)
            if (states[prior].state == state.state ||
                states[prior].state_generation == state.state_generation)
                return false;
    }

    for (std::uint32_t property_index = 0;
         property_index < property_count; ++property_index) {
        const BaselinePlayerProperty& property =
            properties[property_index];
        if (!property.state_generation || !property.property_id ||
            !property.available ||
            property.cache_generation != cache_generation ||
            !CompletePlayerProvider(property.provider) ||
            !HasConfiguredProperty(*baseline, property.property_id))
            return false;
        bool owner_found = false;
        for (std::uint32_t state_index = 0;
             state_index < state_count; ++state_index)
            if (states[state_index].state_generation ==
                property.state_generation) {
                owner_found = true;
                break;
            }
        if (!owner_found) return false;
        for (std::uint32_t prior = 0; prior < property_index; ++prior)
            if (properties[prior].state_generation ==
                    property.state_generation &&
                properties[prior].property_id == property.property_id)
                return false;
    }

    for (std::uint32_t state_index = 0;
         state_index < state_count; ++state_index) {
        for (std::uint32_t required = 0;
             required < baseline->required_property_count; ++required) {
            bool found = false;
            for (std::uint32_t property_index = 0;
                 property_index < property_count; ++property_index) {
                const BaselinePlayerProperty& property =
                    properties[property_index];
                if (property.state_generation ==
                        states[state_index].state_generation &&
                    property.property_id ==
                        baseline->required_property_ids[required]) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
    }

    std::memmove(baseline->player_states, states,
                 sizeof(BaselinePlayerState) * state_count);
    std::memmove(baseline->player_properties, properties,
                 sizeof(BaselinePlayerProperty) * property_count);
    baseline->gameplay_cache_generation = cache_generation;
    baseline->player_state_count = state_count;
    baseline->player_property_count = property_count;
    baseline->player_inventory_complete = true;
    return true;
}

bool AbandonBaselinePlayerSnapshot(FrontendBaseline* baseline) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || baseline->captured ||
        baseline->capture_failed || !baseline->player_inventory_complete)
        return false;
    std::memset(baseline->player_states, 0,
                sizeof(baseline->player_states));
    std::memset(baseline->player_properties, 0,
                sizeof(baseline->player_properties));
    baseline->gameplay_cache_generation = 0;
    baseline->player_state_count = 0;
    baseline->player_property_count = 0;
    baseline->player_inventory_complete = false;
    return true;
}

bool SetBaselineAudioSnapshot(
    FrontendBaseline* baseline, const BaselineAudioSettings* settings,
    const BaselineAudioScript* scripts, std::uint32_t script_count,
    const BaselineAudioBank* banks, std::uint32_t bank_count,
    const BaselineSpeechBank* speech_banks,
    std::uint32_t speech_bank_count,
    std::uint64_t script_manifest_fingerprint,
    std::uint64_t script_provider_fingerprint) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || baseline->captured ||
        baseline->capture_failed || !settings ||
        !CompleteAudioSettings(*settings) || !scripts || !script_count ||
        script_count > kBaselineAudioScriptCapacity || !banks ||
        !bank_count || bank_count > kBaselineAudioBankCapacity ||
        !speech_banks || !speech_bank_count ||
        speech_bank_count > kBaselineSpeechBankCapacity ||
        !script_manifest_fingerprint || !script_provider_fingerprint ||
        baseline->audio_script_count || baseline->audio_bank_count ||
        baseline->speech_bank_count || baseline->audio_inventory_complete ||
        baseline->audio_settings_complete ||
        baseline->audio_script_inventory_complete ||
        baseline->audio_fmod_inventory_complete ||
        baseline->audio_speech_inventory_complete ||
        baseline->audio_provider_coverage_complete)
        return false;

    for (std::uint32_t index = 0; index < script_count; ++index) {
        if (!CompleteAudioScript(scripts[index])) return false;
        for (std::uint32_t prior = 0; prior < index; ++prior)
            if (scripts[prior].load_order == scripts[index].load_order)
                return false;
    }
    for (std::uint32_t index = 0; index < bank_count; ++index) {
        if (!CompleteAudioBank(banks[index])) return false;
        for (std::uint32_t prior = 0; prior < index; ++prior)
            if (banks[prior].logical_bank_id ==
                    banks[index].logical_bank_id ||
                banks[prior].manager_record ==
                    banks[index].manager_record ||
                banks[prior].dependency_order ==
                    banks[index].dependency_order)
                return false;
    }
    for (std::uint32_t index = 0; index < speech_bank_count; ++index) {
        if (!CompleteSpeechBank(speech_banks[index])) return false;
        for (std::uint32_t prior = 0; prior < index; ++prior)
            if (speech_banks[prior].logical_bank_id ==
                    speech_banks[index].logical_bank_id ||
                speech_banks[prior].bank_record ==
                    speech_banks[index].bank_record ||
                speech_banks[prior].load_order ==
                    speech_banks[index].load_order)
                return false;
    }

    std::memmove(baseline->audio_scripts, scripts,
                 sizeof(BaselineAudioScript) * script_count);
    std::memmove(baseline->audio_banks, banks,
                 sizeof(BaselineAudioBank) * bank_count);
    std::memmove(baseline->speech_banks, speech_banks,
                 sizeof(BaselineSpeechBank) * speech_bank_count);
    baseline->audio_settings = *settings;
    baseline->audio_script_fingerprint =
        script_manifest_fingerprint;
    baseline->audio_script_provider_fingerprint =
        script_provider_fingerprint;
    baseline->audio_script_count = script_count;
    baseline->audio_bank_count = bank_count;
    baseline->speech_bank_count = speech_bank_count;
    baseline->audio_settings_complete = true;
    baseline->audio_script_inventory_complete = true;
    baseline->audio_fmod_inventory_complete = true;
    baseline->audio_speech_inventory_complete = true;
    baseline->audio_provider_coverage_complete = true;
    /* Static analysis proves no single engine owner replays all of settings,
     * AudioScript, speech banks and FMOD banks.  Capture completeness is not
     * mutation authority. */
    baseline->audio_replay_owner_complete = false;
    baseline->audio_inventory_complete = true;
    return true;
}

bool AbandonBaselineAudioSnapshot(FrontendBaseline* baseline) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || baseline->captured ||
        baseline->capture_failed || !baseline->audio_inventory_complete)
        return false;
    std::memset(baseline->audio_scripts, 0,
                sizeof(baseline->audio_scripts));
    std::memset(baseline->audio_banks, 0,
                sizeof(baseline->audio_banks));
    std::memset(baseline->speech_banks, 0,
                sizeof(baseline->speech_banks));
    std::memset(&baseline->audio_settings, 0,
                sizeof(baseline->audio_settings));
    baseline->audio_script_fingerprint = 0;
    baseline->audio_script_provider_fingerprint = 0;
    baseline->audio_script_count = 0;
    baseline->audio_bank_count = 0;
    baseline->speech_bank_count = 0;
    baseline->audio_settings_complete = false;
    baseline->audio_script_inventory_complete = false;
    baseline->audio_fmod_inventory_complete = false;
    baseline->audio_speech_inventory_complete = false;
    baseline->audio_provider_coverage_complete = false;
    baseline->audio_replay_owner_complete = false;
    baseline->audio_inventory_complete = false;
    return true;
}

bool AddBaselinePlayerState(FrontendBaseline* baseline,
                            const BaselinePlayerState* state) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || !state ||
        !CompletePlayerState(*state) ||
        baseline->player_state_count >=
            kBaselinePlayerStateCapacity)
        return false;
    for (std::uint32_t index = 0;
         index < baseline->player_state_count; ++index) {
        const BaselinePlayerState& existing =
            baseline->player_states[index];
        if (existing.state == state->state ||
            existing.state_generation == state->state_generation)
            return false;
    }
    baseline->player_states[baseline->player_state_count++] = *state;
    return true;
}

bool AddBaselinePlayerProperty(FrontendBaseline* baseline,
                               const BaselinePlayerProperty* property) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || !property ||
        !property->state_generation || !property->property_id ||
        !property->available || !property->cache_generation ||
        !CompletePlayerProvider(property->provider) ||
        baseline->player_property_count >=
            kBaselinePlayerPropertyCapacity)
        return false;
    for (std::uint32_t index = 0;
         index < baseline->player_property_count; ++index) {
        const BaselinePlayerProperty& existing =
            baseline->player_properties[index];
        if (existing.state_generation == property->state_generation &&
            existing.property_id == property->property_id)
            return false;
    }
    baseline->player_properties[
        baseline->player_property_count++] = *property;
    return true;
}

bool AddBaselineAudioBank(FrontendBaseline* baseline,
                          const BaselineAudioBank* bank) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || !bank ||
        !bank->logical_bank_id || !bank->provider_fingerprint ||
        !bank->content_fingerprint || !bank->instance_generation ||
        bank->role == BaselineAudioBankRole::Unknown ||
        static_cast<std::uint8_t>(bank->role) >
            static_cast<std::uint8_t>(BaselineAudioBankRole::Dlc) ||
        !BoundedNonEmptyText(bank->name, sizeof(bank->name)) ||
        !BoundedNonEmptyText(bank->provider, sizeof(bank->provider)) ||
        baseline->audio_bank_count >= kBaselineAudioBankCapacity)
        return false;
    for (std::uint32_t index = 0;
         index < baseline->audio_bank_count; ++index)
        if (baseline->audio_banks[index].logical_bank_id ==
                bank->logical_bank_id &&
            baseline->audio_banks[index].instance_generation ==
                bank->instance_generation)
            return false;
    baseline->audio_banks[baseline->audio_bank_count++] = *bank;
    return true;
}

bool AddBaselineGlobalDomain(FrontendBaseline* baseline,
                             const BaselineGlobalDomain* domain) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing || !domain ||
        static_cast<std::uint8_t>(domain->id) >=
            static_cast<std::uint8_t>(ContentDomainId::Count) ||
        !domain->available || !domain->fingerprint || !domain->generation ||
        baseline->global_domain_count >=
            kBaselineGlobalDomainCapacity)
        return false;
    for (std::uint32_t index = 0;
         index < baseline->global_domain_count; ++index)
        if (baseline->global_domains[index].id == domain->id)
            return false;
    baseline->global_domains[baseline->global_domain_count++] = *domain;
    return true;
}

bool CompleteStartupBaselineCapture(FrontendBaseline* baseline) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || !baseline->capturing ||
        baseline->capture_failed || baseline->custom_source_count ||
        !baseline->gameplay_cache_generation ||
        !baseline->audio_script_fingerprint ||
        !baseline->audio_script_provider_fingerprint ||
        !baseline->source_count || !baseline->player_state_count ||
        !baseline->audio_script_count || !baseline->audio_bank_count ||
        !baseline->speech_bank_count ||
        !baseline->required_property_count ||
        baseline->required_property_count !=
            kBaselineRequiredPlayerPropertyCount ||
        baseline->player_property_count !=
            baseline->player_state_count *
                kBaselineRequiredPlayerPropertyCount ||
        !baseline->source_inventory_complete ||
        !baseline->player_inventory_complete ||
        !baseline->audio_settings_complete ||
        !baseline->audio_script_inventory_complete ||
        !baseline->audio_fmod_inventory_complete ||
        !baseline->audio_speech_inventory_complete ||
        !baseline->audio_provider_coverage_complete ||
        !baseline->audio_inventory_complete ||
        !baseline->global_domain_inventory_complete ||
        !baseline->loader.loader_pak_state_known ||
        !baseline->loader.loader_pak_present ||
        !baseline->loader.loader_xui_state_known ||
        !baseline->loader.loader_xui_operational ||
        !baseline->loader.loader_pak_identity ||
        baseline->loader.local_project_lua_count ||
        !baseline->audio_settings.available ||
        !baseline->frontend.available ||
        !baseline->frontend.menu_pack_fingerprint ||
        baseline->frontend.custom_assets_pc_resource_count ||
        !HasLoaderPakSource(*baseline) ||
        !CompleteRequiredPropertyConfiguration(*baseline))
        return false;
    for (std::uint32_t index = 0;
         index < baseline->required_property_count; ++index)
        if (!HasRequiredProperty(
                *baseline, baseline->required_property_ids[index]))
            return false;
    for (std::uint32_t state = 0;
         state < baseline->player_state_count; ++state) {
        const BaselinePlayerState& player_state =
            baseline->player_states[state];
        if (!CompletePlayerState(player_state)) return false;
        for (std::uint32_t row = 0; row < kPlayerLogicalTypeCount; ++row)
            if (player_state.rows[row].cache_generation !=
                    baseline->gameplay_cache_generation)
                return false;
        for (std::uint32_t property = 0;
             property < baseline->required_property_count; ++property)
            if (!HasOwnerProperty(
                    *baseline,
                    player_state.state_generation,
                    baseline->required_property_ids[property]))
                return false;
    }
    for (std::uint32_t property = 0;
         property < baseline->player_property_count; ++property) {
        const BaselinePlayerProperty& player_property =
            baseline->player_properties[property];
        if (!player_property.available ||
            player_property.cache_generation !=
                baseline->gameplay_cache_generation ||
            !CompletePlayerProvider(player_property.provider))
            return false;
    }
    baseline->capturing = false;
    baseline->captured = true;
    return true;
}

void CloseIncompleteStartupBaselineCapture(
    FrontendBaseline* baseline) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked() || baseline->captured) return;
    baseline->capturing = false;
    baseline->capture_failed = false;
}

void FailStartupBaselineCapture(FrontendBaseline* baseline) noexcept
{
    BaselineGuard guard(baseline);
    if (!guard.locked()) return;
    baseline->capturing = false;
    baseline->captured = false;
    baseline->capture_failed = true;
}

bool SnapshotBaseline(const FrontendBaseline* baseline,
                      FrontendBaseline* output) noexcept
{
    if (!baseline || !output || baseline == output) return false;
    FrontendBaseline* mutable_baseline =
        const_cast<FrontendBaseline*>(baseline);
    BaselineGuard guard(mutable_baseline);
    if (!guard.locked() || !baseline->captured) return false;
    std::memcpy(output, baseline, sizeof(*output));
    output->lock = 0;
    return true;
}

FrontendBaselineComparison CompareFrontendBaseline(
    const FrontendBaseline* expected,
    const FrontendBaseline* actual) noexcept
{
    FrontendBaselineComparison comparison = {};
    if (!expected || !actual || !expected->captured || !actual->captured)
        return comparison;
    comparison.comparable = true;
    if (actual->custom_source_count) {
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::Filesystem);
        comparison.unexpected_custom_sources = actual->custom_source_count;
    }
    if (!EqualSources(*expected, *actual))
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::Filesystem);
    if (!EqualPlayerOwners(*expected, *actual))
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::PlayerOwners);
    if (!EqualPlayerStates(*expected, *actual,
                           &comparison.stale_player_rows))
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::PlayerRows);
    if (!actual->gameplay_cache_generation ||
        actual->gameplay_cache_generation <=
            expected->gameplay_cache_generation)
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::PlayerRows);
    if (!EqualPlayerProperties(*expected, *actual)) {
        comparison.mismatch_mask |= static_cast<std::uint32_t>(
            BaselineMismatch::PlayerProperties);
        for (std::uint32_t index = 0;
             index < expected->player_property_count; ++index) {
            const BaselinePlayerProperty& property =
                expected->player_properties[index];
            if (HasConfiguredProperty(*expected, property.property_id) &&
                !HasOwnerProperty(*actual, property.state_generation,
                                  property.property_id))
                ++comparison.missing_required_properties;
        }
    }
    if (!EqualAudioSettings(expected->audio_settings,
                            actual->audio_settings))
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::AudioSettings);
    if (expected->audio_script_fingerprint !=
            actual->audio_script_fingerprint ||
        expected->audio_script_provider_fingerprint !=
            actual->audio_script_provider_fingerprint ||
        !EqualAudioScripts(*expected, *actual) ||
        !EqualAudioBanks(*expected, *actual) ||
        !EqualSpeechBanks(*expected, *actual)) {
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::AudioBanks);
        comparison.stale_custom_audio_banks =
            CountUnmatchedAudioBanks(*expected, *actual);
    }
    if (!EqualGlobalDomains(*expected, *actual))
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::GlobalDomains);
    if (!EqualFrontendState(expected->frontend, actual->frontend))
        comparison.mismatch_mask |= static_cast<std::uint32_t>(
            BaselineMismatch::FrontendResources);
    if (!EqualLoaderState(expected->loader, actual->loader))
        comparison.mismatch_mask |=
            static_cast<std::uint32_t>(BaselineMismatch::LoaderState);
    comparison.match = comparison.mismatch_mask == 0;
    return comparison;
}

const char* BaselineSourceKindName(BaselineSourceKind kind) noexcept
{
    switch (kind) {
    case BaselineSourceKind::Unknown: return "Unknown";
    case BaselineSourceKind::Base: return "Base";
    case BaselineSourceKind::Dlc: return "Dlc";
    case BaselineSourceKind::LocaleSpeech: return "LocaleSpeech";
    case BaselineSourceKind::LoaderPak: return "LoaderPak";
    case BaselineSourceKind::ApprovedGlobal: return "ApprovedGlobal";
    default: return "Unknown";
    }
}

const char* BaselineAudioBankRoleName(BaselineAudioBankRole role) noexcept
{
    switch (role) {
    case BaselineAudioBankRole::Unknown: return "Unknown";
    case BaselineAudioBankRole::Menu: return "Menu";
    case BaselineAudioBankRole::Speech: return "Speech";
    case BaselineAudioBankRole::Base: return "Base";
    case BaselineAudioBankRole::Dlc: return "Dlc";
    default: return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot
