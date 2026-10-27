#define _HAS_EXCEPTIONS 0

extern "C" {
#include "../src/dlw_common.h"
}
#include "../src/fixes/custom_map_asset_isolation/PersistentConsumerLedger.h"

#include <cstdio>
#include <cstring>

using namespace dluma::fixes::custom_map_asset_isolation::persistent;
using dluma::fixes::custom_map_asset_isolation::content::CustomSourceKind;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

PersistentConsumerObservation MakeObservation(
    PersistentConsumerDomain domain, std::uint32_t epoch,
    std::uint64_t sequence, const char* path) noexcept
{
    PersistentConsumerObservation observation = {};
    observation.domain = domain;
    observation.epoch = epoch;
    observation.sequence = sequence;
    observation.source_record_index = epoch ? epoch : UINT32_MAX;
    observation.source_kind = epoch ? CustomSourceKind::DataDirectory
                                    : CustomSourceKind::Other;
    observation.consumer_rva = 0x45ADA0U;
    observation.consumer = 0x1000U;
    observation.object_generation = sequence;
    observation.observation_complete = true;
    std::strncpy(observation.logical_path, path,
                 sizeof(observation.logical_path) - 1U);
    return observation;
}

bool CaseIdentityKeepsSubjectsAndConsumersDistinct()
{
    static PersistentConsumerLedger ledger;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    PersistentConsumerObservation first = MakeObservation(
        PersistentConsumerDomain::EffectiveFov, 1, 1,
        "data\\skills\\default_levels.xml");
    first.subject_id = 779;
    first.consumer = 0x1000U;
    PersistentConsumerObservation second = first;
    second.sequence = 2;
    second.subject_id = 780;
    PersistentConsumerObservation third = first;
    third.sequence = 3;
    third.consumer = 0x2000U;
    CHECK(Record(&ledger, first));
    CHECK(Record(&ledger, second));
    CHECK(Record(&ledger, third));
    CHECK(ledger.count == 3);
    first.sequence = 4;
    CHECK(Record(&ledger, first));
    CHECK(ledger.count == 3);
    return true;
}

bool CaseDomainLossIsBoundedAndIndependent()
{
    static PersistentConsumerLedger ledger;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    for (std::uint32_t index = 0;
         index < kPersistentConsumerCapacity; ++index) {
        PersistentConsumerObservation item = MakeObservation(
            PersistentConsumerDomain::AssetsPcFile, index + 1U,
            index + 1U, "assets\\watched.texture");
        item.consumer = index + 0x10000U;
        CHECK(Record(&ledger, item));
    }
    PersistentConsumerObservation overflow = MakeObservation(
        PersistentConsumerDomain::AudioBank, 90, 999,
        "fixture_bank");
    CHECK(!Record(&ledger, overflow));
    CHECK(ledger.count == kPersistentConsumerCapacity);
    CHECK(ledger.losses[static_cast<std::size_t>(
              PersistentConsumerDomain::AudioBank)].count == 1);
    CHECK(ledger.losses[static_cast<std::size_t>(
              PersistentConsumerDomain::AssetsPcFile)].count == 0);
    return true;
}

void BuildDefaultTimeline(
    std::uint64_t restored_hash, std::uint32_t restored_fov_bits,
    bool include_restore, PersistentConsumerSnapshot* snapshot)
{
    static PersistentConsumerLedger ledger;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    PersistentConsumerObservation base = MakeObservation(
        PersistentConsumerDomain::DefaultLevels, 0, 1,
        "default_levels.xml");
    base.definition_hash_after = 100;
    base.subject_id = 0;
    (void)Record(&ledger, base);
    PersistentConsumerObservation base_fov = MakeObservation(
        PersistentConsumerDomain::EffectiveFov, 0, 2,
        "default_levels.xml");
    base_fov.subject_id = 779;
    base_fov.value1 = 0x42A00000U;
    (void)Record(&ledger, base_fov);
    PersistentConsumerObservation custom = MakeObservation(
        PersistentConsumerDomain::DefaultLevels, 7, 3,
        "default_levels.xml");
    custom.definition_hash_after = 200;
    custom.subject_id = 0;
    (void)Record(&ledger, custom);
    if (include_restore) {
        PersistentConsumerObservation restored = MakeObservation(
            PersistentConsumerDomain::DefaultLevels, 0, 4,
            "default_levels.xml");
        restored.definition_hash_after = restored_hash;
        restored.subject_id = 0;
        (void)Record(&ledger, restored);
        PersistentConsumerObservation restored_fov = MakeObservation(
            PersistentConsumerDomain::EffectiveFov, 0, 5,
            "default_levels.xml");
        restored_fov.subject_id = 779;
        restored_fov.value1 = restored_fov_bits;
        (void)Record(&ledger, restored_fov);
    }
    std::memset(snapshot, 0, sizeof(*snapshot));
    (void)CaptureSnapshot(&ledger, snapshot);
}

