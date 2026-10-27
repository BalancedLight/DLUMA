#define _HAS_EXCEPTIONS 0

extern "C" {
#include "../src/dlw_common.h"
}
#include "../src/fixes/custom_map_asset_isolation/PlayerStateObservationLedger.h"

#include <cstdio>
#include <cstring>

using namespace dluma::fixes::custom_map_asset_isolation::playerstate;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

StateApplication MakeApplication(std::uint64_t sequence,
                                 std::uint64_t state_generation,
                                 std::uint32_t epoch,
                                 bool provider_open) noexcept
{
    StateApplication item = {};
    item.sequence = sequence;
    item.state_generation = state_generation;
    item.template_generation = sequence;
    item.cache_generation = 3;
    item.content_epoch = epoch;
    item.state = 0x1000;
    item.player_variables = 0x2000;
    item.rows.player_variables = 0x2000;
    item.rows.row_table_readable = true;
    item.rows.player_variables_readable = true;
    item.rows.all_row_pointers_readable = true;
    item.rows.all_vector_headers_readable = true;
    item.rows.all_bounded_partial_fingerprints_available = true;
    item.rows.deep_definition_complete = true;
    item.rows.combined_fingerprint = 0xC0FFEE;
    std::strcpy(item.progression_selector, "default_levels.xml");
    std::strcpy(item.class_selector, "");
    for (std::size_t index = 0; index < kRowCount; ++index) {
        item.rows.rows[index].definition_vector = 0x3000 + index * 0x100;
        item.rows.rows[index].fingerprint = 0x4000 + index;
        item.rows.rows[index].definition_count = index ? 20 : 385;
        item.rows.rows[index].row_pointer_readable = true;
        item.rows.rows[index].vector_header_readable = true;
        item.rows.rows[index].bounded_partial_fingerprint_available = true;
        item.rows.rows[index].deep_definition_complete = true;
    }
    item.provider.request_observed = provider_open;
    item.provider.open_succeeded = provider_open;
    item.provider.exact_source = provider_open;
    item.provider.source = provider_open ? 0x9000 : 0;
    item.provider.content_epoch = provider_open ? epoch : 0;
    item.provider.source_record_index = provider_open ? 4 : UINT32_MAX;
    item.provider.successful_add_ordinal = provider_open ? 7 : 0;
    if (provider_open) {
        std::strcpy(item.provider.logical_path, "default_levels.xml");
        std::strcpy(item.provider.provider,
                    "X:\\workshop\\first_project run\\data");
    }
    return item;
}

bool CasePointerReuseCreatesNewGeneration()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t first = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::PlayerDi, 0x5000, 1);
    CHECK(first == 1);
    std::uint64_t destroyed = 0;
    CHECK(ObserveDestroyedState(&ledger, 0x1000, 0x5000, 2, &destroyed));
    CHECK(destroyed == first);
    const std::uint64_t second = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::PlayerDi, 0x6000, 3);
    CHECK(second == 2);
    CHECK(second != first);
    return true;
}

bool CaseGlobalOwnerPromotesUnknownObservation()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t generation = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::OtherRttiObserved, 0, 1);
    CHECK(ObserveLiveState(&ledger, 0x1000,
                           OwnerKind::LocalProcessGlobal, 0, 2) == generation);
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.owner_count == 1);
    CHECK(snapshot.owners[0].owner_kind == OwnerKind::LocalProcessGlobal);
    return true;
}

bool CaseFirstProjectToSampleProjectProvesSkippedRebuild()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t generation = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::LocalProcessGlobal, 0, 1);
    StateApplication first_project = MakeApplication(10, generation, 1, true);
    first_project.selectors_changed = true;
    first_project.clear_observed = true;
    first_project.initialize_observed = true;
    CHECK(RecordApplication(&ledger, &first_project));
    StateApplication sample_project = MakeApplication(20, generation, 2, false);
    CHECK(RecordApplication(&ledger, &sample_project));
    CHECK(sample_project.same_selector_provider_changed);
    CHECK(sample_project.provider_change_rebuild_skipped);
    CHECK(sample_project.stale_progression_definition_retained);
    CHECK(!sample_project.provider.request_observed);
    CHECK(sample_project.rows.rows[0].definition_count == 385);
    return true;
}

bool CasePartialFingerprintProvesRetentionWithoutDeepWalk()
{
    DefinitionRowSnapshot before = {};
    before.definition_vector = 0xA080BA0;
    before.definition_count = 385;
    before.fingerprint = 0xC0EE5U;
    before.row_pointer_readable = true;
    before.vector_header_readable = true;
    before.bounded_partial_fingerprint_available = true;
    before.deep_definition_complete = false;
    DefinitionRowSnapshot after = before;
    CHECK(ClassifyRowRetentionWithoutDeepSnapshot(before, after));
    CHECK(!before.deep_definition_complete);
    return true;
}

