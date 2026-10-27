#define _HAS_EXCEPTIONS 0

extern "C" {
#include "../src/dlw_common.h"
}
#include "../src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.h"
#include "../src/fixes/custom_map_asset_isolation/CustomMapAssetTrace.h"

#include <cstdio>
#include <cstring>

using namespace dluma::fixes::custom_map_asset_isolation::trace;

extern "C" int dluma_is_readable(const void* address, SIZE_T bytes)
{
    return address && bytes &&
           reinterpret_cast<std::uintptr_t>(address) >= 0x10000U;
}

namespace {

std::uint64_t g_fake_qpc;

std::uint64_t FakeQpc() noexcept
{
    return ++g_fake_qpc;
}

std::uint32_t FakeThreadId() noexcept
{
    return 7412;
}

bool FakeReadable(const void* address, std::size_t size) noexcept
{
    return address && size &&
           reinterpret_cast<std::uintptr_t>(address) >= 0x10000U;
}

void CountBoundaryCommit(void* opaque) noexcept
{
    int* count = static_cast<int*>(opaque);
    if (count) ++*count;
}

struct ContendedLookupContext {
    AssetTraceEventPayload payload;
    int prepares;
};

bool PrepareContendedLookup(void* opaque,
                            AssetTraceEventPayload* payload) noexcept
{
    ContendedLookupContext* context =
        static_cast<ContendedLookupContext*>(opaque);
    if (!context || !payload) return false;
    ++context->prepares;
    ClearPayload(
        payload, AssetTraceEventType::FirstFrontendPackOrModuleLookup);
    payload->value0 = 29;
    payload->value1 = 0;
    return true;
}

void AttemptLookupWhileBoundaryLocked(void* opaque) noexcept
{
    ContendedLookupContext* context =
        static_cast<ContendedLookupContext*>(opaque);
    if (!context) return;
    (void)PrepareThenPublish(
        &context->payload, PrepareContendedLookup, context);
}

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", \
                         __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (0)

void StartFresh()
{
    const AssetTracePlatform platform = {
        FakeQpc,
        FakeThreadId,
        FakeReadable,
    };
    g_fake_qpc = 1000;
    Initialize(&platform);
}

bool CaseSequenceAndGeneration()
{
    static AssetTraceEvent drained[16];
    StartFresh();
    const AssetTraceContextToken first = BeginTransitionContext();
    CHECK(first.generation == 1);
    CHECK(CurrentTransitionGeneration() == 1);
    CHECK(Publish(AssetTraceEventType::ChangeActiveLevelEnter, 1, 2, 3,
                  4, 0, "map_b", "map_a"));
    CHECK(Publish(AssetTraceEventType::UnloadLevelEnter, 5, 0, 0,
                  0, 0, "map_a", "nested"));
    EndTransitionContext(first);
    CHECK(CurrentTransitionGeneration() == 1);
    CHECK(Publish(AssetTraceEventType::ActiveLevelObserved, 6, 0, 0,
                  0, 0, "map_b", "project_b"));

    const AssetTraceContextToken second = BeginTransitionContext();
    CHECK(second.generation == 2);
    CHECK(Publish(AssetTraceEventType::ChangeActiveLevelEnter, 0, 0, 0,
                  0, 0, "map_a", "map_b"));
    EndTransitionContext(second);

    const std::size_t count = Drain(drained, 16);
    CHECK(count == 4);
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(drained[i].sequence == i + 1);
        CHECK(drained[i].qpc == 1001 + i);
        CHECK(drained[i].thread_id == 7412);
    }
    CHECK(drained[0].transition_generation == 1);
    CHECK(drained[1].transition_generation == 1);
    CHECK(drained[2].transition_generation == 1);
    CHECK(drained[3].transition_generation == 2);
    return true;
}

bool CaseQueueOverflowIsBoundedAndLatched()
{
    static AssetTraceEvent drained[kQueueCapacity];
    StartFresh();
    for (std::size_t i = 0; i < kQueueCapacity + 88; ++i) {
        Publish(AssetTraceEventType::FsOpenWatchedEnter, 0, 0, 0,
                i, 0, "data\\scripts\\watched.scr", nullptr);
    }
    CHECK(PendingCount() ==
          kQueueCapacity - kCriticalTraceReservedCapacity);
    CHECK(DroppedEventCount() ==
          kCriticalTraceReservedCapacity + 89U);
    const std::size_t count = Drain(drained, kQueueCapacity);
    CHECK(count ==
          kQueueCapacity - kCriticalTraceReservedCapacity);
    std::size_t overflow_count = 0;
    std::uint64_t previous_sequence = 0;
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(drained[i].sequence > previous_sequence);
        previous_sequence = drained[i].sequence;
        if (drained[i].type == AssetTraceEventType::QueueOverflow) {
            ++overflow_count;
            CHECK(drained[i].value0 ==
                  kCriticalTraceReservedCapacity + 89U);
            CHECK(drained[i].first ==
                  kQueueCapacity - kCriticalTraceReservedCapacity);
            CHECK(drained[i].second == 601);
            CHECK((drained[i].dropped_domain_mask &
                   AssetTraceDropWatchedOpen) != 0);
        }
    }
    CHECK(overflow_count == 1);
    CHECK(PendingCount() == 0);
    return true;
}

bool CaseOptionalFloodPreservesCriticalReserve()
{
    static AssetTraceEvent drained[kQueueCapacity];
    static const AssetTraceEventType required[] = {
        AssetTraceEventType::CancelledPresentationDetected,
        AssetTraceEventType::CancelledCleanupBegin,
        AssetTraceEventType::CancelledCleanupSourceRemoval,
        AssetTraceEventType::CancelledCleanupVerification,
        AssetTraceEventType::CancelledEpochConsumed,
        AssetTraceEventType::FreshPresentationEpochCreated,
        AssetTraceEventType::ChangeAfterFadeAttestation,
        AssetTraceEventType::PendingPostC10TokenPublished,
        AssetTraceEventType::C10OriginalReturned,
        AssetTraceEventType::PostC10SessionSlotSampled,
        AssetTraceEventType::PendingPostC10TokenConsumed,
        AssetTraceEventType::PostC10PreflightBegin,
        AssetTraceEventType::PostC10PlayerPass1,
        AssetTraceEventType::PostC10PlayerPass2,
        AssetTraceEventType::PostC10PlayerAssessment,
        AssetTraceEventType::PostC10AudioAssessment,
        AssetTraceEventType::PostC10TransactionPreparationResult,
        AssetTraceEventType::PostC10PreflightResult,
        AssetTraceEventType::C10ScopeCleared,
        AssetTraceEventType::FrontendSourceResetBegin,
        AssetTraceEventType::FrontendSourceResetEnd,
        AssetTraceEventType::FirstFrontendLookupDisposition,
        AssetTraceEventType::ShowMenuMainDisposition,
    };
    StartFresh();
    for (std::size_t index = 0; index < kQueueCapacity + 64U; ++index)
        (void)Publish(AssetTraceEventType::FsOpenWatchedEnter,
                      0, 0, 0, index, 0, "watched", nullptr);
    CHECK(OptionalTraceLossCount() != 0);
    CHECK(CriticalTraceLossCount() == 0);
    CHECK(CriticalTraceReservedCapacity() ==
          kCriticalTraceReservedCapacity);
    for (std::size_t index = 0;
         index < sizeof(required) / sizeof(required[0]); ++index) {
        CHECK(IsCriticalEvent(required[index]));
        CHECK(Publish(
            required[index], 0, 0, 0, 7, index,
            "transaction", "reserved"));
    }
    CHECK(Publish(
        AssetTraceEventType::FsRemoveSourceExit,
        0, 0, 0, 1, 1, "project_a\\data", "removed"));
    CHECK(Publish(
        AssetTraceEventType::ExperimentalPlayerPresetRebuildBegin,
        0, 0, 0, 8, 0, "default_levels.xml", "preflighted"));
    const std::size_t count = Drain(drained, kQueueCapacity);
    bool saw_remove = false;
    bool saw_rebuild = false;
    bool saw_required[
        sizeof(required) / sizeof(required[0])] = {};
    for (std::size_t index = 0; index < count; ++index) {
        saw_remove |= drained[index].type ==
            AssetTraceEventType::FsRemoveSourceExit;
        saw_rebuild |= drained[index].type ==
            AssetTraceEventType::ExperimentalPlayerPresetRebuildBegin;
        for (std::size_t required_index = 0;
             required_index <
                 sizeof(required) / sizeof(required[0]);
             ++required_index)
            if (drained[index].type == required[required_index])
                saw_required[required_index] = true;
    }
    CHECK(saw_remove);
    CHECK(saw_rebuild);
    for (std::size_t index = 0;
         index < sizeof(required) / sizeof(required[0]); ++index)
        CHECK(saw_required[index]);
    CHECK(CriticalTraceLossCount() == 0);
    return true;
}

