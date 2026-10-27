#define _HAS_EXCEPTIONS 0

#include "AudioStartupBaselineReconciler.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::
    audio_reconcile {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kNormalizedPathCapacity =
    audio_startup::kAudioStartupProviderPathCapacity;

struct MappedProvider {
    BaselineAudioProviderIdentity identity;
    std::uint64_t resolved_path_fingerprint;
    char root[kNormalizedPathCapacity];
    char resolved[kNormalizedPathCapacity];
};

struct OrderedMatch {
    std::uint64_t enter_sequence;
    std::uint32_t raw_index;
    std::uint32_t observation_index;
};

bool SuccessfulFmodExact(
    const audio_startup::AudioStartupBankObservation& observation) noexcept;
bool FmodCacheHit(
    const audio_startup::AudioStartupBankObservation& observation) noexcept;
bool SuccessfulSpeech(
    const audio_startup::AudioStartupBankObservation& observation) noexcept;

void AddReason(AudioStartupBaselineStaging* output,
               AudioStartupBaselineReconcileReason reason) noexcept
{
    if (output)
        output->reason_mask |= static_cast<std::uint64_t>(reason);
}

bool Reject(AudioStartupBaselineStaging* output,
            AudioStartupBaselineReconcileReason reason) noexcept
{
    AddReason(output, reason);
    if (output) {
        output->result =
            AudioStartupBaselineReconcileResult::Rejected;
        output->provider_coverage_complete = false;
        output->exact_source_coverage_complete = false;
        output->fmod_localized_evidence_complete = false;
        output->speech_localized_evidence_complete = false;
        output->dependency_order_complete = false;
        output->fully_reconciled_observational_inventory = false;
        output->authoritative_b0_complete = false;
        output->reset_ready = false;
        output->reset_owner_proven = false;
    }
    return false;
}

std::size_t BoundedLength(const char* text,
                          std::size_t capacity) noexcept
{
    if (!text || !capacity) return capacity;
    for (std::size_t index = 0; index < capacity; ++index)
        if (!text[index]) return index;
    return capacity;
}

bool CopyText(char* output, std::size_t capacity,
              const char* text) noexcept
{
    if (!output || !capacity || !text) return false;
    const std::size_t length = BoundedLength(text, capacity);
    if (length >= capacity) return false;
    std::memcpy(output, text, length + 1U);
    return true;
}

void HashBytes(std::uint64_t* hash, const void* data,
               std::size_t size) noexcept
{
    const unsigned char* bytes =
        static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        *hash ^= bytes[index];
        *hash *= kFnvPrime;
    }
}

