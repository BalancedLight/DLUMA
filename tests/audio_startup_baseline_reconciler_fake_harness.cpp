#include "fixes/custom_map_asset_isolation/AudioStartupBaselineReconciler.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace reconcile =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_reconcile;
namespace snapshot =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot;
namespace startup =
    dluma::fixes::custom_map_asset_isolation::audio_startup;
namespace vfs = dluma::fixes::custom_map_asset_isolation::vfs;

namespace {

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,  \
                         #expression);                                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

constexpr std::uintptr_t kBaseSource = 0x1000U;
constexpr std::uintptr_t kDataSource = 0x2000U;
constexpr std::uintptr_t kSpeechSource = 0x3000U;
constexpr std::uintptr_t kLoaderSource = 0x3800U;
constexpr std::uintptr_t kFallbackSource = 0x4000U;
constexpr std::uintptr_t kBankManager = 0x8000U;

struct Fixture {
    snapshot::AudioBaselineWindowsSnapshot raw;
    startup::AudioStartupObservationSnapshot observations;
    vfs::FilesystemSourceInventory sources;
};

template <typename T>
void Store(unsigned char* bytes, std::size_t offset, T value) noexcept
{
    std::memcpy(bytes + offset, &value, sizeof(value));
}

void Copy(char* output, std::size_t capacity,
          const char* text) noexcept
{
    const std::size_t length = std::strlen(text);
    if (length + 1U <= capacity)
        std::memcpy(output, text, length + 1U);
}

std::uint32_t EngineHash(const char* name) noexcept
{
    std::uint32_t hash = 0;
    while (*name) {
        const std::int32_t byte =
            static_cast<std::int8_t>(*name++);
        hash = hash * 0x29U + static_cast<std::uint32_t>(byte);
    }
    return hash;
}

void SetProvider(startup::AudioStartupProvider* provider,
                 const char* path, std::uintptr_t source = 0,
                 std::uint64_t registry_generation = 77U) noexcept
{
    std::memset(provider, 0, sizeof(*provider));
    provider->source = source;
    provider->source_registry_generation = registry_generation;
    provider->source_registry_generation_observed = true;
    provider->exact_source = source != 0;
    provider->path_complete = true;
    Copy(provider->path, sizeof(provider->path), path);
}

void SetSource(vfs::FilesystemSourceInventoryRecord* source,
               std::uintptr_t pointer, std::uint32_t ordinal,
               const char* path, bool fallback = false) noexcept
{
    source->source = pointer;
    source->link = pointer + 8U;
    source->list_ordinal = ordinal;
    source->native_fallback = fallback;
    Copy(source->path, sizeof(source->path), path);
}

void SetFmodMember(snapshot::AudioBaselineWindowsBankRecord* member,
                   const char* name, std::uintptr_t record,
                   std::uint32_t record_hash) noexcept
{
    member->record = record;
    member->node_link = record +
        snapshot::kWindowsAudioBankNodeLinkOffset;
    member->native_file = record + 0x1000U;
    member->tree_key = EngineHash(name);
    member->record_hash = record_hash;
    member->name_length =
        static_cast<std::uint32_t>(std::strlen(name));
    member->tree_color = record == 0x9100U ? 1U : 0U;
    member->structural_fingerprint = record + 0xABC000U;
    member->localised = false;
    Copy(member->name, sizeof(member->name), name);
}

void SetFmodObservation(
    startup::AudioStartupBankObservation* observation,
    std::uint64_t sequence, const char* name,
    const char* provider_path) noexcept
{
    observation->enter_sequence = sequence;
    observation->exit_sequence = sequence + 1U;
    observation->scope_generation = sequence + 100U;
    observation->manager = kBankManager;
    observation->result = 0;
    observation->owner = startup::AudioStartupBankOwner::Fmod;
    observation->provider_outcome =
        startup::AudioStartupProviderOutcome::NaturalOpenResolvedPath;
    observation->bank_name_complete = true;
    observation->localized_observed = true;
    observation->observation_complete = true;
    Copy(observation->bank_name, sizeof(observation->bank_name), name);
    SetProvider(&observation->provider, provider_path);
}

void InitializeFixture(Fixture* fixture) noexcept
{
    std::memset(fixture, 0, sizeof(*fixture));

    fixture->sources.reported_count = 5U;
    fixture->sources.source_count = 5U;
    fixture->sources.failure =
        vfs::FilesystemSourceInventoryFailure::None;
    fixture->sources.complete = true;
    fixture->sources.registry_generation = 77U;
    fixture->sources.registry_generation_observed = true;
    SetSource(&fixture->sources.sources[0], kBaseSource, 1U,
              "X:\\Game\\DW");
    SetSource(&fixture->sources.sources[1], kDataSource, 2U,
              "X:\\Game\\DW\\Data");
    SetSource(&fixture->sources.sources[2], kSpeechSource, 3U,
              "X:\\Game\\DW\\SpeechEn");
    SetSource(&fixture->sources.sources[3], kLoaderSource, 4U,
              "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
              "3728869100\\mods\\dluma.pak");
    SetSource(&fixture->sources.sources[4], kFallbackSource, 5U,
              "filesystem:native-fallback", true);

    fixture->raw.complete_fields =
        snapshot::kAudioWindowsRequiredRawFields;
    fixture->raw.raw_snapshot_complete = true;
    fixture->raw.backend_kind =
        snapshot::AudioBaselineWindowsBackendKind::Fmod;
    fixture->raw.bank_manager = kBankManager;
    fixture->raw.speech_vector_observation_stable = true;
    fixture->raw.settings.available = true;
    fixture->raw.settings.fingerprint = 0x1111222233334444ULL;
    fixture->raw.settings.global_settings_fingerprint =
        0x5555666677778888ULL;
    Store(fixture->raw.settings.pod, 0x00U, 1U);
    Store(fixture->raw.settings.pod, 0x04U, 2U);
    Store(fixture->raw.settings.pod, 0x08U, 32U);
    Store(fixture->raw.settings.pod, 0x0CU, 64U);
    Store(fixture->raw.settings.pod, 0x10U, 44100U);
    Store(fixture->raw.settings.pod, 0x14U, 48000U);
    fixture->raw.settings.pod[0x18U] = 3U;
    fixture->raw.settings.pod[0x19U] = 4U;
    Store(fixture->raw.settings.pod, 0x1CU, 5U);
    Store(fixture->raw.settings.pod, 0x20U, 6U);
    Store(fixture->raw.settings.pod, 0x24U, 7U);
    Store(fixture->raw.settings.pod, 0x28U, 8U);
    Store(fixture->raw.settings.pod, 0x2CU, 9U);
    Store(fixture->raw.settings.pod, 0x30U, 10U);
    fixture->raw.settings.fx_volume_bits = 0x3F800000U;
    fixture->raw.settings.wave_volume_bits = 0x3F400000U;
    fixture->raw.settings.speech_volume_bits = 0x3F000000U;
    fixture->raw.settings.movie_volume_bits = 0x3E800000U;
    fixture->raw.settings.movie_volume_source_bits = 0x3E000000U;

    fixture->raw.script_count = 1U;
    fixture->raw.script_manifest_fingerprint = 0x10101010U;
    fixture->raw.scripts[0].logical_path_fingerprint = 0x20U;
    Copy(fixture->raw.scripts[0].logical_path,
         sizeof(fixture->raw.scripts[0].logical_path),
         "Data/Audio/audiobanks.scr");

    /* Raw tree order is ambient then menu.  Natural observation order below
     * is menu then ambient and must own the staged dependency order. */
    fixture->raw.bank_count = 2U;
    SetFmodMember(&fixture->raw.banks[0], "ambient", 0x9000U,
                  0xA0U);
    SetFmodMember(&fixture->raw.banks[1], "menu_main", 0x9100U,
                  0xB0U);

    const char speech_name[] = {'v', 'o', static_cast<char>(0xE9), 0};
    fixture->raw.speech_bank_count = 1U;
    fixture->raw.speech_banks[0].bank = 0xA000U;
    fixture->raw.speech_banks[0].bank_hash = EngineHash(speech_name);
    fixture->raw.speech_banks[0].descriptor_fingerprint = 0xABCDEFU;
    fixture->raw.speech_banks[0].localised = true;

    fixture->observations.revision = 17U;
    fixture->observations.coverage_started_before_audio_singletons = true;
    fixture->observations.hook_plan_complete = true;
    fixture->observations.script_count = 1U;
    startup::AudioStartupScriptObservation& script =
        fixture->observations.scripts[0];
    script.enter_sequence = 10U;
    script.exit_sequence = 11U;
    script.loader = 0x1234U;
    script.request_complete = true;
    script.parse_succeeded = true;
    Copy(script.logical_path, sizeof(script.logical_path),
         "data\\audio\\audiobanks.scr");
    SetProvider(&script.provider,
                "X:\\Game\\DW\\Data\\Audio\\audiobanks.scr",
                kDataSource);

    fixture->observations.bank_count = 4U;
    startup::AudioStartupBankObservation& speech =
        fixture->observations.banks[0];
    speech.enter_sequence = 20U;
    speech.exit_sequence = 21U;
    speech.result = 0;
    speech.owner = startup::AudioStartupBankOwner::Speech;
    speech.provider_outcome =
        startup::AudioStartupProviderOutcome::NaturalOpenResolvedPath;
    speech.bank_name_complete = true;
    speech.localized = true;
    speech.localized_observed = true;
    speech.observation_complete = true;
    Copy(speech.bank_name, sizeof(speech.bank_name), speech_name);
    SetProvider(&speech.provider,
                "X:\\Game\\DW\\SpeechEn\\dialog.bank");

    SetFmodObservation(
        &fixture->observations.banks[1], 30U, "menu_main",
        "X:\\Game\\DW\\Data\\Audio\\menu_main.bank");
    startup::AudioStartupBankObservation& cache =
        fixture->observations.banks[2];
    cache.enter_sequence = 40U;
    cache.exit_sequence = 41U;
    cache.manager = kBankManager;
    cache.result = 1;
    cache.owner = startup::AudioStartupBankOwner::Fmod;
    cache.provider_outcome =
        startup::AudioStartupProviderOutcome::CacheHitProviderMissing;
    cache.bank_name_complete = true;
    cache.localized_observed = true;
    cache.observation_complete = true;
    Copy(cache.bank_name, sizeof(cache.bank_name), "menu_main");
    SetFmodObservation(
        &fixture->observations.banks[3], 50U, "ambient",
        "X:\\Game\\DW\\Data\\Audio\\ambient.bank");
}

bool HasReason(
    const reconcile::AudioStartupBaselineStaging& output,
    reconcile::AudioStartupBaselineReconcileReason reason) noexcept
{
    return (output.reason_mask & static_cast<std::uint64_t>(reason)) != 0;
}

bool TestCompleteObservationalReconciliation() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.result == reconcile::AudioStartupBaselineReconcileResult::
                               ObservationalInventoryReconciled);
    CHECK(output.reason_mask == 0);
    CHECK(output.unresolved_fields ==
          (reconcile::kAudioStartupBaselineUnresolvedFields |
           reconcile::AudioReconcileUnresolvedExactSourceCoverage));
    CHECK(output.provider_coverage_complete);
    CHECK(!output.exact_source_coverage_complete);
    CHECK(output.fmod_localized_evidence_complete);
    CHECK(output.speech_localized_evidence_complete);
    CHECK(!output.fully_reconciled_observational_inventory);
    CHECK(output.dependency_order_complete);
    CHECK(!output.authoritative_b0_complete);
    CHECK(!output.reset_ready && !output.reset_owner_proven);
    CHECK(output.script_count == 1U);
    CHECK(output.fmod_bank_count == 2U);
    CHECK(output.speech_bank_count == 1U);
    CHECK(output.inherited_fmod_cache_hit_count == 1U);
    CHECK(output.settings.applied_sound_mode == 1U);
    CHECK(output.settings.requested_sound_mode == 2U);
    CHECK(output.settings.applied_channels == 32U);
    CHECK(output.settings.requested_channels == 64U);
    CHECK(output.settings.wave_volume_bits == 0x3F400000U);
    CHECK(output.scripts[0].load_order == 1U);
    CHECK(output.scripts[0].provider_identity.source == kDataSource);
    CHECK(output.scripts[0].provider_identity.source_record_index == 1U);
    CHECK(std::strcmp(output.scripts[0].provider,
                      "x:\\game\\dw\\data") == 0);
    CHECK(std::strcmp(output.fmod_banks[0].name, "menu_main") == 0);
    CHECK(std::strcmp(output.fmod_banks[1].name, "ambient") == 0);
    CHECK(output.fmod_banks[0].dependency_order == 1U);
    CHECK(output.fmod_banks[1].dependency_order == 2U);
    CHECK(output.fmod_banks[0].instance_generation == 0);
    CHECK(output.fmod_banks[0].reference_count == 0);
    CHECK(!output.fmod_banks[0].reference_count_observed);
    CHECK(!output.fmod_banks[0].provider_identity.exact_source);
    CHECK(output.fmod_banks[0].provider_identity.source == 0);
    CHECK(output.fmod_banks[0].provider_identity.source_record_index ==
          UINT32_MAX);
    CHECK(output.fmod_banks[0].provider_identity.source_inventory_ordinal ==
          0);
    CHECK(output.fmod_banks[0].role ==
          dluma::fixes::custom_map_asset_isolation::cold_reboot::
              BaselineAudioBankRole::Menu);
    CHECK(output.speech_banks[0].load_order == 1U);
    CHECK(output.speech_banks[0].instance_generation == 0);
    CHECK(!output.speech_banks[0].provider_identity.exact_source);
    CHECK(output.speech_banks[0].provider_identity.source == 0);
    CHECK(output.speech_banks[0].provider_identity.source_record_index ==
          UINT32_MAX);
    CHECK(output.speech_banks[0].provider_identity.source_inventory_ordinal ==
          0);
    CHECK(output.script_manifest_fingerprint ==
          fixture.raw.script_manifest_fingerprint);
    CHECK(output.script_provider_fingerprint != 0);
    return true;
}