bool CaseUnreadableRowRemainsUnknown()
{
    DefinitionRowSnapshot before = {};
    before.definition_vector = 0x3000;
    before.definition_count = 20;
    before.fingerprint = 0x44;
    before.vector_header_readable = true;
    before.bounded_partial_fingerprint_available = true;
    DefinitionRowSnapshot after = before;
    CHECK(!ClassifyRowRetentionWithoutDeepSnapshot(before, after));
    return true;
}

bool CasePartialFingerprintMismatchIsDetected()
{
    DefinitionRowSnapshot before = {};
    before.definition_vector = 0x3000;
    before.definition_count = 20;
    before.fingerprint = 0x44;
    before.row_pointer_readable = true;
    before.vector_header_readable = true;
    before.bounded_partial_fingerprint_available = true;
    before.deep_definition_complete = true;
    DefinitionRowSnapshot after = before;
    after.fingerprint = 0x45;
    CHECK(!ClassifyRowRetentionWithoutDeepSnapshot(before, after));
    return true;
}

bool CaseFirstProjectFixtureSurvivesIncompleteDeepWalk()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t generation = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::LocalProcessGlobal, 0, 1);
    StateApplication first_project = MakeApplication(10, generation, 1, true);
    StateApplication sample_project = MakeApplication(20, generation, 2, false);
    first_project.rows.deep_definition_complete = false;
    sample_project.rows.deep_definition_complete = false;
    for (std::size_t index = 0; index < kRowCount; ++index) {
        first_project.rows.rows[index].deep_definition_complete = false;
        sample_project.rows.rows[index].deep_definition_complete = false;
    }
    CHECK(RecordApplication(&ledger, &first_project));
    CHECK(RecordApplication(&ledger, &sample_project));
    CHECK(sample_project.same_selector_provider_changed);
    CHECK(sample_project.provider_change_rebuild_skipped);
    CHECK(sample_project.stale_progression_definition_retained);
    return true;
}

bool CaseChangedSelectorIsNotProviderSkip()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t generation = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::LocalProcessGlobal, 0, 1);
    StateApplication first = MakeApplication(10, generation, 1, true);
    CHECK(RecordApplication(&ledger, &first));
    StateApplication second = MakeApplication(20, generation, 2, false);
    std::strcpy(second.progression_selector, "irisu_player.xml");
    second.selectors_changed = true;
    CHECK(RecordApplication(&ledger, &second));
    CHECK(!second.same_selector_provider_changed);
    CHECK(!second.provider_change_rebuild_skipped);
    CHECK(!second.stale_progression_definition_retained);
    return true;
}

bool CaseNaturalCacheGenerationsAndMembership()
{
    Ledger ledger = {};
    Initialize(&ledger);
    CHECK(CurrentCacheGeneration(&ledger) == 1);
    CHECK(AdvanceCacheGeneration(&ledger, CacheMutationKind::Append, 1) == 2);
    CHECK(AdvanceCacheGeneration(&ledger,
                                 CacheMutationKind::RecordRebuild, 2) == 3);
    CHECK(AdvanceCacheGeneration(&ledger,
                                 CacheMutationKind::WholeCacheReset, 3) == 4);
    CacheMembershipRecord membership = {};
    membership.sequence = 4;
    membership.state_generation = 1;
    membership.old_cache_generation = 3;
    membership.new_cache_generation = 4;
    membership.before_pointer = 0x3000;
    membership.after_pointer = 0x4000;
    membership.disposition = MembershipDisposition::Rebound;
    CHECK(RecordMembership(&ledger, membership));
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.membership_count == 1);
    CHECK(snapshot.memberships[0].disposition ==
          MembershipDisposition::Rebound);
    CHECK(snapshot.cache_append_count == 1);
    CHECK(snapshot.cache_rebuild_count == 1);
    CHECK(snapshot.cache_reset_count == 1);
    return true;
}

