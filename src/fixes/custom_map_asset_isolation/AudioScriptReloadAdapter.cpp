#define _HAS_EXCEPTIONS 0

#include "AudioScriptReloadAdapter.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_script_reload {
namespace {

bool AddAddress(std::uintptr_t base, std::uintptr_t offset,
                std::uintptr_t* output) noexcept
{
    if (!output || !base || offset > UINTPTR_MAX - base) return false;
    *output = base + offset;
    return true;
}

bool ReadBytes(const AudioScriptReloadTargetInput& input,
               std::uintptr_t rva, const std::uint8_t* expected,
               std::size_t size) noexcept
{
    if (!expected || !size || size > 64U) return false;
    std::uintptr_t address = 0;
    std::uint8_t actual[64] = {};
    return AddAddress(input.engine_base, rva, &address) &&
        input.read_code(
            input.user, address, actual, size) &&
        std::memcmp(actual, expected, size) == 0;
}

bool RelativeCallTargets(
    const AudioScriptReloadTargetInput& input,
    std::uintptr_t call_rva, std::uintptr_t target_rva) noexcept
{
    std::uintptr_t call = 0;
    std::uintptr_t next_instruction = 0;
    std::uintptr_t expected_target = 0;
    std::uint8_t bytes[5] = {};
    if (!AddAddress(input.engine_base, call_rva, &call) ||
        !AddAddress(call, sizeof(bytes), &next_instruction) ||
        !AddAddress(input.engine_base, target_rva, &expected_target) ||
        !input.read_code(input.user, call, bytes, sizeof(bytes)) ||
        bytes[0] != 0xE8U)
        return false;
    const std::uint32_t encoded =
        static_cast<std::uint32_t>(bytes[1]) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        (static_cast<std::uint32_t>(bytes[3]) << 16U) |
        (static_cast<std::uint32_t>(bytes[4]) << 24U);
    std::uintptr_t resolved = 0;
    if ((encoded & 0x80000000U) == 0) {
        if (!AddAddress(next_instruction, encoded, &resolved))
            return false;
    } else {
        const std::uint32_t magnitude = (~encoded) + 1U;
        if (static_cast<std::uintptr_t>(magnitude) > next_instruction)
            return false;
        resolved = next_instruction -
            static_cast<std::uintptr_t>(magnitude);
    }
    return resolved == expected_target;
}

bool ScriptSnapshotsEqual(
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot& left,
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot& right)
    noexcept
{
    return left.complete && right.complete &&
        left.registry_owner == right.registry_owner &&
        left.semantic_fingerprint == right.semantic_fingerprint &&
        left.definition_count == right.definition_count &&
        left.path_count == right.path_count &&
        left.procedure_count == right.procedure_count &&
        left.property_group_count == right.property_group_count;
}

AudioScriptReloadFailure AuthorizationFailure(
    const AudioScriptReloadAuthorization& authorization,
    bool require_sources_removed) noexcept
{
    if (!authorization.current_thread_id ||
        authorization.current_thread_id != authorization.game_thread_id)
        return AudioScriptReloadFailure::WrongThread;
    if (!authorization.target_validated)
        return AudioScriptReloadFailure::TargetPrefixMismatch;
    if (!authorization.receiver_validated)
        return AudioScriptReloadFailure::ReceiverUnvalidated;
    if (!authorization.abi_validated)
        return AudioScriptReloadFailure::AbiUnvalidated;
    if (!authorization.audio_lock_owned)
        return AudioScriptReloadFailure::AudioLockNotOwned;
    if (!authorization.recursion_idle)
        return AudioScriptReloadFailure::RecursionActive;
    if (!authorization.parser_idle)
        return AudioScriptReloadFailure::ParserActive;
    if (!authorization.bank_mutation_idle)
        return AudioScriptReloadFailure::BankMutationActive;
    if (!authorization.execution_fence_idle)
        return AudioScriptReloadFailure::ExecutionFenceBusy;
    if (!authorization.baseline_paths_complete)
        return AudioScriptReloadFailure::BaselinePathsIncomplete;
    if (require_sources_removed &&
        !authorization.outgoing_sources_removed)
        return AudioScriptReloadFailure::ReloadBeforeSourceRemoval;
    if (authorization.next_project_staging_started)
        return AudioScriptReloadFailure::NextProjectStagingStarted;
    return AudioScriptReloadFailure::None;
}

}  // namespace

bool ValidateAudioScriptReloadTarget(
    const AudioScriptReloadTargetInput* input) noexcept
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
    return input && input->engine_base && input->read_code &&
        input->exact_engine_image_validated &&
        ReadBytes(
            *input, kAudioScriptClearRva,
            clear_prefix, sizeof(clear_prefix)) &&
        ReadBytes(
            *input, kAudioScriptReloadRva,
            reload_prefix, sizeof(reload_prefix)) &&
        ReadBytes(
            *input, kAudioScriptLoadRva,
            load_prefix, sizeof(load_prefix)) &&
        RelativeCallTargets(
            *input, kAudioScriptReloadRva + 0x2BU,
            kAudioScriptClearRva) &&
        RelativeCallTargets(
            *input, kAudioScriptReloadRva + 0x61U,
            kAudioScriptLoadRva);
}

