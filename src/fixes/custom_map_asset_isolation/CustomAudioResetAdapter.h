#ifndef DLUMA_FIXES_CUSTOM_AUDIO_RESET_ADAPTER_H
#define DLUMA_FIXES_CUSTOM_AUDIO_RESET_ADAPTER_H

#include "CustomAudioBankLedger.h"
#include "CustomContentResetRegistry.h"

namespace dluma::fixes::custom_map_asset_isolation::audio {

using AudioQuiesceFn = bool (*)(void* user,
                                std::uint32_t epoch) noexcept;
using AudioExactHandleFn = bool (*)(void* user,
                                    std::uintptr_t handle,
                                    std::uintptr_t object) noexcept;
using AudioSharedReferenceFn = bool (*)(void* user,
                                        std::uint64_t logical_bank_id,
                                        std::uintptr_t handle) noexcept;
using AudioBaselineReplayFn = bool (*)(
    void* user, const BaselineAudioBank* bank) noexcept;
using AudioVerifyFn = bool (*)(
    void* user, const BaselineAudioBank* baseline,
    std::size_t baseline_count) noexcept;

struct CustomAudioResetOperations {
    void* user;
    AudioQuiesceFn quiesce_project_audio;
    AudioExactHandleFn release_exact_custom_instance;
    AudioSharedReferenceFn release_exact_shared_reference;
    AudioBaselineReplayFn replay_baseline_bank;
    AudioVerifyFn verify_baseline;
    bool owner_identity_validated;
    bool active_event_fence_validated;
    bool baseline_enumerator_complete;
    bool exact_instance_model_validated;
};

struct CustomAudioResetState {
    CustomAudioBankLedger* ledger;
    CustomAudioResetOperations operations;
    std::uint32_t active_epoch;
    bool quiesced;
    bool pre_source_complete;
    bool post_source_complete;
};

void Initialize(CustomAudioResetState* state,
                CustomAudioBankLedger* ledger,
                const CustomAudioResetOperations* operations) noexcept;
bool IsDestructiveAdapterAvailable(
    const CustomAudioResetState* state) noexcept;

content::ContentResetResult ResetBeforeSourceRemoval(
    CustomAudioResetState* state, std::uint32_t epoch) noexcept;
content::ContentResetResult RestoreAfterSourceRemoval(
    CustomAudioResetState* state, std::uint32_t epoch) noexcept;
content::ContentResetResult VerifyRestoredBaseline(
    CustomAudioResetState* state) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio

#endif