void HashU32(std::uint64_t* hash, std::uint32_t value) noexcept
{
    unsigned char bytes[4] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU64(std::uint64_t* hash, std::uint64_t value) noexcept
{
    unsigned char bytes[8] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
        static_cast<unsigned char>(value >> 32),
        static_cast<unsigned char>(value >> 40),
        static_cast<unsigned char>(value >> 48),
        static_cast<unsigned char>(value >> 56),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

std::uint64_t BeginHash(const char* domain) noexcept
{
    std::uint64_t hash = kFnvOffset;
    while (domain && *domain) {
        HashBytes(&hash, domain, 1U);
        ++domain;
    }
    return hash;
}

std::uint64_t NonzeroHash(std::uint64_t hash) noexcept
{
    return hash ? hash : kFnvOffset;
}

std::uint64_t HashText(const char* domain, const char* text,
                       std::size_t capacity) noexcept
{
    const std::size_t length = BoundedLength(text, capacity);
    if (!length || length >= capacity) return 0;
    std::uint64_t hash = BeginHash(domain);
    HashBytes(&hash, text, length);
    return NonzeroHash(hash);
}

bool IsSeparator(char value) noexcept
{
    return value == '\\' || value == '/';
}

bool HasDotSegment(const char* path) noexcept
{
    if (!path) return true;
    const char* segment = path;
    for (const char* cursor = path;; ++cursor) {
        if (*cursor && *cursor != '\\') continue;
        const std::size_t length =
            static_cast<std::size_t>(cursor - segment);
        if ((length == 1U && segment[0] == '.') ||
            (length == 2U && segment[0] == '.' &&
             segment[1] == '.'))
            return true;
        if (!*cursor) break;
        segment = cursor + 1;
    }
    return false;
}

bool NormalizePath(const char* input, std::size_t input_capacity,
                   bool logical, char* output,
                   std::size_t output_capacity) noexcept
{
    if (!input || !output || output_capacity < 2U) return false;
    const std::size_t input_length =
        BoundedLength(input, input_capacity);
    if (!input_length || input_length >= input_capacity) return false;
    std::size_t out = 0;
    bool separator = false;
    for (std::size_t index = 0; index < input_length; ++index) {
        unsigned char byte = static_cast<unsigned char>(input[index]);
        if (byte < 0x20U || byte == 0x7FU) return false;
        char value = static_cast<char>(byte);
        if (IsSeparator(value)) {
            if (logical && !out) continue;
            if (separator) continue;
            value = '\\';
            separator = true;
        } else {
            separator = false;
            if (value >= 'A' && value <= 'Z')
                value = static_cast<char>(value + ('a' - 'A'));
        }
        if (out + 1U >= output_capacity) return false;
        output[out++] = value;
    }
    while (out > 1U && output[out - 1U] == '\\' &&
           !(out == 3U && output[1] == ':'))
        --out;
    output[out] = 0;
    return out && !HasDotSegment(output);
}

bool EqualText(const char* left, const char* right) noexcept
{
    if (!left || !right) return left == right;
    while (*left && *right)
        if (*left++ != *right++) return false;
    return *left == *right;
}

bool ContainsText(const char* path, const char* needle) noexcept
{
    if (!path || !needle || !needle[0]) return false;
    for (std::size_t start = 0; path[start]; ++start) {
        std::size_t index = 0;
        while (needle[index] && path[start + index] == needle[index])
            ++index;
        if (!needle[index]) return true;
    }
    return false;
}

bool EndsWith(const char* path, const char* suffix) noexcept
{
    const std::size_t path_length =
        BoundedLength(path, kNormalizedPathCapacity);
    const std::size_t suffix_length =
        BoundedLength(suffix, kNormalizedPathCapacity);
    return path_length < kNormalizedPathCapacity &&
        suffix_length < kNormalizedPathCapacity &&
        path_length >= suffix_length &&
        EqualText(path + path_length - suffix_length, suffix);
}

BaselineSourceKind ClassifySource(const char* normalized,
                                  bool native_fallback) noexcept
{
    if (native_fallback &&
        EqualText(normalized, "filesystem:native-fallback"))
        return BaselineSourceKind::Base;
    if (EndsWith(normalized, "\\dluma.pak"))
        return BaselineSourceKind::LoaderPak;
    if (ContainsText(normalized, "\\workshop\\") ||
        EndsWith(normalized, "\\workshop"))
        return BaselineSourceKind::Unknown;
    if (ContainsText(normalized, "speech") ||
        ContainsText(normalized, "locale"))
        return BaselineSourceKind::LocaleSpeech;
    if (ContainsText(normalized, "\\dw_dlc") ||
        ContainsText(normalized, "\\dlc\\"))
        return BaselineSourceKind::Dlc;
    if (ContainsText(normalized, "\\dw\\") ||
        EndsWith(normalized, "\\dw") ||
        ContainsText(normalized, "\\engine\\") ||
        EndsWith(normalized, "\\engine"))
        return BaselineSourceKind::Base;
    return BaselineSourceKind::Unknown;
}

bool PathWithinRoot(const char* path, const char* root) noexcept
{
    if (!path || !root) return false;
    std::size_t index = 0;
    while (root[index] && path[index] == root[index]) ++index;
    if (root[index]) return false;
    return !path[index] || path[index] == '\\' ||
        (index && root[index - 1U] == '\\');
}

bool ValidateSourceInventory(
    const vfs::FilesystemSourceInventory& sources,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!sources.complete ||
        sources.failure != vfs::FilesystemSourceInventoryFailure::None ||
        !sources.registry_generation_observed ||
        !sources.source_count ||
        sources.source_count > vfs::kFilesystemSourceInventoryCapacity ||
        sources.source_count != sources.reported_count)
        return Reject(output,
                      AudioReconcileReasonSourceInventoryIncomplete);
    std::uint32_t fallback_count = 0;
    for (std::uint32_t index = 0; index < sources.source_count; ++index) {
        const vfs::FilesystemSourceInventoryRecord& record =
            sources.sources[index];
        if (!record.source || !record.list_ordinal) return Reject(
            output, AudioReconcileReasonSourceInventoryIncomplete);
        char normalized[kNormalizedPathCapacity] = {};
        if (!NormalizePath(record.path, sizeof(record.path), false,
                           normalized, sizeof(normalized)))
            return Reject(output, AudioReconcileReasonInvalidText);
        if (record.native_fallback) ++fallback_count;
        if (ClassifySource(normalized, record.native_fallback) ==
            BaselineSourceKind::Unknown)
            return Reject(output, AudioReconcileReasonUnknownSourceKind);
        for (std::uint32_t prior = 0; prior < index; ++prior) {
            char prior_normalized[kNormalizedPathCapacity] = {};
            if (!NormalizePath(sources.sources[prior].path,
                               sizeof(sources.sources[prior].path), false,
                               prior_normalized,
                               sizeof(prior_normalized)))
                return Reject(output, AudioReconcileReasonInvalidText);
            if (EqualText(normalized, prior_normalized))
                return Reject(
                    output, AudioReconcileReasonProviderPrefixAmbiguous);
            if (record.source == sources.sources[prior].source ||
                record.list_ordinal ==
                    sources.sources[prior].list_ordinal)
                return Reject(output,
                              AudioReconcileReasonDuplicateSourceRoot);
        }
    }
    if (fallback_count != 1U)
        return Reject(output,
                      AudioReconcileReasonSourceInventoryIncomplete);
    return true;
}

bool MapProvider(
    const audio_startup::AudioStartupProvider& observed,
    const vfs::FilesystemSourceInventory& sources,
    MappedProvider* mapped,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!mapped || !observed.path_complete || !observed.path[0])
        return Reject(output, AudioReconcileReasonProviderUnknown);
    std::memset(mapped, 0, sizeof(*mapped));
    if (!observed.source_registry_generation_observed ||
        observed.source_registry_generation !=
            sources.registry_generation)
        return Reject(
            output,
            AudioReconcileReasonSourceRegistryGenerationMismatch);
    if (!NormalizePath(observed.path, sizeof(observed.path), false,
                       mapped->resolved, sizeof(mapped->resolved)))
        return Reject(output, AudioReconcileReasonInvalidText);

    std::uint32_t selected = UINT32_MAX;
    std::size_t selected_length = 0;
    std::uint32_t equally_long = 0;
    std::uint32_t fallback = UINT32_MAX;
    if (observed.exact_source) {
        if (!observed.source)
            return Reject(
                output, AudioReconcileReasonProviderIdentityMismatch);
        for (std::uint32_t index = 0;
             index < sources.source_count; ++index) {
            if (sources.sources[index].source != observed.source)
                continue;
            if (selected != UINT32_MAX)
                return Reject(
                    output, AudioReconcileReasonProviderIdentityMismatch);
            selected = index;
        }
        if (selected == UINT32_MAX)
            return Reject(
                output, AudioReconcileReasonProviderIdentityMismatch);
    } else {
        if (observed.source)
            return Reject(
                output, AudioReconcileReasonProviderIdentityMismatch);
        for (std::uint32_t index = 0; index < sources.source_count; ++index) {
            const vfs::FilesystemSourceInventoryRecord& candidate =
                sources.sources[index];
            char normalized[kNormalizedPathCapacity] = {};
            if (!NormalizePath(candidate.path, sizeof(candidate.path), false,
                               normalized, sizeof(normalized)))
                return Reject(output, AudioReconcileReasonInvalidText);
            if (candidate.native_fallback) {
                if (fallback != UINT32_MAX)
                    return Reject(
                        output, AudioReconcileReasonProviderPrefixAmbiguous);
                fallback = index;
                continue;
            }
            if (!PathWithinRoot(mapped->resolved, normalized)) continue;
            const std::size_t length =
                BoundedLength(normalized, sizeof(normalized));
            if (length > selected_length) {
                selected = index;
                selected_length = length;
                equally_long = 1U;
            } else if (length == selected_length) {
                ++equally_long;
            }
        }
        if (selected == UINT32_MAX) selected = fallback;
        else if (equally_long != 1U)
            return Reject(output,
                          AudioReconcileReasonProviderPrefixAmbiguous);
        if (selected == UINT32_MAX)
            return Reject(output, AudioReconcileReasonProviderUnknown);
    }

    const vfs::FilesystemSourceInventoryRecord& source =
        sources.sources[selected];
    if (!NormalizePath(source.path, sizeof(source.path), false,
                       mapped->root, sizeof(mapped->root)))
        return Reject(output, AudioReconcileReasonInvalidText);
    const BaselineSourceKind kind =
        ClassifySource(mapped->root, source.native_fallback);
    if (kind == BaselineSourceKind::Unknown)
        return Reject(output, AudioReconcileReasonUnknownSourceKind);
    if (observed.exact_source &&
        (observed.source != source.source ||
         (!source.native_fallback &&
           !PathWithinRoot(mapped->resolved, mapped->root))))
        return Reject(output,
                      AudioReconcileReasonProviderIdentityMismatch);

    /* A physical native path proves the resolved provider path and permits a
     * diagnostic source-root classification.  It does not identify the
     * CFSSource that fs::open would have selected.  Retain pointer/index/list
     * identity only when an exact pointer was observed in the same frozen
     * source-registry generation. */
    mapped->identity.source =
        observed.exact_source ? observed.source : 0;
    mapped->identity.normalized_path_fingerprint = HashText(
        "audio_source_root_v1", mapped->root, sizeof(mapped->root));
    mapped->identity.source_registry_generation =
        sources.registry_generation;
    mapped->identity.source_registry_generation_observed = true;
    mapped->identity.source_record_index = observed.exact_source
        ? selected
        : UINT32_MAX;
    mapped->identity.source_inventory_ordinal = observed.exact_source
        ? source.list_ordinal
        : 0;
    mapped->identity.source_kind = kind;
    mapped->identity.observed = true;
    mapped->identity.exact_source = observed.exact_source;
    if (!mapped->identity.exact_source) {
        output->exact_source_coverage_complete = false;
        output->unresolved_fields |=
            AudioReconcileUnresolvedExactSourceCoverage;
    }
    mapped->resolved_path_fingerprint = HashText(
        "audio_resolved_provider_path_v1", mapped->resolved,
        sizeof(mapped->resolved));
    return mapped->identity.normalized_path_fingerprint &&
        mapped->resolved_path_fingerprint;
}