bool TestExactSourceCoverageRequiresObservedIdentity() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    SetProvider(&fixture.observations.banks[0].provider,
                "X:\\Game\\DW\\SpeechEn\\dialog.bank",
                kSpeechSource);
    SetProvider(&fixture.observations.banks[1].provider,
                "X:\\Game\\DW\\Data\\Audio\\menu_main.bank",
                kDataSource);
    SetProvider(&fixture.observations.banks[3].provider,
                "X:\\Game\\DW\\Data\\Audio\\ambient.bank",
                kDataSource);
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.exact_source_coverage_complete);
    CHECK((output.unresolved_fields &
           reconcile::AudioReconcileUnresolvedExactSourceCoverage) == 0);
    CHECK(output.fmod_banks[0].provider_identity.exact_source);
    CHECK(output.fmod_banks[0].provider_identity.source == kDataSource);
    CHECK(output.speech_banks[0].provider_identity.exact_source);
    CHECK(output.speech_banks[0].provider_identity.source == kSpeechSource);
    CHECK(output.fully_reconciled_observational_inventory);

    /* Exact fs evidence wins over the longest physical-root inference.  A
     * nested Data root must not rewrite an observed parent-source identity. */
    InitializeFixture(&fixture);
    SetProvider(&fixture.observations.scripts[0].provider,
                "X:\\Game\\DW\\Data\\Audio\\audiobanks.scr",
                kBaseSource);
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.scripts[0].provider_identity.exact_source);
    CHECK(output.scripts[0].provider_identity.source == kBaseSource);
    CHECK(output.scripts[0].provider_identity.source_record_index == 0U);
    CHECK(output.scripts[0].provider_identity.source_inventory_ordinal == 1U);

    /* fs::open returns an exact CFSSource pointer before the startup source
     * inventory assigns its frozen record index/list ordinal. A later unique
     * pointer join may complete that identity; path-only native opens may
     * not. */
    InitializeFixture(&fixture);
    SetProvider(&fixture.observations.scripts[0].provider,
                "X:\\Game\\DW\\Data\\Audio\\audiobanks.scr",
                kDataSource);
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.scripts[0].provider_identity.exact_source);
    CHECK(output.scripts[0].provider_identity.source == kDataSource);
    CHECK(output.scripts[0].provider_identity.source_record_index == 1U);
    CHECK(output.scripts[0].provider_identity.source_inventory_ordinal == 2U);
    return true;
}

