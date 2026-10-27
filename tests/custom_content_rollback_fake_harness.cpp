#include "fixes/custom_map_asset_isolation/CustomAudioBankLedger.h"
#include "fixes/custom_map_asset_isolation/CustomAudioResetAdapter.h"
#include "fixes/custom_map_asset_isolation/CancelledPresentationCleanupVerifier.h"
#include "fixes/custom_map_asset_isolation/CustomContentLedger.h"
#include "fixes/custom_map_asset_isolation/CustomContentResetRegistry.h"
#include "fixes/custom_map_asset_isolation/FrontendSourceRollbackLifecycle.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace content =
    dluma::fixes::custom_map_asset_isolation::content;
namespace audio =
    dluma::fixes::custom_map_asset_isolation::audio;
namespace lifecycle =
    dluma::fixes::custom_map_asset_isolation::lifecycle;
namespace cancelled_cleanup =
    dluma::fixes::custom_map_asset_isolation::cancelled_cleanup;
namespace vfs =
    dluma::fixes::custom_map_asset_isolation::vfs;

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,   \
                         __LINE__, #expression);                            \
            return false;                                                   \
        }                                                                   \
    } while (0)

static content::CustomContentLedger g_content;
static content::SourceResetSnapshot
    g_source_snapshot[content::kMaximumSources];
static audio::CustomAudioBankLedger g_audio;

static void* FakePointer(std::uintptr_t value) noexcept
{
    return reinterpret_cast<void*>(value);
}

static bool BeginContentEpoch(content::CustomContentLedger* ledger,
                              std::uint32_t* epoch_out = nullptr) noexcept
{
    content::Initialize(ledger);
    char root[content::kSourcePathCapacity];
    if (!content::NormalizePhysicalPath(
            "X:/Workshop/MapA", root, sizeof(root)))
        return false;
    const WCHAR wide_root[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const std::uint32_t epoch = content::BeginEpoch(
        ledger, "MapA", wide_root, root, 1);
    if (epoch_out) *epoch_out = epoch;
    return epoch != 0;
}

static bool CasePathNormalizationAndSourceClassification()
{
    char root[content::kSourcePathCapacity];
    char path[content::kSourcePathCapacity];
    CHECK(content::NormalizePhysicalPath(
        "X:/Workshop//MapA/", root, sizeof(root)));
    CHECK(std::strcmp(root, "x:\\workshop\\mapa") == 0);
    CHECK(content::NormalizeLogicalPath(
        "//DATA//Scripts/Foo.SCR", path, sizeof(path)));
    CHECK(std::strcmp(path, "data\\scripts\\foo.scr") == 0);
    CHECK(content::PathsEqual("X:/WORKSHOP/MapA/Data/",
                              "x:\\workshop\\mapa\\data"));
    CHECK(content::PathWithinRoot("X:/Workshop/MapA/Data", root));
    CHECK(!content::PathWithinRoot("X:/Workshop/MapAB/Data", root));
    char rejected[256];
    CHECK(!content::NormalizePhysicalPath(
        "X:/Workshop/MapA/../MapB/Data", rejected, sizeof(rejected)));
    CHECK(!content::NormalizeLogicalPath(
        "data/scripts/./unsafe.scr", rejected, sizeof(rejected)));

    struct ClassificationCase {
        const char* path;
        content::CustomSourceKind expected;
    };
    const ClassificationCase cases[] = {
        {"x:\\workshop\\mapa",
         content::CustomSourceKind::ProjectRoot},
        {"x:\\workshop\\mapa\\data",
         content::CustomSourceKind::DataDirectory},
        {"x:\\workshop\\mapa\\dataworkshop_0.pak",
         content::CustomSourceKind::DataWorkshopPak},
        {"x:\\workshop\\mapa\\dataen",
         content::CustomSourceKind::LocaleDirectory},
        {"x:\\workshop\\mapa\\dataen.pak",
         content::CustomSourceKind::LocalePak},
        {"x:\\workshop\\mapa\\speechen",
         content::CustomSourceKind::SpeechDirectory},
        {"x:\\workshop\\mapa\\speechen.pak",
         content::CustomSourceKind::SpeechPak},
        {"x:\\workshop\\mapa\\assets_pc",
         content::CustomSourceKind::AssetsPc},
        {"x:\\workshop\\mapa\\unclassified.pak",
         content::CustomSourceKind::UnknownProjectSource},
        {"x:\\workshop\\other\\data",
         content::CustomSourceKind::UnknownProjectSource},
    };
    for (const ClassificationCase& item : cases)
        CHECK(content::ClassifySourceKind(item.path, root) ==
              item.expected);
    return true;
}

static bool CaseExplicitEpochAttributionCannotDrift()
{
    content::Initialize(&g_content);
    const WCHAR root_a[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const WCHAR root_b[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'B', 0};
    const std::uint32_t first = content::BeginEpoch(
        &g_content, "ProjectA", root_a,
        "x:\\workshop\\mapa", 1);
    CHECK(first != 0);
    const std::uint32_t second = content::BeginEpoch(
        &g_content, "ProjectB", root_b,
        "x:\\workshop\\mapb", 2);
    CHECK(second != 0 && second != first);
    CHECK(!content::RecordSuccessfulSourceAddForEpoch(
        &g_content, first, FakePointer(0x1100), "map-a-data",
        "x:\\workshop\\mapa\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 3));
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, second, FakePointer(0x1200), "map-b-data",
        "x:\\workshop\\mapb\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 4));
    CHECK(g_content.source_count == 1);
    CHECK(g_content.sources[0].epoch == second);
    return true;
}

static bool CaseCurrentEpochReadDistinguishesZeroFromContention()
{
    content::Initialize(&g_content);
    std::uint32_t observed = 99;
    CHECK(content::TryGetCurrentEpochId(&g_content, &observed));
    CHECK(observed == 0);

    const WCHAR root[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const std::uint32_t epoch = content::BeginEpoch(
        &g_content, "ProjectA", root, "x:\\workshop\\mapa", 1);
    CHECK(epoch != 0);
    CHECK(content::TryGetCurrentEpochId(&g_content, &observed));
    CHECK(observed == epoch);

    dluma_atomic_exchange(&g_content.lock, 1);
    observed = 77;
    CHECK(!content::TryGetCurrentEpochId(&g_content, &observed));
    CHECK(observed == 77);
    dluma_atomic_exchange(&g_content.lock, 0);
    CHECK(!content::TryGetCurrentEpochId(&g_content, nullptr));
    return true;
}

static bool CaseCompleteSnapshotRejectsDroppedOwnership()
{
    CHECK(BeginContentEpoch(&g_content));
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, 1, FakePointer(0x1300), "map-a-data",
        "x:\\workshop\\mapa\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 2));
    content::SourceSnapshotResult snapshot =
        content::SnapshotActiveSourcesThroughEpochComplete(
            &g_content, 1, g_source_snapshot,
            content::kMaximumSources, 3);
    CHECK(snapshot.complete);
    CHECK(snapshot.count == 1);
    CHECK(snapshot.expected_active_count == 1);
    content::RecordOwnershipMutationLoss(
        &g_content, content::LedgerLossReason::SourceAddContention);
    snapshot = content::SnapshotActiveSourcesThroughEpochComplete(
        &g_content, 1, g_source_snapshot,
        content::kMaximumSources, 4);
    CHECK(!snapshot.complete);
    CHECK(snapshot.dropped_mutations != 0);
    return true;
}

static bool CasePackPointerReuseCreatesNewestRecord()
{
    CHECK(BeginContentEpoch(&g_content));
    void* const reused = FakePointer(0x1400);
    const char* const pack =
        "x:\\workshop\\mapa\\assets_pc\\shared.rpack";
    CHECK(content::RecordPackLoad(&g_content, reused, pack, 2));
    CHECK(content::RecordPackRelease(&g_content, reused, nullptr, 3));
    CHECK(content::RecordPackLoad(&g_content, reused, pack, 4));
    CHECK(g_content.pack_count == 2);
    CHECK(g_content.packs[0].released_naturally);
    CHECK(!g_content.packs[1].released_naturally);
    CHECK(content::RecordPackRelease(&g_content, reused, nullptr, 5));
    CHECK(g_content.packs[1].released_naturally);
    CHECK(g_content.packs[1].release_sequence == 5);
    return true;
}

static bool CaseCustomSourceExclusionsAreNonDestructive()
{
    CHECK(BeginContentEpoch(&g_content));
    CHECK(!content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x1000), "X:\\Base\\Data",
        "x:\\base\\data", 1,
        content::CustomSourceKind::UnknownProjectSource, true, 2));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x1001),
        "X:\\Workshop\\MapA\\LoaderOwnedButExcluded",
        "x:\\workshop\\mapa\\loaderownedbutexcluded", 1,
        content::CustomSourceKind::UnknownProjectSource, false, 3));
    CHECK(content::CountActiveCustomSources(&g_content) == 0);
    CHECK(content::SnapshotActiveSourcesForReset(
              &g_content, g_source_snapshot,
              content::kMaximumSources, 4) == 0);
    std::uint32_t epoch = 999;
    CHECK(!content::IsTrackedCustomSource(
        &g_content, FakePointer(0x1001), nullptr, &epoch));
    CHECK(epoch == 0);

    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x1002),
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 2,
        content::CustomSourceKind::DataDirectory, true, 5));
    CHECK(content::CountActiveCustomSources(&g_content) == 1);
    CHECK(content::IsTrackedCustomSource(
        &g_content, FakePointer(0x1002), nullptr, &epoch));
    CHECK(epoch == 1);
    return true;
}

static bool CaseSourceLedgerIsBounded()
{
    CHECK(BeginContentEpoch(&g_content));
    char path[256];
    for (std::uint32_t index = 0;
         index < content::kMaximumSources; ++index) {
        const int written = std::snprintf(
            path, sizeof(path),
            "x:\\workshop\\mapa\\source_%03u", index);
        CHECK(written > 0 &&
              static_cast<std::size_t>(written) < sizeof(path));
        CHECK(content::RecordSuccessfulSourceAdd(
            &g_content, FakePointer(0x2000U + index), path, path,
            index, content::CustomSourceKind::UnknownProjectSource,
            true, 2U + index));
    }
    CHECK(!content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x9999),
        "x:\\workshop\\mapa\\overflow",
        "x:\\workshop\\mapa\\overflow", 0,
        content::CustomSourceKind::UnknownProjectSource, true, 999));
    CHECK(g_content.source_count == content::kMaximumSources);
    CHECK(g_content.ownership_mutation_loss > 0);
    return true;
}

static bool CaseFileLedgerDeduplicatesAndIsBounded()
{
    CHECK(BeginContentEpoch(&g_content));
    CHECK(content::RecordCustomFileOpen(
        &g_content, 1, "DATA/Scripts/Duplicate.scr", "custom-a",
        FakePointer(0x3000), 0x180001000ULL, 0x1000,
        "gamedll_x64_rwdi.dll", 11, 2));
    CHECK(content::RecordCustomFileOpen(
        &g_content, 1, "data\\scripts\\duplicate.scr", "custom-a",
        FakePointer(0x3000), 0x180001000ULL, 0x1000,
        "gamedll_x64_rwdi.dll", 11, 3));
    CHECK(g_content.file_count == 1);
    CHECK(g_content.files[0].open_count == 2);
    CHECK(g_content.files[0].first_open_sequence == 2);
    CHECK(g_content.files[0].last_open_sequence == 3);
    CHECK(content::RecordCustomFileOpen(
        &g_content, 1, "data\\scripts\\duplicate.scr", "custom-b",
        FakePointer(0x3001), 0x180001000ULL, 0x1000,
        "gamedll_x64_rwdi.dll", 11, 4));
    CHECK(g_content.file_count == 1);
    CHECK(g_content.files[0].open_count == 3);
    CHECK(g_content.files[0].provider_change_count == 1);
    CHECK(g_content.files[0].source ==
          reinterpret_cast<std::uintptr_t>(FakePointer(0x3000)));
    CHECK(g_content.files[0].last_source ==
          reinterpret_cast<std::uintptr_t>(FakePointer(0x3001)));
    CHECK(std::strcmp(g_content.files[0].resolved_source_path,
                      g_content.files[0].last_resolved_source_path) != 0);
    CHECK(!g_content.files[0].classification_complete);

    content::Initialize(&g_content);
    char path[256];
    for (std::uint32_t index = 0;
         index < content::kMaximumFiles; ++index) {
        const int written = std::snprintf(
            path, sizeof(path), "data\\scripts\\file_%03u.scr",
            index);
        CHECK(written > 0 &&
              static_cast<std::size_t>(written) < sizeof(path));
        CHECK(content::RecordCustomFileOpen(
            &g_content, 7, path, "custom-a", FakePointer(0x3000),
            0x180002000ULL + index, 0x2000U + index,
            "gamedll_x64_rwdi.dll", 12, index + 1U));
    }
    CHECK(!content::RecordCustomFileOpen(
        &g_content, 7, "data\\scripts\\overflow.scr", "custom-a",
        FakePointer(0x3000), 0x180009999ULL, 0x9999,
        "gamedll_x64_rwdi.dll", 12, 999));
    CHECK(g_content.file_count == content::kMaximumFiles);
    CHECK(g_content.diagnostic_observation_loss > 0);
    CHECK(g_content.ownership_mutation_loss == 0);
    return true;
}

