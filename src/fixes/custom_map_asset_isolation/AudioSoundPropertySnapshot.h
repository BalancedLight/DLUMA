#ifndef DLUMA_AUDIO_SOUND_PROPERTY_SNAPSHOT_H
#define DLUMA_AUDIO_SOUND_PROPERTY_SNAPSHOT_H
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_sound_properties {
constexpr std::size_t kPropertyCount = 29;
constexpr std::size_t kNameBytes = 512;
constexpr std::size_t kDataBytes = 256;
constexpr std::size_t kMaximumProperties = 4096;
constexpr std::size_t kMaximumAliases = 16384;
constexpr std::uintptr_t kSoundBankOffset = 7544;
constexpr std::uintptr_t kPropertySchemaRva = 0xA9A339;
constexpr std::uintptr_t kDefaultPropertyRva = 0xAD53E0;
constexpr std::uintptr_t kSoundBankVtableRva = 0x8AE068;

enum class Result : std::uint8_t {
    Succeeded, InvalidArgument, FenceUnavailable, ReadFailed, Changed,
    InvalidSchema, InvalidTree, InvalidProperty, InvalidAlias, Capacity,
    BaselineHolderMissing, ExtraConsumer, NativeCallFailed, VerificationFailed,
};
struct Fence {
    std::uintptr_t audio_owner;
    std::uint64_t owner_generation, revision;
    std::uint32_t thread_id;
    bool sound_lock_owned, producer_admission_closed, borrowed_values_drained;
};
using ReadFn = bool (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
using FenceFn = bool (*)(void*, Fence*) noexcept;
struct Access { void* user; ReadFn read; FenceFn fence; std::uintptr_t engine_base; };
struct Value {
    std::uint32_t mask;
    std::uint16_t length;
    unsigned char data[kDataBytes];
};
struct Property {
    std::uintptr_t link, holder;
    char name[kNameBytes];
    Value value;
};
struct Alias { char name[kNameBytes]; std::uint32_t property_index; };
struct CommonBorrower { std::uintptr_t object, property_holder; };
struct Snapshot {
    Property* properties;
    Alias* aliases;
    std::size_t property_capacity, alias_capacity, property_count, alias_count;
    Fence fence;
    Value fallback;
    // Separate common-bank consumers are not silently declared released by
    // restoring properties. The complete Audio owner must account for them.
    std::uintptr_t common_sources_head, common_banks_data;
    std::uint32_t common_sources_count, common_banks_count;
    bool complete;
};

Result Capture(const Access&, Snapshot*) noexcept;
// Observational B0 capture while the sound mutex protects the native tables.
// Existing borrowers may be live; the output preserves their fence flags.
// Restore continues to require the stronger destructive-owner fence.
Result CaptureReadOnly(const Access&, Snapshot*) noexcept;
// Enumerates every registered common sound whose +96 property slot is reset
// by native ClearAliases. Unknown holders and incomplete/cyclic lists reject.
Result CaptureCommonBorrowers(const Access&,const Snapshot&,CommonBorrower*,
                             std::size_t capacity,std::size_t* count) noexcept;
bool SameValues(const Snapshot&, const Snapshot&, bool require_same_holders) noexcept;
// Checks all old identities and external consumers before the first mutation.
using CanRemoveFn = bool (*)(void*, const Property&) noexcept;
using ClearAliasesFn = bool (*)(void*, std::uintptr_t sound_bank) noexcept;
using StoreValueFn = bool (*)(void*, std::uintptr_t holder, const Value&) noexcept;
using RemovePropertyFn = bool (*)(void*, std::uintptr_t sound_bank, const Property&) noexcept;
using AliasFn = bool (*)(void*, std::uintptr_t sound_bank, const char* alias, const char* group) noexcept;
struct RestoreOperations {
    void* user;
    CanRemoveFn can_remove_extra;
    ClearAliasesFn clear_aliases;
    StoreValueFn store_value;
    RemovePropertyFn remove_property;
    AliasFn register_alias;
};
Result PreflightRestore(const Snapshot& baseline, const Snapshot& current,
                        const RestoreOperations&) noexcept;
Result Restore(const Access&, const Snapshot& baseline, Snapshot* scratch,
               const RestoreOperations&) noexcept;
} // namespace
#endif