bool EqualProviderIdentity(
    const BaselineAudioProviderIdentity& left,
    const BaselineAudioProviderIdentity& right) noexcept
{
    return left.source == right.source &&
        left.normalized_path_fingerprint ==
            right.normalized_path_fingerprint &&
        left.source_registry_generation ==
            right.source_registry_generation &&
        left.source_record_index == right.source_record_index &&
        left.source_inventory_ordinal ==
            right.source_inventory_ordinal &&
        left.source_kind == right.source_kind &&
        left.observed == right.observed &&
        left.exact_source == right.exact_source &&
        left.source_registry_generation_observed ==
            right.source_registry_generation_observed;
}

std::uint32_t BodyU32(const unsigned char* body,
                      std::size_t offset) noexcept
{
    std::uint32_t value = 0;
    std::memcpy(&value, body + offset, sizeof(value));
    return value;
}

bool StageSettings(
    const audio_snapshot::AudioBaselineWindowsSnapshot& raw,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!raw.settings.available || !raw.settings.fingerprint ||
        !raw.settings.global_settings_fingerprint)
        return Reject(output, AudioReconcileReasonSettingsIncomplete);
    BaselineAudioSettings settings = {};
    settings.fingerprint = raw.settings.fingerprint;
    settings.global_settings_selector_fingerprint =
        raw.settings.global_settings_fingerprint;
    settings.applied_sound_mode = BodyU32(raw.settings.pod, 0x00U);
    settings.requested_sound_mode = BodyU32(raw.settings.pod, 0x04U);
    settings.applied_channels = BodyU32(raw.settings.pod, 0x08U);
    settings.requested_channels = BodyU32(raw.settings.pod, 0x0CU);
    settings.applied_mix_frequency = BodyU32(raw.settings.pod, 0x10U);
    settings.requested_mix_frequency = BodyU32(raw.settings.pod, 0x14U);
    settings.applied_flags = raw.settings.pod[0x18U];
    settings.requested_flags = raw.settings.pod[0x19U];
    settings.applied_api = BodyU32(raw.settings.pod, 0x1CU);
    settings.requested_api = BodyU32(raw.settings.pod, 0x20U);
    settings.applied_quality_3d = BodyU32(raw.settings.pod, 0x24U);
    settings.requested_quality_3d = BodyU32(raw.settings.pod, 0x28U);
    settings.applied_mixer_blocks = BodyU32(raw.settings.pod, 0x2CU);
    settings.requested_mixer_blocks = BodyU32(raw.settings.pod, 0x30U);
    settings.fx_volume_bits = raw.settings.fx_volume_bits;
    settings.wave_volume_bits = raw.settings.wave_volume_bits;
    settings.speech_volume_bits = raw.settings.speech_volume_bits;
    settings.movie_volume_bits = raw.settings.movie_volume_bits;
    settings.movie_source_volume_bits =
        raw.settings.movie_volume_source_bits;
    settings.selector_available = true;
    settings.available = true;
    output->settings = settings;
    return true;
}

