#define _HAS_EXCEPTIONS 0
#include "LocalizedTextBaselineOwner.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::localized_texts {
namespace {
struct Bucket { std::uintptr_t head, tail; std::uint32_t count, padding; };
struct Node { std::uintptr_t next, previous, key, value; };
static_assert(sizeof(Bucket) == 24 && sizeof(Node) == 32, "exact Win64 layout");

bool ValidStorage(const TextSnapshot& s) noexcept {
    return s.entries && s.storage && s.entry_capacity && s.storage_capacity &&
        s.entry_capacity <= kMaximumTextEntries &&
        s.storage_capacity <= kMaximumSnapshotBytes &&
        reinterpret_cast<std::uintptr_t>(s.storage) % alignof(std::uint16_t) == 0;
}
bool Overlap(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    const auto x = reinterpret_cast<std::uintptr_t>(a);
    const auto y = reinterpret_cast<std::uintptr_t>(b);
    return x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn || (x < y + bn && y < x + an);
}
TextResult Finish(LocalizedTextBaselineOwner* owner, TextResult result) noexcept {
    owner->last_result = result;
    return result;
}
bool Read(const NativeTextAccess& a, std::uintptr_t address, void* out,
    std::size_t bytes) noexcept {
    return address && address <= UINTPTR_MAX - bytes && a.read(a.user, address, out, bytes);
}
bool EqualFence(const TextFence& a, const TextFence& b) noexcept {
    return a.owner_generation == b.owner_generation &&
        a.mutation_revision == b.mutation_revision &&
        std::memcmp(a.locale, b.locale, sizeof(a.locale)) == 0 &&
        a.authoritative_thread == b.authoritative_thread &&
        a.producer_admission_closed == b.producer_admission_closed &&
        a.readers_drained == b.readers_drained;
}
TextResult GetFence(const NativeTextAccess& access, TextFence* fence) noexcept {
    *fence = {};
    if (!access.fence(access.user, fence) || !fence->owner_generation ||
        !fence->locale[0] || !std::memchr(fence->locale, 0, sizeof(fence->locale)))
        return TextResult::ReadFailed;
    if (!fence->authoritative_thread || !fence->producer_admission_closed || !fence->readers_drained)
        return TextResult::AccessNotQuiescent;
    return TextResult::Succeeded;
}
TextResult Profile(const NativeTextAccess& access) noexcept {
    static const std::uint8_t clear_prefix[] = {
        0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x1d,0x2f,0x59,0x8c};
    static const std::uint8_t add_prefix[] = {
        0x48,0x85,0xd2,0x0f,0x84,0x98,0x01,0x00,0x00,0x4c,0x8b,0xdc,0x49,0x89,0x4b,0x08};
    if (!access.engine_base || access.engine_base > UINTPTR_MAX - 0x00E8D000U ||
        std::memcmp(access.engine_sha256, kPlayerEngineSha256, sizeof(kPlayerEngineSha256)) != 0)
        return TextResult::ProfileMismatch;
    if (access.owned_profile)
        return access.owned_profile(access.user) ? TextResult::Succeeded : TextResult::ProfileMismatch;
    std::uint8_t bytes[16] = {};
    if (!Read(access, access.engine_base + kClearTextsRva, bytes, sizeof(bytes)) ||
        std::memcmp(bytes, clear_prefix, sizeof(bytes)) != 0 ||
        !Read(access, access.engine_base + kInsertTextRva, bytes, sizeof(bytes)) ||
        std::memcmp(bytes, add_prefix, sizeof(bytes)) != 0)
        return TextResult::ProfileMismatch;
    return TextResult::Succeeded;
}
TextResult CopyString(const NativeTextAccess& access, std::uintptr_t address,
    std::size_t unit_bytes, std::size_t limit, TextSnapshot* out,
    std::uint32_t* offset, std::uint32_t* units) noexcept {
    if (!address) return TextResult::InvalidTable;
    if (unit_bytes == 2 && (out->storage_used & 1)) {
        if (out->storage_used >= out->storage_capacity) return TextResult::CapacityExceeded;
        out->storage[out->storage_used++] = 0;
    }
    *offset = static_cast<std::uint32_t>(out->storage_used);
    *units = 0;
    for (std::size_t index = 0; index < limit;) {
        std::uint8_t chunk[64] = {};
        std::size_t take = limit-index < sizeof(chunk)/unit_bytes ? limit-index : sizeof(chunk)/unit_bytes;
        if (address > UINTPTR_MAX-index*unit_bytes) return TextResult::ReadFailed;
        if (!Read(access, address+index*unit_bytes, chunk, take*unit_bytes)) {
            take = 1;
            if (!Read(access, address+index*unit_bytes, chunk, unit_bytes)) return TextResult::ReadFailed;
        }
        for (std::size_t i = 0; i < take; ++i, ++index) {
            std::uint16_t value = 0;
            std::memcpy(&value, chunk+i*unit_bytes, unit_bytes);
            if (out->storage_capacity-out->storage_used < unit_bytes) return TextResult::CapacityExceeded;
            std::memcpy(out->storage+out->storage_used, &value, unit_bytes);
            out->storage_used += unit_bytes;
            if (!value) { *units = static_cast<std::uint32_t>(index); return TextResult::Succeeded; }
        }
    }
    return TextResult::CapacityExceeded;
}
std::uint8_t KeyBucket(const char* key) noexcept {
    std::uint8_t hash = 1;
    while (*key) {
        auto value = static_cast<std::uint8_t>(*key++);
        if (value >= 'a' && value <= 'z') value &= 0xdfU;
        hash = static_cast<std::uint8_t>((33U*hash)^value);
    }
    return hash;
}
bool SameKey(const char* a, const char* b) noexcept {
    for (;;) {
        auto x = static_cast<unsigned char>(*a++), y = static_cast<unsigned char>(*b++);
        if (x >= 'a' && x <= 'z') x &= 0xdfU;
        if (y >= 'a' && y <= 'z') y &= 0xdfU;
        if (x != y) return false;
        if (!x) return true;
    }
}
TextResult Capture(const NativeTextAccess& access, TextSnapshot* out,
    TextFence* captured_fence) noexcept {
    out->complete = false; out->count = 0; out->storage_used = 0;
    std::memset(out->bucket_counts, 0, sizeof(out->bucket_counts));
    auto result = Profile(access);
    if (result != TextResult::Succeeded) return result;
    TextFence before = {}, after = {};
    if ((result = GetFence(access, &before)) != TextResult::Succeeded) return result;
    Bucket buckets[kTextBucketCount] = {}, verify[kTextBucketCount] = {};
    std::uint32_t count = 0, end_count = 0;
    if (!Read(access, access.engine_base + kTextBucketsRva, buckets, sizeof(buckets)) ||
        !Read(access, access.engine_base + kTextCountRva, &count, sizeof(count))) return TextResult::ReadFailed;
    if (count > out->entry_capacity) return TextResult::CapacityExceeded;
    for (std::size_t bucket = 0; bucket < kTextBucketCount; ++bucket) {
        const Bucket& b = buckets[bucket];
        if (b.count > out->entry_capacity - out->count) return TextResult::CapacityExceeded;
        if ((!b.count && (b.head || b.tail)) || (b.count && (!b.head || !b.tail))) return TextResult::InvalidTable;
        out->bucket_counts[bucket] = b.count;
        const std::size_t bucket_start = out->count;
        std::uintptr_t node = b.head, previous = 0;
        for (std::uint32_t i = 0; i < b.count; ++i) {
            Node n = {}, check = {};
            if (!Read(access, node, &n, sizeof(n))) return TextResult::ReadFailed;
            if (n.previous != previous || n.next == node) return TextResult::InvalidTable;
            TextEntry entry = {};
            result = CopyString(access, n.key, 1, kMaximumKeyBytes, out,
                &entry.key_offset, &entry.key_bytes);
            if (result != TextResult::Succeeded) return result;
            if (!entry.key_bytes) return TextResult::InvalidTable;
            const char* key = reinterpret_cast<const char*>(out->storage+entry.key_offset);
            if (KeyBucket(key) != bucket) return TextResult::InvalidTable;
            for (std::size_t prior = bucket_start; prior < out->count; ++prior)
                if (SameKey(key, reinterpret_cast<const char*>(out->storage+out->entries[prior].key_offset)))
                    return TextResult::InvalidTable;
            result = CopyString(access, n.value, 2, kMaximumValueUnits, out,
                &entry.value_offset, &entry.value_units);
            if (result != TextResult::Succeeded) return result;
            if (!Read(access, node, &check, sizeof(check)) ||
                std::memcmp(&n, &check, sizeof(n)) != 0) return TextResult::ChangedDuringCapture;
            out->entries[out->count++] = entry;
            previous = node; node = n.next;
        }
        if (node || previous != b.tail) return TextResult::InvalidTable;
    }
    if (out->count != count) return TextResult::InvalidTable;
    if (!Read(access, access.engine_base + kTextBucketsRva, verify, sizeof(verify)) ||
        !Read(access, access.engine_base + kTextCountRva, &end_count, sizeof(end_count))) return TextResult::ReadFailed;
    if ((result = GetFence(access, &after)) != TextResult::Succeeded) return result;
    if (!EqualFence(before, after) || count != end_count ||
        std::memcmp(buckets, verify, sizeof(buckets)) != 0) return TextResult::ChangedDuringCapture;
    out->complete = true; *captured_fence = after;
    return TextResult::Succeeded;
}
bool EqualSnapshot(const TextSnapshot& a, const TextSnapshot& b) noexcept {
    if (!a.complete || !b.complete || a.count != b.count ||
        std::memcmp(a.bucket_counts, b.bucket_counts, sizeof(a.bucket_counts)) != 0) return false;
    for (std::size_t i = 0; i < a.count; ++i) {
        const TextEntry& x = a.entries[i]; const TextEntry& y = b.entries[i];
        if (x.key_bytes != y.key_bytes || x.value_units != y.value_units ||
            std::memcmp(a.storage+x.key_offset, b.storage+y.key_offset, x.key_bytes+1U) != 0 ||
            std::memcmp(a.storage+x.value_offset, b.storage+y.value_offset, 2U*(x.value_units+1U)) != 0)
            return false;
    }
    return true;
}
TextResult Qualify(LocalizedTextBaselineOwner* owner, TextFence* fence) noexcept {
    if (!owner->baseline.complete) return TextResult::BaselineUnavailable;
    auto result = Profile(owner->access);
    if (result != TextResult::Succeeded) return result;
    if ((result = GetFence(owner->access, fence)) != TextResult::Succeeded) return result;
    if (std::strlen(fence->locale) != std::strlen(owner->baseline_fence.locale) ||
        std::memcmp(fence->locale, owner->baseline_fence.locale,
            std::strlen(fence->locale)) != 0) return TextResult::LocaleMismatch;
    if (fence->owner_generation != owner->baseline_fence.owner_generation) return TextResult::OwnerMismatch;
    return TextResult::Succeeded;
}
}  // namespace

