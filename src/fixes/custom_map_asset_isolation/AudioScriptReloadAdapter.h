#ifndef DLUMA_AUDIO_SCRIPT_RELOAD_ADAPTER_H
#define DLUMA_AUDIO_SCRIPT_RELOAD_ADAPTER_H

#include "AudioSemanticWindowsSnapshot.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_script_reload {

constexpr std::uintptr_t kAudioScriptClearRva = 0x00113FB0U;
constexpr std::uintptr_t kAudioScriptReloadRva = 0x00114040U;
constexpr std::uintptr_t kAudioScriptLoadRva = 0x00114100U;

enum class AudioScriptReloadFailure : std::uint8_t {
    None,
    InvalidArgument,
    UnsupportedImage,
    TargetPrefixMismatch,
    InternalCallMismatch,
    WrongThread,
    ReceiverUnvalidated,
    AbiUnvalidated,
    AudioLockNotOwned,
    RecursionActive,
    ParserActive,
    BankMutationActive,
    ExecutionFenceBusy,
    BaselinePathsIncomplete,
    ReloadBeforeSourceRemoval,
    NextProjectStagingStarted,
    ReloadFailed,
    VerificationFailed,
};

using ReadAudioScriptCodeFn = bool (*)(
    void* user, std::uintptr_t address, void* output,
    std::size_t size) noexcept;
using InvokeAudioScriptReloadFn = bool (*)(void* user) noexcept;
using SnapshotAudioScriptRegistryFn = bool (*)(
    void* user,
    audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot* output)
    noexcept;

struct AudioScriptReloadTargetInput {
    std::uintptr_t engine_base;
    ReadAudioScriptCodeFn read_code;
    void* user;
    bool exact_engine_image_validated;
};

struct AudioScriptReloadAuthorization {
    std::uint32_t current_thread_id;
    std::uint32_t game_thread_id;
    bool target_validated;
    bool receiver_validated;
    bool abi_validated;
    bool audio_lock_owned;
    bool recursion_idle;
    bool parser_idle;
    bool bank_mutation_idle;
    bool execution_fence_idle;
    bool baseline_paths_complete;
    bool outgoing_sources_removed;
    bool next_project_staging_started;
};

struct AudioScriptReloadOperations {
    void* user;
    InvokeAudioScriptReloadFn invoke_reload;
    SnapshotAudioScriptRegistryFn snapshot_registry;
};

struct AudioScriptReloadAdapter {
    AudioScriptReloadOperations operations;
    audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot baseline;
    std::uint64_t baseline_generation;
    AudioScriptReloadFailure last_failure;
    bool baseline_captured;
    bool prepared;
    bool mutation_started;
    bool verified;
};

bool ValidateAudioScriptReloadTarget(
    const AudioScriptReloadTargetInput* input) noexcept;

void InitializeAudioScriptReloadAdapter(
    AudioScriptReloadAdapter* adapter,
    const AudioScriptReloadOperations* operations) noexcept;

bool CaptureAudioScriptReloadBaseline(
    AudioScriptReloadAdapter* adapter,
    std::uint64_t baseline_generation,
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot&
        baseline) noexcept;

bool PrepareAudioScriptReload(
    AudioScriptReloadAdapter* adapter,
    const AudioScriptReloadAuthorization& authorization) noexcept;

bool ExecuteAndVerifyAudioScriptReload(
    AudioScriptReloadAdapter* adapter,
    const AudioScriptReloadAuthorization& authorization) noexcept;

const char* AudioScriptReloadFailureName(
    AudioScriptReloadFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_script_reload

#endif
