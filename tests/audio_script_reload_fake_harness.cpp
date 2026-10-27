#include "fixes/custom_map_asset_isolation/AudioScriptReloadAdapter.h"

#include <cstdio>
#include <cstring>

namespace reload =
    dluma::fixes::custom_map_asset_isolation::audio_script_reload;
namespace snapshot =
    dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

constexpr std::uintptr_t kFakeEngineBase = 0x0000000180000000ULL;
constexpr std::uintptr_t kFakeCodeFirstRva =
    reload::kAudioScriptClearRva;
constexpr std::size_t kFakeCodeSize = 0x200U;

struct FakeCodeImage {
    std::uint8_t bytes[kFakeCodeSize];
};

bool ReadFakeCode(void* user, std::uintptr_t address, void* output,
                  std::size_t size) noexcept
{
    FakeCodeImage* image = static_cast<FakeCodeImage*>(user);
    if (!image || !output || address < kFakeEngineBase ||
        address - kFakeEngineBase < kFakeCodeFirstRva)
        return false;
    const std::uintptr_t offset =
        address - kFakeEngineBase - kFakeCodeFirstRva;
    if (offset > kFakeCodeSize || size > kFakeCodeSize - offset)
        return false;
    std::memcpy(output, image->bytes + offset, size);
    return true;
}

void WriteRelativeCall(FakeCodeImage* image, std::uintptr_t call_rva,
                       std::uintptr_t target_rva) noexcept
{
    const std::size_t offset =
        static_cast<std::size_t>(call_rva - kFakeCodeFirstRva);
    const std::int64_t displacement =
        static_cast<std::int64_t>(target_rva) -
        static_cast<std::int64_t>(call_rva + 5U);
    const std::uint32_t encoded = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(displacement));
    image->bytes[offset] = 0xE8U;
    image->bytes[offset + 1U] =
        static_cast<std::uint8_t>(encoded);
    image->bytes[offset + 2U] =
        static_cast<std::uint8_t>(encoded >> 8U);
    image->bytes[offset + 3U] =
        static_cast<std::uint8_t>(encoded >> 16U);
    image->bytes[offset + 4U] =
        static_cast<std::uint8_t>(encoded >> 24U);
}

FakeCodeImage ValidatedCodeImage() noexcept
{
    static const std::uint8_t clear_prefix[] = {
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0x05,0x3B,0x13,0x9C,0x00,0x33,0xDB,0x89};
    static const std::uint8_t reload_prefix[] = {
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
        0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
        0x89,0x7C,0x24,0x20,0x41,0x54,0x48,0x83,
        0xEC,0x40};
    static const std::uint8_t load_prefix[] = {
        0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x83,0xEC,0x38,0x83,0x3D,0x6D,0x12,
        0x9C,0x00,0x00,0x4C,0x8B,0xE1};
    FakeCodeImage image = {};
    std::memcpy(
        image.bytes +
            (reload::kAudioScriptClearRva - kFakeCodeFirstRva),
        clear_prefix, sizeof(clear_prefix));
    std::memcpy(
        image.bytes +
            (reload::kAudioScriptReloadRva - kFakeCodeFirstRva),
        reload_prefix, sizeof(reload_prefix));
    std::memcpy(
        image.bytes +
            (reload::kAudioScriptLoadRva - kFakeCodeFirstRva),
        load_prefix, sizeof(load_prefix));
    WriteRelativeCall(
        &image, reload::kAudioScriptReloadRva + 0x2BU,
        reload::kAudioScriptClearRva);
    WriteRelativeCall(
        &image, reload::kAudioScriptReloadRva + 0x61U,
        reload::kAudioScriptLoadRva);
    return image;
}

bool TestExactTargetValidation() noexcept
{
    FakeCodeImage image = ValidatedCodeImage();
    reload::AudioScriptReloadTargetInput input = {};
    input.engine_base = kFakeEngineBase;
    input.read_code = &ReadFakeCode;
    input.user = &image;
    input.exact_engine_image_validated = true;
    CHECK(reload::ValidateAudioScriptReloadTarget(&input));

    input.exact_engine_image_validated = false;
    CHECK(!reload::ValidateAudioScriptReloadTarget(&input));
    input.exact_engine_image_validated = true;

    const std::size_t load_call_offset = static_cast<std::size_t>(
        reload::kAudioScriptReloadRva + 0x61U -
        kFakeCodeFirstRva);
    image.bytes[load_call_offset + 1U] ^= 0x01U;
    CHECK(!reload::ValidateAudioScriptReloadTarget(&input));
    CHECK(image.bytes[load_call_offset] == 0xE8U);
    return true;
}

struct FakeWorld {
    snapshot::AudioScriptRegistrySemanticSnapshot current;
    snapshot::AudioScriptRegistrySemanticSnapshot baseline;
    std::uint32_t reload_calls;
    bool project_source_present;
    bool fail_reload;
};

snapshot::AudioScriptRegistrySemanticSnapshot Script(
    std::uint32_t definitions, std::uint32_t paths,
    std::uint64_t fingerprint) noexcept
{
    snapshot::AudioScriptRegistrySemanticSnapshot value = {};
    value.registry_owner = 0xAD52E0U;
    value.definition_count = definitions;
    value.path_count = paths;
    value.procedure_count = 17U;
    value.property_group_count = 23U;
    value.semantic_fingerprint = fingerprint;
    value.complete = true;
    return value;
}

bool InvokeReload(void* user) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world) return false;
    ++world->reload_calls;
    if (world->fail_reload) return false;
    /* Reload copies the current path list, clears the registry, then replays
     * it. Once the project VFS source is absent, that path fails and is not
     * reinserted; every baseline path resolves. */
    if (!world->project_source_present)
        world->current = world->baseline;
    return true;
}

bool SnapshotRegistry(
    void* user,
    snapshot::AudioScriptRegistrySemanticSnapshot* output) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || !output) return false;
    *output = world->current;
    return true;
}

reload::AudioScriptReloadAuthorization Authorization(
    bool sources_removed) noexcept
{
    reload::AudioScriptReloadAuthorization authorization = {};
    authorization.current_thread_id = 19U;
    authorization.game_thread_id = 19U;
    authorization.target_validated = true;
    authorization.receiver_validated = true;
    authorization.abi_validated = true;
    authorization.audio_lock_owned = true;
    authorization.recursion_idle = true;
    authorization.parser_idle = true;
    authorization.bank_mutation_idle = true;
    authorization.execution_fence_idle = true;
    authorization.baseline_paths_complete = true;
    authorization.outgoing_sources_removed = sources_removed;
    return authorization;
}

bool TestReloadOrderingAndSemanticVerification() noexcept
{
    FakeWorld world = {};
    world.baseline = Script(138U, 8U, 0x1010U);
    world.current = Script(139U, 9U, 0x2020U);
    world.project_source_present = true;
    const reload::AudioScriptReloadOperations operations = {
        &world, &InvokeReload, &SnapshotRegistry};
    reload::AudioScriptReloadAdapter adapter = {};
    reload::InitializeAudioScriptReloadAdapter(&adapter, &operations);
    CHECK(reload::CaptureAudioScriptReloadBaseline(
        &adapter, 1U, world.baseline));

    const reload::AudioScriptReloadAuthorization before_removal =
        Authorization(false);
    CHECK(reload::PrepareAudioScriptReload(
        &adapter, before_removal));
    CHECK(!reload::ExecuteAndVerifyAudioScriptReload(
        &adapter, before_removal));
    CHECK(adapter.last_failure ==
          reload::AudioScriptReloadFailure::ReloadBeforeSourceRemoval);
    CHECK(world.reload_calls == 0U);

    world.project_source_present = false;
    const reload::AudioScriptReloadAuthorization authorization =
        Authorization(true);
    CHECK(reload::ExecuteAndVerifyAudioScriptReload(
        &adapter, authorization));
    CHECK(adapter.mutation_started);
    CHECK(adapter.verified);
    CHECK(world.reload_calls == 1U);
    CHECK(world.current.definition_count == 138U);
    CHECK(world.current.path_count == 8U);
    CHECK(world.current.semantic_fingerprint ==
          world.baseline.semantic_fingerprint);
    return true;
}