bool CaseDefaultOutcomeDecisionTable()
{
    static PersistentConsumerSnapshot snapshot;
    BuildDefaultTimeline(0, 0, false, &snapshot);
    CHECK(ClassifyDefaultLevelsOutcome(snapshot, true) ==
          DefaultLevelsOutcome::ProviderRestoredButManagerNotReloaded);
    BuildDefaultTimeline(100, 0x42A00000U, true, &snapshot);
    CHECK(ClassifyDefaultLevelsOutcome(snapshot, true) ==
          DefaultLevelsOutcome::NaturallyRestored);
    BuildDefaultTimeline(100, 0x42B40000U, true, &snapshot);
    CHECK(ClassifyDefaultLevelsOutcome(snapshot, true) ==
          DefaultLevelsOutcome::DefinitionRestoredLiveValuePersisted);
    BuildDefaultTimeline(300, 0x42A00000U, true, &snapshot);
    CHECK(ClassifyDefaultLevelsOutcome(snapshot, true) ==
          DefaultLevelsOutcome::DefinitionPersisted);
    return true;
}

bool CaseLatestLookupIncludesBaseProvider()
{
    static PersistentConsumerLedger ledger;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    PersistentConsumerObservation base = MakeObservation(
        PersistentConsumerDomain::DefaultLevels, 0, 10,
        "default_levels.xml");
    base.exact_source = false;
    CHECK(Record(&ledger, base));
    PersistentConsumerObservation found = {};
    CHECK(FindLatestAny(&ledger,
                        PersistentConsumerDomain::DefaultLevels,
                        &found));
    CHECK(found.epoch == 0);
    CHECK(found.sequence == 10);
    return true;
}

void CopyText(char* output, std::size_t capacity,
              const char* input) noexcept
{
    if (!output || !capacity) return;
    std::strncpy(output, input ? input : "", capacity - 1U);
    output[capacity - 1U] = 0;
}

bool CaseStagedAssetsStateMachine()
{
    static PersistentConsumerLedger ledger;
    static PersistentConsumerSnapshot snapshot;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    StagedAssetsSourceObservation a = {};
    a.sequence = 1;
    a.epoch = 1;
    a.current_epoch = 1;
    a.source_record_index = 3;
    a.source_add_ordinal = 7;
    a.source = 0xA000;
    a.state = StagedAssetsSourceState::ActiveCurrent;
    a.exact_source = true;
    a.source_active = true;
    CopyText(a.source_path, sizeof(a.source_path), "a\\assets_pc");
    CHECK(RecordStagedAssetsSource(&ledger, a));
    CHECK(MarkStagedAssetsEpochCurrent(&ledger, 2, 2));
    StagedAssetsSourceObservation b = a;
    b.sequence = 3;
    b.epoch = 2;
    b.current_epoch = 2;
    b.source_record_index = 8;
    b.source_add_ordinal = 12;
    b.source = 0xB000;
    CopyText(b.source_path, sizeof(b.source_path), "b\\assets_pc");
    CHECK(RecordStagedAssetsSource(&ledger, b));
    CHECK(MarkStagedAssetsEpochCurrent(&ledger, 2, 4));
    CHECK(MarkStagedAssetsSourceRemoved(&ledger, 0xA000, 2, 5));
    std::memset(&snapshot, 0, sizeof(snapshot));
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.staged_assets_source_count == 2);
    CHECK(snapshot.staged_assets_sources[0].state ==
          StagedAssetsSourceState::Removed);
    CHECK(snapshot.staged_assets_sources[1].state ==
          StagedAssetsSourceState::ActiveCurrent);
    return true;
}