static bool CaseSourceResetUsesReverseSuccessfulAddOrder()
{
    /* A complete cancelled presentation removes AssetsPc, DataDirectory,
     * then ProjectRoot in strict reverse successful-add order. */
    CHECK(BeginContentEpoch(&g_content));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4100), "mapa", "x:\\workshop\\mapa",
        1, content::CustomSourceKind::ProjectRoot, true, 2, true));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4200), "mapa-data",
        "x:\\workshop\\mapa\\data", 2,
        content::CustomSourceKind::DataDirectory, true, 3, true));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4300), "mapa-assets",
        "x:\\workshop\\mapa\\assets_pc", 3,
        content::CustomSourceKind::AssetsPc, true, 4, true));
    std::size_t count = content::SnapshotActiveSourcesForReset(
        &g_content, g_source_snapshot, content::kMaximumSources, 5);
    CHECK(count == 3);
    CHECK(g_source_snapshot[0].source == FakePointer(0x4300));
    CHECK(g_source_snapshot[1].source == FakePointer(0x4200));
    CHECK(g_source_snapshot[2].source == FakePointer(0x4100));
    CHECK(g_source_snapshot[0].path_created_ownership);
    CHECK(g_source_snapshot[1].path_created_ownership);
    CHECK(g_source_snapshot[2].path_created_ownership);
    CHECK(g_source_snapshot[0].successful_add_ordinal >
          g_source_snapshot[1].successful_add_ordinal);
    CHECK(g_source_snapshot[1].successful_add_ordinal >
          g_source_snapshot[2].successful_add_ordinal);
    CHECK(content::MarkSourceResetResult(
        &g_content, g_source_snapshot[0].record_index, true, 6));
    CHECK(content::MarkSourceResetResult(
        &g_content, g_source_snapshot[1].record_index, true, 7));
    CHECK(content::MarkSourceResetResult(
        &g_content, g_source_snapshot[2].record_index, true, 8));
    CHECK(content::CountActiveCustomSources(&g_content) == 0);

    /* Natural ProjectRoot/DataDirectory removal does not make
     * SourceSetComplete synonymous with inactivity: the exact AssetsPc
     * object remains in the cleanup snapshot. */
    CHECK(BeginContentEpoch(&g_content));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4500), "mapa", "x:\\workshop\\mapa",
        1, content::CustomSourceKind::ProjectRoot, true, 2, true));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4600), "mapa-data",
        "x:\\workshop\\mapa\\data", 2,
        content::CustomSourceKind::DataDirectory, true, 3, true));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4700), "mapa-assets",
        "x:\\workshop\\mapa\\assets_pc", 3,
        content::CustomSourceKind::AssetsPc, true, 4, true));
    CHECK(content::RecordNaturalSourceRemoval(
        &g_content, FakePointer(0x4500), nullptr, true, 5));
    CHECK(content::RecordNaturalSourceRemoval(
        &g_content, FakePointer(0x4600), nullptr, true, 6));
    count = content::SnapshotActiveSourcesForReset(
        &g_content, g_source_snapshot, content::kMaximumSources, 7);
    CHECK(count == 1);
    CHECK(g_source_snapshot[0].source == FakePointer(0x4700));
    CHECK(g_source_snapshot[0].kind ==
          content::CustomSourceKind::AssetsPc);
    CHECK(content::MarkSourceResetResult(
        &g_content, g_source_snapshot[0].record_index, true, 8));
    CHECK(content::CountActiveCustomSources(&g_content) == 0);
    return true;
}

static bool CaseCancelledCleanupVerifiesLiveRegistryExactMinusSources()
{
    vfs::FilesystemSourceInventory before = {};
    vfs::FilesystemSourceInventory after = {};
    before.complete = true;
    before.fallback_state_known = true;
    before.source_count = 4;
    const std::uintptr_t sources[] = {
        0x5100U, 0x5200U, 0x5300U, 0x5400U};
    const std::uintptr_t links[] = {
        0x6100U, 0x6200U, 0x6300U, 0x6400U};
    const char* paths[] = {
        "x:\\workshop\\loader\\mods\\dluma.pak",
        "x:\\workshop\\mapa",
        "x:\\workshop\\mapa\\data",
        "x:\\workshop\\mapa\\assets_pc",
    };
    for (std::uint32_t index = 0; index < before.source_count; ++index) {
        before.sources[index].source = sources[index];
        before.sources[index].link = links[index];
        const std::size_t length = std::strlen(paths[index]);
        CHECK(length + 1U <= sizeof(before.sources[index].path));
        std::memcpy(
            before.sources[index].path, paths[index], length + 1U);
    }
    after.complete = true;
    after.fallback_state_known = true;
    after.source_count = 1;
    after.sources[0] = before.sources[0];
    content::SourceResetSnapshot removed[3] = {};
    removed[0].source = FakePointer(sources[3]);
    removed[1].source = FakePointer(sources[2]);
    removed[2].source = FakePointer(sources[1]);
    CHECK(cancelled_cleanup::VerifyFilesystemInventoryExactMinusSources(
        &before, &after, removed, 3));

    /* Ledger-side inactive flags cannot replace a live-registry proof. */
    vfs::FilesystemSourceInventory stale_live = before;
    CHECK(!cancelled_cleanup::VerifyFilesystemInventoryExactMinusSources(
        &before, &stale_live, removed, 3));

    /* Loader replacement or mutation is not an exact-minus result. */
    vfs::FilesystemSourceInventory replacement = after;
    replacement.sources[0].source = 0x7100U;
    replacement.sources[0].link = 0x7200U;
    CHECK(!cancelled_cleanup::VerifyFilesystemInventoryExactMinusSources(
        &before, &replacement, removed, 3));
    replacement = after;
    replacement.sources[0].path[3] = 'x';
    CHECK(!cancelled_cleanup::VerifyFilesystemInventoryExactMinusSources(
        &before, &replacement, removed, 3));

    removed[2].source = removed[1].source;
    CHECK(!cancelled_cleanup::VerifyFilesystemInventoryExactMinusSources(
        &before, &after, removed, 3));
    return true;
}

static bool CaseClosedEpochRejectsLateFileOwnership()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4A00),
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 2));
    CHECK(content::CloseEpochForFrontend(&g_content, epoch, 3));
    std::uint32_t observed_epoch = 0;
    bool rejected_closed = false;
    CHECK(!content::IsTrackedCustomSource(
        &g_content, FakePointer(0x4A00), nullptr, &observed_epoch,
        &rejected_closed));
    CHECK(observed_epoch == 0);
    CHECK(rejected_closed);
    CHECK(g_content.diagnostic_observation_loss > 0);
    return true;
}

static bool CaseAmbiguousPathRemovalCannotSelectAnOwner()
{
    CHECK(BeginContentEpoch(&g_content));
    const char* path = "x:\\workshop\\mapa\\data";
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, nullptr, path, path, 1,
        content::CustomSourceKind::UnknownProjectSource, true, 2));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, nullptr, path, path, 2,
        content::CustomSourceKind::UnknownProjectSource, true, 3));
    CHECK(!content::RecordNaturalSourceRemoval(
        &g_content, nullptr, path, true, 4));
    CHECK(content::CountActiveCustomSources(&g_content) == 2);

    content::Initialize(&g_content);
    CHECK(BeginContentEpoch(&g_content));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4B00), path, path, 1,
        content::CustomSourceKind::UnknownProjectSource, true, 2));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x4B01), path, path, 2,
        content::CustomSourceKind::UnknownProjectSource, true, 3));
    std::uint32_t epoch = 0;
    CHECK(content::IsTrackedCustomSource(
        &g_content, FakePointer(0x4B01), path, &epoch));
    CHECK(epoch == 1);
    CHECK(content::RecordNaturalSourceRemoval(
        &g_content, FakePointer(0x4B01), path, true, 4));
    CHECK(content::CountActiveCustomSources(&g_content) == 1);
    CHECK(g_content.sources[0].active);
    CHECK(!g_content.sources[1].active);
    CHECK(!content::IsTrackedCustomSource(
        &g_content, FakePointer(0x4B02), path, &epoch));
    CHECK(g_content.diagnostic_observation_loss > 0);
    return true;
}

static bool CaseDuplicateActiveSourceKindFailsClosed()
{
    const content::CustomSourceKind singleton_kinds[] = {
        content::CustomSourceKind::ProjectRoot,
        content::CustomSourceKind::DataDirectory,
        content::CustomSourceKind::AssetsPc,
    };
    for (content::CustomSourceKind kind : singleton_kinds) {
        content::Initialize(&g_content);
        CHECK(BeginContentEpoch(&g_content));
        const char* path = "x:\\workshop\\mapa\\data";
        CHECK(
            content::RecordSuccessfulSourceAddForEpochDetailed(
                &g_content, 1U, FakePointer(0x4C00), path, path, 1,
                kind, true, 2) ==
            content::SourceAddRecordResult::Recorded);
        CHECK(
            content::RecordSuccessfulSourceAddForEpochDetailed(
                &g_content, 1U, FakePointer(0x4C01), path, path, 2,
                kind, true, 3) ==
            content::SourceAddRecordResult::
                DuplicateActiveKindRecorded);
        CHECK(content::CountActiveCustomSources(&g_content) == 2U);
        CHECK(g_content.ownership_mutation_loss != 0U);
        CHECK(!g_content.epochs[0].ownership_complete);
        CHECK(g_content.epochs[0].ownership_mutation_lost);
    }

    /* Returning to the same normalized root under a fresh epoch cannot hide
     * a still-active AssetsPc source from the earlier presentation. */
    content::Initialize(&g_content);
    const WCHAR root_a[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const WCHAR root_b[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'B', 0};
    const std::uint32_t first_epoch = content::BeginEpoch(
        &g_content, "MapA", root_a, "x:\\workshop\\mapa", 1);
    CHECK(first_epoch != 0);
    CHECK(content::RecordSuccessfulSourceAddForEpochDetailed(
              &g_content, first_epoch, FakePointer(0x4D00),
              "x:\\workshop\\mapa\\assets_pc",
              "x:\\workshop\\mapa\\assets_pc", 1,
              content::CustomSourceKind::AssetsPc, true, 2, true) ==
          content::SourceAddRecordResult::Recorded);
    CHECK(content::BeginEpoch(
        &g_content, "MapB", root_b, "x:\\workshop\\mapb", 3));
    const std::uint32_t fresh_same_root = content::BeginEpoch(
        &g_content, "MapA", root_a, "x:\\workshop\\mapa", 4);
    CHECK(fresh_same_root != 0 && fresh_same_root != first_epoch);
    CHECK(content::RecordSuccessfulSourceAddForEpochDetailed(
              &g_content, fresh_same_root, FakePointer(0x4D01),
              "x:\\workshop\\mapa\\assets_pc",
              "x:\\workshop\\mapa\\assets_pc", 1,
              content::CustomSourceKind::AssetsPc, true, 5, true) ==
          content::SourceAddRecordResult::
              DuplicateActiveKindRecorded);
    CHECK(g_content.ownership_mutation_loss != 0U);
    return true;
}

static bool CasePresentationSourceSetCanonicalJoinAndReasons()
{
    content::Initialize(&g_content);
    const WCHAR wide_root[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const std::uint32_t epoch = content::BeginEpoch(
        &g_content, "MapA", wide_root,
        "\\\\?\\X:\\WORKSHOP\\MapA\\", 1);
    CHECK(epoch != 0U);
    CHECK(std::strcmp(
              g_content.epochs[0].normalized_project_root,
              "x:\\workshop\\mapa") == 0);

    struct RequiredSource {
        void* source;
        const char* path;
        content::CustomSourceKind kind;
    };
    const RequiredSource sources[] = {
        {FakePointer(0x4E00), "X:/Workshop/MapA",
         content::CustomSourceKind::ProjectRoot},
        {FakePointer(0x4E01), "\\?\\X:\\WORKSHOP\\MapA\\Data",
         content::CustomSourceKind::DataDirectory},
        {FakePointer(0x4E02), "//?/X:/workshop/mapa/assets_pc/",
         content::CustomSourceKind::AssetsPc},
    };
    for (std::size_t index = 0;
         index < sizeof(sources) / sizeof(sources[0]); ++index) {
        CHECK(content::RecordSuccessfulSourceAddForEpochDetailed(
                  &g_content, epoch, sources[index].source,
                  sources[index].path, sources[index].path,
                  static_cast<std::uint32_t>(index),
                  sources[index].kind, true, 2U + index, true) ==
              content::SourceAddRecordResult::Recorded);
    }

    content::PresentationSourceSetSnapshot snapshot = {};
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch, "X:\\WORKSHOP\\MapA\\",
              &snapshot) ==
          content::PresentationSourceSetSnapshotResult::Succeeded);
    CHECK(snapshot.complete);
    CHECK(snapshot.root_equal);
    CHECK(snapshot.ownership_complete);
    CHECK(snapshot.exact_handles_complete);
    CHECK(snapshot.active_source_count == 3U);
    CHECK(snapshot.project_root_count == 1U);
    CHECK(snapshot.data_directory_count == 1U);
    CHECK(snapshot.assets_pc_count == 1U);
    CHECK(std::strcmp(
              snapshot.presenter_root_canonical,
              "x:\\workshop\\mapa") == 0);
    CHECK(std::strcmp(
              snapshot.epoch_root_canonical,
              "x:\\workshop\\mapa") == 0);

    const char* equivalent_roots[] = {
        "X:/WORKSHOP/MapA",
        "\\\\?\\X:\\Workshop\\MapA",
        "\\?\\X:\\Workshop\\MapA",
        "//?/X:/Workshop/MapA/",
    };
    for (const char* equivalent_root : equivalent_roots) {
        CHECK(content::SnapshotPresentationSourceSet(
                  &g_content, epoch, equivalent_root, &snapshot) ==
              content::PresentationSourceSetSnapshotResult::Succeeded);
    }

    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch, "X:\\Workshop\\MapB", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::
              ProjectRootMismatch);
    CHECK(!snapshot.complete);
    CHECK(!snapshot.root_equal);
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch,
              "X:\\Workshop\\MapA\\..\\MapB", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::
              ProjectRootInvalid);

    dluma_atomic_exchange(&g_content.lock, 1);
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch, "x:\\workshop\\mapa", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::LedgerBusy);
    dluma_atomic_exchange(&g_content.lock, 0);

    g_content.sources[2].exact_handle_available = false;
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch, "x:\\workshop\\mapa", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::
              ExactHandleIncomplete);
    g_content.sources[2].exact_handle_available = true;
    content::RecordOwnershipMutationLoss(
        &g_content,
        content::LedgerLossReason::SourceIdentityIncomplete);
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch, "x:\\workshop\\mapa", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::
              OwnershipIncomplete);

    CHECK(content::RecordSuccessfulSourceAddForEpochDetailed(
              &g_content, epoch, FakePointer(0x4E03),
              "x:\\workshop\\mapa\\assets_pc",
              "x:\\workshop\\mapa\\assets_pc", 3,
              content::CustomSourceKind::AssetsPc, true, 10, true) ==
          content::SourceAddRecordResult::DuplicateActiveKindRecorded);
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, epoch, "x:\\workshop\\mapa", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::
              DuplicateSourceKind);
    CHECK(snapshot.assets_pc_count == 2U);

    content::Initialize(&g_content);
    const std::uint32_t incomplete_epoch = content::BeginEpoch(
        &g_content, "MapA", wide_root, "x:\\workshop\\mapa", 20);
    CHECK(incomplete_epoch != 0U);
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, incomplete_epoch,
              "x:\\workshop\\mapa", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::
              RequiredSourceKindMissing);
    CHECK(content::SnapshotPresentationSourceSet(
              &g_content, incomplete_epoch + 1U,
              "x:\\workshop\\mapa", &snapshot) ==
          content::PresentationSourceSetSnapshotResult::EpochMissing);

    CHECK(std::strcmp(
              content::PresentationSourceSetSnapshotResultName(
                  content::PresentationSourceSetSnapshotResult::Succeeded),
              "Succeeded") == 0);
    CHECK(std::strcmp(
              content::PresentationSourceSetSnapshotResultName(
                  content::PresentationSourceSetSnapshotResult::LedgerBusy),
              "LedgerBusy") == 0);
    CHECK(std::strcmp(
              content::PresentationSourceSetSnapshotResultName(
                  content::PresentationSourceSetSnapshotResult::
                      ProjectRootMismatch),
              "ProjectRootMismatch") == 0);
    CHECK(std::strcmp(
              content::PresentationSourceSetSnapshotResultName(
                  content::PresentationSourceSetSnapshotResult::
                      DuplicateSourceKind),
              "DuplicateSourceKind") == 0);
    return true;
}

