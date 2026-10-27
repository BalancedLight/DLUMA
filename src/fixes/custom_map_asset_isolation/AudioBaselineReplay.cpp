#define _HAS_EXCEPTIONS 0
#include "AudioBaselineReplay.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_baseline_replay {
namespace {
bool Fail(Replay* replay, Failure failure) noexcept
{ if (replay) replay->failure = failure; return false; }
bool ContentPresent(const Entry& entry) noexcept
{
    unsigned char value = 0;
    for (unsigned char byte : entry.content_sha256) value |= byte;
    return value && entry.owner_identity && entry.provider_identity && entry.logical_path[0] &&
        std::memchr(entry.logical_path, 0, sizeof(entry.logical_path)) && entry.source_content_complete;
}
bool CheckEnvironment(Replay* replay, bool clearing) noexcept
{
    Environment environment = {};
    if (!replay->operations.read_environment(replay->operations.user, &environment) ||
        environment.fence_identity != replay->manifest.fence_identity ||
        !environment.producer_and_script_execution_fences_closed)
        return Fail(replay, Failure::FenceUnavailable);
    if (!environment.thread_id || environment.thread_id != replay->manifest.owner_thread_id)
        return Fail(replay, Failure::WrongThread);
    if (environment.stage != (clearing ? Stage::ReleaseOutgoingConsumers : Stage::RestoreBaseline))
        return Fail(replay, Failure::WrongLifecycleStage);
    if (clearing) {
        if (!environment.outgoing_providers_detached_and_pinned || environment.outgoing_providers_retired)
            return Fail(replay, Failure::ProvidersNotDetached);
    } else {
        if (!environment.outgoing_providers_retired)
            return Fail(replay, Failure::WrongLifecycleStage);
        if (!environment.baseline_provider_leases_held)
            return Fail(replay, Failure::BaselineLeaseUnavailable);
    }
    if (!environment.banks_and_voices_released)
        return Fail(replay, Failure::VoicesNotReleased);
    if (replay->manifest.rebuild_speech && !environment.speech_references_released)
        return Fail(replay, Failure::SpeechReferencesLive);
    /* Load runs OnStart. Holding the sound lock across its bank operations
     * can deadlock the audio worker's unload acknowledgement. */
    if (environment.current_thread_holds_audio_lock)
        return Fail(replay, Failure::AudioLockHeld);
    return true;
}
bool Invoke(Replay* replay, const Entry& entry,
            const Entry* children = nullptr, std::size_t count = 0) noexcept
{
    const bool clearing = entry.kind == Kind::ClearScripts || entry.kind == Kind::ClearSpeech;
    if (!CheckEnvironment(replay, clearing)) return false;
    if (entry.kind != Kind::ClearScripts && entry.kind != Kind::ClearSpeech) {
        if (!replay->operations.verify_selected_provider_and_content(replay->operations.user, &entry))
            return Fail(replay, Failure::ProviderChanged);
        for (std::size_t i = 0; i < count; ++i)
            if (!replay->operations.verify_selected_provider_and_content(replay->operations.user, &children[i]))
                return Fail(replay, Failure::ProviderChanged);
    }
    if (!replay->operations.begin_authenticated_replay(replay->operations.user,
            &replay->manifest, &entry, children, count))
        return Fail(replay, Failure::PermitRejected);
    replay->started = true;
    if (!clearing) replay->replay_started = true;
    const bool invoked = replay->operations.invoke_native(replay->operations.user, &entry);
    const bool effects = replay->operations.end_authenticated_replay(replay->operations.user, invoked);
    if (!invoked) return Fail(replay, Failure::NativeCallFailed);
    if (!effects) return Fail(replay, Failure::SideEffectsMismatch);
    if (!CheckEnvironment(replay, clearing)) return false;
    ++replay->completed_top_level_operations;
    return true;
}
} // namespace
void Initialize(Replay* replay, const Operations* operations) noexcept
{
    if (!replay) return;
    std::memset(replay, 0, sizeof(*replay));
    if (operations) replay->operations = *operations;
}
bool Prepare(Replay* replay, const Manifest& manifest) noexcept
{
    if (!replay || replay->started || replay->prepared || replay->executing) return false;
    if (!manifest.baseline_generation ||
        !manifest.epoch || !manifest.transaction_generation || !manifest.fence_identity ||
        !manifest.preserved_user_settings_fingerprint || !manifest.script_registry_owner ||
        (manifest.rebuild_speech && !manifest.speech_registry_owner) || !manifest.owner_thread_id ||
        manifest.count > kMaximumReplayEntries) return Fail(replay, Failure::InvalidManifest);
    const Operations& ops = replay->operations;
    if (!ops.exact_native_targets || !ops.read_environment || !ops.verify_selected_provider_and_content ||
        !ops.begin_authenticated_replay || !ops.end_authenticated_replay || !ops.invoke_native ||
        !ops.verify_complete_baseline_and_user_settings) return Fail(replay, Failure::InvalidManifest);
    if (!manifest.side_effect_order_sealed) return Fail(replay, Failure::UnsealedOrder);
    std::uint32_t previous_order = 0, active_script = 0;
    for (std::uint32_t i = 0; i < manifest.count; ++i) {
        const Entry& entry = manifest.entries[i];
        if (entry.kind != Kind::Script && entry.kind != Kind::FmodBank && entry.kind != Kind::SpeechBank)
            return Fail(replay, Failure::InvalidManifest);
        if (!entry.order || entry.order <= previous_order) return Fail(replay, Failure::UnsealedOrder);
        if (!ContentPresent(entry)) return Fail(replay, Failure::MissingContent);
        if ((entry.kind == Kind::Script && entry.owner_identity != manifest.script_registry_owner) ||
            (entry.kind == Kind::FmodBank && entry.owner_identity != manifest.fmod_owner) ||
            (entry.kind == Kind::SpeechBank && (!manifest.rebuild_speech ||
                entry.owner_identity != manifest.speech_registry_owner)))
            return Fail(replay, Failure::InvalidManifest);
        if (!entry.bounded_side_effects_verified) return Fail(replay, Failure::UnboundedSideEffect);
        if (entry.parent_order) {
            if (entry.kind == Kind::Script || entry.parent_order != active_script)
                return Fail(replay, Failure::UnsealedOrder);
        } else active_script = entry.kind == Kind::Script ? entry.order : 0;
        previous_order = entry.order;
    }
    replay->manifest = manifest;
    replay->failure = Failure::None;
    replay->prepared = true;
    return true;
}
bool ClearOwnedDefinitions(Replay* replay) noexcept
{
    /* The owner is a single coordinator thread. Native callbacks can reenter
     * it; reject that before calling another environment/provider callback. */
    if (!replay || replay->executing) return false;
    if (!replay->prepared || replay->started || replay->verified || replay->definitions_cleared)
        return Fail(replay, Failure::AlreadyStarted);
    struct ExecutingScope {
        bool& value;
        explicit ExecutingScope(bool& input) noexcept : value(input) { value = true; }
        ~ExecutingScope() { value = false; }
    } scope(replay->executing);
    /* A failed preflight may be retried because it performed no native
     * mutation. Do not carry its diagnostic failure into a successful retry. */
    replay->failure = Failure::None;
    if (!CheckEnvironment(replay, true)) return false;
    /* Validate every replay input before clearing any live registry. Recheck
     * the exact selected provider again immediately before each native call. */
    for (std::uint32_t i = 0; i < replay->manifest.count; ++i)
        if (!replay->operations.verify_selected_provider_and_content(
                replay->operations.user, &replay->manifest.entries[i]))
            return Fail(replay, Failure::ProviderChanged);
    Entry clear = {}; clear.kind = Kind::ClearScripts;
    clear.owner_identity = replay->manifest.script_registry_owner;
    if (!Invoke(replay, clear)) return false;
    if (replay->manifest.rebuild_speech) {
        clear.kind = Kind::ClearSpeech;
        clear.owner_identity = replay->manifest.speech_registry_owner;
        if (!Invoke(replay, clear)) return false;
    }
    if (!CheckEnvironment(replay, true)) return false;
    replay->definitions_cleared = true;
    return true;
}
bool ReplayBaseline(Replay* replay) noexcept
{
    if (!replay || replay->executing) return false;
    if (!replay->prepared || !replay->definitions_cleared || replay->replay_started || replay->verified)
        return Fail(replay,Failure::AlreadyStarted);
    struct ExecutingScope {
        bool& value;
        explicit ExecutingScope(bool& input) noexcept : value(input) { value=true; }
        ~ExecutingScope() { value=false; }
    } scope(replay->executing);
    replay->failure=Failure::None;
    if (!CheckEnvironment(replay,false)) return false;
    for (std::uint32_t i=0;i<replay->manifest.count;++i)
        if (!replay->operations.verify_selected_provider_and_content(
                replay->operations.user,&replay->manifest.entries[i]))
            return Fail(replay,Failure::ProviderChanged);
    for (std::uint32_t i = 0; i < replay->manifest.count;) {
        const Entry& entry = replay->manifest.entries[i];
        if (entry.parent_order) return Fail(replay, Failure::UnsealedOrder);
        std::uint32_t end = i + 1;
        while (end < replay->manifest.count && replay->manifest.entries[end].parent_order == entry.order) ++end;
        if (!Invoke(replay, entry, replay->manifest.entries + i + 1, end - i - 1)) return false;
        i = end;
    }
    if (!CheckEnvironment(replay, false) || !replay->operations.verify_complete_baseline_and_user_settings(
            replay->operations.user, &replay->manifest)) return Fail(replay, Failure::VerificationFailed);
    replay->verified = true;
    return true;
}
bool ValidateNativeScriptTargets(std::uintptr_t base, ReadCodeFn read,
                                void* user, bool exact) noexcept
{
    if (!exact || !base || !read || base > UINTPTR_MAX - 0x114120U) return false;
    static const unsigned char clear[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0x3b,0x13,0x9c,0x00,0x33,0xdb,0x89};
    static const unsigned char load[] = {0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x38,0x83,0x3d,0x6d,0x12,0x9c,0x00,0x00,0x4c,0x8b,0xe1};
    unsigned char actual[32] = {};
    return read(user, base + 0x113FB0U, actual, sizeof(clear)) && !std::memcmp(actual, clear, sizeof(clear)) &&
        read(user, base + 0x114100U, actual, sizeof(load)) && !std::memcmp(actual, load, sizeof(load));
}
bool InvokeNativeScript(std::uintptr_t base, const Entry& entry) noexcept
{
    if (!base || base > UINTPTR_MAX - 0x114100U) return false;
    if (entry.kind == Kind::ClearScripts) {
        reinterpret_cast<void (__fastcall*)()>(base + 0x113FB0U)(); return true;
    }
    if (entry.kind == Kind::Script && ContentPresent(entry))
        return reinterpret_cast<bool (__fastcall*)(const char*)>(base + 0x114100U)(entry.logical_path);
    return false;
}
bool ValidateNativeTargets(NativeTargets* targets, ReadCodeFn read,
                           void* user, bool exact) noexcept
{
    if (!targets) return false;
    targets->validated = false;
    if (!ValidateNativeScriptTargets(targets->engine_base, read, user, exact) ||
        targets->engine_base > UINTPTR_MAX - 0x32AF80U) return false;
    static const unsigned char fmod[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54};
    static const unsigned char speech[] = {0x48,0x85,0xd2,0x0f,0x84,0xc4,0x04,0x00,0x00,0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x55,0x53,0x57,0x41,0x54};
    static const unsigned char clear[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x33,0xed,0x48,0x8b,0xf9};
    unsigned char actual[32] = {};
    const std::uintptr_t base = targets->engine_base;
    targets->validated = read(user, base + 0xFA3B0U, actual, sizeof(fmod)) && !std::memcmp(actual, fmod, sizeof(fmod)) &&
        read(user, base + 0x32A7F0U, actual, sizeof(speech)) && !std::memcmp(actual, speech, sizeof(speech)) &&
        read(user, base + 0x32AF60U, actual, sizeof(clear)) && !std::memcmp(actual, clear, sizeof(clear));
    return targets->validated;
}
bool InvokeNativeEntry(const NativeTargets* targets, const Entry& entry) noexcept
{
    if (!targets || !targets->validated || !entry.owner_identity) return false;
    if (entry.kind == Kind::ClearScripts || entry.kind == Kind::Script)
        return InvokeNativeScript(targets->engine_base, entry);
    if (entry.kind == Kind::ClearSpeech) {
        reinterpret_cast<void (__fastcall*)(void*)>(targets->engine_base + 0x32AF60U)(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.owner_identity)));
        return true;
    }
    if (!ContentPresent(entry)) return false;
    if (entry.kind == Kind::FmodBank)
        return reinterpret_cast<int (__fastcall*)(void*, const char*, bool)>(targets->engine_base + 0xFA3B0U)(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.owner_identity)),
            entry.logical_path, entry.localized) >= 0;
    if (entry.kind == Kind::SpeechBank) {
        reinterpret_cast<void (__fastcall*)(void*, const char*, bool)>(targets->engine_base + 0x32A7F0U)(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.owner_identity)),
            entry.logical_path, entry.localized);
        return true;
    }
    return false;
}
} // namespace dluma::fixes::custom_map_asset_isolation::audio_baseline_replay