bool CaseCriticalOverflowIsIndependent()
{
    StartFresh();
    for (std::size_t index = 0; index < kQueueCapacity + 8U; ++index)
        (void)Publish(AssetTraceEventType::FsRemoveSourceExit,
                      0, 0, 0, index, 1, "source", "removed");
    CHECK(CriticalTraceLossCount() == 9U);
    CHECK(OptionalTraceLossCount() == 0);
    CHECK(PendingCount() == kQueueCapacity);
    return true;
}

bool CaseReadableStringsFailClosed()
{
    StartFresh();
    char output[kEventTextCapacity];
    CHECK(CopyReadableCString(output, sizeof(output), "pack_a.rpack") ==
          AssetTraceStringCopyResult::Copied);
    CHECK(std::strcmp(output, "pack_a.rpack") == 0);
    CHECK(CopyReadableCString(output, sizeof(output),
                              reinterpret_cast<const char*>(1)) ==
          AssetTraceStringCopyResult::Unreadable);
    CHECK(std::strcmp(output, "<unreadable>") == 0);

    const char unterminated[4] = {'a', 'b', 'c', 'd'};
    CHECK(CopyReadableCString(output, sizeof(output), unterminated,
                              sizeof(unterminated)) ==
          AssetTraceStringCopyResult::Unterminated);
    CHECK(std::strcmp(output, "<unterminated>") == 0);

    char long_text[260];
    std::memset(long_text, 'x', sizeof(long_text));
    long_text[sizeof(long_text) - 1] = 0;
    CHECK(CopyReadableCString(output, sizeof(output), long_text) ==
          AssetTraceStringCopyResult::Truncated);
    CHECK(std::strlen(output) == kEventTextCapacity - 1);
    return true;
}

bool CaseReadablePackPathUsesBorrowedMockLayout()
{
    StartFresh();
    alignas(void*) unsigned char pack[0xC8] = {};
    const char* expected = "X:\\Workshop\\MapA\\packs\\duplicate_test.rpack";
    const char** holder = &expected;
    std::memcpy(pack + 0xC0, &holder, sizeof(holder));
    char output[kEventTextCapacity];
    CHECK(CopyRawResourcePackPath(output, sizeof(output), pack) ==
          AssetTraceStringCopyResult::Copied);
    CHECK(std::strcmp(output, expected) == 0);
    CHECK(CopyRawResourcePackPath(
              output, sizeof(output), reinterpret_cast<const void*>(1)) ==
          AssetTraceStringCopyResult::Unreadable);
    CHECK(std::strcmp(output, "<unreadable>") == 0);
    return true;
}

bool CaseWatchedPathNormalizationAndFullMatch()
{
    AssetTraceWatchList watches;
    char normalized[kEventTextCapacity];
    std::uint32_t index = 99;
    ResetWatchList(&watches);
    CHECK(AddWatchedPath(
        &watches, "/DATA//Scripts\\DLUMA_ASSET_ISOLATION_TEST.SCR"));
    CHECK(watches.count == 1);
    CHECK(std::strcmp(watches.paths[0],
                      "data\\scripts\\dluma_asset_isolation_test.scr") == 0);
    CHECK(AddWatchedPath(
        &watches, "data\\scripts\\dluma_asset_isolation_test.scr"));
    CHECK(watches.count == 1);
    CHECK(MatchWatchedPath(
        watches, "\\data/scripts//dluma_asset_isolation_test.scr",
        &index, normalized, sizeof(normalized)));
    CHECK(index == 0);
    CHECK(std::strcmp(normalized,
                      "data\\scripts\\dluma_asset_isolation_test.scr") == 0);
    CHECK(!MatchWatchedPath(
        watches, "other\\dluma_asset_isolation_test.scr"));
    CHECK(!MatchWatchedPath(watches, "dluma_asset_isolation_test.scr"));

    for (std::size_t i = 1; i < kMaxWatchedPaths; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data\\scripts\\watch_%zu.scr", i);
        CHECK(AddWatchedPath(&watches, path));
    }
    CHECK(watches.count == kMaxWatchedPaths);
    CHECK(!AddWatchedPath(&watches, "data\\scripts\\ninth.scr"));
    return true;
}

bool CaseEventFormattingEscapesAndNamesFields()
{
    AssetTraceEvent event = {};
    char first[1024];
    char second[1024];
    std::size_t first_length = 0;
    std::size_t second_length = 0;
    event.sequence = 142;
    event.qpc = 1234567890;
    event.transition_generation = 3;
    event.thread_id = 7412;
    event.type = AssetTraceEventType::FsOpenWatchedResult;
    event.object = 0x1234;
    event.value0 = 1;
    std::strcpy(event.text0, "data\\scripts\\a\"b\t.scr");
    std::strcpy(event.text1, "X:\\MapB\r\nsource");
    CHECK(FormatEventLine(event, first, sizeof(first), &first_length));
    CHECK(FormatEventLine(event, second, sizeof(second), &second_length));
    CHECK(first_length == second_length);
    CHECK(std::strcmp(first, second) == 0);
    CHECK(std::strstr(first, "seq=142 qpc=1234567890 thread=7412 gen=3"));
    CHECK(std::strstr(first, "category=\"filesystem\""));
    CHECK(std::strstr(first, "event=\"FsOpenWatchedResult\""));
    CHECK(std::strstr(first, "path=\"data\\\\scripts\\\\a\\\"b\\t.scr\""));
    CHECK(std::strstr(first, "source=\"X:\\\\MapB\\r\\nsource\""));
    CHECK(first[first_length - 1] == '\n');
    return true;
}

bool CaseEventFormattingHandlesWorstCaseEscaping()
{
    AssetTraceEvent event = {};
    char output[8192];
    std::size_t length = 0;
    event.sequence = UINT64_MAX;
    event.qpc = UINT64_MAX;
    event.transition_generation = UINT64_MAX;
    event.thread_id = UINT32_MAX;
    event.type = AssetTraceEventType::FsOpenWatchedResult;
    event.object = static_cast<std::uintptr_t>(UINT64_MAX);
    event.first = static_cast<std::uintptr_t>(UINT64_MAX);
    event.second = static_cast<std::uintptr_t>(UINT64_MAX);
    event.third = static_cast<std::uintptr_t>(UINT64_MAX);
    event.value0 = UINT64_MAX;
    event.value1 = UINT64_MAX;
    for (std::size_t index = 0; index + 1 < sizeof(event.text0); ++index) {
        event.text0[index] = static_cast<char>(1 + (index % 31));
        event.text1[index] = static_cast<char>(1 + ((index + 7) % 31));
        event.text2[index] = static_cast<char>(1 + ((index + 11) % 31));
        event.text3[index] = static_cast<char>(1 + ((index + 17) % 31));
        event.text4[index] = static_cast<char>(1 + ((index + 23) % 31));
    }
    CHECK(FormatEventLine(event, output, sizeof(output), &length));
    CHECK(length > 4000);
    CHECK(length < sizeof(output));
    CHECK(output[length - 1] == '\n');
    for (std::size_t index = 0; index + 1 < length; ++index)
        CHECK(output[index] != '\r' && output[index] != '\n');
    return true;
}

bool CaseHeaderContainsRequiredIdentityAndWatches()
{
    AssetTraceHeader header = {};
    AssetTraceWatchList watches;
    char output[4096];
    ResetWatchList(&watches);
    AddWatchedPath(&watches, "data\\scripts\\watched.scr");
    header.process_id = 239140;
    header.qpc_frequency = 10000000;
    header.trace_start_qpc = 55;
    std::strcpy(header.loader_version, "0.2.20-diagnostic");
    std::strcpy(header.trace_start_time, "2026-07-19T12:00:00-04:00");
    std::strcpy(header.gamedll_identity, "timestamp=0x623397aa size=0x02082000");
    std::strcpy(header.engine_identity, "timestamp=0x623395d0 size=0x00e8d000");
    std::strcpy(header.filesystem_identity, "timestamp=0x623394cd size=0x00060000");
    std::strcpy(header.enabled_hooks, "ChangeActiveLevel,fs_open");
    CHECK(FormatHeaderLine(header, watches, output, sizeof(output)));
    CHECK(std::strstr(output, "loader_version=\"0.2.20-diagnostic\""));
    CHECK(std::strstr(output, "process=239140"));
    CHECK(std::strstr(output, "gamedll=\"timestamp=0x623397aa"));
    CHECK(std::strstr(output, "enabled_hooks=\"ChangeActiveLevel,fs_open\""));
    CHECK(std::strstr(output, "watch1=\"data\\\\scripts\\\\watched.scr\""));
    return true;
}