static bool CaseUnknownPersistentContentBlocksBaselineMatch()
{
    CHECK(BeginContentEpoch(&g_content));
    CHECK(content::MarkBaselineCaptured(&g_content, true, 2));
    CHECK(content::RecordSuccessfulSourceAdd(
        &g_content, FakePointer(0x5100), "mapa-data",
        "x:\\workshop\\mapa\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 3));
    CHECK(content::RecordCustomFileOpen(
        &g_content, 1, "data\\skills\\default_levels.xml",
        "x:\\workshop\\mapa\\data", FakePointer(0x5100),
        0x180003000ULL, 0x3000, "gamedll_x64_rwdi.dll", 13, 4));
    CHECK(content::CloseEpochForFrontend(&g_content, 5));
    CHECK(content::MarkSourceResetResult(&g_content, 0, true, 6));
    CHECK(content::CountUnknownPersistentFiles(&g_content) == 1);
    CHECK(!content::CanReportBaselineMatch(&g_content));

    CHECK(content::ClassifyFile(
        &g_content, 0, content::ContentLifetimeClass::GlobalDataManager,
        0, true, 7));
    CHECK(!content::CanReportBaselineMatch(&g_content));
    CHECK(content::ClassifyFile(
        &g_content, 0, content::ContentLifetimeClass::GlobalDataManager,
        42, true, 8));
    CHECK(content::CanReportBaselineMatch(&g_content));

    CHECK(content::RecordPackLoad(
        &g_content, FakePointer(0x5200),
        "x:\\workshop\\mapa\\assets_pc\\stale.rpack", 9));
    CHECK(g_content.packs[0].observed_after_frontend_reset);
    CHECK(!content::CanReportBaselineMatch(&g_content));
    return true;
}

struct FakeResetAdapterState {
    content::ContentLifetimeClass supported_lifetime;
    content::ContentResetResult capture_result;
    content::ContentResetResult prepare_result;
    content::ContentResetResult reset_result;
    content::ContentResetResult verify_result;
    std::uint32_t capture_calls;
    std::uint32_t prepare_calls;
    std::uint32_t reset_calls;
    std::uint32_t verify_calls;
};

static bool FakeSupports(
    void* user, const content::CustomFileLoadRecord* record) noexcept
{
    const FakeResetAdapterState* state =
        static_cast<const FakeResetAdapterState*>(user);
    return state && record && record->lifetime == state->supported_lifetime;
}

static content::ContentResetResult FakeCapture(
    void* user, const content::ContentResetContext*) noexcept
{
    FakeResetAdapterState* state =
        static_cast<FakeResetAdapterState*>(user);
    ++state->capture_calls;
    return state->capture_result;
}

static content::ContentResetResult FakePrepare(
    void* user, const content::ContentResetContext*) noexcept
{
    FakeResetAdapterState* state =
        static_cast<FakeResetAdapterState*>(user);
    ++state->prepare_calls;
    return state->prepare_result;
}

static content::ContentResetResult FakeReset(
    void* user, const content::ContentResetContext*) noexcept
{
    FakeResetAdapterState* state =
        static_cast<FakeResetAdapterState*>(user);
    ++state->reset_calls;
    return state->reset_result;
}

static content::ContentResetResult FakeVerify(
    void* user, const content::ContentResetContext*) noexcept
{
    FakeResetAdapterState* state =
        static_cast<FakeResetAdapterState*>(user);
    ++state->verify_calls;
    return state->verify_result;
}

static content::CustomContentResetAdapter MakeResetAdapter(
    std::uint32_t id, const char* name,
    content::ContentResetSemantics semantics,
    FakeResetAdapterState* state) noexcept
{
    content::CustomContentResetAdapter adapter = {};
    adapter.id = id;
    std::size_t index = 0;
    while (name[index] && index + 1 < sizeof(adapter.name)) {
        adapter.name[index] = name[index];
        ++index;
    }
    adapter.name[index] = 0;
    adapter.semantics = semantics;
    adapter.enabled = true;
    adapter.destructive_owner_validated = true;
    adapter.user = state;
    adapter.supports = FakeSupports;
    adapter.capture_baseline = FakeCapture;
    adapter.prepare_for_reset = FakePrepare;
    adapter.reset_to_baseline = FakeReset;
    adapter.verify_baseline = FakeVerify;
    return adapter;
}

static FakeResetAdapterState RestoringAdapterState(
    content::ContentLifetimeClass lifetime) noexcept
{
    FakeResetAdapterState state = {};
    state.supported_lifetime = lifetime;
    state.capture_result = content::ContentResetResult::Restored;
    state.prepare_result = content::ContentResetResult::Restored;
    state.reset_result = content::ContentResetResult::Restored;
    state.verify_result = content::ContentResetResult::Restored;
    return state;
}

static bool CaseResetRegistryValidatesAllSemantics()
{
    FakeResetAdapterState state = RestoringAdapterState(
        content::ContentLifetimeClass::GlobalDataManager);
    content::CustomContentResetAdapter adapter = MakeResetAdapter(
        1, "replace", content::ContentResetSemantics::Replace, &state);
    CHECK(content::ValidateAdapterDefinition(&adapter));

    adapter.semantics = content::ContentResetSemantics::Merge;
    CHECK(!content::ValidateAdapterDefinition(&adapter));
    adapter.authoritative_clear_available = true;
    CHECK(content::ValidateAdapterDefinition(&adapter));

    adapter.authoritative_clear_available = false;
    adapter.semantics = content::ContentResetSemantics::OneShot;
    CHECK(!content::ValidateAdapterDefinition(&adapter));
    adapter.authoritative_reinitialize_available = true;
    CHECK(content::ValidateAdapterDefinition(&adapter));

    adapter.authoritative_reinitialize_available = false;
    adapter.semantics = content::ContentResetSemantics::Snapshot;
    CHECK(!content::ValidateAdapterDefinition(&adapter));
    adapter.logical_snapshot_available = true;
    CHECK(content::ValidateAdapterDefinition(&adapter));
    return true;
}

static bool CaseResetRegistryDispatchesDeterministically()
{
    content::CustomContentResetRegistry registry;
    content::Initialize(&registry);
    FakeResetAdapterState state = RestoringAdapterState(
        content::ContentLifetimeClass::GlobalDataManager);
    content::CustomContentResetAdapter adapter = MakeResetAdapter(
        7, "default-levels", content::ContentResetSemantics::Replace,
        &state);
    CHECK(content::RegisterAdapter(&registry, &adapter));
    content::SealRegistry(&registry);
    CHECK(content::CaptureBaselines(&registry) ==
          content::ContentResetResult::Restored);

    content::CustomFileLoadRecord records[2] = {};
    records[0].lifetime = content::ContentLifetimeClass::GlobalDataManager;
    records[1].lifetime = content::ContentLifetimeClass::SourceOnly;
    const content::ContentResetContext context = {1, records, 2};
    CHECK(content::PrepareForReset(&registry, &context) ==
          content::ContentResetResult::Restored);
    CHECK(content::ResetToBaseline(&registry, &context) ==
          content::ContentResetResult::Restored);
    CHECK(content::VerifyBaseline(&registry, &context) ==
          content::ContentResetResult::Restored);
    CHECK(state.capture_calls == 1 && state.prepare_calls == 1 &&
          state.reset_calls == 1 && state.verify_calls == 1);

    content::ContentResetAdapterState snapshot[1] = {};
    CHECK(content::SnapshotAdapterStates(
              &registry, snapshot, 1) == 1);
    CHECK(snapshot[0].matched_records == 1);
    CHECK(snapshot[0].capture_result ==
          content::ContentResetResult::Restored);
    CHECK(snapshot[0].prepare_result ==
          content::ContentResetResult::Restored);
    CHECK(snapshot[0].reset_result ==
          content::ContentResetResult::Restored);
    CHECK(snapshot[0].verify_result ==
          content::ContentResetResult::Restored);
    return true;
}

static bool CaseResetRegistryRejectsUnknownOwnerAndAmbiguity()
{
    content::CustomContentResetRegistry registry;
    content::Initialize(&registry);
    FakeResetAdapterState first = RestoringAdapterState(
        content::ContentLifetimeClass::GlobalDataManager);
    FakeResetAdapterState second = first;
    content::CustomContentResetAdapter adapter1 = MakeResetAdapter(
        10, "unknown-owner", content::ContentResetSemantics::Replace,
        &first);
    adapter1.destructive_owner_validated = false;
    content::CustomContentResetAdapter adapter2 = MakeResetAdapter(
        11, "ambiguous-owner", content::ContentResetSemantics::Replace,
        &second);
    CHECK(content::RegisterAdapter(&registry, &adapter1));
    CHECK(content::RegisterAdapter(&registry, &adapter2));
    content::SealRegistry(&registry);

    content::CustomFileLoadRecord record = {};
    record.lifetime = content::ContentLifetimeClass::GlobalDataManager;
    std::uint32_t adapter_id = 99;
    CHECK(!content::FindSupportingAdapter(
        &registry, &record, &adapter_id));
    CHECK(adapter_id == 0);
    CHECK(content::CaptureBaselines(&registry) ==
          content::ContentResetResult::Restored);
    const content::ContentResetContext context = {1, &record, 1};
    CHECK(content::PrepareForReset(&registry, &context) ==
          content::ContentResetResult::Unsupported);
    CHECK(first.prepare_calls == 0);
    CHECK(second.prepare_calls == 0);
    content::ContentResetAdapterState states[2] = {};
    CHECK(content::SnapshotAdapterStates(&registry, states, 2) == 2);
    CHECK(states[0].prepare_result ==
          content::ContentResetResult::Unsupported);
    CHECK(states[1].prepare_result ==
          content::ContentResetResult::Unsupported);
    return true;
}

static bool CaptureCompleteAudioBaseline()
{
    audio::Initialize(&g_audio);
    return audio::BeginBaselineCapture(&g_audio) &&
           audio::RecordBaselineBank(
               &g_audio, 100, "shared-bank", "base\\shared.bnk",
               2, true) &&
           audio::RecordBaselineBank(
               &g_audio, 200, "override-bank", "base\\override.bnk",
               1, true) &&
           audio::EndBaselineCapture(&g_audio, true);
}

static bool CaseAudioClassifiesExactProjectDelta()
{
    CHECK(CaptureCompleteAudioBaseline());
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 1, 300, FakePointer(0x6100), FakePointer(0x7100),
        "unique-bank", "data\\audio\\unique.bnk", "custom-a",
        true));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 1, 200, FakePointer(0x6200), FakePointer(0x7200),
        "override-bank", "data\\audio\\override.bnk", "custom-a",
        true));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 1, 100, FakePointer(0x6300), FakePointer(0x7300),
        "shared-bank", "data\\audio\\shared.bnk", "base",
        false));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 1, 201, FakePointer(0x6400), FakePointer(0x7400),
        "override-bank", "data\\audio\\same-name.bnk", "custom-a",
        true));
    CHECK(g_audio.custom_count == 4);
    CHECK(g_audio.custom[0].classification ==
          audio::CustomAudioBankClass::ProjectUnique);
    CHECK(g_audio.custom[1].classification ==
          audio::CustomAudioBankClass::CustomOverrideOfBaseline);
    CHECK(g_audio.custom[2].classification ==
          audio::CustomAudioBankClass::SharedBaselineReference);
    /* Different nonzero logical ids prevent a same-name collision from
     * stealing baseline ownership. */
    CHECK(g_audio.custom[3].classification ==
          audio::CustomAudioBankClass::ProjectUnique);
    CHECK(audio::CanDestructivelyResetEpoch(&g_audio, 1));
    return true;
}