bool TestExecutionFenceRejectsBeforeReload() noexcept
{
    FakeWorld world = {};
    world.baseline = Script(138U, 8U, 0x1010U);
    world.current = Script(139U, 9U, 0x2020U);
    const reload::AudioScriptReloadOperations operations = {
        &world, &InvokeReload, &SnapshotRegistry};
    reload::AudioScriptReloadAdapter adapter = {};
    reload::InitializeAudioScriptReloadAdapter(&adapter, &operations);
    CHECK(reload::CaptureAudioScriptReloadBaseline(
        &adapter, 1U, world.baseline));
    reload::AudioScriptReloadAuthorization authorization =
        Authorization(true);
    authorization.execution_fence_idle = false;
    CHECK(!reload::PrepareAudioScriptReload(
        &adapter, authorization));
    CHECK(adapter.last_failure ==
          reload::AudioScriptReloadFailure::ExecutionFenceBusy);
    CHECK(!adapter.mutation_started);
    CHECK(world.reload_calls == 0U);
    return true;
}

bool TestReceiverThreadLockAndMutationPredicatesReject() noexcept
{
    struct Case {
        std::uint32_t selector;
        reload::AudioScriptReloadFailure failure;
    };
    const Case cases[] = {
        {1U, reload::AudioScriptReloadFailure::WrongThread},
        {2U, reload::AudioScriptReloadFailure::ReceiverUnvalidated},
        {3U, reload::AudioScriptReloadFailure::AbiUnvalidated},
        {4U, reload::AudioScriptReloadFailure::AudioLockNotOwned},
        {5U, reload::AudioScriptReloadFailure::RecursionActive},
        {6U, reload::AudioScriptReloadFailure::ParserActive},
        {7U, reload::AudioScriptReloadFailure::BankMutationActive},
    };
    for (const Case& test : cases) {
        FakeWorld world = {};
        world.baseline = Script(138U, 8U, 0x1010U);
        world.current = Script(139U, 9U, 0x2020U);
        const reload::AudioScriptReloadOperations operations = {
            &world, &InvokeReload, &SnapshotRegistry};
        reload::AudioScriptReloadAdapter adapter = {};
        reload::InitializeAudioScriptReloadAdapter(
            &adapter, &operations);
        CHECK(reload::CaptureAudioScriptReloadBaseline(
            &adapter, 1U, world.baseline));
        reload::AudioScriptReloadAuthorization authorization =
            Authorization(false);
        switch (test.selector) {
        case 1U: authorization.current_thread_id = 20U; break;
        case 2U: authorization.receiver_validated = false; break;
        case 3U: authorization.abi_validated = false; break;
        case 4U: authorization.audio_lock_owned = false; break;
        case 5U: authorization.recursion_idle = false; break;
        case 6U: authorization.parser_idle = false; break;
        case 7U: authorization.bank_mutation_idle = false; break;
        default: return false;
        }
        CHECK(!reload::PrepareAudioScriptReload(
            &adapter, authorization));
        CHECK(adapter.last_failure == test.failure);
        CHECK(!adapter.mutation_started);
        CHECK(world.reload_calls == 0U);
    }
    return true;
}

}  // namespace

int main()
{
    if (!TestExactTargetValidation() ||
        !TestReloadOrderingAndSemanticVerification() ||
        !TestExecutionFenceRejectsBeforeReload() ||
        !TestReceiverThreadLockAndMutationPredicatesReject())
        return 1;
    std::puts("audio script reload fake harness: PASS");
    return 0;
}