bool ValidateObservationHeader(
    const audio_startup::AudioStartupObservationSnapshot& observations,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!observations.revision ||
        observations.script_count >
            audio_startup::kAudioStartupScriptCapacity ||
        observations.bank_count >
            audio_startup::kAudioStartupBankCapacity)
        return Reject(output, AudioReconcileReasonInputCountOutOfRange);
    if (!audio_startup::AudioStartupObservationLossEmpty(
            observations.loss))
        return Reject(output, AudioReconcileReasonObservationLoss);
    if (!observations.coverage_started_before_audio_singletons)
        return Reject(output, AudioReconcileReasonLateHookCoverage);
    if (!observations.hook_plan_complete)
        return Reject(output, AudioReconcileReasonHookPlanIncomplete);

    struct Interval {
        std::uint64_t enter;
        std::uint64_t exit;
    };
    Interval intervals[
        audio_startup::kAudioStartupScriptCapacity +
        audio_startup::kAudioStartupBankCapacity] = {};
    std::uint32_t interval_count = 0;
    for (std::uint32_t index = 0;
         index < observations.script_count; ++index) {
        const auto& observation = observations.scripts[index];
        if (!observation.enter_sequence || !observation.exit_sequence ||
            observation.exit_sequence <= observation.enter_sequence)
            return Reject(
                output, AudioReconcileReasonInvalidObservationInterval);
        intervals[interval_count++] = {
            observation.enter_sequence, observation.exit_sequence};
    }
    for (std::uint32_t index = 0;
         index < observations.bank_count; ++index) {
        const audio_startup::AudioStartupBankObservation& observation =
            observations.banks[index];
        if (!observation.enter_sequence || !observation.exit_sequence ||
            observation.exit_sequence <= observation.enter_sequence)
            return Reject(
                output, AudioReconcileReasonInvalidObservationInterval);
        if (observation.owner ==
                audio_startup::AudioStartupBankOwner::Unknown ||
            !observation.bank_name_complete ||
            !observation.bank_name[0] ||
            !observation.observation_complete ||
            observation.provider_outcome ==
                audio_startup::AudioStartupProviderOutcome::Unknown ||
            observation.provider_outcome ==
                audio_startup::AudioStartupProviderOutcome::
                    ObservationIncomplete)
            return Reject(output, AudioReconcileReasonObservationLoss);
        if (observation.owner ==
                audio_startup::AudioStartupBankOwner::Fmod &&
            observation.reload && observation.result >= 0)
            return Reject(
                output, AudioReconcileReasonFmodReloadObservation);
        if (observation.result >= 0 &&
            observation.owner ==
                audio_startup::AudioStartupBankOwner::Fmod &&
            !SuccessfulFmodExact(observation) &&
            !FmodCacheHit(observation))
            return Reject(output, AudioReconcileReasonObservationLoss);
        if (observation.result == 0 &&
            observation.owner ==
                audio_startup::AudioStartupBankOwner::Speech &&
            !SuccessfulSpeech(observation))
            return Reject(output, AudioReconcileReasonObservationLoss);
        intervals[interval_count++] = {
            observation.enter_sequence, observation.exit_sequence};
    }
    for (std::uint32_t index = 0; index < interval_count; ++index) {
        for (std::uint32_t prior = 0; prior < index; ++prior) {
            const Interval& left = intervals[prior];
            const Interval& right = intervals[index];
            if (left.enter == right.enter || left.enter == right.exit ||
                left.exit == right.enter || left.exit == right.exit)
                return Reject(
                    output,
                    AudioReconcileReasonDuplicateObservationEndpoint);
            if (right.enter < left.exit && left.enter < right.exit) {
                output->dependency_order_complete = false;
                output->unresolved_fields |=
                    AudioReconcileUnresolvedDependencyOrdering;
            }
        }
    }
    return true;
}

void SortMatches(OrderedMatch* matches,
                 std::uint32_t count) noexcept
{
    for (std::uint32_t index = 1; index < count; ++index) {
        const OrderedMatch value = matches[index];
        std::uint32_t insert = index;
        while (insert &&
               matches[insert - 1U].enter_sequence >
                   value.enter_sequence) {
            matches[insert] = matches[insert - 1U];
            --insert;
        }
        matches[insert] = value;
    }
}