TextResult InitializeOwner(LocalizedTextBaselineOwner* owner,
    const NativeTextAccess* access, const TextSnapshot* baseline,
    const TextSnapshot* scratch) noexcept {
    if (!owner || !access || !baseline || !scratch || !access->read || !access->fence ||
        !access->clear || !access->insert || !ValidStorage(*baseline) || !ValidStorage(*scratch))
        return TextResult::InvalidArgument;
    if (owner->phase != TextPhase::Empty) return TextResult::WrongPhase;
    const void* blocks[] = {baseline->entries, baseline->storage, scratch->entries, scratch->storage};
    const std::size_t sizes[] = {baseline->entry_capacity*sizeof(TextEntry), baseline->storage_capacity,
        scratch->entry_capacity*sizeof(TextEntry), scratch->storage_capacity};
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = i+1; j < 4; ++j)
        if (Overlap(blocks[i], sizes[i], blocks[j], sizes[j])) return TextResult::InvalidArgument;
    const auto result = Profile(*access);
    if (result != TextResult::Succeeded) return result;
    owner->access = *access; owner->baseline = *baseline; owner->scratch = *scratch;
    owner->baseline.complete = false; owner->scratch.complete = false;
    return Finish(owner, TextResult::Succeeded);
}
TextResult CaptureCleanBaseline(LocalizedTextBaselineOwner* owner) noexcept {
    if (!owner || !owner->access.read) return TextResult::InvalidArgument;
    if (owner->phase != TextPhase::Empty || owner->baseline.complete) return TextResult::AlreadyComplete;
    TextFence fence = {};
    auto result = GetFence(owner->access, &fence);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    if (!fence.first_clean_frontend || fence.returned_frontend || fence.custom_epoch_count)
        return Finish(owner, TextResult::NotCleanStartup);
    result = Capture(owner->access, &owner->baseline, &fence);
    if (result == TextResult::Succeeded && !owner->baseline.count) {
        owner->baseline.complete = false; result = TextResult::BaselineUnavailable;
    }
    if (result == TextResult::Succeeded &&
        (!fence.first_clean_frontend || fence.returned_frontend || fence.custom_epoch_count)) {
        owner->baseline.complete = false; result = TextResult::NotCleanStartup;
    }
    if (result == TextResult::Succeeded) {
        owner->baseline_fence = fence; owner->phase = TextPhase::BaselineCaptured;
    }
    return Finish(owner, result);
}
TextResult PreflightTextReset(LocalizedTextBaselineOwner* owner, std::uint64_t transaction) noexcept {
    if (!owner || !transaction) return TextResult::InvalidArgument;
    if (!owner->baseline.complete) return Finish(owner, TextResult::BaselineUnavailable);
    if (owner->phase != TextPhase::BaselineCaptured && owner->phase != TextPhase::Verified)
        return Finish(owner, TextResult::WrongPhase);
    if (transaction <= owner->transaction_generation) return Finish(owner, TextResult::WrongTransaction);
    auto result = Profile(owner->access);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    TextFence fence = {};
    if (!owner->access.fence(owner->access.user, &fence) || !fence.owner_generation ||
        !std::memchr(fence.locale, 0, sizeof(fence.locale))) return Finish(owner, TextResult::ReadFailed);
    if (!fence.authoritative_thread) return Finish(owner, TextResult::AccessNotQuiescent);
    if (std::memcmp(fence.locale, owner->baseline_fence.locale, sizeof(fence.locale))) return Finish(owner, TextResult::LocaleMismatch);
    if (fence.owner_generation != owner->baseline_fence.owner_generation) return Finish(owner, TextResult::OwnerMismatch);
    return Finish(owner, TextResult::Succeeded);
}
TextResult PrepareTextReset(LocalizedTextBaselineOwner* owner, std::uint64_t transaction) noexcept {
    if (!owner || !transaction) return TextResult::InvalidArgument;
    if (owner->phase != TextPhase::BaselineCaptured && owner->phase != TextPhase::Verified)
        return Finish(owner, TextResult::WrongPhase);
    if (transaction <= owner->transaction_generation) return Finish(owner, TextResult::WrongTransaction);
    TextFence fence = {};
    auto result = Qualify(owner, &fence);
    if (result == TextResult::Succeeded) result = Capture(owner->access, &owner->scratch, &fence);
    if (result == TextResult::Succeeded) result = Qualify(owner, &fence);
    if (result == TextResult::Succeeded) {
        owner->transaction_generation = transaction; owner->phase = TextPhase::Prepared;
    }
    return Finish(owner, result);
}
TextResult ClearCustomTexts(LocalizedTextBaselineOwner* owner, std::uint64_t transaction) noexcept {
    if (!owner || transaction != owner->transaction_generation) return TextResult::WrongTransaction;
    if (owner->phase == TextPhase::Cleared) return TextResult::AlreadyComplete;
    if (owner->phase != TextPhase::Prepared) return Finish(owner, TextResult::WrongPhase);
    TextFence fence = {};
    auto result = Qualify(owner, &fence);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    result = Capture(owner->access, &owner->scratch, &fence);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    TextFence current = {};
    result = Qualify(owner, &current);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    if (!EqualFence(fence, current)) return Finish(owner, TextResult::ChangedDuringCapture);
    owner->phase = TextPhase::Failed;
    if (!owner->access.clear(owner->access.user, owner->access.engine_base+kClearTextsRva))
        return Finish(owner, TextResult::NativeCallFailed);
    result = Capture(owner->access, &owner->scratch, &fence);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    if (!EqualFence(current, fence)) return Finish(owner, TextResult::ChangedDuringCapture);
    if (owner->scratch.count) return Finish(owner, TextResult::BaselineMismatch);
    owner->phase = TextPhase::Cleared;
    return Finish(owner, TextResult::Succeeded);
}
TextResult RestoreBaselineTexts(LocalizedTextBaselineOwner* owner, std::uint64_t transaction) noexcept {
    if (!owner || transaction != owner->transaction_generation) return TextResult::WrongTransaction;
    if (owner->phase == TextPhase::Restored || owner->phase == TextPhase::Verified) return TextResult::AlreadyComplete;
    if (owner->phase != TextPhase::Cleared) return Finish(owner, TextResult::WrongPhase);
    TextFence fence = {};
    auto result = Qualify(owner, &fence);
    if (result != TextResult::Succeeded) return Finish(owner, result);
    owner->phase = TextPhase::Failed;
    for (std::size_t i = 0; i < owner->baseline.count; ++i) {
        TextFence current = {};
        if ((result = Qualify(owner, &current)) != TextResult::Succeeded) return Finish(owner, result);
        if (!EqualFence(fence, current)) return Finish(owner, TextResult::ChangedDuringCapture);
        const TextEntry& e = owner->baseline.entries[i];
        const char* key = reinterpret_cast<const char*>(owner->baseline.storage+e.key_offset);
        const auto* value = reinterpret_cast<const std::uint16_t*>(owner->baseline.storage+e.value_offset);
        if (!owner->access.insert(owner->access.user, owner->access.engine_base+kInsertTextRva, key, value))
            return Finish(owner, TextResult::NativeCallFailed);
    }
    TextFence after = {};
    if ((result = Qualify(owner, &after)) != TextResult::Succeeded) return Finish(owner, result);
    if (!EqualFence(fence, after)) return Finish(owner, TextResult::ChangedDuringCapture);
    owner->phase = TextPhase::Restored;
    return Finish(owner, TextResult::Succeeded);
}
TextResult VerifyBaselineTexts(LocalizedTextBaselineOwner* owner, std::uint64_t transaction) noexcept {
    if (!owner || transaction != owner->transaction_generation) return TextResult::WrongTransaction;
    if (owner->phase == TextPhase::Verified) return TextResult::AlreadyComplete;
    if (owner->phase != TextPhase::Restored) return Finish(owner, TextResult::WrongPhase);
    TextFence fence = {}; auto result = Qualify(owner, &fence);
    if (result == TextResult::Succeeded) result = Capture(owner->access, &owner->scratch, &fence);
    if (result == TextResult::Succeeded) result = Qualify(owner, &fence);
    if (result == TextResult::Succeeded && !EqualSnapshot(owner->baseline, owner->scratch)) result = TextResult::BaselineMismatch;
    owner->phase = result == TextResult::Succeeded ? TextPhase::Verified : TextPhase::Failed;
    return Finish(owner, result);
}
TextResult CancelPreparedTextReset(LocalizedTextBaselineOwner* owner, std::uint64_t transaction) noexcept {
    if (!owner || transaction != owner->transaction_generation) return TextResult::WrongTransaction;
    if (owner->phase != TextPhase::Prepared) return Finish(owner, TextResult::WrongPhase);
    owner->phase = TextPhase::BaselineCaptured;
    return Finish(owner, TextResult::Succeeded);
}
TextResult ReverifyBaselineTexts(LocalizedTextBaselineOwner* owner,std::uint64_t transaction) noexcept {
    if(!owner || transaction!=owner->transaction_generation)return TextResult::WrongTransaction;
    if(owner->phase!=TextPhase::Verified)return Finish(owner,TextResult::WrongPhase);
    TextFence fence={};auto result=Qualify(owner,&fence);
    if(result==TextResult::Succeeded)result=Capture(owner->access,&owner->scratch,&fence);
    if(result==TextResult::Succeeded)result=Qualify(owner,&fence);
    if(result==TextResult::Succeeded && !EqualSnapshot(owner->baseline,owner->scratch))result=TextResult::BaselineMismatch;
    if(result!=TextResult::Succeeded)owner->phase=TextPhase::Failed;
    return Finish(owner,result);
}
const char* TextResultName(TextResult result) noexcept {
    switch (result) {
#define TEXT_CASE(name) case TextResult::name: return #name
    TEXT_CASE(Succeeded); TEXT_CASE(AlreadyComplete); TEXT_CASE(InvalidArgument);
    TEXT_CASE(ProfileMismatch); TEXT_CASE(NotCleanStartup); TEXT_CASE(BaselineUnavailable);
    TEXT_CASE(LocaleMismatch); TEXT_CASE(OwnerMismatch); TEXT_CASE(AccessNotQuiescent);
    TEXT_CASE(ChangedDuringCapture); TEXT_CASE(ReadFailed); TEXT_CASE(InvalidTable);
    TEXT_CASE(CapacityExceeded); TEXT_CASE(WrongTransaction); TEXT_CASE(WrongPhase);
    TEXT_CASE(NativeCallFailed); TEXT_CASE(BaselineMismatch);
#undef TEXT_CASE
    }
    return "Unknown";
}
}  // namespace dluma::fixes::custom_map_asset_isolation::localized_texts