void Consume(AssetTraceSummary* summary, std::uint64_t sequence,
             AssetTraceEventType type, std::uint64_t value0 = 0,
             std::uint64_t value1 = 0, std::uintptr_t first = 0,
             const char* text0 = nullptr,
             const char* text1 = nullptr)
{
    AssetTraceEvent event = {};
    event.sequence = sequence;
    event.transition_generation = 1;
    event.type = type;
    event.value0 = value0;
    event.value1 = value1;
    event.first = first;
    if (text0) std::strncpy(event.text0, text0, sizeof(event.text0) - 1);
    if (text1) std::strncpy(event.text1, text1, sizeof(event.text1) - 1);
    ConsumeSummaryEvent(summary, event);
}

bool CaseSummaryRequiresCompleteNaturalEvidence()
{
    AssetTraceSummary summary;
    char output[8192];
    ResetSummary(&summary, true, true);
    summary.asset_manager_hook_available = true;
    Consume(&summary, 1, AssetTraceEventType::ChangeActiveLevelEnter);
    Consume(&summary, 2, AssetTraceEventType::ChangeActiveLevelBranch,
            1, 0, 0, "destroy_active_level");
    Consume(&summary, 3, AssetTraceEventType::DestroyActiveLevelEnter,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 4, AssetTraceEventType::DestroyActiveLevelExit,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 5, AssetTraceEventType::UpdateDLCContentEnter,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 6, AssetTraceEventType::UpdateDLCContentExit,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 7, AssetTraceEventType::UnloadLevelEnter);
    Consume(&summary, 8, AssetTraceEventType::UnloadLevelExit);
    Consume(&summary, 9, AssetTraceEventType::FsRemoveSourceEnter);
    Consume(&summary, 10, AssetTraceEventType::FsRemoveSourceExit, 1,
            AssetTraceEvidenceOutgoing |
                AssetTraceEvidenceOriginalSucceeded);
    Consume(&summary, 11, AssetTraceEventType::LevelReleasePacksEnter);
    Consume(&summary, 12, AssetTraceEventType::LevelReleasePacksPack,
            0, AssetTraceEvidenceOutgoing, 0xAA, "map_a_test.rpack");
    Consume(&summary, 13, AssetTraceEventType::OnDemandEvictPackExit,
            0, 0, 0xAA, "map_a_test.rpack");
    Consume(&summary, 14, AssetTraceEventType::LevelReleasePacksExit,
            0, AssetTraceEvidenceOutgoing);
    Consume(&summary, 15, AssetTraceEventType::FsOpenWatchedResult, 1,
            AssetTraceEvidenceDestination, 0,
            "data\\scripts\\watched.scr", "X:\\MapB");
    Consume(&summary, 16, AssetTraceEventType::ScriptLoaderWatchedParse);
    Consume(&summary, 17, AssetTraceEventType::ChangeActiveLevelExit);

    CHECK(summary.transition_count == 1);
    CHECK(CleanupBeforeDestinationLookup(summary.transitions[0]) ==
          AssetTraceTriState::Yes);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "branch=\"destroy_active_level\""));
    CHECK(std::strstr(output, "destroy_active_level_called=yes"));
    CHECK(std::strstr(output, "update_dlc_content_called=yes"));
    CHECK(std::strstr(output, "unload_level_called=yes"));
    CHECK(std::strstr(output, "outgoing_source_removal_observed=yes"));
    CHECK(std::strstr(output, "outgoing_level_pack_release_observed=yes"));
    CHECK(std::strstr(output, "on_demand_pack_eviction_observed=yes"));
    CHECK(std::strstr(
        output, "cleanup_completed_before_first_destination_lookup=yes"));
    CHECK(std::strstr(output, "parser_cache_outcome=parsed"));
    CHECK(std::strstr(output, "path=\"data\\\\scripts\\\\watched.scr\""));
    return true;
}

bool CaseSummaryDoesNotInventCleanupCompletion()
{
    AssetTraceSummary summary;
    char output[8192];
    ResetSummary(&summary, false, false);
    Consume(&summary, 1, AssetTraceEventType::ChangeActiveLevelEnter);
    Consume(&summary, 2, AssetTraceEventType::DestroyActiveLevelExit);
    Consume(&summary, 3, AssetTraceEventType::FsOpenWatchedResult, 1,
            AssetTraceEvidenceDestination, 0,
            "data\\scripts\\watched.scr", "unknown");
    CHECK(CleanupBeforeDestinationLookup(summary.transitions[0]) ==
          AssetTraceTriState::Unknown);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(
        output, "cleanup_completed_before_first_destination_lookup=unknown"));
    CHECK(std::strstr(output, "on_demand_pack_eviction_observed=unknown"));
    CHECK(std::strstr(output, "limitation=script_cache_trace_unresolved"));
    return true;
}

bool CaseSummaryOverflowAndGenerationZeroAreConservative()
{
    AssetTraceSummary summary;
    char output[8192];
    ResetSummary(&summary, true, false);

    AssetTraceEvent generation_zero = {};
    generation_zero.sequence = 1;
    generation_zero.type = AssetTraceEventType::FsOpenWatchedResult;
    generation_zero.value0 = 1;
    std::strcpy(generation_zero.text0, "data\\scripts\\watched.scr");
    std::strcpy(generation_zero.text1, "X:\\MapB");
    ConsumeSummaryEvent(&summary, generation_zero);
    CHECK(summary.watched_open_count == 1);
    CHECK(summary.watched_opens[0].generation == 0);

    Consume(&summary, 2, AssetTraceEventType::ChangeActiveLevelEnter);
    Consume(&summary, 3, AssetTraceEventType::UnloadLevelExit);
    Consume(&summary, 4, AssetTraceEventType::DestroyActiveLevelExit,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 5, AssetTraceEventType::UpdateDLCContentExit,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 6, AssetTraceEventType::FsRemoveSourceExit, 1,
            AssetTraceEvidenceOutgoing |
                AssetTraceEvidenceOriginalSucceeded);
    Consume(&summary, 7, AssetTraceEventType::LevelReleasePacksPack,
            0, AssetTraceEvidenceOutgoing, 0xAA);
    Consume(&summary, 8, AssetTraceEventType::LevelReleasePacksExit,
            0, AssetTraceEvidenceOutgoing);
    Consume(&summary, 9, AssetTraceEventType::FsOpenWatchedResult, 1,
            AssetTraceEvidenceDestination, 0,
            "data\\scripts\\watched.scr", "X:\\MapB");
    Consume(&summary, 10, AssetTraceEventType::QueueOverflow, 3);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "queue_overflow_observed=yes"));
    CHECK(std::strstr(
        output,
        "cleanup_completed_before_first_destination_lookup=unknown"));
    CHECK(std::strstr(output, "watched_open seq=1 epoch=0 gen=0"));
    CHECK(std::strstr(output, "limitation=asset_manager_trace_unresolved"));
    return true;
}

bool CaseSummaryOutputIncompleteIsConservative()
{
    AssetTraceSummary summary;
    char output[8192];
    ResetSummary(&summary, true, false);
    Consume(&summary, 1, AssetTraceEventType::ChangeActiveLevelEnter);
    Consume(&summary, 2, AssetTraceEventType::UnloadLevelEnter);
    Consume(&summary, 3, AssetTraceEventType::FsOpenWatchedResult, 1,
            AssetTraceEvidenceDestination, 0,
            "data\\scripts\\watched.scr", "X:\\MapB");
    summary.trace_output_incomplete = true;
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "trace_output_incomplete=yes"));
    /* Persisted-file failure is an independent domain.  It must not erase
     * already committed in-memory engine evidence or invent queue loss. */
    CHECK(std::strstr(output, "destroy_active_level_called=no"));
    CHECK(std::strstr(output, "any_source_removal_observed=no"));
    CHECK(std::strstr(output, "trace_event_stream_complete=yes"));
    CHECK(std::strstr(
        output,
        "cleanup_completed_before_first_destination_lookup=unknown"));
    return true;
}