bool StageScripts(
    const audio_snapshot::AudioBaselineWindowsSnapshot& raw,
    const audio_startup::AudioStartupObservationSnapshot& observations,
    const vfs::FilesystemSourceInventory& sources,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!raw.script_count ||
        raw.script_count > kBaselineAudioScriptCapacity)
        return Reject(output, AudioReconcileReasonOutputCapacityExceeded);
    if (!observations.script_count ||
        observations.script_count > kBaselineAudioScriptCapacity)
        return Reject(output, AudioReconcileReasonScriptCountMismatch);

    OrderedMatch matches[kBaselineAudioScriptCapacity] = {};
    bool raw_seen[kBaselineAudioScriptCapacity] = {};
    char raw_paths[kBaselineAudioScriptCapacity]
                  [audio_startup::kAudioStartupLogicalPathCapacity] = {};
    for (std::uint32_t raw_index = 0;
         raw_index < raw.script_count; ++raw_index) {
        if (!NormalizePath(
                raw.scripts[raw_index].logical_path,
                sizeof(raw.scripts[raw_index].logical_path), true,
                raw_paths[raw_index], sizeof(raw_paths[raw_index])))
            return Reject(output, AudioReconcileReasonInvalidText);
        for (std::uint32_t prior = 0; prior < raw_index; ++prior)
            if (EqualText(raw_paths[prior], raw_paths[raw_index]))
                return Reject(output, AudioReconcileReasonScriptDuplicate);
    }

    for (std::uint32_t observation_index = 0;
         observation_index < observations.script_count;
         ++observation_index) {
        const audio_startup::AudioStartupScriptObservation& observed =
            observations.scripts[observation_index];
        if (!observed.request_complete)
            return Reject(output, AudioReconcileReasonInvalidText);
        if (!observed.parse_succeeded)
            return Reject(output, AudioReconcileReasonScriptParseFailed);
        char normalized[
            audio_startup::kAudioStartupLogicalPathCapacity] = {};
        if (!NormalizePath(observed.logical_path,
                           sizeof(observed.logical_path), true,
                           normalized, sizeof(normalized)))
            return Reject(output, AudioReconcileReasonInvalidText);
        std::uint32_t raw_match = UINT32_MAX;
        for (std::uint32_t raw_index = 0;
             raw_index < raw.script_count; ++raw_index) {
            if (!EqualText(raw_paths[raw_index], normalized)) continue;
            if (raw_match != UINT32_MAX)
                return Reject(output, AudioReconcileReasonScriptDuplicate);
            raw_match = raw_index;
        }
        if (raw_match == UINT32_MAX)
            return Reject(output,
                          AudioReconcileReasonScriptObservationMissing);
        raw_seen[raw_match] = true;
        matches[observation_index] = {
            observed.enter_sequence, raw_match, observation_index};
    }
    for (std::uint32_t raw_index = 0;
         raw_index < raw.script_count; ++raw_index)
        if (!raw_seen[raw_index])
            return Reject(
                output, AudioReconcileReasonScriptObservationMissing);
    SortMatches(matches, observations.script_count);

    std::uint64_t provider_manifest =
        BeginHash("audio_script_provider_manifest_v1");
    std::uint64_t occurrence_manifest =
        BeginHash("audio_script_occurrence_manifest_v1");
    HashU32(&provider_manifest, observations.script_count);
    HashU32(&occurrence_manifest, observations.script_count);
    for (std::uint32_t order = 0;
         order < observations.script_count; ++order) {
        const OrderedMatch& match = matches[order];
        const audio_startup::AudioStartupScriptObservation& observed =
            observations.scripts[match.observation_index];
        MappedProvider provider = {};
        if (!MapProvider(observed.provider, sources, &provider, output))
            return false;
        BaselineAudioScript& staged = output->scripts[order];
        staged.logical_path_fingerprint = HashText(
            "audio_script_logical_path_v1", raw_paths[match.raw_index],
            sizeof(raw_paths[match.raw_index]));
        std::uint64_t content =
            BeginHash("audio_script_provider_identity_v1");
        HashU64(&content, staged.logical_path_fingerprint);
        HashU64(&content, provider.resolved_path_fingerprint);
        HashU64(&content, provider.identity.normalized_path_fingerprint);
        HashU64(&content, provider.identity.source_registry_generation);
        HashU32(&content, provider.identity.source_record_index);
        HashU32(&content, provider.identity.source_inventory_ordinal);
        staged.content_identity_fingerprint = NonzeroHash(content);
        staged.load_order = order + 1U;
        staged.provider_identity = provider.identity;
        staged.natural_load_observed = true;
        staged.parse_succeeded = true;
        if (!CopyText(staged.logical_path, sizeof(staged.logical_path),
                      raw_paths[match.raw_index]) ||
            !CopyText(staged.provider, sizeof(staged.provider),
                      provider.root))
            return Reject(output,
                          AudioReconcileReasonOutputCapacityExceeded);
        HashU64(&provider_manifest,
                provider.identity.normalized_path_fingerprint);
        HashU64(&provider_manifest,
                provider.identity.source_registry_generation);
        HashU32(&provider_manifest,
                provider.identity.source_record_index);
        HashU64(&occurrence_manifest,
                staged.logical_path_fingerprint);
        HashU64(&occurrence_manifest,
                staged.content_identity_fingerprint);
        HashU32(&occurrence_manifest, staged.load_order);
    }
    output->script_count = observations.script_count;
    output->script_manifest_fingerprint =
        raw.script_manifest_fingerprint;
    output->script_occurrence_manifest_fingerprint =
        NonzeroHash(occurrence_manifest);
    output->script_provider_fingerprint =
        NonzeroHash(provider_manifest);
    return output->script_manifest_fingerprint != 0 &&
        output->script_occurrence_manifest_fingerprint != 0;
}

bool NormalizeBankName(const char* input, std::size_t capacity,
                       char* output, std::size_t output_capacity) noexcept
{
    return NormalizePath(input, capacity, true, output, output_capacity);
}

BaselineAudioBankRole BankRole(BaselineSourceKind source_kind,
                               bool localized,
                               const char* name) noexcept
{
    if (ContainsText(name, "menu")) return BaselineAudioBankRole::Menu;
    if (localized || source_kind == BaselineSourceKind::LocaleSpeech)
        return BaselineAudioBankRole::Speech;
    if (source_kind == BaselineSourceKind::Dlc)
        return BaselineAudioBankRole::Dlc;
    if (source_kind == BaselineSourceKind::Base)
        return BaselineAudioBankRole::Base;
    return BaselineAudioBankRole::Unknown;
}

bool SuccessfulFmodExact(
    const audio_startup::AudioStartupBankObservation& observation) noexcept
{
    return observation.owner ==
            audio_startup::AudioStartupBankOwner::Fmod &&
        observation.result == 0 && observation.observation_complete &&
        observation.provider_outcome ==
            audio_startup::AudioStartupProviderOutcome::
                NaturalOpenResolvedPath;
}

bool FmodCacheHit(
    const audio_startup::AudioStartupBankObservation& observation) noexcept
{
    return observation.owner ==
            audio_startup::AudioStartupBankOwner::Fmod &&
        observation.result == 1 && observation.observation_complete &&
        (observation.provider_outcome ==
             audio_startup::AudioStartupProviderOutcome::
                 CacheHitProviderMissing ||
         observation.provider_outcome ==
             audio_startup::AudioStartupProviderOutcome::
                 CacheHitPriorExact);
}

