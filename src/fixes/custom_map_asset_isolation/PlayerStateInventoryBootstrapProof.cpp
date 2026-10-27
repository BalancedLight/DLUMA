#define _HAS_EXCEPTIONS 0

#include "PlayerStateInventoryBootstrapProof.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart {
namespace {

/* These prefixes are from the exact supported Player/engine images.  The
 * full-image SHA-256 gates in production remain mandatory; the prefixes keep
 * the specific semantic facts independently visible and fail closed if a
 * future profile accidentally reuses the same RVAs.
 *
 * engine 0x24B9A2:
 *   load stored initializer callback; call it; publish its result; only then
 *   allocate/publish CFactory.
 */
constexpr unsigned char kEngineOrderPrefix[] = {
    0x48, 0x8B, 0x3D, 0xAF, 0x12, 0x88, 0x00,
    0xFF, 0xD7,
    0x48, 0x8B, 0xF8,
    0x48, 0x89, 0x05, 0xB3, 0x12, 0x88, 0x00,
    0x48, 0x85, 0xC0,
    0x75, 0x39,
};

/* PlayerState's dynamic initializer registers the RTTI descriptor and writes
 * CRTTIGameScript<PlayerState>'s vtable.  The exact-image static-initializer
 * audit found no PlayerState instance construction: the only direct branch
 * to the constructor is the tail branch in the RTTI allocator below. */
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

bool AddRva(std::uintptr_t base, std::uintptr_t rva,
            std::uintptr_t* output) noexcept
{
    if (!output || !base || base > UINTPTR_MAX - rva) return false;
    *output = base + rva;
    return true;
}

bool ReadExact(const PlayerInventoryBootstrapProofInput& input,
               std::uintptr_t base, std::uintptr_t rva,
               const unsigned char* expected,
               std::size_t size) noexcept
{
    if (!expected || !size || size > sizeof(kPlayerRttiFactoryPrefix))
        return false;
    std::uintptr_t address = 0;
    unsigned char actual[sizeof(kPlayerRttiFactoryPrefix)] = {};
    return AddRva(base, rva, &address) &&
        input.read_memory(input.read_memory_user, address, actual, size) &&
        std::memcmp(actual, expected, size) == 0;
}

bool ReadPointer(const PlayerInventoryBootstrapProofInput& input,
                 std::uintptr_t base, std::uintptr_t rva,
                 std::uintptr_t* output) noexcept
{
    std::uintptr_t address = 0;
    if (!output || !AddRva(base, rva, &address)) return false;
    return input.read_memory(
        input.read_memory_user, address, output, sizeof(*output));
}

void Fail(PlayerInventoryBootstrapProofResult* output,
          PlayerInventoryBootstrapProofFailure failure) noexcept
{
    std::memset(output, 0, sizeof(*output));
    output->failure = failure;
}

}  // namespace