bool CaseSummaryOptionalCoverageIsRequiredForCompletion()
{
    AssetTraceSummary summary;
    char output[8192];
    ResetSummary(&summary, false, false);
    Consume(&summary, 1, AssetTraceEventType::ChangeActiveLevelEnter);
    Consume(&summary, 2, AssetTraceEventType::UnloadLevelExit);
    Consume(&summary, 3, AssetTraceEventType::DestroyActiveLevelExit,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 4, AssetTraceEventType::UpdateDLCContentExit,
            0, AssetTraceEvidenceTransitionContext);
    Consume(&summary, 5, AssetTraceEventType::FsRemoveSourceExit, 1,
            AssetTraceEvidenceOutgoing |
                AssetTraceEvidenceOriginalSucceeded);
    Consume(&summary, 6, AssetTraceEventType::LevelReleasePacksPack,
            0, AssetTraceEvidenceOutgoing, 0xAA);
    Consume(&summary, 7, AssetTraceEventType::LevelReleasePacksExit,
            0, AssetTraceEvidenceOutgoing);
    Consume(&summary, 8, AssetTraceEventType::FsOpenWatchedResult, 1,
            AssetTraceEvidenceDestination, 0,
            "data\\scripts\\watched.scr", "X:\\MapB");
    CHECK(CleanupBeforeDestinationLookup(summary.transitions[0]) ==
          AssetTraceTriState::Yes);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "limitation=asset_manager_trace_unresolved"));
    CHECK(std::strstr(output, "limitation=optional_on_demand_unresolved"));
    CHECK(std::strstr(
        output,
        "cleanup_completed_before_first_destination_lookup=unknown"));
    return true;
}

bool CaseSummaryPackPointerTruncationIsConservative()
{
    AssetTraceSummary summary;
    char output[8192];
    ResetSummary(&summary, true, false);
    for (std::uintptr_t index = 1;
         index <= kMaxSummaryPackPointers + 1; ++index)
        Consume(&summary, index, AssetTraceEventType::LevelReleasePacksPack,
                0, AssetTraceEvidenceOutgoing, index);
    Consume(&summary, 20, AssetTraceEventType::LevelReleasePacksExit,
            0, AssetTraceEvidenceOutgoing);
    CHECK(summary.transitions[0].pack_pointer_correlation_truncated);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "on_demand_pack_eviction_observed=unknown"));
    CHECK(std::strstr(
        output, "limitation=pack_pointer_correlation_truncated"));
    return true;
}

bool CaseSummaryHandlesBoundedWorstCaseEscaping()
{
    AssetTraceSummary summary;
    static char output[256 * 1024];
    std::size_t written = 0;
    ResetSummary(&summary, true, true);
    summary.asset_manager_hook_available = true;
    summary.transition_count = kMaxSummaryTransitions;
    for (std::uint32_t index = 0; index < kMaxSummaryTransitions; ++index) {
        AssetTraceGenerationSummary& item = summary.transitions[index];
        item.generation = index + 1;
        item.branch_observed = true;
        for (std::size_t character = 0;
             character + 1 < sizeof(item.selected_branch); ++character)
            item.selected_branch[character] =
                static_cast<char>(1 + (character % 31));
    }
    summary.watched_open_count = kMaxSummaryWatchedOpens;
    for (std::uint32_t index = 0; index < kMaxSummaryWatchedOpens; ++index) {
        AssetTraceWatchedResolution& open = summary.watched_opens[index];
        open.sequence = index + 1;
        open.generation = index + 1;
        open.result = 1;
        for (std::size_t character = 0;
             character + 1 < sizeof(open.logical_path); ++character) {
            open.logical_path[character] =
                static_cast<char>(1 + (character % 31));
            open.resolved_source[character] =
                static_cast<char>(1 + ((character + 11) % 31));
        }
    }
    CHECK(FormatSummary(summary, output, sizeof(output), &written));
    CHECK(written > 100000);
    CHECK(written < sizeof(output));
    return true;
}

bool CaseFrontendRollbackBarrierEventsPreserveOrder()
{
    static AssetTraceEvent drained[8];
    AssetTraceSummary summary;
    char summary_text[16384];
    static const AssetTraceEventType expected[] = {
        AssetTraceEventType::SessionOnDestroyEnter,
        AssetTraceEventType::OnDestroyOwnedUnloadObserved,
        AssetTraceEventType::OwnedReleasePacksCompleted,
        AssetTraceEventType::OwnedFrontendUnloadCompleted,
        AssetTraceEventType::SessionOnDestroyOriginalReturned,
        AssetTraceEventType::FrontendSourceResetBegin,
        AssetTraceEventType::FrontendSourceResetEnd,
        AssetTraceEventType::FirstFrontendPackOrModuleLookup,
    };
    StartFresh();
    const AssetTraceContextToken transition = BeginTransitionContext();
    CHECK(transition.generation == 1);
    for (std::size_t index = 0;
         index < sizeof(expected) / sizeof(expected[0]); ++index) {
        const std::uint64_t barrier_evidence =
            expected[index] ==
                    AssetTraceEventType::OwnedFrontendUnloadCompleted
                ? (2ULL << 32U) | 2U
                :
            expected[index] == AssetTraceEventType::FrontendSourceResetEnd ||
                    expected[index] ==
                        AssetTraceEventType::FirstFrontendPackOrModuleLookup
                ? 1U
                : 0U;
        CHECK(Publish(expected[index], 0x8100U + index,
                      0x8200U + index, 0x8300U + index,
                      17, barrier_evidence, "MapA", "frontend"));
    }
    EndTransitionContext(transition);
    const std::size_t count = Drain(drained, 8);
    CHECK(count == sizeof(expected) / sizeof(expected[0]));
    for (std::size_t index = 0; index < count; ++index) {
        CHECK(drained[index].sequence == index + 1);
        CHECK(drained[index].transition_generation == 1);
        CHECK(drained[index].type == expected[index]);
        CHECK(std::strcmp(EventTypeName(drained[index].type),
                          EventTypeName(expected[index])) == 0);
    }
    CHECK(std::strcmp(EventCategoryName(drained[0].type),
                      "transition") == 0);
    CHECK(std::strcmp(EventCategoryName(drained[2].type),
                      "rollback") == 0);
    CHECK(std::strcmp(EventCategoryName(drained[7].type),
                      "rollback") == 0);
    ResetSummary(&summary, true, true);
    for (std::size_t index = 0; index < count; ++index)
        ConsumeSummaryEvent(&summary, drained[index]);
    CHECK(summary.rollback_barrier_count == 1);
    CHECK(summary.rollback_barriers[0].epoch == 17);
    CHECK(summary.rollback_barriers[0].on_destroy_enter_sequence == 1);
    CHECK(summary.rollback_barriers[0].owned_unload_sequence == 2);
    CHECK(summary.rollback_barriers[0].outgoing_release_sequence == 3);
    CHECK(summary.rollback_barriers[0].owned_unload_completed_sequence == 4);
    CHECK(summary.rollback_barriers[0].release_enter_count == 2);
    CHECK(summary.rollback_barriers[0].release_exit_count == 2);
    CHECK(summary.rollback_barriers[0].original_return_sequence == 5);
    CHECK(summary.rollback_barriers[0].reset_begin_sequence == 6);
    CHECK(summary.rollback_barriers[0].reset_end_sequence == 7);
    CHECK(summary.rollback_barriers[0].first_frontend_lookup_sequence == 8);
    CHECK(summary.rollback_barriers[0].reset_succeeded);
    CHECK(summary.rollback_barriers[0].first_lookup_after_reset);
    CHECK(FormatSummary(summary, summary_text, sizeof(summary_text)));
    CHECK(std::strstr(
        summary_text,
        "rollback epoch=17 on_destroy_enter_seq=1 "
        "owned_unload_0x431C83_seq=2 owned_unload_completed_seq=4 "
        "outgoing_release_packs_seq=3 release_enters=2 "
        "release_completions=2 original_return_seq=5 "
        "source_reset_begin_seq=6 source_reset_end_seq=7 "
        "first_frontend_lookup_seq=8 "
        "reset_completed_before_first_frontend_lookup=yes"));
    return true;
}

