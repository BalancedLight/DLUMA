#define _HAS_EXCEPTIONS 0

#include "CustomAudioResetAdapter.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio {
namespace {

CustomAudioBankRecord g_audio_reset_snapshot[kMaximumCustomAudioBanks];
BaselineAudioBank g_audio_baseline_snapshot[kMaximumBaselineAudioBanks];

const BaselineAudioBank* FindBaseline(
    const BaselineAudioBank* banks, std::size_t count,
    const CustomAudioBankRecord& record) noexcept
{
    for (std::size_t index = 0; index < count; ++index) {
        if (record.logical_bank_id && banks[index].logical_bank_id ==
                                          record.logical_bank_id)
            return &banks[index];
        if (!record.logical_bank_id && record.bank_name[0]) {
            const char* left = record.bank_name;
            const char* right = banks[index].bank_name;
            bool equal = true;
            while (*left && *right) {
                char a = *left++;
                char b = *right++;
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) { equal = false; break; }
            }
            if (equal && *left == *right) return &banks[index];
        }
    }
    return nullptr;
}

}  // namespace

void Initialize(CustomAudioResetState* state,
                CustomAudioBankLedger* ledger,
                const CustomAudioResetOperations* operations) noexcept
{
    if (!state) return;
    std::memset(state, 0, sizeof(*state));
    state->ledger = ledger;
    if (operations) state->operations = *operations;
}

bool IsDestructiveAdapterAvailable(
    const CustomAudioResetState* state) noexcept
{
    return state && state->ledger &&
           state->operations.owner_identity_validated &&
           state->operations.active_event_fence_validated &&
           state->operations.baseline_enumerator_complete &&
           state->operations.exact_instance_model_validated &&
           state->operations.quiesce_project_audio &&
           state->operations.release_exact_custom_instance &&
           state->operations.release_exact_shared_reference &&
           state->operations.replay_baseline_bank &&
           state->operations.verify_baseline;
}

content::ContentResetResult ResetBeforeSourceRemoval(
    CustomAudioResetState* state, std::uint32_t epoch) noexcept
{
    if (!IsDestructiveAdapterAvailable(state) || !epoch ||
        !CanDestructivelyResetEpoch(state->ledger, epoch))
        return content::ContentResetResult::Unsupported;
    const std::size_t count = SnapshotEpochBanks(
        state->ledger, epoch, g_audio_reset_snapshot,
        kMaximumCustomAudioBanks);
    if (!state->operations.quiesce_project_audio(
            state->operations.user, epoch))
        return content::ContentResetResult::Failed;
    state->quiesced = true;
    state->active_epoch = epoch;
    bool partial = false;
    for (std::size_t index = count; index > 0; --index) {
        const CustomAudioBankRecord& record =
            g_audio_reset_snapshot[index - 1];
        if (record.unloaded_naturally || record.unloaded_by_reset) continue;
        bool succeeded = false;
        switch (record.classification) {
        case CustomAudioBankClass::ProjectUnique:
        case CustomAudioBankClass::CustomOverrideOfBaseline:
            succeeded = record.exact_handle_available &&
                state->operations.release_exact_custom_instance(
                    state->operations.user, record.bank_handle,
                    record.bank_object);
            if (succeeded)
                succeeded = RecordResetRelease(
                    state->ledger, record.ledger_record_index);
            break;
        case CustomAudioBankClass::SharedBaselineReference:
            succeeded = state->operations.release_exact_shared_reference(
                state->operations.user, record.logical_bank_id,
                record.bank_handle);
            if (succeeded)
                succeeded = RecordResetRelease(
                    state->ledger, record.ledger_record_index);
            break;
        case CustomAudioBankClass::ProjectReloadOfSameCustomInstance:
            /* Duplicate acquisitions preserve their underlying ownership
             * flags. Release exactly the observed project delta that natural
             * teardown has not already released. */
            succeeded = true;
            for (std::uint32_t acquisition =
                     record.natural_unload_count +
                     record.reset_release_count;
                 acquisition < record.project_load_count; ++acquisition) {
                const bool released = record.shared_baseline_reference
                    ? state->operations.release_exact_shared_reference(
                          state->operations.user,
                          record.logical_bank_id, record.bank_handle)
                    : record.exact_handle_available &&
                          state->operations.release_exact_custom_instance(
                              state->operations.user,
                              record.bank_handle, record.bank_object);
                if (!released) {
                    succeeded = false;
                    break;
                }
                if (!RecordResetRelease(
                        state->ledger,
                        record.ledger_record_index)) {
                    succeeded = false;
                    break;
                }
            }
            break;
        case CustomAudioBankClass::Unknown:
        default:
            succeeded = false;
            break;
        }
        const bool result_recorded = MarkResetResult(
            state->ledger, record.ledger_record_index, succeeded);
        if (!succeeded || !result_recorded) partial = true;
    }
    state->pre_source_complete = !partial;
    return partial ? content::ContentResetResult::Partial
                   : content::ContentResetResult::Restored;
}

content::ContentResetResult RestoreAfterSourceRemoval(
    CustomAudioResetState* state, std::uint32_t epoch) noexcept
{
    if (!IsDestructiveAdapterAvailable(state) || !state->quiesced ||
        state->active_epoch != epoch)
        return content::ContentResetResult::Unsupported;
    const std::size_t custom_count = SnapshotEpochBanks(
        state->ledger, epoch, g_audio_reset_snapshot,
        kMaximumCustomAudioBanks);
    const std::size_t baseline_count = SnapshotBaselineBanks(
        state->ledger, g_audio_baseline_snapshot,
        kMaximumBaselineAudioBanks);
    bool partial = false;
    for (std::size_t index = 0; index < custom_count; ++index) {
        const CustomAudioBankRecord& record = g_audio_reset_snapshot[index];
        if (!record.custom_override_of_baseline)
            continue;
        const BaselineAudioBank* baseline = FindBaseline(
            g_audio_baseline_snapshot, baseline_count, record);
        if (!baseline || !state->operations.replay_baseline_bank(
                             state->operations.user, baseline))
            partial = true;
    }
    state->post_source_complete = !partial;
    return partial ? content::ContentResetResult::Failed
                   : content::ContentResetResult::Restored;
}

content::ContentResetResult VerifyRestoredBaseline(
    CustomAudioResetState* state) noexcept
{
    if (!IsDestructiveAdapterAvailable(state) ||
        !state->pre_source_complete || !state->post_source_complete)
        return content::ContentResetResult::Unsupported;
    const std::size_t baseline_count = SnapshotBaselineBanks(
        state->ledger, g_audio_baseline_snapshot,
        kMaximumBaselineAudioBanks);
    return state->operations.verify_baseline(
               state->operations.user, g_audio_baseline_snapshot,
               baseline_count) && VerifyBaseline(state->ledger)
        ? content::ContentResetResult::Restored
        : content::ContentResetResult::Failed;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio
