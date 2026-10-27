#ifndef DLUMA_FIXES_LOCALIZED_TEXT_BASELINE_OWNER_H
#define DLUMA_FIXES_LOCALIZED_TEXT_BASELINE_OWNER_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::localized_texts {

constexpr std::uintptr_t kTextBucketsRva = 0x00B4CDA0;
constexpr std::uintptr_t kTextCountRva = 0x00B4E5A0;
constexpr std::uintptr_t kClearTextsRva = 0x00287460;
constexpr std::uintptr_t kInsertTextRva = 0x006B5FE0;
constexpr std::size_t kTextBucketCount = 256;
constexpr std::size_t kMaximumTextEntries = 65536;
constexpr std::size_t kMaximumSnapshotBytes = 32U * 1024U * 1024U;
constexpr std::size_t kMaximumKeyBytes = 4096;
constexpr std::size_t kMaximumValueUnits = 65536;
constexpr char kPlayerEngineSha256[] =
    "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c";

enum class TextResult : std::uint8_t {
    Succeeded, AlreadyComplete, InvalidArgument, ProfileMismatch,
    NotCleanStartup, BaselineUnavailable, LocaleMismatch, OwnerMismatch,
    AccessNotQuiescent, ChangedDuringCapture, ReadFailed, InvalidTable,
    CapacityExceeded, WrongTransaction, WrongPhase, NativeCallFailed,
    BaselineMismatch,
};
enum class TextPhase : std::uint8_t {
    Empty, BaselineCaptured, Prepared, Cleared, Restored, Verified, Failed,
};
struct TextFence {
    std::uint64_t owner_generation;
    std::uint64_t mutation_revision;
    std::uint32_t custom_epoch_count;
    char locale[8];
    bool first_clean_frontend;
    bool returned_frontend;
    bool authoritative_thread;
    bool producer_admission_closed;
    bool readers_drained;
};
using ReadMemoryFn = bool (*)(void* user, std::uintptr_t address,
    void* output, std::size_t bytes) noexcept;
using CaptureFenceFn = bool (*)(void* user, TextFence* output) noexcept;
using InvokeClearFn = bool (*)(void* user, std::uintptr_t function) noexcept;
using InvokeInsertFn = bool (*)(void* user, std::uintptr_t function,
    const char* key, const std::uint16_t* value) noexcept;
using VerifyOwnedTextProfileFn = bool (*)(void* user) noexcept;

struct NativeTextAccess {
    std::uintptr_t engine_base;
    char engine_sha256[65];
    ReadMemoryFn read;
    CaptureFenceFn fence;
    void* user;
    // Production uses the observation gate's guarded replay callbacks.
    InvokeClearFn clear;
    InvokeInsertFn insert;
    // When hooks own the entry points, validate those exact owned routes.
    // A missing callback retains the original-prefix check for offline fakes.
    VerifyOwnedTextProfileFn owned_profile;
};
struct TextEntry {
    std::uint32_t key_offset;
    std::uint32_t key_bytes;
    std::uint32_t value_offset;
    std::uint32_t value_units;
};
struct TextSnapshot {
    TextEntry* entries;
    std::uint8_t* storage;
    std::size_t entry_capacity;
    std::size_t storage_capacity;
    std::size_t count;
    std::size_t storage_used;
    std::uint32_t bucket_counts[kTextBucketCount];
    bool complete;
};
struct LocalizedTextBaselineOwner {
    NativeTextAccess access;
    TextSnapshot baseline;
    TextSnapshot scratch;
    TextFence baseline_fence;
    std::uint64_t transaction_generation;
    TextPhase phase;
    TextResult last_result;
};

// Storage is caller-owned and must stay alive and non-overlapping for the
// owner's lifetime. Snapshot bytes never borrow native string pointers.
TextResult InitializeOwner(LocalizedTextBaselineOwner* owner,
    const NativeTextAccess* access, const TextSnapshot* baseline_storage,
    const TextSnapshot* scratch_storage) noexcept;
TextResult CaptureCleanBaseline(LocalizedTextBaselineOwner* owner) noexcept;
// Read-only metadata qualification before the registry closes admission.
// This neither traverses native nodes nor promises producers are drained.
TextResult PreflightTextReset(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;
TextResult PrepareTextReset(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;
TextResult ClearCustomTexts(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;
TextResult RestoreBaselineTexts(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;
TextResult VerifyBaselineTexts(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;
// Explicit final fresh capture after readers were reopened for other domains.
TextResult ReverifyBaselineTexts(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;
TextResult CancelPreparedTextReset(LocalizedTextBaselineOwner* owner,
    std::uint64_t transaction_generation) noexcept;

const char* TextResultName(TextResult result) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::localized_texts
#endif