bool CaseFrontendLookupBeforeResetIsVisible()
{
    static AssetTraceEvent drained[4];
    AssetTraceSummary summary;
    char line[1024];
    char summary_text[16384];
    std::size_t line_length = 0;
    StartFresh();
    CHECK(Publish(AssetTraceEventType::SessionOnDestroyEnter,
                  0x8400, 0x8500, 0, 19, 0, "MapA", nullptr));
    CHECK(Publish(
        AssetTraceEventType::FirstFrontendPackOrModuleLookup,
        0x8600, 0, 0, 19, 0,
        "data\\menu_main_pc.rpack", "before_source_reset"));
    CHECK(Publish(AssetTraceEventType::FrontendSourceResetBegin,
                  0x8400, 0, 0, 19, 0, "MapA", nullptr));
    CHECK(Publish(AssetTraceEventType::FrontendSourceResetEnd,
                  0x8400, 0, 0, 19, 1, "MapA", "completed-too-late"));
    const std::size_t count = Drain(drained, 4);
    CHECK(count == 4);
    CHECK(drained[0].type ==
          AssetTraceEventType::SessionOnDestroyEnter);
    CHECK(drained[1].type ==
          AssetTraceEventType::FirstFrontendPackOrModuleLookup);
    CHECK(drained[2].type ==
          AssetTraceEventType::FrontendSourceResetBegin);
    CHECK(drained[3].type ==
          AssetTraceEventType::FrontendSourceResetEnd);
    CHECK(drained[1].sequence < drained[2].sequence);
    CHECK(FormatEventLine(drained[1], line, sizeof(line),
                          &line_length));
    CHECK(line_length != 0);
    CHECK(std::strstr(
        line, "event=\"FirstFrontendPackOrModuleLookup\""));
    CHECK(std::strstr(line, "category=\"rollback\""));
    CHECK(std::strstr(
        line, "text0=\"data\\\\menu_main_pc.rpack\""));
    CHECK(std::strstr(line, "text1=\"before_source_reset\""));
    ResetSummary(&summary, true, true);
    for (std::size_t index = 0; index < count; ++index)
        ConsumeSummaryEvent(&summary, drained[index]);
    CHECK(summary.rollback_barrier_count == 1);
    CHECK(summary.rollback_barriers[0].epoch == 19);
    CHECK(summary.rollback_barriers[0].first_frontend_lookup_sequence == 2);
    CHECK(summary.rollback_barriers[0].reset_end_sequence == 4);
    CHECK(!summary.rollback_barriers[0].first_lookup_after_reset);
    CHECK(FormatSummary(summary, summary_text, sizeof(summary_text)));
    CHECK(std::strstr(
        summary_text,
        "rollback epoch=19 on_destroy_enter_seq=1"));
    CHECK(std::strstr(
        summary_text,
        "source_reset_end_seq=4 first_frontend_lookup_seq=2 "
        "reset_completed_before_first_frontend_lookup=no"));
    return true;
}

bool CaseBoundaryPublishCommitsExactlyOnce()
{
    static AssetTraceEvent drained[kQueueCapacity];
    AssetTraceEventPayload payload;
    int commits = 0;
    StartFresh();
    ClearPayload(&payload, AssetTraceEventType::FrontendSourceResetEnd);
    payload.value0 = 23;
    payload.value1 = 1;
    std::strcpy(payload.text0, "MapA");
    std::strcpy(payload.text1, "source-removal-complete");
    CHECK(PublishThenCommit(payload, CountBoundaryCommit, &commits));
    CHECK(commits == 1);
    CHECK(Publish(
        AssetTraceEventType::FirstFrontendPackOrModuleLookup,
        0, 0, 0, 23, 1, "menu_main_pc.rpack", "after_source_reset"));
    CHECK(Drain(drained, kQueueCapacity) == 2);
    CHECK(drained[0].type == AssetTraceEventType::FrontendSourceResetEnd);
    CHECK(drained[1].type ==
          AssetTraceEventType::FirstFrontendPackOrModuleLookup);
    CHECK(drained[0].sequence < drained[1].sequence);

    StartFresh();
    commits = 0;
    for (std::size_t index = 0; index < kQueueCapacity; ++index)
        (void)Publish(AssetTraceEventType::FrontendSourceResetBegin,
                      0, 0, 0, index, 0, "critical", nullptr);
    CHECK(!PublishThenCommit(payload, CountBoundaryCommit, &commits));
    CHECK(commits == 1);

    /* A lookup classified while ResetEnd owns the trace lock is explicitly
     * dropped, never appended later with a contradictory sequence. */
    StartFresh();
    ContendedLookupContext lookup = {};
    CHECK(PublishThenCommit(
        payload, AttemptLookupWhileBoundaryLocked, &lookup));
    CHECK(lookup.prepares == 1);
    CHECK(Publish(AssetTraceEventType::ActiveLevelObserved,
                  0, 0, 0, 29, 0, "frontend", "fold-overflow"));
    const std::size_t count = Drain(drained, kQueueCapacity);
    CHECK(count == 3);
    CHECK(drained[0].type == AssetTraceEventType::FrontendSourceResetEnd);
    CHECK(drained[1].type == AssetTraceEventType::QueueOverflow);
    CHECK(drained[1].value0 == 1);
    CHECK(drained[2].type == AssetTraceEventType::ActiveLevelObserved);
    for (std::size_t index = 0; index < count; ++index)
        CHECK(drained[index].type !=
              AssetTraceEventType::FirstFrontendPackOrModuleLookup);
    return true;
}

bool CaseEpochNativeWatchedOpenReplay()
{
    AssetTraceSummary summary;
    char output[16384];
    ResetSummary(&summary, true, true);
    summary.asset_manager_hook_available = true;

    AssetTraceEvent event = {};
    event.sequence = 1747;
    event.transition_generation = 0;
    event.content_epoch = 1;
    event.source_record_index = 2;
    event.source_add_ordinal = 3;
    event.type = AssetTraceEventType::FsOpenWatchedResult;
    event.object = 0xA001;
    event.first = 0xA100;
    event.value0 = 1;
    event.source_kind = AssetTraceSourceKind::DataDirectory;
    event.staging_phase = AssetTraceStagingPhase::CustomContentStaging;
    event.update_dlc_depth = 1;
    std::strcpy(event.text0, "data\\campaign_custom_workshop.scr");
    std::strcpy(event.text1, "X:\\Workshop\\ProjectA\\Data");
    std::strcpy(event.text2, "X:\\Workshop\\ProjectA");
    ConsumeSummaryEvent(&summary, event);

    event = {};
    event.sequence = 1798;
    event.type = AssetTraceEventType::FrontendSourceResetEnd;
    event.value0 = 1;
    event.value1 = 1;
    ConsumeSummaryEvent(&summary, event);
    event.sequence = 1805;
    event.type = AssetTraceEventType::FirstFrontendPackOrModuleLookup;
    ConsumeSummaryEvent(&summary, event);

    event = {};
    event.sequence = 2659;
    event.transition_generation = 1;
    event.content_epoch = 2;
    event.source_record_index = 7;
    event.source_add_ordinal = 8;
    event.type = AssetTraceEventType::FsOpenWatchedResult;
    event.object = 0xB001;
    event.first = 0xB100;
    event.value0 = 1;
    event.source_kind = AssetTraceSourceKind::DataDirectory;
    event.staging_phase = AssetTraceStagingPhase::CustomContentStaging;
    event.update_dlc_depth = 1;
    std::strcpy(event.caller_module, "filesystem_x64_rwdi.dll");
    event.caller_rva = 0x18D0E;
    std::strcpy(event.text0, "data\\campaign_custom_workshop.scr");
    std::strcpy(event.text1, "X:\\Workshop\\ProjectB\\Data");
    std::strcpy(event.text2, "X:\\Workshop\\ProjectB");
    ConsumeSummaryEvent(&summary, event);

    event = {};
    event.sequence = 2661;
    event.transition_generation = 2;
    event.type = AssetTraceEventType::ChangeAfterFadeEnter;
    ConsumeSummaryEvent(&summary, event);

    CHECK(summary.watched_open_count == 2);
    CHECK(summary.watched_opens[0].content_epoch == 1);
    CHECK(summary.watched_opens[0].generation == 0);
    CHECK(summary.watched_opens[1].content_epoch == 2);
    CHECK(summary.watched_opens[1].generation == 1);
    CHECK(summary.watched_opens[1].source_record_index == 7);
    CHECK(summary.watched_opens[1].source_add_ordinal == 8);
    CHECK(summary.watched_opens[1].source_kind ==
          AssetTraceSourceKind::DataDirectory);
    CHECK(summary.watched_opens[1].staging_phase ==
          AssetTraceStagingPhase::CustomContentStaging);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(
        output,
        "watched_open seq=2659 epoch=2 gen=1 "
        "staging=\"custom_content_staging\""));
    CHECK(std::strstr(output, "source_record=7 source_ordinal=8"));
    CHECK(std::strstr(output, "source_kind=\"DataDirectory\""));
    CHECK(std::strstr(output, "project=\"X:\\\\Workshop\\\\ProjectB\""));
    return true;
}

bool CaseParserOutcomeClassifierIsSignalDriven()
{
    WatchedScriptClassificationSignals signals = {};
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::CorrelationUnknown);

    signals.correlation_exact = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::ParseFailed);
    signals.parser_entered = true;
    signals.parser_succeeded = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::ParsedNew);
    signals.registration_replaced = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::ParsedReplaced);
    signals.registration_replaced = false;
    signals.registration_merged = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::ParsedMerged);
    signals.registration_merged = false;
    signals.cache_hit = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::CacheHitSameEpoch);
    signals.prior_epoch_object = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::CacheHitPriorEpoch);
    signals.registration_conflict = true;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::RegistrationConflict);
    signals.registration_conflict = false;
    signals.cache_hit = false;
    signals.parser_succeeded = false;
    CHECK(ClassifyWatchedScriptObservation(signals) ==
          WatchedScriptOutcome::ParseFailed);
    return true;
}