bool StageFmodBanks(
    const audio_snapshot::AudioBaselineWindowsSnapshot& raw,
    const audio_startup::AudioStartupObservationSnapshot& observations,
    const vfs::FilesystemSourceInventory& sources,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!raw.bank_count || raw.bank_count > kBaselineAudioBankCapacity)
        return Reject(output, AudioReconcileReasonOutputCapacityExceeded);
    OrderedMatch matches[kBaselineAudioBankCapacity] = {};
    bool used_exact[audio_startup::kAudioStartupBankCapacity] = {};
    bool used_cache[audio_startup::kAudioStartupBankCapacity] = {};
    char raw_names[kBaselineAudioBankCapacity]
                  [audio_startup::kAudioStartupLogicalPathCapacity] = {};

    for (std::uint32_t raw_index = 0;
         raw_index < raw.bank_count; ++raw_index) {
        if (!NormalizeBankName(raw.banks[raw_index].name,
                               sizeof(raw.banks[raw_index].name),
                               raw_names[raw_index],
                               sizeof(raw_names[raw_index])))
            return Reject(output, AudioReconcileReasonInvalidText);
        for (std::uint32_t prior = 0; prior < raw_index; ++prior)
            if (EqualText(raw_names[prior], raw_names[raw_index]) ||
                raw.banks[prior].tree_key ==
                    raw.banks[raw_index].tree_key ||
                raw.banks[prior].record == raw.banks[raw_index].record)
                return Reject(output, AudioReconcileReasonFmodDuplicate);

        std::uint32_t exact = UINT32_MAX;
        for (std::uint32_t observation_index = 0;
             observation_index < observations.bank_count;
             ++observation_index) {
            const audio_startup::AudioStartupBankObservation& observed =
                observations.banks[observation_index];
            if (observed.owner !=
                audio_startup::AudioStartupBankOwner::Fmod)
                continue;
            if (!observed.bank_name_complete)
                return Reject(output, AudioReconcileReasonInvalidText);
            char name[audio_startup::kAudioStartupLogicalPathCapacity] = {};
            if (!NormalizeBankName(observed.bank_name,
                                   sizeof(observed.bank_name), name,
                                   sizeof(name)))
                return Reject(output, AudioReconcileReasonInvalidText);
            if (!EqualText(raw_names[raw_index], name)) continue;
            if (SuccessfulFmodExact(observed)) {
                if (exact != UINT32_MAX)
                    return Reject(output,
                                  AudioReconcileReasonFmodDuplicate);
                exact = observation_index;
            }
        }
        if (exact == UINT32_MAX) {
            bool matching_cache = false;
            for (std::uint32_t observation_index = 0;
                 observation_index < observations.bank_count;
                 ++observation_index) {
                const audio_startup::AudioStartupBankObservation& cache =
                    observations.banks[observation_index];
                if (!FmodCacheHit(cache)) continue;
                char name[
                    audio_startup::kAudioStartupLogicalPathCapacity] = {};
                if (!NormalizeBankName(cache.bank_name,
                                       sizeof(cache.bank_name), name,
                                       sizeof(name)))
                    return Reject(output,
                                  AudioReconcileReasonInvalidText);
                if (EqualText(raw_names[raw_index], name)) {
                    matching_cache = true;
                    break;
                }
            }
            return Reject(
                output,
                matching_cache
                    ? AudioReconcileReasonFmodCacheHitWithoutPriorExact
                    : AudioReconcileReasonFmodObservationMissing);
        }
        const audio_startup::AudioStartupBankObservation& exact_observation =
            observations.banks[exact];
        if (exact_observation.manager != raw.bank_manager)
            return Reject(output,
                          AudioReconcileReasonFmodManagerMismatch);
        if (exact_observation.localized_observed) {
            if (exact_observation.localized !=
                raw.banks[raw_index].localised)
                return Reject(output,
                              AudioReconcileReasonLocalizedMismatch);
        } else {
            output->fmod_localized_evidence_complete = false;
            output->unresolved_fields |=
                AudioReconcileUnresolvedFmodLocalizedEvidence;
        }
        used_exact[exact] = true;
        matches[raw_index] = {
            exact_observation.enter_sequence, raw_index, exact};

        for (std::uint32_t observation_index = 0;
             observation_index < observations.bank_count;
             ++observation_index) {
            const audio_startup::AudioStartupBankObservation& cache =
                observations.banks[observation_index];
            if (!FmodCacheHit(cache)) continue;
            char name[audio_startup::kAudioStartupLogicalPathCapacity] = {};
            if (!NormalizeBankName(cache.bank_name,
                                   sizeof(cache.bank_name), name,
                                   sizeof(name)))
                return Reject(output, AudioReconcileReasonInvalidText);
            if (!EqualText(raw_names[raw_index], name)) continue;
            if (cache.enter_sequence <=
                    exact_observation.exit_sequence ||
                cache.manager != raw.bank_manager)
                return Reject(
                    output,
                    AudioReconcileReasonFmodCacheHitWithoutPriorExact);
            if (cache.localized_observed) {
                if (cache.localized != raw.banks[raw_index].localised)
                    return Reject(
                        output, AudioReconcileReasonLocalizedMismatch);
            } else {
                output->fmod_localized_evidence_complete = false;
                output->unresolved_fields |=
                    AudioReconcileUnresolvedFmodLocalizedEvidence;
            }
            if (cache.provider_outcome ==
                audio_startup::AudioStartupProviderOutcome::
                    CacheHitPriorExact) {
                MappedProvider exact_provider = {};
                MappedProvider inherited_provider = {};
                if (!MapProvider(exact_observation.provider, sources,
                                 &exact_provider, output) ||
                    !MapProvider(cache.provider, sources,
                                 &inherited_provider, output))
                    return false;
                if (!EqualProviderIdentity(
                        exact_provider.identity,
                        inherited_provider.identity) ||
                    exact_provider.resolved_path_fingerprint !=
                        inherited_provider.resolved_path_fingerprint)
                    return Reject(
                        output,
                        AudioReconcileReasonProviderIdentityMismatch);
            } else if (cache.provider.path_complete ||
                       cache.provider.source ||
                       cache.provider.
                           source_registry_generation_observed) {
                return Reject(
                    output,
                    AudioReconcileReasonProviderIdentityMismatch);
            }
            used_cache[observation_index] = true;
            ++output->inherited_fmod_cache_hit_count;
        }
    }

    std::uint32_t successful_count = 0;
    for (std::uint32_t index = 0; index < observations.bank_count; ++index) {
        const audio_startup::AudioStartupBankObservation& observed =
            observations.banks[index];
        if (SuccessfulFmodExact(observed)) {
            ++successful_count;
            if (!used_exact[index])
                return Reject(output,
                              AudioReconcileReasonFmodCountMismatch);
        } else if (FmodCacheHit(observed) && !used_cache[index]) {
            return Reject(
                output,
                AudioReconcileReasonFmodCacheHitWithoutPriorExact);
        }
    }
    if (successful_count != raw.bank_count)
        return Reject(output, AudioReconcileReasonFmodCountMismatch);
    SortMatches(matches, raw.bank_count);

    for (std::uint32_t order = 0; order < raw.bank_count; ++order) {
        const OrderedMatch& match = matches[order];
        const audio_snapshot::AudioBaselineWindowsBankRecord& member =
            raw.banks[match.raw_index];
        const audio_startup::AudioStartupBankObservation& observed =
            observations.banks[match.observation_index];
        MappedProvider provider = {};
        if (!MapProvider(observed.provider, sources, &provider, output))
            return false;
        const BaselineAudioBankRole role = BankRole(
            provider.identity.source_kind, member.localised,
            raw_names[match.raw_index]);
        if (role == BaselineAudioBankRole::Unknown)
            return Reject(output,
                          AudioReconcileReasonUnknownSourceKind);

        BaselineAudioBank& staged = output->fmod_banks[order];
        staged.logical_bank_id = member.tree_key;
        staged.provider_fingerprint =
            provider.identity.normalized_path_fingerprint;
        std::uint64_t content =
            BeginHash("fmod_bank_provider_identity_v1");
        HashU64(&content, member.structural_fingerprint);
        HashU64(&content, provider.resolved_path_fingerprint);
        HashU32(&content, member.record_hash);
        staged.content_fingerprint = NonzeroHash(content);
        staged.dependency_order = order + 1U;
        staged.reference_count = 0;
        staged.instance_generation = 0;
        staged.manager_record = member.record;
        staged.retained_native_file = member.native_file;
        staged.provider_identity = provider.identity;
        staged.role = role;
        staged.membership_observed = true;
        staged.natural_load_observed = true;
        staged.dependency_order_observed = true;
        staged.reference_count_observed = false;
        staged.localized = member.localised;
        if (!CopyText(staged.name, sizeof(staged.name),
                      raw_names[match.raw_index]) ||
            !CopyText(staged.provider, sizeof(staged.provider),
                      provider.root))
            return Reject(output,
                          AudioReconcileReasonOutputCapacityExceeded);
        if (!staged.logical_bank_id || !staged.content_fingerprint)
            return Reject(output,
                          AudioReconcileReasonContentIdentityUnavailable);
    }
    output->fmod_bank_count = raw.bank_count;
    return true;
}

