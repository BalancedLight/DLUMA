#ifndef DLUMA_AUDIO_BASELINE_REPLAY_H
#define DLUMA_AUDIO_BASELINE_REPLAY_H
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_baseline_replay {
constexpr std::size_t kMaximumReplayEntries = 512U;
constexpr std::size_t kReplayPathCapacity = 512U;
enum class Kind : std::uint8_t { ClearScripts, ClearSpeech, Script, FmodBank, SpeechBank };
enum class ContentFormat : std::uint8_t { RawFileBytes, EngineParserUtf16DefinitionAndBody, PinnedRawSpeechBankFile };
enum class Stage : std::uint8_t { ReleaseOutgoingConsumers, RestoreBaseline };
enum class Failure : std::uint8_t {
    None, InvalidManifest, UnsealedOrder, MissingContent, WrongThread,
    FenceUnavailable, ProvidersNotDetached, ProviderChanged, VoicesNotReleased,
    SpeechReferencesLive, AudioLockHeld, UnboundedSideEffect,
    PermitRejected, NativeCallFailed, SideEffectsMismatch, VerificationFailed,
    AlreadyStarted, WrongLifecycleStage, BaselineLeaseUnavailable,
};
struct Entry {
    Kind kind;
    ContentFormat content_format;
    std::uint32_t order;
    /* Nonzero names the enclosing Script entry. Its native OnStart already
     * performs this operation; observe/authenticate it, never replay twice. */
    std::uint32_t parent_order;
    std::uint64_t owner_identity;
    std::uint64_t provider_identity;
    unsigned char content_sha256[32];
    char logical_path[kReplayPathCapacity];
    bool localized;
    bool source_content_complete;
    bool bounded_side_effects_verified;
};
struct Manifest {
    std::uint64_t baseline_generation;
    std::uint64_t epoch;
    std::uint64_t transaction_generation;
    std::uint64_t fence_identity;
    std::uint64_t preserved_user_settings_fingerprint;
    std::uint64_t script_registry_owner;
    std::uint64_t fmod_owner;
    std::uint64_t speech_registry_owner;
    std::uint32_t owner_thread_id;
    Entry entries[kMaximumReplayEntries];
    std::uint32_t count;
    bool side_effect_order_sealed;
    bool rebuild_speech;
};
struct Environment {
    std::uint64_t fence_identity;
    std::uint32_t thread_id;
    bool producer_and_script_execution_fences_closed;
    bool outgoing_providers_detached_and_pinned;
    bool banks_and_voices_released;
    bool speech_references_released;
    bool current_thread_holds_audio_lock;
    Stage stage;
    bool outgoing_providers_retired;
    bool baseline_provider_leases_held;
};
using EnvironmentFn = bool (*)(void*, Environment*) noexcept;
using ResolveFn = bool (*)(void*, const Entry*) noexcept;
using PermitFn = bool (*)(void*, const Manifest*, const Entry*,
                         const Entry* children, std::size_t count) noexcept;
using EndPermitFn = bool (*)(void*, bool native_succeeded) noexcept;
using InvokeFn = bool (*)(void*, const Entry*) noexcept;
using VerifyFn = bool (*)(void*, const Manifest*) noexcept;
struct Operations {
    void* user;
    EnvironmentFn read_environment;
    ResolveFn verify_selected_provider_and_content;
    PermitFn begin_authenticated_replay;
    EndPermitFn end_authenticated_replay;
    InvokeFn invoke_native;
    VerifyFn verify_complete_baseline_and_user_settings;
    bool exact_native_targets;
};
struct Replay {
    Manifest manifest;
    Operations operations;
    Failure failure;
    std::uint32_t completed_top_level_operations;
    bool prepared;
    bool started;
    bool verified;
    bool executing;
    bool definitions_cleared;
    bool replay_started;
};
void Initialize(Replay*, const Operations*) noexcept;
bool Prepare(Replay*, const Manifest&) noexcept;
/* Reset phase, before provider retirement. */
bool ClearOwnedDefinitions(Replay*) noexcept;
/* Reload phase, after outgoing-provider retirement and with baseline leases. */
bool ReplayBaseline(Replay*) noexcept;
/* Real supported-Player bindings for the script portion only. They deliberately
 * do not call AudioScript::Reload, which replays current contaminated paths.
 * The complete reset caller must own the exclusive script-execution fence
 * without holding the audio critical section across native OnStart effects. */
using ReadCodeFn = bool (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
bool ValidateNativeScriptTargets(std::uintptr_t engine_base, ReadCodeFn,
                                void*, bool exact_player_image) noexcept;
bool InvokeNativeScript(std::uintptr_t validated_engine_base,
                        const Entry&) noexcept;
struct NativeTargets { std::uintptr_t engine_base; bool validated; };
bool ValidateNativeTargets(NativeTargets*, ReadCodeFn, void*,
                           bool exact_player_image) noexcept;
/* The replay coordinator owns authorization and post-call membership checks.
 * Speech's native void return only reports completion, not a loaded bank. */
bool InvokeNativeEntry(const NativeTargets*, const Entry&) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::audio_baseline_replay
#endif