bool CasePostSelectionClearRetainsStaleType0()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t generation = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::LocalProcessGlobal, 0x9000, 1);
    StateApplication first_project = MakeApplication(10, generation, 1, true);
    first_project.cache_generation = CurrentCacheGeneration(&ledger);
    CHECK(RecordApplication(&ledger, &first_project));
    StateApplication sample_project = MakeApplication(20, generation, 2, false);
    sample_project.cache_generation = CurrentCacheGeneration(&ledger);
    sample_project.rows.rows[1].definition_vector = 0xDEAD;
    CHECK(RecordApplication(&ledger, &sample_project));
    CHECK(sample_project.same_selector_provider_changed);
    CHECK(!sample_project.provider_change_rebuild_skipped);
    NaturalClearObservation clear = {};
    clear.enter_sequence = 21;
    clear.exit_sequence = 22;
    clear.state_generation = generation;
    clear.content_epoch = 2;
    clear.state = 0x1000;
    clear.before = sample_project.rows;
    clear.after = sample_project.rows;
    CHECK(RecordNaturalClear(&ledger, &clear));
    CHECK(clear.selector_equal_provider_change_correlated);
    CHECK(clear.type0_pointer_retained);
    CHECK(clear.type0_content_retained);
    CHECK(clear.rebuild_skipped_after_clear);
    CHECK(clear.stale_type0_retained_after_clear);
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.natural_clear_count == 1);
    CHECK(snapshot.applications[1].post_selection_clear_observed);
    CHECK(snapshot.applications[1].post_selection_clear_type0_retained);
    CHECK(snapshot.applications[1].provider_change_rebuild_skipped);
    CHECK(snapshot.applications[1].stale_progression_definition_retained);
    return true;
}

bool CasePostSelectionClearChangesType0()
{
    Ledger ledger = {};
    Initialize(&ledger);
    const std::uint64_t generation = ObserveLiveState(
        &ledger, 0x1000, OwnerKind::LocalProcessGlobal, 0x9000, 1);
    StateApplication first = MakeApplication(10, generation, 1, true);
    first.cache_generation = CurrentCacheGeneration(&ledger);
    CHECK(RecordApplication(&ledger, &first));
    StateApplication second = MakeApplication(20, generation, 2, false);
    second.cache_generation = CurrentCacheGeneration(&ledger);
    second.rows.rows[1].definition_vector = 0xDEAD;
    CHECK(RecordApplication(&ledger, &second));
    NaturalClearObservation clear = {};
    clear.enter_sequence = 21;
    clear.exit_sequence = 22;
    clear.state_generation = generation;
    clear.content_epoch = 2;
    clear.state = 0x1000;
    clear.before = second.rows;
    clear.after = second.rows;
    clear.after.rows[0].definition_vector = 0xBEEF;
    clear.after.rows[0].fingerprint = 0xBAD;
    CHECK(RecordNaturalClear(&ledger, &clear));
    CHECK(!clear.type0_pointer_retained);
    CHECK(!clear.type0_content_retained);
    CHECK(!clear.rebuild_skipped_after_clear);
    CHECK(!clear.stale_type0_retained_after_clear);
    return true;
}

bool CaseNaturalClearLedgerIsBounded()
{
    Ledger ledger = {};
    Initialize(&ledger);
    for (std::uint32_t index = 0; index < kNaturalClearCapacity; ++index) {
        NaturalClearObservation clear = {};
        clear.enter_sequence = index * 2 + 1;
        clear.exit_sequence = index * 2 + 2;
        CHECK(RecordNaturalClear(&ledger, &clear));
    }
    NaturalClearObservation overflow = {};
    CHECK(!RecordNaturalClear(&ledger, &overflow));
    CHECK(ledger.natural_clear_count == kNaturalClearCapacity);
    CHECK(ledger.loss.natural_clear_overflow == 1);
    return true;
}

bool CaseLedgerIsBoundedAndNonBlocking()
{
    Ledger ledger = {};
    Initialize(&ledger);
    for (std::uint32_t index = 0; index < kOwnerCapacity; ++index)
        CHECK(ObserveLiveState(&ledger, 0x1000 + index,
                               OwnerKind::OtherRttiObserved, 0,
                               index + 1) != 0);
    CHECK(ObserveLiveState(&ledger, 0x9000,
                           OwnerKind::OtherRttiObserved, 0, 1000) == 0);
    CHECK(ledger.owner_count == kOwnerCapacity);
    CHECK(ledger.loss.owner_overflow == 1);
    ledger.lock = 1;
    CHECK(FindLiveGeneration(&ledger, 0x1000) == 0);
    CHECK(ledger.loss.lock_contention == 1);
    ledger.lock = 0;
    return true;
}