bool TestMissingLocalizedEvidenceIsExplicitNotMismatch() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.banks[0].localized = false;
    fixture.observations.banks[0].localized_observed = false;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(!output.speech_localized_evidence_complete);
    CHECK((output.unresolved_fields &
           reconcile::AudioReconcileUnresolvedSpeechLocalizedEvidence) != 0);
    CHECK(output.speech_banks[0].localized);
    CHECK(!HasReason(output,
                     reconcile::AudioReconcileReasonLocalizedMismatch));

    InitializeFixture(&fixture);
    fixture.observations.banks[1].localized = true;
    fixture.observations.banks[1].localized_observed = false;
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(!output.fmod_localized_evidence_complete);
    CHECK((output.unresolved_fields &
           reconcile::AudioReconcileUnresolvedFmodLocalizedEvidence) != 0);
    CHECK(!HasReason(output,
                     reconcile::AudioReconcileReasonLocalizedMismatch));
    return true;
}

bool TestObservationLossRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.loss.bank_overflow = 1U;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output, reconcile::AudioReconcileReasonObservationLoss));
    return true;
}

bool TestLateAndIncompleteCoverageRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.coverage_started_before_audio_singletons = false;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonLateHookCoverage));
    InitializeFixture(&fixture);
    fixture.observations.hook_plan_complete = false;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonHookPlanIncomplete));
    return true;
}

bool TestSourceInventoryAndAmbiguityRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.sources.complete = false;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonSourceInventoryIncomplete));

    InitializeFixture(&fixture);
    Copy(fixture.sources.sources[1].path,
         sizeof(fixture.sources.sources[1].path), "X:\\Game\\DW");
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonProviderPrefixAmbiguous));
    return true;
}

bool TestProviderFailuresRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.scripts[0].provider.path_complete = false;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonProviderUnknown));

    InitializeFixture(&fixture);
    fixture.observations.scripts[0].provider.source = 0xDEADU;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonProviderIdentityMismatch));
    return true;
}

bool TestSourceRegistryGenerationMismatchRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.scripts[0].provider.
        source_registry_generation =
            fixture.sources.registry_generation + 1U;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output,
        reconcile::AudioReconcileReasonSourceRegistryGenerationMismatch));
    CHECK(!output.provider_coverage_complete);
    CHECK(!output.fully_reconciled_observational_inventory);

    /* Generation zero is a valid fenced value. The explicit observed bit,
     * not numeric nonzero-ness, distinguishes it from unknown identity. */
    InitializeFixture(&fixture);
    fixture.sources.registry_generation = 0U;
    SetProvider(&fixture.observations.scripts[0].provider,
                "X:\\Game\\DW\\Data\\Audio\\audiobanks.scr",
                kDataSource, 0U);
    SetProvider(&fixture.observations.banks[0].provider,
                "X:\\Game\\DW\\SpeechEn\\dialog.bank", 0, 0U);
    SetProvider(&fixture.observations.banks[1].provider,
                "X:\\Game\\DW\\Data\\Audio\\menu_main.bank", 0, 0U);
    SetProvider(&fixture.observations.banks[3].provider,
                "X:\\Game\\DW\\Data\\Audio\\ambient.bank", 0, 0U);
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.scripts[0].provider_identity.
          source_registry_generation == 0U);
    CHECK(output.scripts[0].provider_identity.
          source_registry_generation_observed);
    return true;
}