enum class AudioOperation : std::uint8_t {
    Quiesce,
    ReleaseCustom,
    ReleaseShared,
    ReplayBaseline,
    Verify,
};

struct FakeAudioOperations {
    AudioOperation order[32];
    std::uintptr_t handles[32];
    std::uint64_t logical_ids[32];
    std::uint32_t count;
    std::uint32_t custom_release_count;
    std::uint32_t shared_release_count;
    std::uint32_t replay_count;
    std::uint32_t verify_count;
    std::uint32_t fail_release_ordinal;
    std::uint64_t replayed_id;
    bool quiesce_result;
    bool release_result;
    bool replay_result;
    bool verify_result;
};

static void PushAudioOperation(FakeAudioOperations* operations,
                               AudioOperation operation,
                               std::uintptr_t handle,
                               std::uint64_t logical_id) noexcept
{
    if (!operations || operations->count >= 32) return;
    const std::uint32_t index = operations->count++;
    operations->order[index] = operation;
    operations->handles[index] = handle;
    operations->logical_ids[index] = logical_id;
}

static bool FakeAudioQuiesce(void* user, std::uint32_t) noexcept
{
    FakeAudioOperations* operations =
        static_cast<FakeAudioOperations*>(user);
    PushAudioOperation(operations, AudioOperation::Quiesce, 0, 0);
    return operations->quiesce_result;
}

static bool FakeReleaseCustom(void* user, std::uintptr_t handle,
                              std::uintptr_t) noexcept
{
    FakeAudioOperations* operations =
        static_cast<FakeAudioOperations*>(user);
    ++operations->custom_release_count;
    PushAudioOperation(operations, AudioOperation::ReleaseCustom,
                       handle, 0);
    const std::uint32_t ordinal = operations->custom_release_count +
                                  operations->shared_release_count;
    return operations->release_result &&
           (!operations->fail_release_ordinal ||
            ordinal != operations->fail_release_ordinal);
}

static bool FakeReleaseShared(void* user, std::uint64_t logical_id,
                              std::uintptr_t handle) noexcept
{
    FakeAudioOperations* operations =
        static_cast<FakeAudioOperations*>(user);
    ++operations->shared_release_count;
    PushAudioOperation(operations, AudioOperation::ReleaseShared,
                       handle, logical_id);
    const std::uint32_t ordinal = operations->custom_release_count +
                                  operations->shared_release_count;
    return operations->release_result &&
           (!operations->fail_release_ordinal ||
            ordinal != operations->fail_release_ordinal);
}

static bool FakeReplayBaseline(
    void* user, const audio::BaselineAudioBank* bank) noexcept
{
    FakeAudioOperations* operations =
        static_cast<FakeAudioOperations*>(user);
    ++operations->replay_count;
    operations->replayed_id = bank ? bank->logical_bank_id : 0;
    PushAudioOperation(operations, AudioOperation::ReplayBaseline,
                       0, operations->replayed_id);
    return operations->replay_result;
}

static bool FakeVerifyAudio(
    void* user, const audio::BaselineAudioBank*, std::size_t) noexcept
{
    FakeAudioOperations* operations =
        static_cast<FakeAudioOperations*>(user);
    ++operations->verify_count;
    PushAudioOperation(operations, AudioOperation::Verify, 0, 0);
    return operations->verify_result;
}

static audio::CustomAudioResetOperations MakeAudioOperations(
    FakeAudioOperations* fake) noexcept
{
    audio::CustomAudioResetOperations operations = {};
    operations.user = fake;
    operations.quiesce_project_audio = FakeAudioQuiesce;
    operations.release_exact_custom_instance = FakeReleaseCustom;
    operations.release_exact_shared_reference = FakeReleaseShared;
    operations.replay_baseline_bank = FakeReplayBaseline;
    operations.verify_baseline = FakeVerifyAudio;
    operations.owner_identity_validated = true;
    operations.active_event_fence_validated = true;
    operations.baseline_enumerator_complete = true;
    operations.exact_instance_model_validated = true;
    return operations;
}

static FakeAudioOperations SuccessfulFakeAudioOperations() noexcept
{
    FakeAudioOperations operations = {};
    operations.quiesce_result = true;
    operations.release_result = true;
    operations.replay_result = true;
    operations.verify_result = true;
    return operations;
}

static bool CaseAudioResetQuiescesBeforeExactRelease()
{
    CHECK(CaptureCompleteAudioBaseline());
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 5, 300, FakePointer(0x8100), FakePointer(0x9100),
        "unique-bank", "data\\audio\\unique.bnk", "custom-a",
        true));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 5, 100, FakePointer(0x8200), FakePointer(0x9200),
        "shared-bank", "data\\audio\\shared.bnk", "base",
        false));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 5, 200, FakePointer(0x8300), FakePointer(0x9300),
        "override-bank", "data\\audio\\override.bnk", "custom-a",
        true));
    /* The same exact instance was acquired twice by the project. */
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 5, 200, FakePointer(0x8300), FakePointer(0x9300),
        "override-bank", "data\\audio\\override.bnk", "custom-a",
        true));
    CHECK(g_audio.custom_count == 3);
    CHECK(g_audio.custom[2].classification ==
          audio::CustomAudioBankClass::ProjectReloadOfSameCustomInstance);
    CHECK(g_audio.custom[2].project_load_count == 2);
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 5, FakePointer(0x8300), FakePointer(0x9300)));
    CHECK(g_audio.custom[2].natural_unload_count == 1);
    CHECK(!g_audio.custom[2].unloaded_naturally);

    FakeAudioOperations fake = SuccessfulFakeAudioOperations();
    const audio::CustomAudioResetOperations operations =
        MakeAudioOperations(&fake);
    audio::CustomAudioResetState state;
    audio::Initialize(&state, &g_audio, &operations);
    CHECK(audio::IsDestructiveAdapterAvailable(&state));
    CHECK(audio::ResetBeforeSourceRemoval(&state, 5) ==
          content::ContentResetResult::Restored);
    CHECK(fake.count == 4);
    CHECK(fake.order[0] == AudioOperation::Quiesce);
    CHECK(fake.order[1] == AudioOperation::ReleaseCustom &&
          fake.handles[1] == 0x8300);
    CHECK(fake.order[2] == AudioOperation::ReleaseShared &&
          fake.handles[2] == 0x8200 && fake.logical_ids[2] == 100);
    CHECK(fake.order[3] == AudioOperation::ReleaseCustom &&
          fake.handles[3] == 0x8100);
    CHECK(audio::RestoreAfterSourceRemoval(&state, 5) ==
          content::ContentResetResult::Restored);
    CHECK(fake.replay_count == 1 && fake.replayed_id == 200);
    CHECK(audio::VerifyRestoredBaseline(&state) ==
          content::ContentResetResult::Restored);
    CHECK(fake.verify_count == 1);
    return true;
}

static bool CaseAudioQuiesceFailurePreventsAnyRelease()
{
    CHECK(CaptureCompleteAudioBaseline());
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 6, 300, FakePointer(0xA100), FakePointer(0xA200),
        "unique-bank", "data\\audio\\unique.bnk", "custom-a",
        true));
    FakeAudioOperations fake = SuccessfulFakeAudioOperations();
    fake.quiesce_result = false;
    const audio::CustomAudioResetOperations operations =
        MakeAudioOperations(&fake);
    audio::CustomAudioResetState state;
    audio::Initialize(&state, &g_audio, &operations);
    CHECK(audio::ResetBeforeSourceRemoval(&state, 6) ==
          content::ContentResetResult::Failed);
    CHECK(fake.count == 1);
    CHECK(fake.order[0] == AudioOperation::Quiesce);
    CHECK(fake.custom_release_count == 0 &&
          fake.shared_release_count == 0);
    return true;
}

static bool CaseAudioNeverUnloadsByNameAlone()
{
    audio::Initialize(&g_audio);
    CHECK(audio::BeginBaselineCapture(&g_audio));
    CHECK(audio::RecordBaselineBank(
        &g_audio, 0, "same-name", "base\\same-name.bnk", 1, true));
    CHECK(audio::EndBaselineCapture(&g_audio, true));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 7, 0, nullptr, nullptr, "same-name",
        "data\\audio\\same-name.bnk", "base", false));
    CHECK(g_audio.custom[0].classification ==
          audio::CustomAudioBankClass::SharedBaselineReference);
    CHECK(!g_audio.custom[0].exact_handle_available);
    CHECK(!audio::CanDestructivelyResetEpoch(&g_audio, 7));

    FakeAudioOperations fake = SuccessfulFakeAudioOperations();
    const audio::CustomAudioResetOperations operations =
        MakeAudioOperations(&fake);
    audio::CustomAudioResetState state;
    audio::Initialize(&state, &g_audio, &operations);
    CHECK(audio::ResetBeforeSourceRemoval(&state, 7) ==
          content::ContentResetResult::Unsupported);
    CHECK(fake.count == 0);
    CHECK(fake.shared_release_count == 0 &&
          fake.custom_release_count == 0);
    return true;
}

static bool CaseIncompleteAudioEnumerationDisablesReset()
{
    audio::Initialize(&g_audio);
    CHECK(audio::BeginBaselineCapture(&g_audio));
    CHECK(audio::RecordBaselineBank(
        &g_audio, 100, "shared-bank", "base\\shared.bnk", 1, true));
    CHECK(!audio::EndBaselineCapture(&g_audio, false));
    CHECK(!g_audio.baseline_captured && !g_audio.enumeration_complete);
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 8, 300, FakePointer(0xB100), FakePointer(0xB200),
        "unique-bank", "data\\audio\\unique.bnk", "custom-a",
        true));
    CHECK(!audio::CanDestructivelyResetEpoch(&g_audio, 8));

    FakeAudioOperations fake = SuccessfulFakeAudioOperations();
    audio::CustomAudioResetOperations operations =
        MakeAudioOperations(&fake);
    audio::CustomAudioResetState state;
    audio::Initialize(&state, &g_audio, &operations);
    CHECK(audio::ResetBeforeSourceRemoval(&state, 8) ==
          content::ContentResetResult::Unsupported);
    CHECK(fake.count == 0);

    operations.baseline_enumerator_complete = false;
    audio::Initialize(&state, &g_audio, &operations);
    CHECK(!audio::IsDestructiveAdapterAvailable(&state));
    return true;
}

static bool CaseAudioPointerReuseCreatesNewRecord()
{
    CHECK(CaptureCompleteAudioBaseline());
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 9, 300, FakePointer(0xC100), FakePointer(0xC200),
        "first-bank", "data\\audio\\first.bnk", "custom-a", true));
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 9, FakePointer(0xC100), FakePointer(0xC200)));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 9, 400, FakePointer(0xC100), FakePointer(0xC200),
        "reused-bank", "data\\audio\\reused.bnk", "custom-b", true));
    CHECK(g_audio.custom_count == 2);
    CHECK(g_audio.custom[0].unloaded_naturally);
    CHECK(g_audio.custom[0].logical_bank_id == 300);
    CHECK(g_audio.custom[1].logical_bank_id == 400);
    CHECK(g_audio.custom[1].project_load_count == 1);
    CHECK(g_audio.custom[1].classification ==
          audio::CustomAudioBankClass::ProjectUnique);
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 9, FakePointer(0xC100), FakePointer(0xC200)));
    CHECK(g_audio.custom[0].natural_unload_count == 1);
    CHECK(g_audio.custom[1].natural_unload_count == 1);
    CHECK(g_audio.custom[1].unloaded_naturally);
    return true;
}