bool ProvePlayerStateInventoryBootstrap(
    const PlayerInventoryBootstrapProofInput* input,
    PlayerInventoryBootstrapProofResult* output) noexcept
{
    if (!output) return false;
    Fail(output, PlayerInventoryBootstrapProofFailure::InvalidArgument);
    if (!input || !input->read_memory || !input->engine_base ||
        !input->gamedll_base || !input->proxy_initialize_callback)
        return false;
    if (input->engine_base >
            UINTPTR_MAX - kEngineFactoryGlobalRva ||
        input->gamedll_base >
            UINTPTR_MAX - kBootstrapPlayerStateRttiObjectRva) {
        Fail(output, PlayerInventoryBootstrapProofFailure::AddressOverflow);
        return false;
    }
    if (!input->exact_engine_image_validated ||
        !input->exact_gamedll_image_validated) {
        Fail(output, PlayerInventoryBootstrapProofFailure::UnsupportedImage);
        return false;
    }
    if (!input->before_original_game_script_initializer) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         NotBeforeOriginalInitializer);
        return false;
    }

    if (!ReadExact(*input, input->engine_base,
                   kEngineInitializeCallbackCallRva - 7U,
                   kEngineOrderPrefix, sizeof(kEngineOrderPrefix))) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineOrderMismatch);
        return false;
    }

    std::uintptr_t pointer = 0;
    if (!ReadPointer(*input, input->engine_base,
                     kEngineInitializeCallbackGlobalRva, &pointer)) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineCallbackUnreadable);
        return false;
    }
    if (pointer != input->proxy_initialize_callback) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineCallbackMismatch);
        return false;
    }
    output->engine_callback_owned = true;

    if (!ReadPointer(*input, input->engine_base,
                     kEnginePublishedScriptManagerGlobalRva, &pointer)) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineScriptManagerUnreadable);
        return false;
    }
    if (pointer) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineScriptManagerAlreadyPublished);
        return false;
    }
    output->engine_callback_owned = true;
    output->engine_script_manager_unpublished = true;

    if (!ReadPointer(*input, input->engine_base,
                     kEngineFactoryGlobalRva, &pointer)) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineFactoryUnreadable);
        return false;
    }
    if (pointer) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         EngineFactoryAlreadyPublished);
        return false;
    }
    output->engine_callback_owned = true;
    output->engine_script_manager_unpublished = true;
    output->engine_factory_unavailable = true;

    if (!ReadExact(*input, input->gamedll_base,
                   kBootstrapPlayerStateRttiInitializerRva,
                   kPlayerRttiInitializerPrefix,
                   sizeof(kPlayerRttiInitializerPrefix))) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PlayerRttiInitializerMismatch);
        return false;
    }
    output->engine_callback_owned = true;
    output->engine_script_manager_unpublished = true;
    output->engine_factory_unavailable = true;
    output->player_rtti_static_audit_validated = true;

    std::uintptr_t expected = 0;
    if (!AddRva(input->gamedll_base,
                kBootstrapPlayerStateRttiVtableRva, &expected) ||
        !ReadPointer(*input, input->gamedll_base,
                     kBootstrapPlayerStateRttiObjectRva, &pointer)) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PlayerRttiObjectUnreadable);
        return false;
    }
    if (pointer != expected) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PlayerRttiObjectMismatch);
        return false;
    }

    if (!ReadExact(*input, input->gamedll_base,
                   kBootstrapPlayerStateRttiFactoryRva,
                   kPlayerRttiFactoryPrefix,
                   sizeof(kPlayerRttiFactoryPrefix)) ||
        !AddRva(input->gamedll_base,
                kBootstrapPlayerStateRttiFactoryRva, &expected) ||
        !ReadPointer(*input, input->gamedll_base,
                     kBootstrapPlayerStateRttiFactorySlot0Rva,
                     &pointer) || pointer != expected ||
        !ReadPointer(*input, input->gamedll_base,
                     kBootstrapPlayerStateRttiFactorySlot1Rva,
                     &pointer) || pointer != expected) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PlayerRttiCreatorMismatch);
        return false;
    }
    output->engine_callback_owned = true;
    output->engine_script_manager_unpublished = true;
    output->engine_factory_unavailable = true;
    output->player_rtti_static_audit_validated = true;
    output->player_rtti_creator_validated = true;

    /* The function entries contain loader routing after installation.  Use
     * the hook plan's preserved pre-install validation result rather than
     * rereading and mistaking loader-owned detours for an image mismatch. */
    if (!input->constructor_original_prefix_validated) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PlayerConstructorMismatch);
        return false;
    }
    if (!input->complete_destructor_original_prefix_validated) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PlayerCompleteDestructorMismatch);
        return false;
    }
    if (!input->before_global_slots_readable) {
        Fail(output,
             PlayerInventoryBootstrapProofFailure::GlobalSlotsUnreadable);
        return false;
    }
    if (input->before_local_process_global_state ||
        input->before_character_manager_global_state) {
        Fail(output,
             PlayerInventoryBootstrapProofFailure::ExistingGlobalState);
        return false;
    }

    std::uintptr_t after_local = 0;
    std::uintptr_t after_character = 0;
    if (!ReadPointer(*input, input->gamedll_base,
                     kBootstrapLocalPlayerStateGlobalRva, &after_local) ||
        !ReadPointer(*input, input->gamedll_base,
                     kBootstrapCharacterPlayerStateGlobalRva,
                     &after_character)) {
        Fail(output,
             PlayerInventoryBootstrapProofFailure::GlobalSlotsUnreadable);
        return false;
    }
    if (after_local || after_character) {
        Fail(output,
             PlayerInventoryBootstrapProofFailure::ExistingGlobalState);
        return false;
    }
    if (!input->constructor_route_installed) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         ConstructorRouteUnavailable);
        return false;
    }
    if (!input->complete_destructor_route_installed) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         CompleteDestructorRouteUnavailable);
        return false;
    }
    if (input->constructor_active_calls ||
        input->complete_destructor_active_calls) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         HookActiveDuringBootstrap);
        return false;
    }
    if (input->precoverage_observation_loss) {
        Fail(output, PlayerInventoryBootstrapProofFailure::
                         PrecoverageObservationLoss);
        return false;
    }

    std::memset(&output->evidence, 0, sizeof(output->evidence));
    output->evidence.before_local_process_global_state =
        input->before_local_process_global_state;
    output->evidence.before_character_manager_global_state =
        input->before_character_manager_global_state;
    output->evidence.after_local_process_global_state = after_local;
    output->evidence.after_character_manager_global_state = after_character;
    output->evidence.constructor_active_calls =
        input->constructor_active_calls;
    output->evidence.complete_destructor_active_calls =
        input->complete_destructor_active_calls;
    output->evidence.supported_image = true;
    output->evidence.before_original_game_script_initializer = true;
    output->evidence.closed_creator_set_validated = true;
    output->evidence.constructor_route_installed = true;
    output->evidence.complete_destructor_route_installed = true;
    output->evidence.before_global_slots_readable = true;
    output->evidence.after_global_slots_readable = true;
    output->failure = PlayerInventoryBootstrapProofFailure::None;
    output->engine_callback_owned = true;
    output->engine_script_manager_unpublished = true;
    output->engine_factory_unavailable = true;
    output->player_rtti_static_audit_validated = true;
    output->player_rtti_creator_validated = true;
    return true;
}