bool CaseEveryDroppedMutationInvalidatesFrozenRevision()
{
    Ledger ledger = {};
    Snapshot frozen = {};
    Initialize(&ledger);

    ledger.owner_count = static_cast<std::uint32_t>(kOwnerCapacity);
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(ObserveLiveState(&ledger, 0x9000,
                           OwnerKind::OtherRttiObserved, 0, 1) == 0);
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    ledger.definition_vector_count =
        static_cast<std::uint32_t>(kDefinitionVectorCapacity);
    DefinitionVectorRecord vector = {};
    vector.pointer = 0xA000;
    vector.cache_generation = 1;
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(!RecordDefinitionVector(&ledger, vector));
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    ledger.application_count =
        static_cast<std::uint32_t>(kApplicationCapacity);
    StateApplication application = {};
    application.state = 0xB000;
    application.state_generation = 1;
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(!RecordApplication(&ledger, &application));
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    ledger.membership_count =
        static_cast<std::uint32_t>(kMembershipCapacity);
    CacheMembershipRecord membership = {};
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(!RecordMembership(&ledger, membership));
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    ledger.side_effect_count =
        static_cast<std::uint32_t>(kSideEffectCapacity);
    SideEffectObservation side_effect = {};
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(!RecordSideEffect(&ledger, side_effect));
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    ledger.appearance_count =
        static_cast<std::uint32_t>(kAppearanceCapacity);
    AppearanceObservation appearance = {};
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(!RecordAppearance(&ledger, appearance));
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    ledger.natural_clear_count =
        static_cast<std::uint32_t>(kNaturalClearCapacity);
    NaturalClearObservation natural_clear = {};
    CHECK(CaptureSnapshot(&ledger, &frozen));
    CHECK(!RecordNaturalClear(&ledger, &natural_clear));
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    CHECK(CaptureSnapshot(&ledger, &frozen));
    ledger.lock = 1;
    CHECK(!RecordMembership(&ledger, membership));
    ledger.lock = 0;
    CHECK(CurrentRevision(&ledger) == frozen.revision + 1);

    CHECK(ledger.loss.owner_overflow == 1);
    CHECK(ledger.loss.vector_overflow == 1);
    CHECK(ledger.loss.application_overflow == 1);
    CHECK(ledger.loss.membership_overflow == 1);
    CHECK(ledger.loss.side_effect_overflow == 1);
    CHECK(ledger.loss.appearance_overflow == 1);
    CHECK(ledger.loss.natural_clear_overflow == 1);
    CHECK(ledger.loss.lock_contention == 1);
    return true;
}

bool CaseAppearanceProviderChangeIsObserved()
{
    Ledger ledger = {};
    Initialize(&ledger);
    AppearanceObservation first = {};
    first.sequence = 1;
    first.state_generation = 1;
    first.provider.open_succeeded = true;
    first.provider.source = 0x100;
    std::strcpy(first.provider.provider, "first_project\\data");
    CHECK(RecordAppearance(&ledger, first));
    AppearanceObservation second = first;
    second.sequence = 2;
    second.provider.source = 0x200;
    std::strcpy(second.provider.provider, "base\\data");
    CHECK(RecordAppearance(&ledger, second));
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.appearance_count == 2);
    CHECK(snapshot.appearances[1].provider_changed);
    return true;
}

}  // namespace

int main()
{
    struct TestCase { const char* name; bool (*run)(); };
    const TestCase tests[] = {
        {"pointer reuse generation", CasePointerReuseCreatesNewGeneration},
        {"global owner promotion", CaseGlobalOwnerPromotesUnknownObservation},
        {"first_project to sample_project", CaseFirstProjectToSampleProjectProvesSkippedRebuild},
        {"partial fingerprint retention",
         CasePartialFingerprintProvesRetentionWithoutDeepWalk},
        {"unreadable row unknown", CaseUnreadableRowRemainsUnknown},
        {"partial fingerprint mismatch",
         CasePartialFingerprintMismatchIsDetected},
        {"first_project incomplete deep fixture",
         CaseFirstProjectFixtureSurvivesIncompleteDeepWalk},
        {"changed selector", CaseChangedSelectorIsNotProviderSkip},
        {"cache generations", CaseNaturalCacheGenerationsAndMembership},
        {"post-clear stale type0", CasePostSelectionClearRetainsStaleType0},
        {"post-clear changed type0", CasePostSelectionClearChangesType0},
        {"natural clear bounded", CaseNaturalClearLedgerIsBounded},
        {"bounded nonblocking", CaseLedgerIsBoundedAndNonBlocking},
        {"dropped mutation revision",
         CaseEveryDroppedMutationInvalidatesFrozenRevision},
        {"appearance provider", CaseAppearanceProviderChangeIsObserved},
    };
    std::uint32_t passed = 0;
    for (const TestCase& test : tests) {
        if (!test.run()) return 1;
        ++passed;
        std::printf("[PASS] %s\n", test.name);
    }
    std::printf("player state observation cases passed: %u\n", passed);
    return 0;
}