std::uint32_t EngineBankHash(const char* name) noexcept
{
    std::uint32_t hash = 0;
    if (!name) return hash;
    while (*name) {
        const std::int32_t byte =
            static_cast<std::int8_t>(*name++);
        hash = hash * 0x29U + static_cast<std::uint32_t>(byte);
    }
    return hash;
}

bool SuccessfulSpeech(
    const audio_startup::AudioStartupBankObservation& observation) noexcept
{
    return observation.owner ==
            audio_startup::AudioStartupBankOwner::Speech &&
        observation.result == 0 && observation.observation_complete &&
        observation.provider_outcome ==
            audio_startup::AudioStartupProviderOutcome::
                NaturalOpenResolvedPath;
}

bool StageSpeechBanks(
    const audio_snapshot::AudioBaselineWindowsSnapshot& raw,
    const audio_startup::AudioStartupObservationSnapshot& observations,
    const vfs::FilesystemSourceInventory& sources,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!raw.speech_bank_count ||
        raw.speech_bank_count > kBaselineSpeechBankCapacity)
        return Reject(output, AudioReconcileReasonOutputCapacityExceeded);
    OrderedMatch matches[kBaselineSpeechBankCapacity] = {};
    bool used[audio_startup::kAudioStartupBankCapacity] = {};
    for (std::uint32_t raw_index = 0;
         raw_index < raw.speech_bank_count; ++raw_index) {
        const audio_snapshot::AudioBaselineWindowsSpeechBankRecord& member =
            raw.speech_banks[raw_index];
        if (!member.bank || !member.bank_hash ||
            !member.descriptor_fingerprint)
            return Reject(output,
                          AudioReconcileReasonContentIdentityUnavailable);
        for (std::uint32_t prior = 0; prior < raw_index; ++prior)
            if (raw.speech_banks[prior].bank == member.bank ||
                raw.speech_banks[prior].bank_hash == member.bank_hash)
                return Reject(output, AudioReconcileReasonSpeechDuplicate);

        std::uint32_t found = UINT32_MAX;
        for (std::uint32_t observation_index = 0;
             observation_index < observations.bank_count;
             ++observation_index) {
            const audio_startup::AudioStartupBankObservation& observed =
                observations.banks[observation_index];
            if (!SuccessfulSpeech(observed)) continue;
            if (!observed.bank_name_complete)
                return Reject(output, AudioReconcileReasonInvalidText);
            char name[audio_startup::kAudioStartupLogicalPathCapacity] = {};
            if (!NormalizeBankName(observed.bank_name,
                                   sizeof(observed.bank_name), name,
                                   sizeof(name)))
                return Reject(output, AudioReconcileReasonInvalidText);
            if (EngineBankHash(name) != member.bank_hash) continue;
            if (found != UINT32_MAX)
                return Reject(output,
                              AudioReconcileReasonSpeechDuplicate);
            found = observation_index;
        }
        if (found == UINT32_MAX)
            return Reject(output,
                          AudioReconcileReasonSpeechObservationMissing);
        if (used[found])
            return Reject(output, AudioReconcileReasonSpeechDuplicate);
        if (observations.banks[found].localized_observed) {
            if (observations.banks[found].localized != member.localised)
                return Reject(output,
                              AudioReconcileReasonLocalizedMismatch);
        } else {
            output->speech_localized_evidence_complete = false;
            output->unresolved_fields |=
                AudioReconcileUnresolvedSpeechLocalizedEvidence;
        }
        used[found] = true;
        matches[raw_index] = {
            observations.banks[found].enter_sequence,
            raw_index, found};
    }

    std::uint32_t successful_count = 0;
    for (std::uint32_t index = 0; index < observations.bank_count; ++index) {
        if (!SuccessfulSpeech(observations.banks[index])) continue;
        ++successful_count;
        if (!used[index])
            return Reject(output, AudioReconcileReasonSpeechHashMismatch);
    }
    if (successful_count != raw.speech_bank_count)
        return Reject(output, AudioReconcileReasonSpeechCountMismatch);
    SortMatches(matches, raw.speech_bank_count);

    for (std::uint32_t order = 0;
         order < raw.speech_bank_count; ++order) {
        const OrderedMatch& match = matches[order];
        const audio_snapshot::AudioBaselineWindowsSpeechBankRecord& member =
            raw.speech_banks[match.raw_index];
        const audio_startup::AudioStartupBankObservation& observed =
            observations.banks[match.observation_index];
        MappedProvider provider = {};
        if (!MapProvider(observed.provider, sources, &provider, output))
            return false;
        char name[audio_startup::kAudioStartupLogicalPathCapacity] = {};
        if (!NormalizeBankName(observed.bank_name,
                               sizeof(observed.bank_name), name,
                               sizeof(name)))
            return Reject(output, AudioReconcileReasonInvalidText);

        BaselineSpeechBank& staged = output->speech_banks[order];
        staged.logical_bank_id = member.bank_hash;
        staged.provider_fingerprint =
            provider.identity.normalized_path_fingerprint;
        std::uint64_t content =
            BeginHash("speech_bank_provider_identity_v1");
        HashU64(&content, member.descriptor_fingerprint);
        HashU64(&content, provider.resolved_path_fingerprint);
        staged.content_identity_fingerprint = NonzeroHash(content);
        staged.instance_generation = 0;
        staged.bank_record = member.bank;
        staged.load_order = order + 1U;
        staged.provider_identity = provider.identity;
        staged.membership_observed = true;
        staged.natural_load_observed = true;
        staged.load_order_observed = true;
        staged.localized = member.localised;
        if (!CopyText(staged.name, sizeof(staged.name), name) ||
            !CopyText(staged.provider, sizeof(staged.provider),
                      provider.root))
            return Reject(output,
                          AudioReconcileReasonOutputCapacityExceeded);
    }
    output->speech_bank_count = raw.speech_bank_count;
    return true;
}

}  // namespace

