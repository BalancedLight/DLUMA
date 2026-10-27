#define _HAS_EXCEPTIONS 0
#include "AudioSoundPropertyNative.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_sound_properties {
namespace {
constexpr std::uintptr_t targets[] = {0x11B8E0, 0x11EA20, 0x6A4080, 0x11DAF0, 0x11B940};
constexpr unsigned char prefixes[][16] = {
    {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x51,0x70,0x48,0x8b},
    {0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x53,0x55,0x56,0x57,0x48,0x81,0xec},
    {0x40,0x53,0x48,0x83,0xec,0x20,0x4c,0x8b,0x52,0x10,0x48,0x8b,0xd9,0x48,0x8b,0xc2},
    {0x48,0x85,0xd2,0x0f,0x84,0x97,0x00,0x00,0x00,0x48,0x89,0x5c,0x24,0x10,0x48,0x89},
    {0x40,0x53,0x57,0x41,0x54,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x89,0x6c,0x24},
};
struct String { const char* data; std::uint32_t size, capacity; };
struct Strings { const String* data; std::uint32_t size, capacity, tag, padding; };
static_assert(sizeof(String) == 16 && sizeof(Strings) == 24, "native ttl input layout");
bool Read(const NativeBinding& b, std::uintptr_t at, void* out, std::size_t n) noexcept {
    return at && at <= UINTPTR_MAX - n && b.access.read(b.access.user, at, out, n);
}
bool TargetsMatch(const NativeBinding& b) noexcept {
    unsigned char actual[16] = {};
    for (std::size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
        if (!Read(b, b.access.engine_base + targets[i], actual, sizeof(actual)) ||
            std::memcmp(actual, prefixes[i], sizeof(actual))) return false;
    return true;
}
bool Authorize(NativeBinding* b, NativeOperation operation, std::uintptr_t owner,
               const char* first = nullptr, const char* second = nullptr) noexcept {
    Fence fence = {};
    return b && b->validated && b->authorize && TargetsMatch(*b) &&
        b->access.fence(b->access.user, &fence) && fence.audio_owner && fence.owner_generation &&
        fence.revision && fence.thread_id && fence.sound_lock_owned &&
        fence.producer_admission_closed && fence.borrowed_values_drained &&
        b->authorize(b->owner_user, operation, owner, first, second);
}
bool CanRemove(void* user, const Property& property) noexcept {
    auto* b = static_cast<NativeBinding*>(user);
    return b && b->validated && b->can_remove_extra &&
        b->can_remove_extra(b->owner_user, property);
}
bool Clear(void* user, std::uintptr_t bank) noexcept {
    auto* b = static_cast<NativeBinding*>(user);
    if (!Authorize(b, NativeOperation::ClearAliases, bank)) return false;
    __try {
        reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(b->access.engine_base + targets[0])(bank);
    } __except(1) { return false; }
    std::uintptr_t root = 1; std::uint32_t count = 1;
    return Read(*b, bank + 112, &root, sizeof(root)) && Read(*b, bank + 120, &count, sizeof(count)) && !root && !count;
}
bool Store(void* user, std::uintptr_t holder, const Value& value) noexcept {
    auto* b = static_cast<NativeBinding*>(user);
    if (value.length > kDataBytes || (value.mask >> kPropertyCount) ||
        !Authorize(b, NativeOperation::StoreValue, holder)) return false;
    std::uint32_t expected_length = 0;
    for (std::size_t i = 0; i < kPropertyCount; ++i) {
        if (!(value.mask & (1U << i))) continue;
        unsigned char schema[2] = {};
        if (!Read(*b, b->access.engine_base + kPropertySchemaRva + i * 24, schema, 2) ||
            schema[0] > 2 || !schema[1]) return false;
        expected_length += (schema[0] ? 4U : 1U) * schema[1];
    }
    if (expected_length != value.length) return false;
    __try {
        // Native allocator replaces only the packed data allocation and offsets.
        // The property-holder address borrowed by shared sources stays stable.
        const auto mask = reinterpret_cast<std::uint32_t(__fastcall*)(void*, std::uint32_t)>(
            b->access.engine_base + targets[1])(reinterpret_cast<void*>(holder + 8), value.mask);
        if (mask != value.mask) return false;
        std::uintptr_t data = 0;
        if (!Read(*b, holder + 8, &data, sizeof(data)) || (value.length && !data)) return false;
        if (value.length) std::memcpy(reinterpret_cast<void*>(data), value.data, value.length);
        *reinterpret_cast<std::uint32_t*>(holder) = mask;
        return !value.length || !std::memcmp(reinterpret_cast<const void*>(data), value.data, value.length);
    } __except(1) { return false; }
}
bool Remove(void* user, std::uintptr_t bank, const Property& property) noexcept {
    auto* b = static_cast<NativeBinding*>(user);
    if (!property.link || property.link < 96 || property.holder != property.link - 80 ||
        !Authorize(b, NativeOperation::RemoveProperty, property.holder, property.name) ||
        !CanRemove(user, property)) return false;
    std::uint32_t before = 0;
    if (!Read(*b, bank + 96, &before, sizeof(before)) || !before) return false;
    __try {
        reinterpret_cast<void*(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
            b->access.engine_base + targets[2])(bank + 88, property.link);
        std::uint32_t after = 0;
        if (!Read(*b, bank + 96, &after, sizeof(after)) || after + 1 != before) return false;
        // The native erase unlinks but does not clear the detached node's links.
        // Dispose this one owned node, never an attached subtree.
        *reinterpret_cast<std::uintptr_t*>(property.link) = 0;
        *reinterpret_cast<std::uintptr_t*>(property.link + 16) = 0;
        reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
            b->access.engine_base + targets[3])(bank + 88, property.link);
    } __except(1) { return false; }
    return true;
}
bool Register(void* user, std::uintptr_t bank, const char* alias, const char* group) noexcept {
    auto* b = static_cast<NativeBinding*>(user);
    if (!alias || !group || !std::memchr(alias, 0, kNameBytes) || !std::memchr(group, 0, kNameBytes) ||
        !Authorize(b, NativeOperation::RegisterAlias, bank, alias, group)) return false;
    const auto n = static_cast<std::uint32_t>(std::strlen(alias));
    const String name = {alias, n, n};
    const Strings names = {&name, 1, 1, 10, 0};
    __try {
        reinterpret_cast<void(__fastcall*)(std::uintptr_t, const Strings*, const char*)>(
            b->access.engine_base + targets[4])(bank, &names, group);
    } __except(1) { return false; }
    return true;
}
}
bool ValidateNativeBinding(NativeBinding* b, const char* sha) noexcept {
    static constexpr char expected[] = "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c";
    if (!b) return false;
    b->validated = false;
    if (!sha || !b->access.read || !b->access.fence || !b->authorize || !b->can_remove_extra ||
        !b->access.engine_base || b->access.engine_base > UINTPTR_MAX - 0xE8D000U) return false;
    for (std::size_t i = 0; i < sizeof(expected); ++i) if (sha[i] != expected[i]) return false;
    b->validated = TargetsMatch(*b); return b->validated;
}
RestoreOperations MakeNativeRestoreOperations(NativeBinding* binding) noexcept {
    return binding && binding->validated
        ? RestoreOperations{binding, &CanRemove, &Clear, &Store, &Remove, &Register}
        : RestoreOperations{};
}
} // namespace
