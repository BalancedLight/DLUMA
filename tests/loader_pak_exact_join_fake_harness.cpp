#include "fixes/custom_map_asset_isolation/StartupFilesystemSourceClassifier.h"

#include <cstdio>
#include <cstring>

namespace startup =
    dluma::fixes::custom_map_asset_isolation::startupsource;
namespace vfs =
    dluma::fixes::custom_map_asset_isolation::vfs;

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(                                                   \
                stderr, "CHECK failed at line %d: %s\n",                  \
                __LINE__, #expression);                                     \
            return false;                                                   \
        }                                                                   \
    } while (0)

namespace {

constexpr std::uint64_t kGeneration = 55U;
constexpr std::uintptr_t kPackVtable = 0x180020000ULL;

struct FakeSource {
    startup::FilesystemSourceRuntimeType type;
    std::uintptr_t vtable;
};

struct Fixture {
    FakeSource source;
    FakeSource replacement;
    vfs::FilesystemSourceInventory inventory;
    DlumaProcessLifetimeSourceSnapshot mounts;
    DlumaProcessLifetimeSourceRecord expected;
    startup::StartupSourceClassificationPolicy policy;
};

void FillHex(char output[65], char value) noexcept
{
    for (std::uint32_t index = 0; index < 64U; ++index)
        output[index] = value;
    output[64] = 0;
}

bool Inspect(
    void*, std::uintptr_t source, std::uintptr_t* vtable,
    startup::FilesystemSourceRuntimeType* type) noexcept
{
    const FakeSource* fake =
        reinterpret_cast<const FakeSource*>(source);
    if (!fake || !vtable || !type || !fake->vtable)
        return false;
    *vtable = fake->vtable;
    *type = fake->type;
    return true;
}

DlumaNativeCompanionOwnerValidationResult ValidateOwner(
    void* user,
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic) noexcept
{
    const DlumaProcessLifetimeSourceRecord* expected =
        static_cast<const DlumaProcessLifetimeSourceRecord*>(user);
    const bool valid = expected && record &&
        record->source_object == expected->source_object &&
        record->mount_generation == expected->mount_generation &&
        record->file_size == expected->file_size &&
        _stricmp(
            record->content_sha256,
            expected->content_sha256) == 0;
    if (diagnostic) {
        std::memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->result = valid
            ? DLUMA_COMPANION_ARCHIVE_OWNER_VALID
            : DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_POINTER_MISMATCH;
    }
    return valid
        ? DLUMA_COMPANION_ARCHIVE_OWNER_VALID
        : DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_POINTER_MISMATCH;
}

void Initialize(Fixture* fixture) noexcept
{
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->source.type =
        startup::FilesystemSourceRuntimeType::Pack;
    fixture->source.vtable = kPackVtable;
    fixture->replacement = fixture->source;

    fixture->inventory.reported_count = 1U;
    fixture->inventory.source_count = 1U;
    fixture->inventory.registry_generation = kGeneration;
    fixture->inventory.registry_generation_observed = true;
    fixture->inventory.complete = true;
    fixture->inventory.sources[0].source =
        reinterpret_cast<std::uintptr_t>(&fixture->source);
    fixture->inventory.sources[0].link = 0x180100000ULL;
    fixture->inventory.sources[0].list_ordinal = 49U;
    strcpy_s(
        fixture->inventory.sources[0].path,
        sizeof(fixture->inventory.sources[0].path),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
        "3771245504\\mods");

    fixture->expected.owner_kind =
        DLUMA_SOURCE_OWNER_LOADER_PAK;
    strcpy_s(
        fixture->expected.owner_id,
        sizeof(fixture->expected.owner_id),
        DLUMA_SOURCE_OWNER_ID_LOADER_PAK);
    fixture->expected.source_object = &fixture->source;
    fixture->expected.intrusive_link =
        reinterpret_cast<void*>(
            fixture->inventory.sources[0].link);
    fixture->expected.list_ordinal = 49U;
    strcpy_s(
        fixture->expected.requested_mount_path,
        sizeof(fixture->expected.requested_mount_path),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
        "3771245504\\mods\\dluma.pak");
    strcpy_s(
        fixture->expected.observed_source_path,
        sizeof(fixture->expected.observed_source_path),
        fixture->inventory.sources[0].path);
    fixture->expected.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
    fixture->expected.source_flags = 9U;
    fixture->expected.registry_generation = kGeneration;
    fixture->expected.mount_generation = 1U;
    fixture->expected.volume_serial = 11U;
    fixture->expected.file_index_high = 12U;
    fixture->expected.file_index_low = 13U;
    fixture->expected.file_size = 4096U;
    FillHex(fixture->expected.content_sha256, 'a');
    fixture->expected.metadata_complete = 1U;
    fixture->expected.exact_source_joined = 1U;
    fixture->mounts.records[0] = fixture->expected;
    fixture->mounts.count = 1U;
    fixture->mounts.complete = 1U;

    strcpy_s(
        fixture->policy.player_root,
        sizeof(fixture->policy.player_root),
        "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\DevTools");
    strcpy_s(
        fixture->policy.game_install_root,
        sizeof(fixture->policy.game_install_root),
        "X:\\FixtureLibrary\\steamapps\\common\\Dying Light");
    strcpy_s(
        fixture->policy.workshop_roots[0],
        sizeof(fixture->policy.workshop_roots[0]),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140");
    fixture->policy.workshop_root_count = 1U;
    fixture->policy.process_lifetime_sources =
        &fixture->mounts;
    fixture->policy.inspect_source = Inspect;
    fixture->policy.validate_process_lifetime_owner =
        ValidateOwner;
    fixture->policy.validate_process_lifetime_owner_user =
        &fixture->expected;
    fixture->policy.committed_registry_generation =
        kGeneration;
}

bool ExactParentJoinAccepted() noexcept
{
    Fixture fixture;
    startup::StartupSourceClassificationReport report = {};
    Initialize(&fixture);
    CHECK(startup::ClassifyStartupFilesystemSources(
        &fixture.inventory, &fixture.policy, &report));
    CHECK(report.complete);
    CHECK(report.accepted_count == 1U);
    CHECK(report.loader_match_count == 1U);
    CHECK(report.loader_source_identity != 0U);
    CHECK(report.rows[0].exact_process_lifetime_join);
    CHECK(report.rows[0].path_corroborated);
    return true;
}

bool MissingOrReplacementObjectRejected() noexcept
{
    Fixture fixture;
    startup::StartupSourceClassificationReport report = {};
    Initialize(&fixture);
    fixture.mounts.count = 0U;
    CHECK(!startup::ClassifyStartupFilesystemSources(
        &fixture.inventory, &fixture.policy, &report));
    CHECK(report.rejected_count != 0U);

    Initialize(&fixture);
    fixture.inventory.sources[0].source =
        reinterpret_cast<std::uintptr_t>(&fixture.replacement);
    CHECK(!startup::ClassifyStartupFilesystemSources(
        &fixture.inventory, &fixture.policy, &report));
    CHECK(report.loader_source_identity == 0U);
    return true;
}

bool ChangedContentAndDuplicateLoaderRejected() noexcept
{
    Fixture fixture;
    startup::StartupSourceClassificationReport report = {};
    Initialize(&fixture);
    FillHex(fixture.mounts.records[0].content_sha256, 'b');
    CHECK(!startup::ClassifyStartupFilesystemSources(
        &fixture.inventory, &fixture.policy, &report));
    CHECK(report.rows[0].result ==
        startup::StartupSourceClassificationResult::
            ProcessLifetimeOwnerValidationFailed);

    Initialize(&fixture);
    fixture.mounts.records[1] = fixture.mounts.records[0];
    fixture.mounts.records[1].source_object =
        &fixture.replacement;
    fixture.mounts.records[1].intrusive_link =
        reinterpret_cast<void*>(0x180100020ULL);
    fixture.mounts.records[1].list_ordinal = 50U;
    fixture.mounts.records[1].mount_generation = 2U;
    fixture.mounts.count = 2U;
    CHECK(!startup::ClassifyStartupFilesystemSources(
        &fixture.inventory, &fixture.policy, &report));
    CHECK(report.loader_source_identity == 0U);
    return true;
}

}  // namespace

int main()
{
    if (!ExactParentJoinAccepted() ||
        !MissingOrReplacementObjectRejected() ||
        !ChangedContentAndDuplicateLoaderRejected())
        return 1;
    std::puts("loader PAK exact-join fake harness passed");
    return 0;
}