bool CaseParserProvenanceDoesNotInferFromPointerOrHash()
{
    AssetTraceSummary summary;
    char output[16384];
    ResetSummary(&summary, true, true);
    AssetTraceEvent event = {};
    event.type = AssetTraceEventType::ScriptLoaderWatchedExit;
    event.script_outcome = WatchedScriptOutcome::ParsedNew;
    event.object = 0xCAFE;
    event.first = 0xA100;
    event.value0 = 1024;
    event.value1 = 9;
    event.content_epoch = 1;
    event.sequence = 10;
    event.observation_generation = 1;
    std::strcpy(event.text0, "data\\campaign_custom_workshop.scr");
    std::strcpy(event.text1, "X:\\Workshop\\ProjectA\\Data");
    std::strcpy(event.text2, "1033a42b");
    std::strcpy(event.text3, "<registry-owner-unresolved>");
    ConsumeSummaryEvent(&summary, event);

    /* The allocator may reuse the same result address. A distinct bounded
     * parser-observation generation remains a separate observation, and no
     * cache/replacement outcome is fabricated from pointer reuse. */
    event.sequence = 20;
    event.content_epoch = 2;
    event.first = 0xB100;
    event.observation_generation = 2;
    event.value0 = 2048;
    event.value1 = 19;
    std::strcpy(event.text1, "X:\\Workshop\\ProjectB\\Data");
    std::strcpy(event.text2, "e09ef9bd");
    ConsumeSummaryEvent(&summary, event);

    /* Identical bytes under the same logical path likewise remain a fresh
     * parser observation unless a proven registry/cache branch says more. */
    event.sequence = 30;
    event.object = 0xD00D;
    event.observation_generation = 3;
    std::strcpy(event.text2, "e09ef9bd");
    ConsumeSummaryEvent(&summary, event);

    CHECK(summary.script_observation_count == 3);
    CHECK(summary.script_observations[0].result_object == 0xCAFE);
    CHECK(summary.script_observations[1].result_object == 0xCAFE);
    CHECK(summary.script_observations[0].parser_observation_generation == 1);
    CHECK(summary.script_observations[1].parser_observation_generation == 2);
    CHECK(summary.script_observations[1].input_size == 2048);
    CHECK(summary.script_observations[1].open_sequence == 19);
    CHECK(summary.script_observations[1].outcome ==
          WatchedScriptOutcome::ParsedNew);
    CHECK(std::strcmp(summary.script_observations[1].content_hash,
                      summary.script_observations[2].content_hash) == 0);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "parser_observation_generation=2"));
    CHECK(std::strstr(output, "input_size=2048 open_sequence=19"));
    CHECK(std::strstr(output, "registry_generation=0"));
    CHECK(std::strstr(output, "registry_key=\"<registry-owner-unresolved>\""));
    return true;
}

bool CaseReleaseLabelSourcesAreExplicit()
{
    AssetTraceEvent event = {};
    char output[4096];
    event.sequence = 1;
    event.type = AssetTraceEventType::OwnedReleasePacksCompleted;
    event.label_source = AssetTraceLabelSource::OnDestroyBinding;
    std::strcpy(event.text0, "wired");
    CHECK(FormatEventLine(event, output, sizeof(output)));
    CHECK(std::strstr(output, "label_source=\"on_destroy_binding\""));
    CHECK(std::strstr(output, "text0=\"wired\""));

    std::strcpy(event.text0, "irisu");
    CHECK(FormatEventLine(event, output, sizeof(output)));
    CHECK(std::strstr(output, "text0=\"irisu\""));
    CHECK(!std::strstr(output, "text0=\"menu_lvl_new\""));

    event.type = AssetTraceEventType::LevelReleasePacksPack;
    event.label_source = AssetTraceLabelSource::TransitionBinding;
    std::strcpy(event.text0, "map_2");
    CHECK(FormatEventLine(event, output, sizeof(output)));
    CHECK(std::strstr(output, "label_source=\"transition_binding\""));

    event.label_source = AssetTraceLabelSource::ActiveMapAnnotation;
    std::strcpy(event.text0, "menu_lvl_new");
    CHECK(FormatEventLine(event, output, sizeof(output)));
    CHECK(std::strstr(output, "label_source=\"active_map_annotation\""));

    event.label_source = AssetTraceLabelSource::Unknown;
    std::strcpy(event.text0, "<unknown>");
    CHECK(FormatEventLine(event, output, sizeof(output)));
    CHECK(std::strstr(output, "label_source=\"unknown\""));
    return true;
}

void ConsumeOverflow(AssetTraceSummary* summary, std::uint32_t domain,
                     std::uint64_t sequence = 50)
{
    AssetTraceEvent event = {};
    event.sequence = sequence + 1;
    event.type = AssetTraceEventType::QueueOverflow;
    event.value0 = 1;
    event.first = static_cast<std::uintptr_t>(sequence);
    event.second = static_cast<std::uintptr_t>(sequence);
    event.dropped_domain_mask = domain;
    ConsumeSummaryEvent(summary, event);
}

bool CaseCompletenessIsDomainAware()
{
    AssetTraceSummary summary;
    char output[16384];

    ResetSummary(&summary, true, true);
    ConsumeOverflow(&summary, AssetTraceDropFilesystem);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "trace_file_io_complete=yes"));
    CHECK(std::strstr(output, "trace_event_stream_complete=no"));
    CHECK(std::strstr(output, "summary_coverage_complete=no"));
    CHECK(std::strstr(output, "dropped_domains=\"source_removal\""));
    CHECK(std::strstr(output, "first_drop_sequence=50 last_drop_sequence=50"));
    CHECK(std::strstr(output, "watched_open_coverage=yes"));
    CHECK(std::strstr(output, "parser_cache_coverage=yes"));
    CHECK(std::strstr(output, "resource_trace_coverage=yes"));
    CHECK(std::strstr(output, "transition_trace_coverage=yes"));

    ResetSummary(&summary, true, true);
    ConsumeOverflow(&summary, AssetTraceDropWatchedOpen);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "watched_open_coverage=no"));

    ResetSummary(&summary, true, true);
    ConsumeOverflow(&summary, AssetTraceDropParserCache);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "parser_cache_coverage=no"));

    ResetSummary(&summary, true, true);
    ConsumeOverflow(&summary, AssetTraceDropRollback);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "rollback_barrier_coverage=no"));

    ResetSummary(&summary, true, true);
    summary.trace_output_incomplete = true;
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "trace_file_io_complete=no"));
    CHECK(std::strstr(output, "trace_event_stream_complete=yes"));

    /* A generic filesystem drop does not erase the positive ordering that
     * the atomic frontend-reset phase committed and successfully recorded. */
    ResetSummary(&summary, true, true);
    Consume(&summary, 1, AssetTraceEventType::SessionOnDestroyEnter, 4);
    Consume(&summary, 2, AssetTraceEventType::OnDestroyOwnedUnloadObserved, 4);
    Consume(&summary, 3, AssetTraceEventType::OwnedReleasePacksCompleted, 4);
    Consume(&summary, 4, AssetTraceEventType::OwnedFrontendUnloadCompleted,
            4, (1ULL << 32U) | 1U);
    Consume(&summary, 5, AssetTraceEventType::SessionOnDestroyOriginalReturned,
            4);
    Consume(&summary, 6, AssetTraceEventType::FrontendSourceResetBegin, 4);
    Consume(&summary, 7, AssetTraceEventType::FrontendSourceResetEnd, 4, 1);
    Consume(&summary, 8,
            AssetTraceEventType::FirstFrontendPackOrModuleLookup, 4, 1);
    ConsumeOverflow(&summary, AssetTraceDropFilesystem, 40);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(
        output,
        "reset_completed_before_first_frontend_lookup=yes "
        "ordering_authority=atomic_frontend_reset_phase"));
    CHECK(std::strstr(output, "rollback_barrier_coverage=yes"));
    return true;
}

bool CaseOptionalOverflowPreservesTransactionEvidence()
{
    Reset();
    RecordPreMountDecision(true, 41, 3, "");
    RecordSourceBinding(
        DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        41, 7, true, "");
    RecordSourceBinding(
        DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
        41, 7, true, "");
    RecordSourceBinding(
        DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC,
        41, 7, true, "");

    AssetTraceEventPayload optional = {};
    ClearPayload(
        &optional, AssetTraceEventType::ActiveLevelObserved);
    for (std::size_t index = 0;
         index < kQueueCapacity + 64U; ++index)
        (void)Publish(optional);
    CHECK(OptionalTraceLossCount() != 0);

    AssetTraceEventPayload required = {};
    ClearPayload(
        &required, AssetTraceEventType::FsRemoveSourceExit);
    required.value0 = 1;
    (void)Publish(required);
    ClearPayload(
        &required, AssetTraceEventType::SessionOnDestroyEnter);
    required.value1 = 1;
    (void)Publish(required);
    ClearPayload(
        &required, AssetTraceEventType::FrontendSourceResetEnd);
    required.value1 = 1;
    (void)Publish(required);
    ClearPayload(
        &required,
        AssetTraceEventType::FirstFrontendPackOrModuleLookup);
    required.value1 = 1;
    (void)Publish(required);
    RecordPreMountDecision(
        false, 0, 2, "first_missing_premount_boundary");

    AssetTraceTransactionEvidence evidence = {};
    CHECK(SnapshotTransactionEvidence(&evidence));
    CHECK(evidence.premount_decision_count == 2);
    CHECK(evidence.premount_allowed_count == 1);
    CHECK(evidence.allowed_token_count == 1);
    CHECK(evidence.project_root_binding_count == 1);
    CHECK(evidence.data_directory_binding_count == 1);
    CHECK(evidence.assets_pc_binding_count == 1);
    CHECK(evidence.source_removal_count == 1);
    CHECK(evidence.rollback_barrier_count == 1);
    CHECK(evidence.frontend_reset_count == 1);
    CHECK(evidence.frontend_verification_count == 1);
    CHECK(evidence.last_token_generation == 41);
    CHECK(evidence.last_bound_epoch == 7);
    CHECK(std::strcmp(
        evidence.first_failure_reason,
        "first_missing_premount_boundary") == 0);
    CHECK(evidence.transaction_evidence_loss == 0);
    CHECK(TransactionEvidenceLossCount() == 0);

    AssetTraceSummary summary = {};
    char output[32768];
    ResetSummary(&summary, true, true);
    CaptureLossDomainStats(&summary);
    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(
        output, "transaction_evidence_loss=0"));
    CHECK(std::strstr(
        output, "project_root_bindings=1"));
    CHECK(std::strstr(
        output,
        "first_failure=\"first_missing_premount_boundary\""));
    return true;
}

CampaignStateSnapshot MakeCampaignSnapshot(
    std::uintptr_t selected, std::uint32_t selected_index,
    const char* campaign, const char* rewards,
    const char* respawn) noexcept
{
    CampaignStateSnapshot snapshot = {};
    snapshot.manager = 0x100000U;
    snapshot.selected_campaign = selected;
    snapshot.selected_campaign_index = selected_index;
    snapshot.campaign_table_count = 4;
    snapshot.campaign_id = 17;
    snapshot.campaign_flags = 3;
    snapshot.completeness_flags =
        CampaignSnapshotSelectedIndex |
        CampaignSnapshotCampaignName |
        CampaignSnapshotMaps |
        CampaignSnapshotRewards |
        CampaignSnapshotAfterRespawn |
        CampaignSnapshotFlags |
        CampaignSnapshotComplete;
    std::strncpy(snapshot.campaign_name, campaign,
                 sizeof(snapshot.campaign_name) - 1U);
    std::strncpy(snapshot.rewards, rewards,
                 sizeof(snapshot.rewards) - 1U);
    snapshot.rewards_count = rewards && rewards[0] ? 1U : 0U;
    std::strncpy(snapshot.after_respawn0, respawn,
                 sizeof(snapshot.after_respawn0) - 1U);
    snapshot.after_respawn_count = respawn && respawn[0] ? 1U : 0U;
    return snapshot;
}

void AddCampaignMap(CampaignStateSnapshot* snapshot,
                    const char* map, const char* level,
                    std::int32_t id) noexcept
{
    if (!snapshot ||
        snapshot->map_count >= kMaximumObservedCampaignMaps)
        return;
    BoundedCampaignMap& row =
        snapshot->maps[snapshot->map_count];
    row.registration_order = snapshot->map_count;
    row.map_id = id;
    row.complete = true;
    std::strncpy(row.map_name, map, sizeof(row.map_name) - 1U);
    std::strncpy(row.level_name, level,
                 sizeof(row.level_name) - 1U);
    ++snapshot->map_count;
}

void FinalizeCampaign(CampaignStateSnapshot* snapshot) noexcept
{
    if (snapshot)
        snapshot->logical_fingerprint =
            FingerprintCampaignState(*snapshot);
}

bool CaseCampaignPersistenceFixtures()
{
    CampaignStateSnapshot empty = MakeCampaignSnapshot(
        0x200000U, 1, "workshop", "", "");
    FinalizeCampaign(&empty);
    CampaignStateSnapshot a = MakeCampaignSnapshot(
        0x200000U, 1, "workshop", "reward_a", "respawn_a");
    AddCampaignMap(&a, "wired", "wired", 11);
    FinalizeCampaign(&a);
    CampaignStateSnapshot b = MakeCampaignSnapshot(
        0x200000U, 1, "workshop", "reward_b", "respawn_b");
    AddCampaignMap(&b, "maphell", "maphell", 22);
    FinalizeCampaign(&b);

    CHECK(ClassifyCampaignPersistence(empty, b) ==
          CampaignLoadPersistenceOutcome::ReusedEmptyCampaign);

    CampaignStateSnapshot new_object = b;
    new_object.selected_campaign = 0x300000U;
    CHECK(ClassifyCampaignPersistence(a, new_object) ==
          CampaignLoadPersistenceOutcome::NewCampaignObject);

    CampaignStateSnapshot different_slot = b;
    different_slot.selected_campaign_index = 2;
    CHECK(ClassifyCampaignPersistence(a, different_slot) ==
          CampaignLoadPersistenceOutcome::DifferentSelectedCampaign);

    CampaignStateSnapshot cleared = MakeCampaignSnapshot(
        0x200000U, 1, "workshop", "", "");
    FinalizeCampaign(&cleared);
    CHECK(ClassifyCampaignPersistence(a, cleared) ==
          CampaignLoadPersistenceOutcome::ReusedClearedCampaign);
    CHECK(ClassifyCampaignPersistence(a, b) ==
          CampaignLoadPersistenceOutcome::ReusedReplacedCampaign);

    CampaignStateSnapshot appended = a;
    AddCampaignMap(&appended, "maphell", "maphell", 22);
    std::strcpy(appended.rewards, "reward_b");
    FinalizeCampaign(&appended);
    CHECK(ClassifyCampaignPersistence(a, appended) ==
          CampaignLoadPersistenceOutcome::ReusedAppendedCampaign);

    CampaignStateSnapshot merged = appended;
    merged.maps[0].registration_order = 1;
    merged.maps[1].registration_order = 0;
    const BoundedCampaignMap temporary = merged.maps[0];
    merged.maps[0] = merged.maps[1];
    merged.maps[1] = temporary;
    FinalizeCampaign(&merged);
    CHECK(ClassifyCampaignPersistence(a, merged) ==
          CampaignLoadPersistenceOutcome::ReusedMergedCampaign);

    CampaignStateSnapshot identical = a;
    identical.manager = 0xABC000U;
    identical.selected_campaign = 0xDEF000U;
    CHECK(FingerprintCampaignState(a) ==
          FingerprintCampaignState(identical));
    identical.selected_campaign = a.selected_campaign;
    CHECK(ClassifyCampaignPersistence(a, identical) ==
          CampaignLoadPersistenceOutcome::ReusedMergedCampaign);

    CampaignStateSnapshot incomplete = a;
    incomplete.completeness_flags &= ~CampaignSnapshotMaps;
    incomplete.completeness_flags &= ~CampaignSnapshotComplete;
    CHECK(ClassifyCampaignPersistence(incomplete, b) ==
          CampaignLoadPersistenceOutcome::ObservationIncomplete);
    return true;
}

bool CasePackCrossCheckFixtures()
{
    CHECK(ClassifyPackCrossCheck(
              PackCollectionState::Empty, 0, 0, 0, 0, 0, true) ==
          PackCrossCheckOutcome::ValidatedEmptyCollection);
    CHECK(ClassifyPackCrossCheck(
              PackCollectionState::ReadableNonempty, 2, 2, 2,
              0, 0, true) ==
          PackCrossCheckOutcome::CollectionAndDynamicMatch);
    CHECK(ClassifyPackCrossCheck(
              PackCollectionState::ReadableNonempty, 1, 1, 1,
              1, 0, true) ==
          PackCrossCheckOutcome::DynamicPackMissingFromSnapshot);
    CHECK(ClassifyPackCrossCheck(
              PackCollectionState::ReadableNonempty, 1, 1, 0,
              0, 1, true) ==
          PackCrossCheckOutcome::SnapshotPackWithoutDynamicRelease);
    const PackCollectionState incomplete_states[] = {
        PackCollectionState::CountOutOfBounds,
        PackCollectionState::DataPointerUnreadable,
        PackCollectionState::ElementUnreadable,
        PackCollectionState::LayoutValidationFailed,
        PackCollectionState::SnapshotTruncated,
        PackCollectionState::Unknown,
    };
    for (PackCollectionState state : incomplete_states)
        CHECK(ClassifyPackCrossCheck(
                  state, 1, 0, 0, 0, 0, true) ==
              PackCrossCheckOutcome::LayoutObservationIncomplete);
    CHECK(ClassifyPackCrossCheck(
              PackCollectionState::ReadableNonempty, 1, 1, 1,
              0, 0, false) ==
          PackCrossCheckOutcome::LayoutObservationIncomplete);
    return true;
}