bool ReconcileAudioStartupBaseline(
    const audio_snapshot::AudioBaselineWindowsSnapshot* raw,
    const audio_startup::AudioStartupObservationSnapshot* observations,
    const vfs::FilesystemSourceInventory* sources,
    AudioStartupBaselineStaging* output) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    output->result = AudioStartupBaselineReconcileResult::Rejected;
    output->unresolved_fields =
        kAudioStartupBaselineUnresolvedFields;
    output->exact_source_coverage_complete = true;
    output->fmod_localized_evidence_complete = true;
    output->speech_localized_evidence_complete = true;
    output->dependency_order_complete = true;
    if (!raw || !observations || !sources)
        return Reject(output, AudioReconcileReasonInvalidArgument);
    if (!raw->raw_snapshot_complete ||
        (raw->complete_fields &
         audio_snapshot::kAudioWindowsRequiredRawFields) !=
            audio_snapshot::kAudioWindowsRequiredRawFields)
        return Reject(output,
                      AudioReconcileReasonRawSnapshotIncomplete);
    if (raw->reason_mask)
        return Reject(output, AudioReconcileReasonRawSnapshotFailure);
    if (raw->backend_kind !=
        audio_snapshot::AudioBaselineWindowsBackendKind::Fmod)
        return Reject(output, AudioReconcileReasonBackendUnsupported);
    if (!raw->speech_vector_observation_stable)
        return Reject(output,
                      AudioReconcileReasonRawSnapshotIncomplete);
    if (!ValidateObservationHeader(*observations, output) ||
        !ValidateSourceInventory(*sources, output) ||
        !StageSettings(*raw, output) ||
        !StageScripts(*raw, *observations, *sources, output) ||
        !StageFmodBanks(*raw, *observations, *sources, output) ||
        !StageSpeechBanks(*raw, *observations, *sources, output))
        return false;

    output->provider_coverage_complete = true;
    output->fully_reconciled_observational_inventory =
        output->exact_source_coverage_complete &&
        output->fmod_localized_evidence_complete &&
        output->speech_localized_evidence_complete &&
        output->dependency_order_complete;
    /* Observation completeness is deliberately not reset authority.  Raw
     * speech synchronization, content bytes, reference models, instance
     * generations, and a complete replay owner remain unresolved. */
    output->authoritative_b0_complete = false;
    output->reset_ready = false;
    output->reset_owner_proven = false;
    output->result = AudioStartupBaselineReconcileResult::
        ObservationalInventoryReconciled;
    return true;
}

const char* AudioStartupBaselineReconcileResultName(
    AudioStartupBaselineReconcileResult result) noexcept
{
    switch (result) {
    case AudioStartupBaselineReconcileResult::Rejected:
        return "rejected";
    case AudioStartupBaselineReconcileResult::
            ObservationalInventoryReconciled:
        return "observational_inventory_reconciled";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_reconcile