static bool CaseAudioUnknownReloadRemainsUnknown()
{
    CHECK(CaptureCompleteAudioBaseline());
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 11, 900, FakePointer(0xE100), FakePointer(0xE200),
        "unknown-bank", "data\\audio\\unknown.bnk", "unknown-provider",
        false));
    CHECK(g_audio.custom_count == 1);
    CHECK(g_audio.custom[0].classification ==
          audio::CustomAudioBankClass::Unknown);
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 11, 900, FakePointer(0xE100), FakePointer(0xE200),
        "unknown-bank", "data\\audio\\unknown.bnk", "unknown-provider",
        false));
    CHECK(g_audio.custom[0].project_load_count == 2);
    CHECK(g_audio.custom[0].classification ==
          audio::CustomAudioBankClass::Unknown);
    CHECK(!audio::CanDestructivelyResetEpoch(&g_audio, 11));
    return true;
}

static bool CaseAudioCrossPairedIdentityIsRejected()
{
    CHECK(CaptureCompleteAudioBaseline());
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 12, 901, FakePointer(0xF100), FakePointer(0xF200),
        "first-bank", "data\\audio\\first.bnk", "custom-a", true));
    CHECK(audio::RecordProjectBankLoad(
        &g_audio, 12, 902, FakePointer(0xF300), FakePointer(0xF400),
        "second-bank", "data\\audio\\second.bnk", "custom-a", true));
    CHECK(!audio::RecordNaturalBankUnload(
        &g_audio, 12, FakePointer(0xF100), FakePointer(0xF400)));
    CHECK(g_audio.custom[0].natural_unload_count == 0);
    CHECK(g_audio.custom[1].natural_unload_count == 0);
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 12, FakePointer(0xF100), FakePointer(0xF200)));
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 12, FakePointer(0xF300), FakePointer(0xF400)));
    return true;
}

static bool CaseAudioPartialReleaseFailureIsLatched()
{
    CHECK(CaptureCompleteAudioBaseline());
    for (unsigned int acquisition = 0; acquisition < 3; ++acquisition)
        CHECK(audio::RecordProjectBankLoad(
            &g_audio, 10, 300, FakePointer(0xD100), FakePointer(0xD200),
            "unique-bank", "data\\audio\\unique.bnk", "custom-a", true));
    CHECK(g_audio.custom_count == 1);
    CHECK(g_audio.custom[0].project_load_count == 3);

    FakeAudioOperations fake = SuccessfulFakeAudioOperations();
    fake.fail_release_ordinal = 2;
    const audio::CustomAudioResetOperations operations =
        MakeAudioOperations(&fake);
    audio::CustomAudioResetState state;
    audio::Initialize(&state, &g_audio, &operations);
    CHECK(audio::ResetBeforeSourceRemoval(&state, 10) ==
          content::ContentResetResult::Partial);
    CHECK(fake.custom_release_count == 2);
    CHECK(g_audio.custom[0].reset_release_count == 1);
    CHECK(g_audio.custom[0].reset_failed);
    CHECK(!audio::CanDestructivelyResetEpoch(&g_audio, 10));
    CHECK(audio::ResetBeforeSourceRemoval(&state, 10) ==
          content::ContentResetResult::Unsupported);
    CHECK(fake.custom_release_count == 2);
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 10, FakePointer(0xD100), FakePointer(0xD200)));
    CHECK(audio::RecordNaturalBankUnload(
        &g_audio, 10, FakePointer(0xD100), FakePointer(0xD200)));
    CHECK(!audio::RecordNaturalBankUnload(
        &g_audio, 10, FakePointer(0xD100), FakePointer(0xD200)));
    CHECK(g_audio.custom[0].natural_unload_count == 2);
    CHECK(g_audio.custom[0].reset_release_count == 1);
    CHECK(g_audio.custom[0].unloaded_by_reset);
    CHECK(g_audio.custom[0].reset_failed);
    return true;
}

struct FakeFrontendReplay {
    lifecycle::FrontendSourceRollbackBarrier barrier;
    content::CustomContentLedger* ledger;
    void* session;
    void* outgoing_level;
    void* outgoing_clevel;
    void* observed_session;
    void* observed_level;
    void* observed_clevel;
    std::uintptr_t unload_caller_rva;
    std::uintptr_t original_return_value;
    std::uintptr_t returned_value;
    std::uintptr_t nested_original_return_value;
    std::uintptr_t nested_returned_value;
    std::uint32_t epoch;
    std::uint32_t owner_thread_id;
    std::uint32_t original_calls;
    std::uint32_t nested_original_calls;
    std::uint32_t reset_calls;
    std::uint32_t unlink_calls;
    std::uint32_t deleting_destructor_calls;
    std::uint32_t additional_release_receiver_count;
    void* removed_sources[content::kMaximumSources];
    std::uint32_t removed_source_count;
    std::uint32_t order_clock;
    std::uint32_t on_destroy_enter_order;
    std::uint32_t unload_order;
    std::uint32_t release_order;
    std::uint32_t original_return_order;
    std::uint32_t reset_begin_order;
    std::uint32_t reset_end_order;
    std::uint32_t frontend_lookup_order;
    bool binding_created;
    bool observe_unload;
    bool observe_release;
    bool observe_ambiguous_release;
    bool shutdown_or_quiescing;
    bool nested_on_destroy;
    bool lookup_during_original;
    bool destructor_available;
    bool unlink_succeeds;
    bool reset_succeeded;
    bool reset_runtime_failed;
};

static void InitializeFrontendReplay(
    FakeFrontendReplay* replay, content::CustomContentLedger* ledger,
    std::uint32_t epoch) noexcept
{
    std::memset(replay, 0, sizeof(*replay));
    lifecycle::Initialize(&replay->barrier);
    replay->ledger = ledger;
    replay->epoch = epoch;
    replay->owner_thread_id = 0x1234U;
    replay->session = FakePointer(0x7100);
    replay->outgoing_level = FakePointer(0x7200);
    /* The live ABI exposes an ILevel pointer here, while ReleasePacks receives
       its owning CLevel. They are deliberately not pointer-equal. */
    replay->outgoing_clevel = FakePointer(0x7300);
    replay->observed_session = replay->session;
    replay->observed_level = replay->outgoing_level;
    replay->observed_clevel = replay->outgoing_clevel;
    replay->unload_caller_rva =
        lifecycle::kOnDestroyUnloadReturnRva;
    replay->original_return_value = 0x5A170001U;
    replay->nested_original_return_value = 0x5A170002U;
    replay->observe_unload = true;
    replay->observe_release = true;
    replay->destructor_available = true;
    replay->unlink_succeeds = true;
}

static bool AddExactOwnedSource(content::CustomContentLedger* ledger,
                                std::uint32_t epoch, void* source,
                                const char* path,
                                std::uint32_t flags,
                                std::uint64_t sequence) noexcept
{
    return content::RecordSuccessfulSourceAddForEpoch(
        ledger, epoch, source, path, path, flags,
        content::ClassifySourceKind(path,
                                   "x:\\workshop\\mapa"),
        true, sequence, true);
}

static bool FakeResetExactOwnedSources(FakeFrontendReplay* replay) noexcept
{
    ++replay->reset_calls;
    replay->reset_begin_order = ++replay->order_clock;
    content::SourceResetSnapshot snapshot[content::kMaximumSources];
    const content::SourceSnapshotResult result =
        content::SnapshotActiveSourcesThroughEpochComplete(
            replay->ledger, replay->epoch, snapshot,
            content::kMaximumSources, replay->order_clock);
    if (!result.complete || !result.count) {
        replay->reset_end_order = ++replay->order_clock;
        return false;
    }
    if (!replay->destructor_available) {
        replay->reset_runtime_failed = true;
        replay->reset_end_order = ++replay->order_clock;
        return false;
    }
    for (std::size_t index = 0; index < result.count; ++index) {
        if (!snapshot[index].source ||
            !snapshot[index].exact_handle_available ||
            !snapshot[index].path_created_ownership) {
            replay->reset_end_order = ++replay->order_clock;
            return false;
        }
    }
    bool succeeded = true;
    for (std::size_t index = 0; index < result.count; ++index) {
        ++replay->unlink_calls;
        replay->removed_sources[replay->removed_source_count++] =
            snapshot[index].source;
        const bool removed = replay->unlink_succeeds;
        CHECK(content::MarkSourceResetResult(
            replay->ledger, snapshot[index].record_index, removed,
            ++replay->order_clock));
        if (!removed) {
            replay->reset_runtime_failed = true;
            succeeded = false;
            continue;
        }
        ++replay->deleting_destructor_calls;
    }
    replay->reset_end_order = ++replay->order_clock;
    return succeeded;
}

static std::uintptr_t FakeNestedOriginalOnDestroy(
    FakeFrontendReplay* replay) noexcept
{
    ++replay->nested_original_calls;
    return replay->nested_original_return_value;
}

static std::uintptr_t FakeOriginalOnDestroy(
    FakeFrontendReplay* replay) noexcept
{
    ++replay->original_calls;
    if (replay->nested_on_destroy) {
        CHECK(lifecycle::EnterNestedOnDestroy(
            &replay->barrier, replay->session,
            replay->outgoing_level, replay->owner_thread_id));
        replay->nested_returned_value =
            FakeNestedOriginalOnDestroy(replay);
        lifecycle::LeaveNestedOnDestroy(&replay->barrier);
    }
    if (replay->observe_unload) {
        replay->unload_order = ++replay->order_clock;
        const bool owned = lifecycle::ObserveOnDestroyOwnedUnload(
            &replay->barrier, replay->unload_caller_rva,
            replay->observed_session, replay->observed_level,
            replay->owner_thread_id,
            replay->shutdown_or_quiescing);
        if (owned && replay->observe_release) {
            const bool entered = lifecycle::BeginOwnedLevelRelease(
                &replay->barrier, replay->observed_clevel,
                replay->owner_thread_id,
                replay->shutdown_or_quiescing);
            const bool completed = entered &&
                lifecycle::CompleteOwnedLevelRelease(
                    &replay->barrier, replay->observed_clevel,
                    replay->owner_thread_id,
                    replay->shutdown_or_quiescing);
            if (completed)
                replay->release_order = ++replay->order_clock;
            if (completed && replay->observe_ambiguous_release) {
                CHECK(lifecycle::BeginOwnedLevelRelease(
                    &replay->barrier, FakePointer(0xDEAD),
                    replay->owner_thread_id, false));
                /* Deliberately omit completion to prove mismatch fails closed. */
            }
            if (completed) {
                for (std::uint32_t index = 0;
                     index < replay->additional_release_receiver_count;
                     ++index) {
                    void* receiver = FakePointer(0x7400 + index);
                    CHECK(lifecycle::BeginOwnedLevelRelease(
                        &replay->barrier, receiver,
                        replay->owner_thread_id, false));
                    CHECK(lifecycle::CompleteOwnedLevelRelease(
                        &replay->barrier, receiver,
                        replay->owner_thread_id, false));
                }
            }
        }
        if (owned)
            (void)lifecycle::CompleteOnDestroyOwnedUnload(
                &replay->barrier);
    }
    if (replay->lookup_during_original) {
        replay->frontend_lookup_order = ++replay->order_clock;
        lifecycle::ObserveFirstFrontendLookup(&replay->barrier);
    }
    return replay->original_return_value;
}

static std::uintptr_t RunBoundFrontendOnDestroy(
    FakeFrontendReplay* replay) noexcept
{
    replay->on_destroy_enter_order = ++replay->order_clock;
    replay->binding_created = lifecycle::BeginOnDestroy(
        &replay->barrier, replay->epoch, replay->session,
        replay->outgoing_level, replay->owner_thread_id);
    replay->returned_value = FakeOriginalOnDestroy(replay);
    if (!replay->binding_created) return replay->returned_value;

    replay->original_return_order = ++replay->order_clock;
    if (!lifecycle::MarkOriginalReturned(&replay->barrier)) {
        if (replay->barrier.release_enter_count != 0 &&
            replay->barrier.release_enter_count !=
                replay->barrier.release_exit_count)
            lifecycle::MarkRuntimeFailed(&replay->barrier);
        else
            lifecycle::MarkRuntimePartial(&replay->barrier);
        return replay->returned_value;
    }
    if (!lifecycle::ConsumeForReset(&replay->barrier) ||
        !lifecycle::BeginReset(&replay->barrier)) {
        lifecycle::MarkRuntimePartial(&replay->barrier);
        return replay->returned_value;
    }
    replay->reset_succeeded = FakeResetExactOwnedSources(replay);
    lifecycle::CompleteReset(&replay->barrier,
                             replay->reset_succeeded,
                             replay->reset_runtime_failed);
    return replay->returned_value;
}