bool CasePostSupersessionCapacityIsIndependent()
{
    static PersistentConsumerLedger ledger;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    PostSupersessionAssetsOpen item = {};
    item.old_epoch = 1;
    item.current_epoch = 2;
    item.exact_source = true;
    for (std::uint32_t index = 0;
         index < kPostSupersessionOpenCapacity; ++index) {
        item.sequence = index + 1U;
        item.file_handle = 0x10000U + index;
        CHECK(RecordPostSupersessionOpen(&ledger, item));
    }
    item.sequence = 999;
    CHECK(!RecordPostSupersessionOpen(&ledger, item));
    CHECK(ledger.staged_assets_loss.count == 1);
    CHECK(ledger.material_loss.count == 0);
    CHECK(ledger.player_progression_loss.count == 0);
    return true;
}

bool CaseMaterialNaturalReplacementAndSupersededFallback()
{
    static PersistentConsumerLedger ledger;
    static PersistentConsumerSnapshot snapshot;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    MaterialReloadObservation reload = {};
    reload.sequence = 10;
    reload.observation_generation = 1;
    reload.staged_content_epoch = 2;
    reload.old_local_pack = 0xA100;
    reload.new_local_pack = 0xB100;
    reload.manager_pointer_swapped = true;
    reload.old_pack_destructor_entered = true;
    reload.old_pack_destructor_completed = true;
    reload.scope_complete = true;
    CHECK(RecordMaterialReload(&ledger, reload));
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(ClassifyMaterialOutcome(snapshot, true) ==
          MaterialObservationOutcome::NaturallyReplaced);
    PostSupersessionAssetsOpen fallback = {};
    fallback.sequence = 11;
    fallback.old_epoch = 1;
    fallback.current_epoch = 2;
    fallback.exact_source = true;
    CHECK(RecordPostSupersessionOpen(&ledger, fallback));
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(ClassifyMaterialOutcome(snapshot, true) ==
          MaterialObservationOutcome::
              SupersededSourceServedCurrentProject);
    return true;
}

bool CaseMaterialPointerGenerationIsPartOfIdentity()
{
    static PersistentConsumerLedger ledger;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    MaterialResourceIdentityObservation identity = {};
    identity.sequence = 1;
    identity.logical_key = 44;
    identity.object = 0xDEAD;
    identity.registration_generation = 10;
    identity.action = MaterialIdentityAction::Constructed;
    CHECK(RecordMaterialIdentity(&ledger, identity));
    identity.sequence = 2;
    identity.registration_generation = 11;
    CHECK(RecordMaterialIdentity(&ledger, identity));
    CHECK(ledger.material_identity_count == 2);
    identity.sequence = 3;
    CHECK(RecordMaterialIdentity(&ledger, identity));
    CHECK(ledger.material_identity_count == 2);
    return true;
}

bool CaseProgressionOverlayOrderAndMultipleStates()
{
    static PersistentConsumerLedger ledger;
    static PersistentConsumerSnapshot snapshot;
    std::memset(&ledger, 0, sizeof(ledger));
    Initialize(&ledger);
    ProgressionOverlayObservation base = {};
    base.sequence = 1;
    base.template_generation = 1;
    base.stage = ProgressionOverlayStage::Base;
    base.definition_hash_before = 0;
    base.definition_hash_after = 100;
    base.definition_count_before = 0;
    base.definition_count_after = 2;
    base.observation_complete = true;
    CHECK(RecordProgressionOverlay(&ledger, base));
    ProgressionOverlayObservation progression = base;
    progression.sequence = 2;
    progression.epoch = 7;
    progression.stage = ProgressionOverlayStage::Progression;
    progression.definition_hash_before = 100;
    progression.definition_hash_after = 200;
    progression.definition_count_before = 2;
    progression.definition_count_after = 2;  // replacement by property ID
    CHECK(RecordProgressionOverlay(&ledger, progression));
    ProgressionOverlayObservation class_overlay = progression;
    class_overlay.sequence = 3;
    class_overlay.stage = ProgressionOverlayStage::ClassProgression;
    class_overlay.definition_hash_before = 200;
    class_overlay.definition_hash_after = 300;
    class_overlay.definition_count_before = 2;
    class_overlay.definition_count_after = 3;  // custom-only append
    CHECK(RecordProgressionOverlay(&ledger, class_overlay));
    PlayerStateSnapshot state_a = {};
    state_a.sequence = 4;
    state_a.template_generation = 1;
    state_a.player_state = 0x1000;
    state_a.observation_complete = true;
    CHECK(RecordPlayerStateSnapshot(&ledger, state_a));
    PlayerStateSnapshot state_b = state_a;
    state_b.sequence = 5;
    state_b.player_state = 0x2000;
    CHECK(RecordPlayerStateSnapshot(&ledger, state_b));
    CHECK(ledger.player_state_snapshot_count == 2);
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.progression_overlays[0].stage ==
          ProgressionOverlayStage::Base);
    CHECK(snapshot.progression_overlays[1].stage ==
          ProgressionOverlayStage::Progression);
    CHECK(snapshot.progression_overlays[2].stage ==
          ProgressionOverlayStage::ClassProgression);
    CHECK(snapshot.progression_overlays[1].definition_count_after == 2);
    CHECK(snapshot.progression_overlays[2].definition_count_after == 3);
    return true;
}