bool TestOutputCapacityRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.raw.script_count = static_cast<std::uint32_t>(
        dluma::fixes::custom_map_asset_isolation::cold_reboot::
            kBaselineAudioScriptCapacity + 1U);
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonOutputCapacityExceeded));
    return true;
}

bool TestScriptFailuresRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.scripts[0].parse_succeeded = false;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonScriptParseFailed));

    InitializeFixture(&fixture);
    fixture.raw.script_count = 2U;
    fixture.observations.script_count = 2U;
    fixture.raw.scripts[1] = fixture.raw.scripts[0];
    fixture.observations.scripts[1] = fixture.observations.scripts[0];
    fixture.observations.scripts[1].enter_sequence = 12U;
    fixture.observations.scripts[1].exit_sequence = 13U;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonScriptDuplicate));
    return true;
}

bool TestDuplicateLogicalPathOccurrencesRequireDistinctOrder() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.script_count = 2U;
    fixture.observations.scripts[1] = fixture.observations.scripts[0];
    fixture.observations.scripts[1].enter_sequence = 12U;
    fixture.observations.scripts[1].exit_sequence = 13U;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.script_count == 2U);
    CHECK(output.scripts[0].logical_path_fingerprint ==
          output.scripts[1].logical_path_fingerprint);
    CHECK(output.scripts[0].load_order == 1U);
    CHECK(output.scripts[1].load_order == 2U);
    CHECK(output.scripts[0].load_order != output.scripts[1].load_order);
    CHECK(output.script_occurrence_manifest_fingerprint != 0U);

    InitializeFixture(&fixture);
    fixture.observations.script_count = 2U;
    fixture.observations.scripts[1] = fixture.observations.scripts[0];
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output,
        reconcile::AudioReconcileReasonDuplicateObservationEndpoint));
    CHECK(output.script_count == 0U);
    return true;
}