static bool CaseFrontendEpochSelectionIsFailClosed()
{
    content::Initialize(&g_content);
    content::FrontendEpochSelection selection =
        content::SelectFrontendResetEpoch(&g_content, nullptr, false);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::NoActiveEpoch);

    std::uint32_t first = 0;
    CHECK(BeginContentEpoch(&g_content, &first));
    selection = content::SelectFrontendResetEpoch(
        &g_content, nullptr, false);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::NoActiveSources);
    CHECK(AddExactOwnedSource(
        &g_content, first, FakePointer(0x7400),
        "x:\\workshop\\mapa\\data", 1, 2));
    selection = content::SelectFrontendResetEpoch(
        &g_content, nullptr, false);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::SelectedSingleActiveFallback);
    CHECK(selection.epoch.id == first);
    selection = content::SelectFrontendResetEpoch(
        &g_content, "MapA", true);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::SelectedByContentName);
    CHECK(selection.epoch.id == first);

    const WCHAR root_b[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'B', 0};
    const std::uint32_t second = content::BeginEpoch(
        &g_content, "MapB", root_b, "x:\\workshop\\mapb", 3);
    CHECK(second != 0 && second != first);
    CHECK(AddExactOwnedSource(
        &g_content, second, FakePointer(0x7500),
        "x:\\workshop\\mapb\\data", 1, 4));
    /* Manufacture the ambiguous replay state the selector must reject. */
    g_content.epochs[0].active = true;
    g_content.epochs[0].accepting_records = true;
    selection = content::SelectFrontendResetEpoch(
        &g_content, nullptr, false);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::AmbiguousEpochs);
    selection = content::SelectFrontendResetEpoch(
        &g_content, "MapA", true);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::SelectedByContentName);
    CHECK(selection.epoch.id == first);
    return true;
}

static bool CaseSingleMapOnDestroyResetsOnceAfterOriginal()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7600),
        "x:\\workshop\\mapa", 1, 2));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7601),
        "x:\\workshop\\mapa\\data", 2, 3));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7602),
        "x:\\workshop\\mapa\\assets_pc", 3, 4));

    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    const std::uintptr_t result = RunBoundFrontendOnDestroy(&replay);
    CHECK(result == replay.original_return_value);
    CHECK(replay.returned_value == replay.original_return_value);
    CHECK(replay.original_calls == 1);
    CHECK(replay.reset_calls == 1);
    CHECK(replay.unlink_calls == 3);
    CHECK(replay.deleting_destructor_calls == 3);
    CHECK(replay.reset_succeeded);
    CHECK(content::CountActiveCustomSources(&g_content) == 0);
    CHECK(replay.on_destroy_enter_order < replay.unload_order);
    CHECK(replay.unload_order < replay.release_order);
    CHECK(replay.release_order < replay.original_return_order);
    CHECK(replay.original_return_order < replay.reset_begin_order);
    CHECK(replay.reset_begin_order < replay.reset_end_order);
    CHECK(replay.barrier.phase ==
          lifecycle::FrontendRollbackPhase::ResetComplete);
    return true;
}

static bool CaseSameProjectTwoMapDefersResetUntilFinalOnDestroy()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7700),
        "x:\\workshop\\mapa\\data", 1, 2));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7701),
        "x:\\workshop\\mapa\\assets_pc", 2, 3));

    lifecycle::FrontendSourceRollbackBarrier transition;
    lifecycle::Initialize(&transition);
    /* ChangeActiveLevel map 1 -> map 2 never creates reset authority. */
    CHECK(transition.phase == lifecycle::FrontendRollbackPhase::Idle);
    CHECK(!transition.binding_consumed);
    CHECK(content::CountActiveCustomSources(&g_content) == 2);

    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    replay.outgoing_level = FakePointer(0x7720);
    replay.observed_level = replay.outgoing_level;
    CHECK(RunBoundFrontendOnDestroy(&replay) ==
          replay.original_return_value);
    CHECK(replay.original_calls == 1);
    CHECK(replay.reset_calls == 1);
    CHECK(content::CountActiveCustomSources(&g_content) == 0);
    return true;
}

static bool CaseLatestNullNameColocatedTwoCLevelReplay()
{
    content::Initialize(&g_content);
    const char* root =
        "X:\\steam\\workshop\\content\\239140\\3728869100";
    const std::uint32_t epoch = content::BeginEpoch(
        &g_content, "", nullptr, root, 1);
    CHECK(epoch != 0);
    CHECK(!g_content.epochs[0].selected_content_name_available);
    void* project = FakePointer(0xA100);
    void* data = FakePointer(0xA101);
    void* assets = FakePointer(0xA102);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, project, root, root, 256,
        content::CustomSourceKind::ProjectRoot, true, 2, true));
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, data,
        "X:\\steam\\workshop\\content\\239140\\3728869100\\data",
        "X:\\steam\\workshop\\content\\239140\\3728869100\\data",
        269, content::CustomSourceKind::DataDirectory, true, 3, true));
    /* Failed DataWorkshop/locale/speech additions deliberately create no
       record. The third successful exact add is Assets_PC. */
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, assets,
        "X:\\steam\\workshop\\content\\239140\\3728869100\\assets_pc",
        "X:\\steam\\workshop\\content\\239140\\3728869100\\assets_pc",
        89, content::CustomSourceKind::AssetsPc, true, 4, true));
    CHECK(g_content.source_count == 3);
    CHECK(g_content.epochs[0].successful_source_count == 3);

    const content::FrontendEpochSelection selection =
        content::SelectFrontendResetEpoch(&g_content, nullptr, false);
    CHECK(selection.status ==
          content::FrontendEpochSelectionStatus::
              SelectedSingleActiveFallback);
    CHECK(selection.epoch.id == epoch);
    CHECK(selection.active_exact_owned_source_count == 3);

    /* The separately mounted loader PAK source exists outside this game-owned
       UpdateDLC epoch and is intentionally absent from the ledger. */
    void* loader_pack_source = FakePointer(0xA0FF);
    for (std::uint32_t index = 0; index < g_content.source_count; ++index)
        CHECK(g_content.sources[index].source != loader_pack_source);

    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    replay.additional_release_receiver_count = 1;
    CHECK(RunBoundFrontendOnDestroy(&replay) ==
          replay.original_return_value);
    CHECK(replay.barrier.release_enter_count == 2);
    CHECK(replay.barrier.release_exit_count == 2);
    CHECK(replay.barrier.unique_receiver_count == 2);
    CHECK(!replay.barrier.receiver_overflow);
    CHECK(!replay.barrier.release_mismatch);
    CHECK(replay.reset_calls == 1);
    CHECK(replay.removed_source_count == 3);
    CHECK(replay.removed_sources[0] == assets);
    CHECK(replay.removed_sources[1] == data);
    CHECK(replay.removed_sources[2] == project);
    CHECK(content::CountActiveCustomSources(&g_content) == 0);
    return true;
}

static bool CaseMutationLossCategoriesAreIndependent()
{
    content::Initialize(&g_content);
    CHECK(!content::RecordPackLoad(
        &g_content, FakePointer(0xB100),
        "x:\\base\\menu_main_pc.rpack", 1));
    content::LedgerLossState losses =
        content::CaptureLossState(&g_content);
    CHECK(losses.irrelevant_without_epoch == 1);
    CHECK(losses.ownership_mutation_loss == 0);
    content::RecordDiagnosticObservationLoss(
        &g_content,
        content::LedgerLossReason::OptionalObservationContention);
    losses = content::CaptureLossState(&g_content);
    CHECK(losses.diagnostic_observation_loss == 1);
    CHECK(losses.ownership_mutation_loss == 0);
    content::RecordOwnershipMutationLoss(
        &g_content, content::LedgerLossReason::SourceAddContention);
    losses = content::CaptureLossState(&g_content);
    CHECK(losses.ownership_mutation_loss == 1);
    CHECK(content::DroppedMutationCount(&g_content) == 1);
    return true;
}

static bool CaseExactOwnedUnloadAndReleaseAreRequired()
{
    struct EvidenceCase {
        bool unload;
        bool release;
        bool ambiguous_release;
    };
    const EvidenceCase cases[] = {
        {false, true, false},
        {true, false, false},
        {true, true, true},
    };
    for (const EvidenceCase& evidence : cases) {
        std::uint32_t epoch = 0;
        CHECK(BeginContentEpoch(&g_content, &epoch));
        CHECK(AddExactOwnedSource(
            &g_content, epoch, FakePointer(0x7800),
            "x:\\workshop\\mapa\\data", 1, 2));
        FakeFrontendReplay replay;
        InitializeFrontendReplay(&replay, &g_content, epoch);
        replay.observe_unload = evidence.unload;
        replay.observe_release = evidence.release;
        replay.observe_ambiguous_release =
            evidence.ambiguous_release;
        CHECK(RunBoundFrontendOnDestroy(&replay) ==
              replay.original_return_value);
        CHECK(replay.original_calls == 1);
        CHECK(replay.reset_calls == 0);
        CHECK(content::CountActiveCustomSources(&g_content) == 1);
        CHECK(replay.barrier.release_mismatch ==
              evidence.ambiguous_release || !evidence.release);
        CHECK(replay.barrier.phase ==
              (evidence.ambiguous_release
                   ? lifecycle::FrontendRollbackPhase::RuntimeFailed
                   : lifecycle::FrontendRollbackPhase::RuntimePartial));
    }
    return true;
}

static bool CaseWrongSessionLevelShutdownAndAfterFadeAreRejected()
{
    enum class Rejection : std::uint8_t {
        WrongSession,
        WrongLevel,
        Shutdown,
        AfterFade,
    };
    const Rejection cases[] = {
        Rejection::WrongSession,
        Rejection::WrongLevel,
        Rejection::Shutdown,
        Rejection::AfterFade,
    };
    for (const Rejection rejection : cases) {
        std::uint32_t epoch = 0;
        CHECK(BeginContentEpoch(&g_content, &epoch));
        CHECK(AddExactOwnedSource(
            &g_content, epoch, FakePointer(0x7900),
            "x:\\workshop\\mapa\\data", 1, 2));
        FakeFrontendReplay replay;
        InitializeFrontendReplay(&replay, &g_content, epoch);
        if (rejection == Rejection::WrongSession)
            replay.observed_session = FakePointer(0x7901);
        else if (rejection == Rejection::WrongLevel)
            replay.observed_level = FakePointer(0x7902);
        else if (rejection == Rejection::Shutdown)
            replay.shutdown_or_quiescing = true;
        else
            replay.unload_caller_rva =
                lifecycle::kAfterFadeUnloadReturnRva;
        CHECK(RunBoundFrontendOnDestroy(&replay) ==
              replay.original_return_value);
        CHECK(replay.original_calls == 1);
        CHECK(replay.reset_calls == 0);
        CHECK(!replay.barrier.on_destroy_unload_observed);
        CHECK(content::CountActiveCustomSources(&g_content) == 1);
    }
    return true;
}

static bool CaseNestedOnDestroyResetsOnce()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7A00),
        "x:\\workshop\\mapa\\data", 1, 2));
    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    replay.nested_on_destroy = true;
    CHECK(RunBoundFrontendOnDestroy(&replay) ==
          replay.original_return_value);
    CHECK(replay.original_calls == 1);
    CHECK(replay.nested_original_calls == 1);
    CHECK(replay.nested_returned_value ==
          replay.nested_original_return_value);
    CHECK(replay.reset_calls == 1);
    CHECK(replay.barrier.depth == 0);
    return true;
}

static bool CaseConsumedBindingCannotResetTwice()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7B00),
        "x:\\workshop\\mapa\\data", 1, 2));
    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    CHECK(RunBoundFrontendOnDestroy(&replay) ==
          replay.original_return_value);
    CHECK(replay.reset_calls == 1);
    CHECK(replay.barrier.binding_consumed);
    CHECK(!lifecycle::ConsumeForReset(&replay.barrier));
    CHECK(!lifecycle::BeginReset(&replay.barrier));
    CHECK(replay.reset_calls == 1);
    return true;
}

static bool CaseDroppedOrNonExactSourcesBlockReset()
{
    for (unsigned int failure = 0; failure < 2; ++failure) {
        std::uint32_t epoch = 0;
        CHECK(BeginContentEpoch(&g_content, &epoch));
        const bool path_created = failure != 1;
        CHECK(content::RecordSuccessfulSourceAddForEpoch(
            &g_content, epoch, FakePointer(0x7C00),
            "x:\\workshop\\mapa\\data",
            "x:\\workshop\\mapa\\data", 1,
            content::CustomSourceKind::DataDirectory, true, 2,
            path_created));
        if (failure == 0)
            content::RecordOwnershipMutationLoss(
                &g_content,
                content::LedgerLossReason::SourceAddContention);
        FakeFrontendReplay replay;
        InitializeFrontendReplay(&replay, &g_content, epoch);
        CHECK(RunBoundFrontendOnDestroy(&replay) ==
              replay.original_return_value);
        CHECK(replay.original_calls == 1);
        CHECK(replay.reset_calls == 1);
        CHECK(!replay.reset_succeeded);
        CHECK(replay.unlink_calls == 0);
        CHECK(replay.deleting_destructor_calls == 0);
        CHECK(replay.barrier.phase ==
              lifecycle::FrontendRollbackPhase::RuntimePartial);
        CHECK(content::CountActiveCustomSources(&g_content) == 1);
    }
    return true;
}

