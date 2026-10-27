#ifndef DLUMA_AUDIO_WORKER_RESET_WINDOWS_H
#define DLUMA_AUDIO_WORKER_RESET_WINDOWS_H
#include "AudioWorkerReset.h"

namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset::windows {
constexpr std::uintptr_t kWorkerUpdateRva = 0x000F86A0U;
constexpr std::uintptr_t kWorkerThreadEntryRva = 0x001103D0U;
constexpr std::uintptr_t kWorkerUpdateReturnRva = 0x0011042DU;
constexpr std::uintptr_t kMarkVoicesRva = 0x000FA250U;
constexpr std::uintptr_t kUnloadSourcesExceptRva = 0x00102690U;
constexpr std::uintptr_t kTrySoundLockRva = 0x000F4D50U;
constexpr std::uintptr_t kLeaveSoundLockRva = 0x000F4D40U;
constexpr std::uintptr_t kFmodVtableRva = 0x008A9628U;
constexpr std::uintptr_t kBankManagerOffset = 0x1E28U;
constexpr std::uintptr_t kPendingUnloadOffset = 0x1E48U;
constexpr std::uintptr_t kActiveVoiceHeadOffset = 0x1E70U;
constexpr std::uintptr_t kDeferredVoiceCountOffset = 0x1E98U;

using ReadMemoryFn = bool (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
struct SourceReference {
    std::uintptr_t source, property_holder, native_file;
    std::uint32_t sound_hash, bank_hash;
};
struct NativeInventory {
    SourceReference* sources;
    std::size_t source_capacity, source_count;
    std::uint32_t banks, instances, pending_bank_reads, project_banks;
    std::uint32_t unreleased_source_instances;
    std::uint64_t kept_source_fingerprint;
    bool kept_bank_addresses_match, complete;
};
struct Binding {
    std::uintptr_t engine_base;
    void* read_user;
    ReadMemoryFn read_memory;
    /* Integration supplies the complete epoch/provenance/reference proof.
     * The binding adds exact native voice-list and pending-request reads. */
    void* integration_user;
    FenceFn integration_fence;
    SnapshotFn integration_snapshot;
    bool exact_player_engine_image;
    bool validated;
    void* reader_gate_user;
    TryLockFn try_reader_gate;
    UnlockFn leave_reader_gate;
};
bool Validate(Binding*) noexcept;
Operations MakeOperations(Binding*) noexcept;
/* Called from the update wrapper using its saved native return address. The
 * same saved value must be used after original returns. This authenticates
 * the native worker-loop call, receiver singleton and exact vtable. */
bool MakeWorkerFrame(Binding*,std::uintptr_t audio_owner,
                     std::uintptr_t saved_caller_return,std::uint32_t thread_id,
                     std::uint64_t tick,std::uint64_t completed_original_updates,
                     bool original_returned,WorkerFrame*) noexcept;
// Pure route observation for the installed worker wrapper. Does not expose
// destructive operations or require an enqueued reset plan.
bool ObserveWorkerFrame(Binding*,std::uintptr_t audio_owner,
                        std::uintptr_t saved_caller_return,std::uint32_t thread_id,
                        std::uint64_t tick,std::uint64_t completed_original_updates,
                        bool original_returned,WorkerFrame*) noexcept;
// Read-only exact native graph. The caller holds the sound mutex and source
// (AD5138) and payload (AD5110) owner locks; no ready flag is inferred here.
bool ReadNativeInventory(Binding*,const Plan&,NativeInventory*) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset::windows
#endif