bool TestFmodMissingAndCacheWithoutPriorRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.banks[3].result = -1;
    fixture.observations.banks[3].provider_outcome =
        startup::AudioStartupProviderOutcome::LoadFailed;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonFmodObservationMissing));

    InitializeFixture(&fixture);
    fixture.observations.banks[1].result = -1;
    fixture.observations.banks[1].provider_outcome =
        startup::AudioStartupProviderOutcome::LoadFailed;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output,
        reconcile::AudioReconcileReasonFmodCacheHitWithoutPriorExact));
    return true;
}

bool TestExplicitCacheInheritanceMustMatchPriorExact() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    startup::AudioStartupBankObservation& cache =
        fixture.observations.banks[2];
    cache.provider_outcome =
        startup::AudioStartupProviderOutcome::CacheHitPriorExact;
    SetProvider(&cache.provider,
                "X:\\Game\\DW\\Data\\Audio\\menu_main.bank");
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.inherited_fmod_cache_hit_count == 1U);

    InitializeFixture(&fixture);
    startup::AudioStartupBankObservation& mismatch =
        fixture.observations.banks[2];
    mismatch.provider_outcome =
        startup::AudioStartupProviderOutcome::CacheHitPriorExact;
    SetProvider(&mismatch.provider,
                "X:\\Game\\DW\\Data\\Audio\\other.bank");
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonProviderIdentityMismatch));
    return true;
}

bool TestFmodIdentityFailuresRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.banks[1].manager = kBankManager + 8U;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonFmodManagerMismatch));

    InitializeFixture(&fixture);
    fixture.observations.banks[1].localized = true;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonLocalizedMismatch));

    InitializeFixture(&fixture);
    fixture.raw.banks[1].tree_key = fixture.raw.banks[0].tree_key;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonFmodDuplicate));
    return true;
}

bool TestSpeechFailuresRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    ++fixture.raw.speech_banks[0].bank_hash;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonSpeechObservationMissing));

    InitializeFixture(&fixture);
    fixture.observations.banks[0].localized = false;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonLocalizedMismatch));

    InitializeFixture(&fixture);
    fixture.raw.speech_bank_count = 2U;
    fixture.raw.speech_banks[1] = fixture.raw.speech_banks[0];
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonSpeechDuplicate));
    return true;
}

bool TestDuplicateObservationEndpointRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.banks[0].enter_sequence =
        fixture.observations.scripts[0].enter_sequence;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::
                        AudioReconcileReasonDuplicateObservationEndpoint));
    return true;
}