static bool CaseUnlinkOrDestructorFailureIsRuntimeFailed()
{
    for (unsigned int failure = 0; failure < 2; ++failure) {
        std::uint32_t epoch = 0;
        CHECK(BeginContentEpoch(&g_content, &epoch));
        CHECK(AddExactOwnedSource(
            &g_content, epoch, FakePointer(0x7D00),
            "x:\\workshop\\mapa\\data", 1, 2));
        FakeFrontendReplay replay;
        InitializeFrontendReplay(&replay, &g_content, epoch);
        if (failure == 0)
            replay.destructor_available = false;
        else
            replay.unlink_succeeds = false;
        CHECK(RunBoundFrontendOnDestroy(&replay) ==
              replay.original_return_value);
        CHECK(replay.original_calls == 1);
        CHECK(replay.reset_calls == 1);
        CHECK(!replay.reset_succeeded);
        CHECK(replay.deleting_destructor_calls == 0);
        CHECK(replay.unlink_calls == (failure == 0 ? 0U : 1U));
        CHECK(replay.barrier.phase ==
              lifecycle::FrontendRollbackPhase::RuntimeFailed);
        CHECK(content::CountActiveCustomSources(&g_content) == 1);
    }
    return true;
}

static bool CaseFrontendLookupBeforeResetFails()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7E00),
        "x:\\workshop\\mapa\\data", 1, 2));
    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    replay.lookup_during_original = true;
    CHECK(RunBoundFrontendOnDestroy(&replay) ==
          replay.original_return_value);
    CHECK(replay.original_calls == 1);
    CHECK(replay.reset_calls == 0);
    CHECK(replay.barrier.first_frontend_lookup ==
          lifecycle::FrontendLookupOrder::BeforeReset);
    CHECK(replay.barrier.phase ==
          lifecycle::FrontendRollbackPhase::RuntimeFailed);
    CHECK(content::CountActiveCustomSources(&g_content) == 1);
    return true;
}

static bool CaseFrontendLookupAfterResetSucceeds()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x7F00),
        "x:\\workshop\\mapa\\data", 1, 2));
    FakeFrontendReplay replay;
    InitializeFrontendReplay(&replay, &g_content, epoch);
    CHECK(RunBoundFrontendOnDestroy(&replay) ==
          replay.original_return_value);
    replay.frontend_lookup_order = ++replay.order_clock;
    CHECK(lifecycle::ObserveFirstFrontendLookup(&replay.barrier) ==
          lifecycle::FrontendLookupOrder::AfterReset);
    CHECK(replay.barrier.first_frontend_lookup ==
          lifecycle::FrontendLookupOrder::AfterReset);
    CHECK(replay.reset_end_order < replay.frontend_lookup_order);
    CHECK(replay.barrier.phase ==
          lifecycle::FrontendRollbackPhase::ResetComplete);
    return true;
}

static bool CaseStartupAndNonOnDestroyRoutesDoNotReset()
{
    content::Initialize(&g_content);
    FakeFrontendReplay startup;
    InitializeFrontendReplay(&startup, &g_content, 0);
    CHECK(RunBoundFrontendOnDestroy(&startup) ==
          startup.original_return_value);
    CHECK(!startup.binding_created);
    CHECK(startup.original_calls == 1);
    CHECK(startup.reset_calls == 0);

    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(AddExactOwnedSource(
        &g_content, epoch, FakePointer(0x8000),
        "x:\\workshop\\mapa\\data", 1, 2));
    lifecycle::FrontendSourceRollbackBarrier frontend_to_custom;
    lifecycle::FrontendSourceRollbackBarrier direct_custom_transition;
    lifecycle::Initialize(&frontend_to_custom);
    lifecycle::Initialize(&direct_custom_transition);
    CHECK(frontend_to_custom.phase ==
          lifecycle::FrontendRollbackPhase::Idle);
    CHECK(direct_custom_transition.phase ==
          lifecycle::FrontendRollbackPhase::Idle);
    CHECK(content::CountActiveCustomSources(&g_content) == 1);
    return true;
}

static bool CaseOwnedReleaseScopeVariations()
{
    void* session = FakePointer(0xC100);
    void* level = FakePointer(0xC200);
    void* receiver = FakePointer(0xC300);
    constexpr std::uint32_t thread_id = 0x4321U;

    lifecycle::FrontendSourceRollbackBarrier repeated;
    lifecycle::Initialize(&repeated);
    CHECK(lifecycle::BeginOnDestroy(
        &repeated, 1, session, level, thread_id));
    CHECK(lifecycle::ObserveOnDestroyOwnedUnload(
        &repeated, lifecycle::kOnDestroyUnloadReturnRva,
        session, level, thread_id, false));
    CHECK(!lifecycle::BeginOwnedLevelRelease(
        &repeated, receiver, thread_id + 1U, false));
    CHECK(lifecycle::BeginOwnedLevelRelease(
        &repeated, receiver, thread_id, false));
    CHECK(lifecycle::BeginOwnedLevelRelease(
        &repeated, receiver, thread_id, false));
    CHECK(lifecycle::CompleteOwnedLevelRelease(
        &repeated, receiver, thread_id, false));
    CHECK(lifecycle::CompleteOwnedLevelRelease(
        &repeated, receiver, thread_id, false));
    CHECK(repeated.release_enter_count == 2);
    CHECK(repeated.release_exit_count == 2);
    CHECK(repeated.unique_receiver_count == 1);
    CHECK(lifecycle::CompleteOnDestroyOwnedUnload(&repeated));
    CHECK(!lifecycle::BeginOwnedLevelRelease(
        &repeated, receiver, thread_id, false));
    CHECK(lifecycle::MarkOriginalReturned(&repeated));

    lifecycle::FrontendSourceRollbackBarrier overflow;
    lifecycle::Initialize(&overflow);
    CHECK(lifecycle::BeginOnDestroy(
        &overflow, 2, session, level, thread_id));
    CHECK(lifecycle::ObserveOnDestroyOwnedUnload(
        &overflow, lifecycle::kOnDestroyUnloadReturnRva,
        session, level, thread_id, false));
    for (std::size_t index = 0;
         index <= lifecycle::kMaximumOwnedReleaseReceivers; ++index) {
        void* current = FakePointer(0xC400 + index);
        CHECK(lifecycle::BeginOwnedLevelRelease(
            &overflow, current, thread_id, false));
        CHECK(lifecycle::CompleteOwnedLevelRelease(
            &overflow, current, thread_id, false));
    }
    CHECK(overflow.receiver_overflow);
    CHECK(overflow.unique_receiver_count ==
          lifecycle::kMaximumOwnedReleaseReceivers);
    CHECK(!lifecycle::CompleteOnDestroyOwnedUnload(&overflow));
    CHECK(overflow.phase ==
          lifecycle::FrontendRollbackPhase::RuntimePartial);
    return true;
}

static bool CaseExactSourceIdentityAndWatchedMetadata()
{
    std::uint32_t first_epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &first_epoch));
    void* const reused_source = FakePointer(0xD100);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, first_epoch, reused_source,
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 7,
        content::CustomSourceKind::DataDirectory, true, 2, true));

    content::CustomSourceIdentitySnapshot identity = {};
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, reused_source, "X:/Workshop/MapA/Data", &identity));
    CHECK(identity.epoch == first_epoch);
    CHECK(identity.source_record_index == 0);
    CHECK(identity.successful_add_ordinal == 1);
    CHECK(identity.kind == content::CustomSourceKind::DataDirectory);
    CHECK(identity.source_active);
    CHECK(std::strcmp(identity.normalized_project_root,
                      "x:\\workshop\\mapa") == 0);
    CHECK(!content::LookupExactCustomSourceIdentity(
        &g_content, reused_source, "X:/Workshop/MapA/Assets_PC",
        &identity));
    CHECK(!content::LookupExactCustomSourceIdentity(
        &g_content, FakePointer(0xD101),
        "X:/Workshop/MapA/Data", &identity));

    CHECK(content::RecordNaturalSourceRemoval(
        &g_content, reused_source, "X:/Workshop/MapA/Data", true, 3));
    CHECK(content::CloseEpochForFrontend(
        &g_content, first_epoch, 4));
    CHECK(content::MarkEpochConsumed(&g_content, first_epoch, 5));

    const WCHAR wide_root[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const std::uint32_t second_epoch = content::BeginEpoch(
        &g_content, "ProjectA-reloaded", wide_root,
        "x:\\workshop\\mapa", 6);
    CHECK(second_epoch != 0 && second_epoch != first_epoch);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, second_epoch, reused_source,
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 9,
        content::CustomSourceKind::DataDirectory, true, 7, true));
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, reused_source, "X:/Workshop/MapA/Data", &identity));
    CHECK(identity.epoch == second_epoch);
    CHECK(identity.source_record_index == 1);
    CHECK(identity.successful_add_ordinal == 2);
    CHECK(identity.source_active);

    std::uint32_t file_index = UINT32_MAX;
    CHECK(content::RecordCustomFileOpen(
        &g_content, identity.epoch,
        "DATA/campaign_custom_workshop.scr",
        "X:/Workshop/MapA/Data", reused_source,
        0x180018D0EULL, 0x18D0E,
        "filesystem_x64_rwdi.dll", 7412, 2659,
        &file_index, &identity, 1,
        content::ContentOpenStagingPhase::CustomContentStaging, 1));
    CHECK(file_index == 0);
    const content::CustomFileLoadRecord& file = g_content.files[file_index];
    CHECK(file.epoch == second_epoch);
    CHECK(file.source_record_index == 1);
    CHECK(file.source_add_ordinal == 2);
    CHECK(file.source_kind == content::CustomSourceKind::DataDirectory);
    CHECK(file.first_open_sequence == 2659);
    CHECK(file.first_transition_generation == 1);
    CHECK(file.first_staging_phase ==
          content::ContentOpenStagingPhase::CustomContentStaging);
    CHECK(file.first_update_dlc_depth == 1);
    CHECK(file.lifetime == content::ContentLifetimeClass::Unknown);
    CHECK(!file.classification_complete);

    CHECK(content::RecordFileParserObservation(
        &g_content, file_index,
        content::WatchedParserOutcome::ParsedNew,
        "e09ef9bd", 2048, "<registry-owner-unresolved>",
        0, 0xD200, 0, second_epoch, 2660));
    CHECK(g_content.files[file_index].parser_outcome ==
          content::WatchedParserOutcome::ParsedNew);
    CHECK(std::strcmp(g_content.files[file_index].parser_input_hash,
                      "e09ef9bd") == 0);
    CHECK(g_content.files[file_index].parser_registration_generation == 0);
    CHECK(g_content.files[file_index].parsed_object_epoch == second_epoch);
    CHECK(g_content.files[file_index].lifetime ==
          content::ContentLifetimeClass::Unknown);
    CHECK(!g_content.files[file_index].classification_complete);
    return true;
}