bool CaseCampaignPackAndLedgerSummaryCoverage()
{
    AssetTraceSummary summary;
    char output[32768];
    ResetSummary(&summary, true, true);
    ConfigureSummaryObservers(&summary, true, true);
    ConfigureSummaryLedgerCompleteness(
        &summary, true, true, true, 7, 41, 47,
        "broad_file_capacity");

    AssetTraceEvent before = {};
    before.sequence = 1;
    before.type = AssetTraceEventType::CampaignStateBeforeLoad;
    before.observation_generation = 9;
    before.detail3 = CampaignSnapshotComplete;
    before.object = 0x100000U;
    before.first = 0x200000U;
    std::strcpy(before.text0, "workshop");
    std::strcpy(before.text1, "data\\campaign_custom_workshop.scr");
    ConsumeSummaryEvent(&summary, before);
    AssetTraceEvent after = before;
    after.sequence = 2;
    after.type = AssetTraceEventType::CampaignStateAfterLoad;
    after.content_epoch = 2;
    after.detail0 = 1;
    std::strcpy(after.text2, "X:\\workshop\\venti");
    ConsumeSummaryEvent(&summary, after);
    AssetTraceEvent exit = after;
    exit.sequence = 3;
    exit.type = AssetTraceEventType::CampaignManagerLoadExit;
    exit.detail3 = static_cast<std::uint32_t>(
        CampaignLoadPersistenceOutcome::ReusedReplacedCampaign);
    ConsumeSummaryEvent(&summary, exit);

    AssetTraceEvent pack = {};
    pack.sequence = 4;
    pack.type = AssetTraceEventType::LevelReleasePacksEnter;
    pack.observation_generation = 12;
    pack.content_epoch = 2;
    pack.object = 0x300000U;
    pack.detail1 = 1;
    pack.detail3 = static_cast<std::uint32_t>(
        PackCollectionState::Empty);
    std::strcpy(pack.text0, "maphell");
    ConsumeSummaryEvent(&summary, pack);
    pack.sequence = 5;
    pack.type = AssetTraceEventType::LevelReleasePacksCrossCheck;
    pack.detail3 = static_cast<std::uint32_t>(
        PackCrossCheckOutcome::ValidatedEmptyCollection);
    std::strcpy(pack.text3, "dynamic-observers-complete");
    ConsumeSummaryEvent(&summary, pack);

    CHECK(FormatSummary(summary, output, sizeof(output)));
    CHECK(std::strstr(output, "campaign_loads_observed=1"));
    CHECK(std::strstr(output,
                      "outcome=\"reused_replaced_campaign\""));
    CHECK(std::strstr(output, "pack_releases_observed=1"));
    CHECK(std::strstr(output,
                      "collection_state=\"empty\""));
    CHECK(std::strstr(output,
                      "cross_check=\"validated_empty_collection\""));
    CHECK(std::strstr(output, "ownership_ledger_complete=yes"));
    CHECK(std::strstr(output, "broad_file_ledger_complete=no"));
    CHECK(std::strstr(
        output,
        "ledger_observation_incomplete_domains=\"broad_file_open\""));
    CHECK(std::strstr(output, "broad_file_loss count=7"));
    return true;
}

bool CaseExitGameFinalizerExcludesOnlyCurrentOwner()
{
    using dluma::fixes::custom_map_asset_isolation::
        ClassifyExitGameFinalizationCounts;
    using dluma::fixes::custom_map_asset_isolation::
        ExitGameFinalizationCounts;
    using dluma::fixes::custom_map_asset_isolation::
        ExitGameFinalizationWaitResult;

    ExitGameFinalizationCounts counts = {
        1, 1, 1, 1, 1, 1, 0};
    CHECK(ClassifyExitGameFinalizationCounts(counts) ==
          ExitGameFinalizationWaitResult::Ready);

    counts.hook_active_calls = 2;
    CHECK(ClassifyExitGameFinalizationCounts(counts) ==
          ExitGameFinalizationWaitResult::OtherActiveCalls);
    counts.hook_active_calls = 1;
    counts.consumer_active_calls = 2;
    CHECK(ClassifyExitGameFinalizationCounts(counts) ==
          ExitGameFinalizationWaitResult::OtherActiveCalls);
    counts.consumer_active_calls = 1;
    counts.observed_exit_owner_calls = 2;
    CHECK(ClassifyExitGameFinalizationCounts(counts) ==
          ExitGameFinalizationWaitResult::SelfCountMismatch);
    return true;
}

struct NamedCase {
    const char* name;
    bool (*function)();
};

}  // namespace

int main()
{
    const NamedCase cases[] = {
        {"sequence_and_generation", CaseSequenceAndGeneration},
        {"queue_overflow_is_bounded_and_latched",
         CaseQueueOverflowIsBoundedAndLatched},
        {"optional_flood_preserves_critical_reserve",
         CaseOptionalFloodPreservesCriticalReserve},
        {"critical_overflow_is_independent",
         CaseCriticalOverflowIsIndependent},
        {"readable_strings_fail_closed", CaseReadableStringsFailClosed},
        {"readable_pack_path_uses_borrowed_mock_layout",
         CaseReadablePackPathUsesBorrowedMockLayout},
        {"watched_path_normalization_and_full_match",
         CaseWatchedPathNormalizationAndFullMatch},
        {"event_formatting_escapes_and_names_fields",
         CaseEventFormattingEscapesAndNamesFields},
        {"event_formatting_handles_worst_case_escaping",
         CaseEventFormattingHandlesWorstCaseEscaping},
        {"header_contains_required_identity_and_watches",
         CaseHeaderContainsRequiredIdentityAndWatches},
        {"summary_requires_complete_natural_evidence",
         CaseSummaryRequiresCompleteNaturalEvidence},
        {"summary_does_not_invent_cleanup_completion",
         CaseSummaryDoesNotInventCleanupCompletion},
        {"summary_overflow_and_generation_zero_are_conservative",
         CaseSummaryOverflowAndGenerationZeroAreConservative},
        {"summary_output_incomplete_is_conservative",
         CaseSummaryOutputIncompleteIsConservative},
        {"summary_optional_coverage_is_required_for_completion",
         CaseSummaryOptionalCoverageIsRequiredForCompletion},
        {"summary_pack_pointer_truncation_is_conservative",
         CaseSummaryPackPointerTruncationIsConservative},
        {"summary_handles_bounded_worst_case_escaping",
         CaseSummaryHandlesBoundedWorstCaseEscaping},
        {"frontend_rollback_barrier_events_preserve_order",
         CaseFrontendRollbackBarrierEventsPreserveOrder},
        {"frontend_lookup_before_reset_is_visible",
         CaseFrontendLookupBeforeResetIsVisible},
        {"boundary_publish_commits_exactly_once",
         CaseBoundaryPublishCommitsExactlyOnce},
        {"epoch_native_watched_open_replay",
         CaseEpochNativeWatchedOpenReplay},
        {"parser_outcome_classifier_is_signal_driven",
         CaseParserOutcomeClassifierIsSignalDriven},
        {"parser_provenance_does_not_infer_from_pointer_or_hash",
         CaseParserProvenanceDoesNotInferFromPointerOrHash},
        {"release_label_sources_are_explicit",
         CaseReleaseLabelSourcesAreExplicit},
        {"completeness_is_domain_aware",
         CaseCompletenessIsDomainAware},
        {"optional_overflow_preserves_transaction_evidence",
         CaseOptionalOverflowPreservesTransactionEvidence},
        {"campaign_persistence_fixtures",
         CaseCampaignPersistenceFixtures},
        {"pack_cross_check_fixtures",
         CasePackCrossCheckFixtures},
        {"campaign_pack_and_ledger_summary_coverage",
         CaseCampaignPackAndLedgerSummaryCoverage},
        {"exit_game_finalizer_excludes_only_current_owner",
         CaseExitGameFinalizerExcludesOnlyCurrentOwner},
    };
    for (const NamedCase& item : cases) {
        if (!item.function()) {
            std::fprintf(stderr, "[FAIL] %s\n", item.name);
            return 1;
        }
        std::printf("[PASS] %s\n", item.name);
    }
    std::printf("custom map asset trace core cases passed: %zu\n",
                sizeof(cases) / sizeof(cases[0]));
    return 0;
}