void InitializeAudioScriptReloadAdapter(
    AudioScriptReloadAdapter* adapter,
    const AudioScriptReloadOperations* operations) noexcept
{
    if (!adapter) return;
    std::memset(adapter, 0, sizeof(*adapter));
    if (operations) adapter->operations = *operations;
}

bool CaptureAudioScriptReloadBaseline(
    AudioScriptReloadAdapter* adapter,
    std::uint64_t baseline_generation,
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot&
        baseline) noexcept
{
    if (!adapter || !baseline_generation || !baseline.complete ||
        !baseline.registry_owner || !baseline.semantic_fingerprint ||
        !baseline.path_count || adapter->baseline_captured)
        return false;
    adapter->baseline = baseline;
    adapter->baseline_generation = baseline_generation;
    adapter->baseline_captured = true;
    adapter->last_failure = AudioScriptReloadFailure::None;
    return true;
}

bool PrepareAudioScriptReload(
    AudioScriptReloadAdapter* adapter,
    const AudioScriptReloadAuthorization& authorization) noexcept
{
    if (!adapter || !adapter->baseline_captured ||
        !adapter->operations.snapshot_registry) {
        if (adapter)
            adapter->last_failure =
                AudioScriptReloadFailure::InvalidArgument;
        return false;
    }
    const AudioScriptReloadFailure failure =
        AuthorizationFailure(authorization, false);
    if (failure != AudioScriptReloadFailure::None)
        adapter->last_failure = failure;
    else if (!adapter->operations.invoke_reload)
        adapter->last_failure =
            AudioScriptReloadFailure::ReceiverUnvalidated;
    else {
        adapter->prepared = true;
        adapter->last_failure = AudioScriptReloadFailure::None;
        return true;
    }
    return false;
}

bool ExecuteAndVerifyAudioScriptReload(
    AudioScriptReloadAdapter* adapter,
    const AudioScriptReloadAuthorization& authorization) noexcept
{
    if (!adapter || !adapter->prepared ||
        adapter->mutation_started || adapter->verified ||
        !adapter->operations.invoke_reload ||
        !adapter->operations.snapshot_registry) {
        if (adapter)
            adapter->last_failure = AudioScriptReloadFailure::InvalidArgument;
        return false;
    }
    const AudioScriptReloadFailure authorization_failure =
        AuthorizationFailure(authorization, true);
    if (authorization_failure != AudioScriptReloadFailure::None) {
        adapter->last_failure = authorization_failure;
        return false;
    }
    adapter->mutation_started = true;
    if (!adapter->operations.invoke_reload(
            adapter->operations.user)) {
        adapter->last_failure = AudioScriptReloadFailure::ReloadFailed;
        return false;
    }
    audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot after = {};
    if (!adapter->operations.snapshot_registry(
            adapter->operations.user, &after) ||
        !ScriptSnapshotsEqual(adapter->baseline, after)) {
        adapter->last_failure =
            AudioScriptReloadFailure::VerificationFailed;
        return false;
    }
    adapter->verified = true;
    adapter->last_failure = AudioScriptReloadFailure::None;
    return true;
}

const char* AudioScriptReloadFailureName(
    AudioScriptReloadFailure failure) noexcept
{
    switch (failure) {
    case AudioScriptReloadFailure::None: return "none";
    case AudioScriptReloadFailure::InvalidArgument:
        return "invalid_argument";
    case AudioScriptReloadFailure::UnsupportedImage:
        return "unsupported_image";
    case AudioScriptReloadFailure::TargetPrefixMismatch:
        return "target_prefix_mismatch";
    case AudioScriptReloadFailure::InternalCallMismatch:
        return "internal_call_mismatch";
    case AudioScriptReloadFailure::WrongThread: return "wrong_thread";
    case AudioScriptReloadFailure::ReceiverUnvalidated:
        return "receiver_unvalidated";
    case AudioScriptReloadFailure::AbiUnvalidated:
        return "abi_unvalidated";
    case AudioScriptReloadFailure::AudioLockNotOwned:
        return "audio_lock_not_owned";
    case AudioScriptReloadFailure::RecursionActive:
        return "recursion_active";
    case AudioScriptReloadFailure::ParserActive:
        return "parser_active";
    case AudioScriptReloadFailure::BankMutationActive:
        return "bank_mutation_active";
    case AudioScriptReloadFailure::ExecutionFenceBusy:
        return "execution_fence_busy";
    case AudioScriptReloadFailure::BaselinePathsIncomplete:
        return "baseline_paths_incomplete";
    case AudioScriptReloadFailure::ReloadBeforeSourceRemoval:
        return "reload_before_source_removal";
    case AudioScriptReloadFailure::NextProjectStagingStarted:
        return "next_project_staging_started";
    case AudioScriptReloadFailure::ReloadFailed: return "reload_failed";
    case AudioScriptReloadFailure::VerificationFailed:
        return "verification_failed";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_script_reload