static bool CaseExactMapOwnerProofIsCurrentAndFailClosed()
{
    std::uint32_t epoch = 0;
    CHECK(BeginContentEpoch(&g_content, &epoch));
    void* const source = FakePointer(0xDA00);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, source,
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 269,
        content::CustomSourceKind::DataDirectory, true, 2, true));
    content::CustomSourceIdentitySnapshot identity = {};
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, source, "X:/Workshop/MapA/Data", &identity));
    CHECK(!content::RecordExactMapOwnerEvidence(
        &g_content, "data/maps/nowhere/not_nowhere.scr",
        &identity, 7, 3));
    CHECK(content::RecordExactMapOwnerEvidence(
        &g_content, "DATA/MAPS/NOWHERE/NOWHERE.SCR",
        &identity, 7, 4));

    content::ExactMapOwnerProofSnapshot proof = {};
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "NoWhErE", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Ready);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::None);
    CHECK(proof.epoch == epoch);
    CHECK(proof.source_record_index == 0);
    CHECK(proof.source_add_ordinal == 1);
    CHECK(proof.transition_generation == 7);
    CHECK(std::strcmp(proof.map_name, "nowhere") == 0);
    CHECK(std::strcmp(proof.normalized_project_root,
                      "x:\\workshop\\mapa") == 0);
    CHECK(proof.project_root[0] == 'E');
    CHECK(content::ConsumeExactMapOwnerProof(
        &g_content, &proof));
    CHECK(!content::ConsumeExactMapOwnerProof(
        &g_content, &proof));
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Pending);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::Consumed);
    CHECK(!content::RecordExactMapOwnerEvidence(
        &g_content, "data/maps/nowhere/nowhere.scr",
        &identity, 7, 5));

    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "other", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Pending);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::MapMismatch);
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 8, &proof) ==
          content::ExactMapOwnerProofStatus::Pending);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::TransitionMismatch);
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "..", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Rejected);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::InvalidPath);
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "CON", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Rejected);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::InvalidPath);

    dluma_atomic_exchange(&g_content.lock, 1);
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Pending);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::LedgerBusy);
    dluma_atomic_exchange(&g_content.lock, 0);

    CHECK(content::RecordNaturalSourceRemoval(
        &g_content, source, "X:/Workshop/MapA/Data", true, 5));
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 7, &proof) ==
          content::ExactMapOwnerProofStatus::Rejected);

    CHECK(BeginContentEpoch(&g_content, &epoch));
    void* const source_a = FakePointer(0xDA10);
    void* const source_b = FakePointer(0xDA20);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, source_a,
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 269,
        content::CustomSourceKind::DataDirectory, true, 6, true));
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, source_b,
        "X:\\Workshop\\MapA\\DataWorkshop_0.pak",
        "x:\\workshop\\mapa\\dataworkshop_0.pak", 269,
        content::CustomSourceKind::DataWorkshopPak, true, 7, true));
    content::CustomSourceIdentitySnapshot first = {};
    content::CustomSourceIdentitySnapshot second = {};
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, source_a, "X:/Workshop/MapA/Data", &first));
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, source_b,
        "X:/Workshop/MapA/DataWorkshop_0.pak", &second));
    CHECK(content::RecordExactMapOwnerEvidence(
        &g_content, "data/maps/nowhere/nowhere.scr",
        &first, 9, 8));
    CHECK(!content::RecordExactMapOwnerEvidence(
        &g_content, "data/maps/nowhere/nowhere.scr",
        &second, 9, 9));
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 9, &proof) ==
          content::ExactMapOwnerProofStatus::Rejected);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::AmbiguousEvidence);

    CHECK(BeginContentEpoch(&g_content, &epoch));
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 10, &proof) ==
          content::ExactMapOwnerProofStatus::Pending);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::NoEvidence);

    CHECK(BeginContentEpoch(&g_content, &epoch));
    void* const lost_source = FakePointer(0xDA30);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, lost_source,
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 269,
        content::CustomSourceKind::DataDirectory, true, 11, true));
    content::CustomSourceIdentitySnapshot lost_identity = {};
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, lost_source,
        "X:/Workshop/MapA/Data", &lost_identity));
    CHECK(content::RecordExactMapOwnerEvidence(
        &g_content, "data/maps/nowhere/nowhere.scr",
        &lost_identity, 11, 12));
    CHECK(content::MarkEpochOwnershipLost(
        &g_content, epoch,
        content::LedgerLossReason::EpochConflict, true, 13));
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 11, &proof) ==
          content::ExactMapOwnerProofStatus::Rejected);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::OwnershipIncomplete);

    CHECK(BeginContentEpoch(&g_content, &epoch));
    void* const closed_source = FakePointer(0xDA40);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch, closed_source,
        "X:\\Workshop\\MapA\\Data",
        "x:\\workshop\\mapa\\data", 269,
        content::CustomSourceKind::DataDirectory, true, 14, true));
    content::CustomSourceIdentitySnapshot closed_identity = {};
    CHECK(content::LookupExactCustomSourceIdentity(
        &g_content, closed_source,
        "X:/Workshop/MapA/Data", &closed_identity));
    CHECK(content::RecordExactMapOwnerEvidence(
        &g_content, "data/maps/nowhere/nowhere.scr",
        &closed_identity, 12, 15));
    CHECK(content::CloseEpochForFrontend(
        &g_content, epoch, 16));
    CHECK(content::SnapshotExactMapOwnerProof(
              &g_content, "nowhere", 12, &proof) ==
          content::ExactMapOwnerProofStatus::Rejected);
    CHECK(proof.reason ==
          content::ExactMapOwnerProofReason::OwnershipIncomplete);
    return true;
}

static bool CasePlayerPresetProofRejectsDirectProviderTransition()
{
    content::Initialize(&g_content);
    const WCHAR root_a[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const WCHAR root_b[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'B', 0};
    void* const source_a = FakePointer(0xE100);
    const char* const provider_a = "x:\\workshop\\mapa\\data";
    const std::uint32_t epoch_a = content::BeginEpoch(
        &g_content, "ProjectA", root_a, "x:\\workshop\\mapa", 1);
    CHECK(epoch_a != 0);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch_a, source_a, provider_a, provider_a, 1,
        content::CustomSourceKind::DataDirectory, true, 2, true));
    const std::uint32_t epoch_b = content::BeginEpoch(
        &g_content, "ProjectB", root_b, "x:\\workshop\\mapb", 3);
    CHECK(epoch_b != 0 && epoch_b != epoch_a);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch_b, FakePointer(0xE200),
        "x:\\workshop\\mapb\\data",
        "x:\\workshop\\mapb\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 4, true));

    content::PlayerPresetRepairSourceProof proof = {};
    CHECK(content::EvaluatePlayerPresetRepairSourceProof(
        &g_content, epoch_a, epoch_b, source_a, provider_a, &proof));
    CHECK(proof.old_data_source_identified);
    CHECK(!proof.old_data_sources_inactive);
    CHECK(proof.current_epoch_sources_ready);
    return true;
}

static bool CasePlayerPresetProofAcceptsExactCompletedReset()
{
    content::Initialize(&g_content);
    const WCHAR root_a[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'A', 0};
    const WCHAR root_b[] = {
        'E', ':', '\\', 'W', 'o', 'r', 'k', 's', 'h', 'o', 'p',
        '\\', 'M', 'a', 'p', 'B', 0};
    void* const source_a = FakePointer(0xE300);
    const char* const provider_a = "x:\\workshop\\mapa\\data";
    const std::uint32_t epoch_a = content::BeginEpoch(
        &g_content, "ProjectA", root_a, "x:\\workshop\\mapa", 1);
    CHECK(epoch_a != 0);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch_a, source_a, provider_a, provider_a, 1,
        content::CustomSourceKind::DataDirectory, true, 2, true));
    const std::uint32_t epoch_b = content::BeginEpoch(
        &g_content, "ProjectB", root_b, "x:\\workshop\\mapb", 3);
    CHECK(epoch_b != 0 && epoch_b != epoch_a);
    CHECK(content::RecordSuccessfulSourceAddForEpoch(
        &g_content, epoch_b, FakePointer(0xE400),
        "x:\\workshop\\mapb\\data",
        "x:\\workshop\\mapb\\data", 1,
        content::CustomSourceKind::DataDirectory, true, 4, true));
    CHECK(content::RecordNaturalSourceRemoval(
        &g_content, source_a, provider_a, true, 5));
    CHECK(content::MarkEpochConsumed(&g_content, epoch_a, 6));

    content::PlayerPresetRepairSourceProof proof = {};
    CHECK(content::EvaluatePlayerPresetRepairSourceProof(
        &g_content, epoch_a, epoch_b, source_a, provider_a, &proof));
    CHECK(proof.old_data_source_identified);
    CHECK(proof.old_data_sources_inactive);
    CHECK(proof.current_epoch_sources_ready);
    CHECK(proof.ownership_complete);

    content::RecordOwnershipMutationLoss(
        &g_content, content::LedgerLossReason::SourceAddContention);
    CHECK(content::EvaluatePlayerPresetRepairSourceProof(
        &g_content, epoch_a, epoch_b, source_a, provider_a, &proof));
    CHECK(!proof.ownership_complete);
    CHECK(!proof.current_epoch_sources_ready);
    return true;
}

struct NamedCase {
    const char* name;
    bool (*function)();
};

int main()
{
    const NamedCase cases[] = {
        {"path_normalization_and_source_classification",
         CasePathNormalizationAndSourceClassification},
        {"explicit_epoch_attribution_cannot_drift",
         CaseExplicitEpochAttributionCannotDrift},
        {"current_epoch_read_distinguishes_zero_from_contention",
         CaseCurrentEpochReadDistinguishesZeroFromContention},
        {"complete_snapshot_rejects_dropped_ownership",
         CaseCompleteSnapshotRejectsDroppedOwnership},
        {"pack_pointer_reuse_creates_newest_record",
         CasePackPointerReuseCreatesNewestRecord},
        {"custom_source_exclusions_are_non_destructive",
         CaseCustomSourceExclusionsAreNonDestructive},
        {"source_ledger_is_bounded", CaseSourceLedgerIsBounded},
        {"file_ledger_deduplicates_and_is_bounded",
         CaseFileLedgerDeduplicatesAndIsBounded},
        {"source_reset_uses_reverse_successful_add_order",
         CaseSourceResetUsesReverseSuccessfulAddOrder},
        {"cancelled_cleanup_verifies_live_registry_exact_minus_sources",
         CaseCancelledCleanupVerifiesLiveRegistryExactMinusSources},
        {"closed_epoch_rejects_late_file_ownership",
         CaseClosedEpochRejectsLateFileOwnership},
        {"ambiguous_path_removal_cannot_select_an_owner",
         CaseAmbiguousPathRemovalCannotSelectAnOwner},
        {"duplicate_active_source_kind_fails_closed",
         CaseDuplicateActiveSourceKindFailsClosed},
        {"presentation_source_set_canonical_join_and_reasons",
         CasePresentationSourceSetCanonicalJoinAndReasons},
        {"unknown_persistent_content_blocks_baseline_match",
         CaseUnknownPersistentContentBlocksBaselineMatch},
        {"reset_registry_validates_all_semantics",
         CaseResetRegistryValidatesAllSemantics},
        {"reset_registry_dispatches_deterministically",
         CaseResetRegistryDispatchesDeterministically},
        {"reset_registry_rejects_unknown_owner_and_ambiguity",
         CaseResetRegistryRejectsUnknownOwnerAndAmbiguity},
        {"audio_classifies_exact_project_delta",
         CaseAudioClassifiesExactProjectDelta},
        {"audio_reset_quiesces_before_exact_release",
         CaseAudioResetQuiescesBeforeExactRelease},
        {"audio_quiesce_failure_prevents_any_release",
         CaseAudioQuiesceFailurePreventsAnyRelease},
        {"audio_never_unloads_by_name_alone",
         CaseAudioNeverUnloadsByNameAlone},
        {"incomplete_audio_enumeration_disables_reset",
         CaseIncompleteAudioEnumerationDisablesReset},
        {"audio_pointer_reuse_creates_new_record",
         CaseAudioPointerReuseCreatesNewRecord},
        {"audio_unknown_reload_remains_unknown",
         CaseAudioUnknownReloadRemainsUnknown},
        {"audio_cross_paired_identity_is_rejected",
         CaseAudioCrossPairedIdentityIsRejected},
        {"audio_partial_release_failure_is_latched",
         CaseAudioPartialReleaseFailureIsLatched},
        {"frontend_epoch_selection_is_fail_closed",
         CaseFrontendEpochSelectionIsFailClosed},
        {"single_map_on_destroy_resets_once_after_original",
         CaseSingleMapOnDestroyResetsOnceAfterOriginal},
        {"same_project_two_map_defers_reset_until_final_on_destroy",
         CaseSameProjectTwoMapDefersResetUntilFinalOnDestroy},
        {"latest_null_name_colocated_two_clevel_replay",
         CaseLatestNullNameColocatedTwoCLevelReplay},
        {"mutation_loss_categories_are_independent",
         CaseMutationLossCategoriesAreIndependent},
        {"exact_owned_unload_and_release_are_required",
         CaseExactOwnedUnloadAndReleaseAreRequired},
        {"wrong_session_level_shutdown_and_afterfade_are_rejected",
         CaseWrongSessionLevelShutdownAndAfterFadeAreRejected},
        {"nested_on_destroy_resets_once",
         CaseNestedOnDestroyResetsOnce},
        {"consumed_binding_cannot_reset_twice",
         CaseConsumedBindingCannotResetTwice},
        {"dropped_or_non_exact_sources_block_reset",
         CaseDroppedOrNonExactSourcesBlockReset},
        {"unlink_or_destructor_failure_is_runtime_failed",
         CaseUnlinkOrDestructorFailureIsRuntimeFailed},
        {"frontend_lookup_before_reset_fails",
         CaseFrontendLookupBeforeResetFails},
        {"frontend_lookup_after_reset_succeeds",
         CaseFrontendLookupAfterResetSucceeds},
        {"startup_and_non_on_destroy_routes_do_not_reset",
         CaseStartupAndNonOnDestroyRoutesDoNotReset},
        {"owned_release_scope_variations",
         CaseOwnedReleaseScopeVariations},
        {"exact_source_identity_and_watched_metadata",
         CaseExactSourceIdentityAndWatchedMetadata},
        {"exact_map_owner_proof_is_current_and_fail_closed",
         CaseExactMapOwnerProofIsCurrentAndFailClosed},
        {"player_preset_proof_rejects_direct_provider_transition",
         CasePlayerPresetProofRejectsDirectProviderTransition},
        {"player_preset_proof_accepts_exact_completed_reset",
         CasePlayerPresetProofAcceptsExactCompletedReset},
    };
    for (const NamedCase& item : cases) {
        if (!item.function()) {
            std::fprintf(stderr, "[FAIL] %s\n", item.name);
            return 1;
        }
        std::printf("[PASS] %s\n", item.name);
    }
    std::printf("custom content rollback cases passed: %zu\n",
                sizeof(cases) / sizeof(cases[0]));
    return 0;
}
