#ifndef DLUMA_FIXES_PLAYER_STATE_INVENTORY_BOOTSTRAP_PROOF_H
#define DLUMA_FIXES_PLAYER_STATE_INVENTORY_BOOTSTRAP_PROOF_H

#include "PlayerGameplayParamsRestartAdapter.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart {

/* Exact supported-image proof for the interval in which the engine is inside
 * the loader proxy's InitializeGameScriptDLL callback.  The engine has stored
 * the callback address at this point, but it has not yet published the
 * returned game-script manager or constructed CFactory. */
constexpr std::uintptr_t kEngineInitializeCallbackGlobalRva = 0x00ACCC58U;
constexpr std::uintptr_t kEnginePublishedScriptManagerGlobalRva =
    0x00ACCC68U;
constexpr std::uintptr_t kEngineFactoryGlobalRva = 0x00ACCCD0U;
constexpr std::uintptr_t kEngineInitializeCallbackCallRva = 0x0024B9A9U;
constexpr std::uintptr_t kEngineFactoryPublishRva = 0x0024BA3DU;

constexpr std::uintptr_t kBootstrapPlayerStateConstructorRva = 0x00BB5D40U;
constexpr std::uintptr_t kBootstrapPlayerStateCompleteDestructorRva =
    0x00BB62F0U;
constexpr std::uintptr_t kBootstrapPlayerStateRttiFactoryRva = 0x00BBF2A0U;
constexpr std::uintptr_t kBootstrapPlayerStateRttiInitializerRva =
    0x0134D840U;
constexpr std::uintptr_t kBootstrapPlayerStateRttiObjectRva = 0x01D889B0U;
constexpr std::uintptr_t kBootstrapPlayerStateRttiVtableRva = 0x017E37D8U;
constexpr std::uintptr_t kBootstrapPlayerStateRttiFactorySlot0Rva =
    0x017E37F0U;
constexpr std::uintptr_t kBootstrapPlayerStateRttiFactorySlot1Rva =
    0x017E37F8U;
constexpr std::uintptr_t kBootstrapLocalPlayerStateGlobalRva = 0x01D07698U;
constexpr std::uintptr_t kBootstrapCharacterPlayerStateGlobalRva =
    0x01D078F0U;

enum class PlayerInventoryBootstrapProofFailure : std::uint8_t {
    None,
    InvalidArgument,
    UnsupportedImage,
    NotBeforeOriginalInitializer,
    AddressOverflow,
    EngineOrderMismatch,
    EngineCallbackUnreadable,
    EngineCallbackMismatch,
    EngineScriptManagerUnreadable,
    EngineScriptManagerAlreadyPublished,
    EngineFactoryUnreadable,
    EngineFactoryAlreadyPublished,
    PlayerRttiInitializerMismatch,
    PlayerRttiObjectUnreadable,
    PlayerRttiObjectMismatch,
    PlayerRttiCreatorMismatch,
    PlayerConstructorMismatch,
    PlayerCompleteDestructorMismatch,
    GlobalSlotsUnreadable,
    ExistingGlobalState,
    ConstructorRouteUnavailable,
    CompleteDestructorRouteUnavailable,
    HookActiveDuringBootstrap,
    PrecoverageObservationLoss,
};

using ReadPlayerInventoryBootstrapMemoryFn = bool (*)(
    void* user, std::uintptr_t address, void* output,
    std::size_t size) noexcept;

struct PlayerInventoryBootstrapProofInput {
    std::uintptr_t engine_base;
    std::uintptr_t gamedll_base;
    std::uintptr_t proxy_initialize_callback;
    std::uintptr_t before_local_process_global_state;
    std::uintptr_t before_character_manager_global_state;
    std::uint32_t constructor_active_calls;
    std::uint32_t complete_destructor_active_calls;
    ReadPlayerInventoryBootstrapMemoryFn read_memory;
    void* read_memory_user;
    bool exact_engine_image_validated;
    bool exact_gamedll_image_validated;
    bool before_original_game_script_initializer;
    bool before_global_slots_readable;
    bool constructor_original_prefix_validated;
    bool complete_destructor_original_prefix_validated;
    bool constructor_route_installed;
    bool complete_destructor_route_installed;
    bool precoverage_observation_loss;
};

struct PlayerInventoryBootstrapProofResult {
    PlayerInventoryBootstrapEvidence evidence;
    PlayerInventoryBootstrapProofFailure failure;
    bool engine_callback_owned;
    bool engine_script_manager_unpublished;
    bool engine_factory_unavailable;
    bool player_rtti_static_audit_validated;
    bool player_rtti_creator_validated;
};

/* This routine observes and validates only.  It does not install a hook,
 * enumerate a heap, allocate memory, or call any engine/game function. */
bool ProvePlayerStateInventoryBootstrap(
    const PlayerInventoryBootstrapProofInput* input,
    PlayerInventoryBootstrapProofResult* output) noexcept;

const char* PlayerInventoryBootstrapProofFailureName(
    PlayerInventoryBootstrapProofFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart

#endif
