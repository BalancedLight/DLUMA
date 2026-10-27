#include "fixes/custom_map_asset_isolation/PlayerStateInventoryBootstrapProof.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace player =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart;

namespace {

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,  \
                         #expression);                                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

constexpr std::uintptr_t kEngineBase = 0x0000000180000000ULL;
constexpr std::uintptr_t kGamedllBase = 0x0000000280000000ULL;
constexpr std::uintptr_t kProxyInitialize = 0x0000000380001000ULL;

constexpr unsigned char kEngineOrderPrefix[] = {
    0x48, 0x8B, 0x3D, 0xAF, 0x12, 0x88, 0x00,
    0xFF, 0xD7,
    0x48, 0x8B, 0xF8,
    0x48, 0x89, 0x05, 0xB3, 0x12, 0x88, 0x00,
    0x48, 0x85, 0xC0,
    0x75, 0x39,
};
constexpr unsigned char kPlayerRttiInitializerPrefix[] = {
    0x48, 0x83, 0xEC, 0x68,
    0x8B, 0x05, 0x5A, 0x5E, 0xAB, 0x00,
    0xA8, 0x01,
    0x75, 0x28,
    0x83, 0xC8, 0x01,
};
constexpr unsigned char kPlayerRttiFactoryPrefix[] = {
    0x48, 0x83, 0xEC, 0x28,
    0xB9, 0xB0, 0x04, 0x00, 0x00,
    0xFF, 0x15, 0xE1, 0xCE, 0x80, 0x00,
    0x48, 0x85, 0xC0,
    0x74, 0x0C,
    0x48, 0x8B, 0xC8,
    0x48, 0x83, 0xC4, 0x28,
    0xE9, 0x80, 0x6A, 0xFF, 0xFF,
};
enum class Corruption : std::uint8_t {
    None,
    EngineOrder,
    PlayerRttiInitializer,
    PlayerRttiFactory,
};

struct FakeMemory {
    std::uintptr_t engine_callback = kProxyInitialize;
    std::uintptr_t engine_script_manager = 0;
    std::uintptr_t engine_factory = 0;
    std::uintptr_t player_rtti_object =
        kGamedllBase + player::kBootstrapPlayerStateRttiVtableRva;
    std::uintptr_t player_rtti_slot0 =
        kGamedllBase + player::kBootstrapPlayerStateRttiFactoryRva;
    std::uintptr_t player_rtti_slot1 =
        kGamedllBase + player::kBootstrapPlayerStateRttiFactoryRva;
    std::uintptr_t local_player_state = 0;
    std::uintptr_t character_player_state = 0;
    std::uintptr_t unreadable_address = 0;
    Corruption corruption = Corruption::None;
};

bool CopyBytes(void* output, std::size_t size,
               const unsigned char* source, std::size_t source_size,
               bool corrupt) noexcept
{
    if (!output || size != source_size) return false;
    std::memcpy(output, source, size);
    if (corrupt)
        static_cast<unsigned char*>(output)[0] ^= 0xFFU;
    return true;
}

bool ReadFakeMemory(void* user, std::uintptr_t address, void* output,
                    std::size_t size) noexcept
{
    FakeMemory* memory = static_cast<FakeMemory*>(user);
    if (!memory || !output || address == memory->unreadable_address)
        return false;
    if (address == kEngineBase +
            player::kEngineInitializeCallbackCallRva - 7U)
        return CopyBytes(
            output, size, kEngineOrderPrefix, sizeof(kEngineOrderPrefix),
            memory->corruption == Corruption::EngineOrder);
    if (address == kGamedllBase +
            player::kBootstrapPlayerStateRttiInitializerRva)
        return CopyBytes(
            output, size, kPlayerRttiInitializerPrefix,
            sizeof(kPlayerRttiInitializerPrefix),
            memory->corruption == Corruption::PlayerRttiInitializer);
    if (address == kGamedllBase +
            player::kBootstrapPlayerStateRttiFactoryRva)
        return CopyBytes(
            output, size, kPlayerRttiFactoryPrefix,
            sizeof(kPlayerRttiFactoryPrefix),
            memory->corruption == Corruption::PlayerRttiFactory);
    const std::uintptr_t* value = nullptr;
    if (address == kEngineBase + player::kEngineInitializeCallbackGlobalRva)
        value = &memory->engine_callback;
    else if (address == kEngineBase +
                 player::kEnginePublishedScriptManagerGlobalRva)
        value = &memory->engine_script_manager;
    else if (address == kEngineBase + player::kEngineFactoryGlobalRva)
        value = &memory->engine_factory;
    else if (address == kGamedllBase +
                 player::kBootstrapPlayerStateRttiObjectRva)
        value = &memory->player_rtti_object;
    else if (address == kGamedllBase +
                 player::kBootstrapPlayerStateRttiFactorySlot0Rva)
        value = &memory->player_rtti_slot0;
    else if (address == kGamedllBase +
                 player::kBootstrapPlayerStateRttiFactorySlot1Rva)
        value = &memory->player_rtti_slot1;
    else if (address == kGamedllBase +
                 player::kBootstrapLocalPlayerStateGlobalRva)
        value = &memory->local_player_state;
    else if (address == kGamedllBase +
                 player::kBootstrapCharacterPlayerStateGlobalRva)
        value = &memory->character_player_state;
    if (!value || size != sizeof(*value)) return false;
    std::memcpy(output, value, size);
    return true;
}

player::PlayerInventoryBootstrapProofInput CompleteInput(
    FakeMemory* memory) noexcept
{
    player::PlayerInventoryBootstrapProofInput input = {};
    input.engine_base = kEngineBase;
    input.gamedll_base = kGamedllBase;
    input.proxy_initialize_callback = kProxyInitialize;
    input.read_memory = ReadFakeMemory;
    input.read_memory_user = memory;
    input.exact_engine_image_validated = true;
    input.exact_gamedll_image_validated = true;
    input.before_original_game_script_initializer = true;
    input.before_global_slots_readable = true;
    input.constructor_original_prefix_validated = true;
    input.complete_destructor_original_prefix_validated = true;
    input.constructor_route_installed = true;
    input.complete_destructor_route_installed = true;
    return input;
}

bool ExpectFailure(
    player::PlayerInventoryBootstrapProofInput* input,
    player::PlayerInventoryBootstrapProofFailure expected) noexcept
{
    player::PlayerInventoryBootstrapProofResult result = {};
    CHECK(!player::ProvePlayerStateInventoryBootstrap(input, &result));
    CHECK(result.failure == expected);
    CHECK(!result.evidence.closed_creator_set_validated);
    return true;
}

bool TestCompleteProof() noexcept
{
    FakeMemory memory;
    player::PlayerInventoryBootstrapProofInput input =
        CompleteInput(&memory);
    player::PlayerInventoryBootstrapProofResult result = {};
    CHECK(player::ProvePlayerStateInventoryBootstrap(&input, &result));
    CHECK(result.failure ==
          player::PlayerInventoryBootstrapProofFailure::None);
    CHECK(result.engine_callback_owned);
    CHECK(result.engine_script_manager_unpublished);
    CHECK(result.engine_factory_unavailable);
    CHECK(result.player_rtti_static_audit_validated);
    CHECK(result.player_rtti_creator_validated);
    CHECK(result.evidence.supported_image);
    CHECK(result.evidence.before_original_game_script_initializer);
    CHECK(result.evidence.closed_creator_set_validated);
    CHECK(result.evidence.constructor_route_installed);
    CHECK(result.evidence.complete_destructor_route_installed);
    CHECK(result.evidence.before_global_slots_readable);
    CHECK(result.evidence.after_global_slots_readable);
    return true;
}

bool TestEnginePublicationFailures() noexcept
{
    {
        FakeMemory memory;
        memory.engine_callback = kProxyInitialize + 1U;
        auto input = CompleteInput(&memory);
        CHECK(ExpectFailure(
            &input, player::PlayerInventoryBootstrapProofFailure::
                        EngineCallbackMismatch));
    }
    {
        FakeMemory memory;
        memory.engine_script_manager = 0x44U;
        auto input = CompleteInput(&memory);
        CHECK(ExpectFailure(
            &input, player::PlayerInventoryBootstrapProofFailure::
                        EngineScriptManagerAlreadyPublished));
    }
    {
        FakeMemory memory;
        memory.engine_factory = 0x55U;
        auto input = CompleteInput(&memory);
        CHECK(ExpectFailure(
            &input, player::PlayerInventoryBootstrapProofFailure::
                        EngineFactoryAlreadyPublished));
    }
    {
        FakeMemory memory;
        memory.unreadable_address =
            kEngineBase + player::kEngineFactoryGlobalRva;
        auto input = CompleteInput(&memory);
        CHECK(ExpectFailure(
            &input, player::PlayerInventoryBootstrapProofFailure::
                        EngineFactoryUnreadable));
    }
    return true;
}

bool TestExactProfileFailures() noexcept
{
    for (int raw = static_cast<int>(Corruption::EngineOrder);
         raw <= static_cast<int>(Corruption::PlayerRttiFactory); ++raw) {
        FakeMemory memory;
        memory.corruption = static_cast<Corruption>(raw);
        auto input = CompleteInput(&memory);
        player::PlayerInventoryBootstrapProofFailure expected =
            player::PlayerInventoryBootstrapProofFailure::EngineOrderMismatch;
        if (memory.corruption == Corruption::PlayerRttiInitializer)
            expected = player::PlayerInventoryBootstrapProofFailure::
                PlayerRttiInitializerMismatch;
        else if (memory.corruption == Corruption::PlayerRttiFactory)
            expected = player::PlayerInventoryBootstrapProofFailure::
                PlayerRttiCreatorMismatch;
        CHECK(ExpectFailure(&input, expected));
    }
    {
        FakeMemory memory;
        memory.player_rtti_object += 8U;
        auto input = CompleteInput(&memory);
        CHECK(ExpectFailure(
            &input, player::PlayerInventoryBootstrapProofFailure::
                        PlayerRttiObjectMismatch));
    }
    {
        FakeMemory memory;
        memory.player_rtti_slot1 += 8U;
        auto input = CompleteInput(&memory);
        CHECK(ExpectFailure(
            &input, player::PlayerInventoryBootstrapProofFailure::
                        PlayerRttiCreatorMismatch));
    }
    return true;
}

bool TestLifecycleAndRouteFailures() noexcept
{
    for (int test = 0; test != 12; ++test) {
        FakeMemory memory;
        auto input = CompleteInput(&memory);
        player::PlayerInventoryBootstrapProofFailure expected =
            player::PlayerInventoryBootstrapProofFailure::UnsupportedImage;
        switch (test) {
        case 0: input.exact_engine_image_validated = false; break;
        case 1: input.exact_gamedll_image_validated = false; break;
        case 2:
            input.before_original_game_script_initializer = false;
            expected = player::PlayerInventoryBootstrapProofFailure::
                NotBeforeOriginalInitializer;
            break;
        case 3:
            input.before_global_slots_readable = false;
            expected = player::PlayerInventoryBootstrapProofFailure::
                GlobalSlotsUnreadable;
            break;
        case 4:
            input.before_local_process_global_state = 1U;
            expected = player::PlayerInventoryBootstrapProofFailure::
                ExistingGlobalState;
            break;
        case 5:
            memory.character_player_state = 2U;
            expected = player::PlayerInventoryBootstrapProofFailure::
                ExistingGlobalState;
            break;
        case 6:
            input.constructor_original_prefix_validated = false;
            expected = player::PlayerInventoryBootstrapProofFailure::
                PlayerConstructorMismatch;
            break;
        case 7:
            input.complete_destructor_original_prefix_validated = false;
            expected = player::PlayerInventoryBootstrapProofFailure::
                PlayerCompleteDestructorMismatch;
            break;
        case 8:
            input.constructor_route_installed = false;
            expected = player::PlayerInventoryBootstrapProofFailure::
                ConstructorRouteUnavailable;
            break;
        case 9:
            input.complete_destructor_route_installed = false;
            expected = player::PlayerInventoryBootstrapProofFailure::
                CompleteDestructorRouteUnavailable;
            break;
        case 10:
            input.constructor_active_calls = 1U;
            expected = player::PlayerInventoryBootstrapProofFailure::
                HookActiveDuringBootstrap;
            break;
        case 11:
            input.precoverage_observation_loss = true;
            expected = player::PlayerInventoryBootstrapProofFailure::
                PrecoverageObservationLoss;
            break;
        }
        CHECK(ExpectFailure(&input, expected));
    }
    return true;
}

}  // namespace

int main()
{
    if (!TestCompleteProof() || !TestEnginePublicationFailures() ||
        !TestExactProfileFailures() || !TestLifecycleAndRouteFailures())
        return 1;
    std::puts("player_state_inventory_bootstrap_proof_fake_harness: ok");
    return 0;
}