bool CasePlayerProgressionDecisionTable()
{
    static PersistentConsumerLedger ledger;
    static PersistentConsumerSnapshot snapshot;
    auto record_timeline = [&](std::uint64_t restored_hash,
                               bool include_restore,
                               bool live_available) {
        std::memset(&ledger, 0, sizeof(ledger));
        Initialize(&ledger);
        ProgressionOverlayObservation base = {};
        base.sequence = 1;
        base.stage = ProgressionOverlayStage::Base;
        base.definition_hash_after = 100;
        base.observation_complete = true;
        (void)RecordProgressionOverlay(&ledger, base);
        ProgressionOverlayObservation custom = base;
        custom.sequence = 2;
        custom.epoch = 5;
        custom.stage = ProgressionOverlayStage::Progression;
        custom.definition_hash_after = 200;
        (void)RecordProgressionOverlay(&ledger, custom);
        if (include_restore) {
            ProgressionOverlayObservation restored = base;
            restored.sequence = 3;
            restored.definition_hash_after = restored_hash;
            (void)RecordProgressionOverlay(&ledger, restored);
            PlayerStateSnapshot state = {};
            state.sequence = 4;
            state.template_generation = 2;
            state.player_state = 0x3000;
            state.effective_fov_available = live_available;
            state.observation_complete = true;
            (void)RecordPlayerStateSnapshot(&ledger, state);
        }
        std::memset(&snapshot, 0, sizeof(snapshot));
        (void)CaptureSnapshot(&ledger, &snapshot);
    };
    record_timeline(0, false, false);
    CHECK(ClassifyPlayerProgressionOutcome(snapshot, true, true) ==
          PlayerProgressionOutcome::ProviderRestoredManagerStale);
    record_timeline(300, true, true);
    CHECK(ClassifyPlayerProgressionOutcome(snapshot, true, true) ==
          PlayerProgressionOutcome::DefinitionPersisted);
    record_timeline(100, true, false);
    CHECK(ClassifyPlayerProgressionOutcome(snapshot, true, true) ==
          PlayerProgressionOutcome::ObservationIncomplete);
    record_timeline(100, true, true);
    CHECK(ClassifyPlayerProgressionOutcome(snapshot, true, true) ==
          PlayerProgressionOutcome::NaturallyRestored);
    return true;
}

struct Case {
    const char* name;
    bool (*run)();
};

}  // namespace

int main()
{
    static const Case cases[] = {
        {"identity_keeps_subjects_and_consumers_distinct",
         &CaseIdentityKeepsSubjectsAndConsumersDistinct},
        {"domain_loss_is_bounded_and_independent",
         &CaseDomainLossIsBoundedAndIndependent},
        {"default_outcome_decision_table",
         &CaseDefaultOutcomeDecisionTable},
        {"latest_lookup_includes_base_provider",
         &CaseLatestLookupIncludesBaseProvider},
        {"staged_assets_state_machine", &CaseStagedAssetsStateMachine},
        {"post_supersession_capacity_is_independent",
         &CasePostSupersessionCapacityIsIndependent},
        {"material_natural_replacement_and_superseded_fallback",
         &CaseMaterialNaturalReplacementAndSupersededFallback},
        {"material_pointer_generation_is_part_of_identity",
         &CaseMaterialPointerGenerationIsPartOfIdentity},
        {"progression_overlay_order_and_multiple_states",
         &CaseProgressionOverlayOrderAndMultipleStates},
        {"player_progression_decision_table",
         &CasePlayerProgressionDecisionTable},
    };
    for (const Case& test : cases) {
        if (!test.run()) {
            std::printf("[FAIL] %s\n", test.name);
            return 1;
        }
        std::printf("[PASS] %s\n", test.name);
    }
    std::printf("persistent consumer ledger cases passed: %zu\n",
                sizeof(cases) / sizeof(cases[0]));
    return 0;
}