bool TestOverlappingIntervalsLeaveOrderingUnresolved() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    SetProvider(&fixture.observations.banks[0].provider,
                "X:\\Game\\DW\\SpeechEn\\dialog.bank",
                kSpeechSource);
    SetProvider(&fixture.observations.banks[1].provider,
                "X:\\Game\\DW\\Data\\Audio\\menu_main.bank",
                kDataSource);
    SetProvider(&fixture.observations.banks[3].provider,
                "X:\\Game\\DW\\Data\\Audio\\ambient.bank",
                kDataSource);
    /* Script parsing now overlaps the speech-bank interval without sharing
     * an endpoint. Entry order remains observable, but a complete dependency
     * order cannot be inferred from overlapping natural calls. */
    fixture.observations.scripts[0].exit_sequence = 22U;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(output.reason_mask == 0U);
    CHECK(output.exact_source_coverage_complete);
    CHECK(!output.dependency_order_complete);
    CHECK((output.unresolved_fields &
           reconcile::AudioReconcileUnresolvedDependencyOrdering) != 0U);
    CHECK(!output.fully_reconciled_observational_inventory);
    CHECK(!output.authoritative_b0_complete);
    CHECK(!output.reset_ready && !output.reset_owner_proven);
    return true;
}

bool TestMalformedAndReloadObservationsRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.observations.banks[1].reload = true;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonFmodReloadObservation));

    InitializeFixture(&fixture);
    fixture.observations.banks[0].owner =
        startup::AudioStartupBankOwner::Unknown;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonObservationLoss));
    return true;
}

bool TestRawAndBackendFailuresRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.raw.raw_snapshot_complete = false;
    reconcile::AudioStartupBaselineStaging output = {};
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(
        output, reconcile::AudioReconcileReasonRawSnapshotIncomplete));

    InitializeFixture(&fixture);
    fixture.raw.backend_kind =
        snapshot::AudioBaselineWindowsBackendKind::NoSound;
    CHECK(!reconcile::ReconcileAudioStartupBaseline(
        &fixture.raw, &fixture.observations, &fixture.sources, &output));
    CHECK(HasReason(output,
                    reconcile::AudioReconcileReasonBackendUnsupported));
    return true;
}

using TestFn = bool (*)() noexcept;
struct TestCase { const char* name; TestFn function; };

}  // namespace

int main()
{
    const TestCase tests[] = {
        {"complete_observational_reconciliation",
         TestCompleteObservationalReconciliation},
        {"exact_source_coverage",
         TestExactSourceCoverageRequiresObservedIdentity},
        {"missing_localized_evidence",
         TestMissingLocalizedEvidenceIsExplicitNotMismatch},
        {"observation_loss", TestObservationLossRejected},
        {"coverage", TestLateAndIncompleteCoverageRejected},
        {"source_inventory", TestSourceInventoryAndAmbiguityRejected},
        {"provider_failures", TestProviderFailuresRejected},
        {"source_registry_generation_mismatch",
         TestSourceRegistryGenerationMismatchRejected},
        {"capacity", TestOutputCapacityRejected},
        {"script_failures", TestScriptFailuresRejected},
        {"duplicate_script_occurrence_order",
         TestDuplicateLogicalPathOccurrencesRequireDistinctOrder},
        {"fmod_missing_cache", TestFmodMissingAndCacheWithoutPriorRejected},
        {"cache_provider_inheritance",
         TestExplicitCacheInheritanceMustMatchPriorExact},
        {"fmod_identity", TestFmodIdentityFailuresRejected},
        {"speech_failures", TestSpeechFailuresRejected},
        {"duplicate_observation_endpoint",
         TestDuplicateObservationEndpointRejected},
        {"overlapping_intervals",
         TestOverlappingIntervalsLeaveOrderingUnresolved},
        {"malformed_reload", TestMalformedAndReloadObservationsRejected},
        {"raw_backend", TestRawAndBackendFailuresRejected},
    };
    constexpr std::size_t count = sizeof(tests) / sizeof(tests[0]);
    for (std::size_t index = 0; index < count; ++index) {
        if (!tests[index].function()) {
            std::fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return 1;
        }
        std::printf("[PASS] %s\n", tests[index].name);
    }
    std::printf("audio_startup_baseline_reconciler_fake_harness: %zu/%zu ok\n",
                count, count);
    return 0;
}