const char* PlayerInventoryBootstrapProofFailureName(
    PlayerInventoryBootstrapProofFailure failure) noexcept
{
    switch (failure) {
    case PlayerInventoryBootstrapProofFailure::None: return "none";
    case PlayerInventoryBootstrapProofFailure::InvalidArgument:
        return "invalid_argument";
    case PlayerInventoryBootstrapProofFailure::UnsupportedImage:
        return "unsupported_image";
    case PlayerInventoryBootstrapProofFailure::NotBeforeOriginalInitializer:
        return "not_before_original_initializer";
    case PlayerInventoryBootstrapProofFailure::AddressOverflow:
        return "address_overflow";
    case PlayerInventoryBootstrapProofFailure::EngineOrderMismatch:
        return "engine_order_mismatch";
    case PlayerInventoryBootstrapProofFailure::EngineCallbackUnreadable:
        return "engine_callback_unreadable";
    case PlayerInventoryBootstrapProofFailure::EngineCallbackMismatch:
        return "engine_callback_mismatch";
    case PlayerInventoryBootstrapProofFailure::EngineScriptManagerUnreadable:
        return "engine_script_manager_unreadable";
    case PlayerInventoryBootstrapProofFailure::
            EngineScriptManagerAlreadyPublished:
        return "engine_script_manager_already_published";
    case PlayerInventoryBootstrapProofFailure::EngineFactoryUnreadable:
        return "engine_factory_unreadable";
    case PlayerInventoryBootstrapProofFailure::EngineFactoryAlreadyPublished:
        return "engine_factory_already_published";
    case PlayerInventoryBootstrapProofFailure::PlayerRttiInitializerMismatch:
        return "player_rtti_initializer_mismatch";
    case PlayerInventoryBootstrapProofFailure::PlayerRttiObjectUnreadable:
        return "player_rtti_object_unreadable";
    case PlayerInventoryBootstrapProofFailure::PlayerRttiObjectMismatch:
        return "player_rtti_object_mismatch";
    case PlayerInventoryBootstrapProofFailure::PlayerRttiCreatorMismatch:
        return "player_rtti_creator_mismatch";
    case PlayerInventoryBootstrapProofFailure::PlayerConstructorMismatch:
        return "player_constructor_mismatch";
    case PlayerInventoryBootstrapProofFailure::
            PlayerCompleteDestructorMismatch:
        return "player_complete_destructor_mismatch";
    case PlayerInventoryBootstrapProofFailure::GlobalSlotsUnreadable:
        return "global_slots_unreadable";
    case PlayerInventoryBootstrapProofFailure::ExistingGlobalState:
        return "existing_global_state";
    case PlayerInventoryBootstrapProofFailure::ConstructorRouteUnavailable:
        return "constructor_route_unavailable";
    case PlayerInventoryBootstrapProofFailure::
            CompleteDestructorRouteUnavailable:
        return "complete_destructor_route_unavailable";
    case PlayerInventoryBootstrapProofFailure::HookActiveDuringBootstrap:
        return "hook_active_during_bootstrap";
    case PlayerInventoryBootstrapProofFailure::PrecoverageObservationLoss:
        return "precoverage_observation_loss";
    }
    return "unknown";
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart
