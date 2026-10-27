#include "NativeRetainedResourceWindows.h"
#include "../../dlw_win32.h"
#include <cstring>
#include <limits>

namespace dluma::fixes::custom_map_asset_isolation::retained::windows {
namespace {
using TryCriticalSectionFn = int (WINAPI*)(void*);
using LeaveCriticalSectionFn = void (WINAPI*)(void*);
using AcquireRendererFn = void (__fastcall*)(void*, const char*);
using ReleaseRendererFn = void (__fastcall*)(void*);
template<class T> T KernelExport(const char* name) noexcept
{ return reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(reinterpret_cast<LPCWSTR>(L"kernel32.dll")), name)); }
bool Read(const Binding& b, std::uintptr_t address, void* out, std::size_t size) noexcept
{
    return b.read && out && size && address && size - 1 <= UINTPTR_MAX - address &&
        b.read(b.read_user, address, out, size);
}
template<class T> bool Field(const Binding& b, std::uintptr_t object, std::uintptr_t offset, T* out) noexcept
{ return offset <= UINTPTR_MAX - object && Read(b, object + offset, out, sizeof(*out)); }
bool Name(const Binding& b, std::uintptr_t pointer, char* out) noexcept
{
    std::memset(out, 0, kNameCapacity);
    if (!pointer) return true;
    for (std::size_t i = 0; i < kNameCapacity; ++i) {
        if (i > UINTPTR_MAX - pointer || !Read(b, pointer + i, out + i, 1)) return false;
        if (!out[i]) return true;
    }
    return false;
}
bool SameFence(const Fence& a, const Fence& b) noexcept
{
    return a.transaction == b.transaction && a.epoch == b.epoch &&
        a.owner_generation == b.owner_generation && a.mutation_revision == b.mutation_revision &&
        a.thread_id == b.thread_id && a.authoritative_post_destroy == b.authoritative_post_destroy &&
        a.producers_closed == b.producers_closed && a.readers_drained == b.readers_drained &&
        a.native_lock_owned == b.native_lock_owned && a.worker_completions_drained == b.worker_completions_drained;
}
bool ReadFence(const Binding& b, Fence* out) noexcept
{
    if (!b.gate_thread || b.gate_thread != GetCurrentThreadId() || !b.renderer_section_owned ||
        !b.renderer_acquired || !b.texture_section_owned || !b.fence || !b.fence(b.fence_user, out)) return false;
    std::uintptr_t renderer = 0, vtable = 0;
    std::uint32_t native_depth = 0;
    unsigned char locked_mode = 0, manager_flags = 0;
    if (!Field(b, b.engine_base, kRendererGlobalRva, &renderer) || renderer != b.gate_renderer ||
        !Field(b, b.engine_base+kTextureManagerRva,156,&manager_flags)||(manager_flags&8)||
        !Field(b, renderer, 0, &vtable) || vtable != b.engine_base + kRendererVtableRva ||
        !Field(b, renderer, 360, &locked_mode) || locked_mode != 1 ||
        !Field(b, renderer, 316, &native_depth) || !native_depth || !b.ledger ||
        b.ledger->revision > UINT64_MAX - out->mutation_revision) return false;
    out->mutation_revision += b.ledger->revision;
    out->native_lock_owned = true;
    return out->owner_generation && out->thread_id == b.gate_thread &&
        out->producers_closed && out->readers_drained && out->native_lock_owned &&
        out->worker_completions_drained &&
        ((!out->transaction && !out->epoch) || (out->transaction && out->epoch && out->authoritative_post_destroy));
}
Result ReadTexture(const Binding& b, std::uintptr_t manager, std::uintptr_t object, NativeState* out) noexcept
{
    *out = {}; out->object = object; out->manager = manager; out->kind = Kind::Texture;
    std::uintptr_t name = 0;std::uint32_t backend_format=0;
    if (!Field(b, object, 0, &out->vtable) || out->vtable != b.engine_base + kTextureVtableRva ||
        !Field(b, object, 8, &out->backend) || !Field(b, object, 52, &out->flags) ||
        !Field(b,object,56,&backend_format)||(out->backend&&((backend_format>>24)&15)>2)||
        !Field(b, object, 60, &out->references) || !Field(b, object, 64, &out->callback_context) ||
        !Field(b, object, 72, &out->callback) || !Field(b, object, 80, &out->deferred) ||
        !Field(b, object, 96, &name) || !Name(b, name, out->name)) return Result::CoverageIncomplete;
    out->definition=backend_format;return Result::Succeeded;
}
Result Capture(void* user, Inventory* inventory) noexcept
{ return CaptureTextures(static_cast<Binding*>(user), inventory); }
bool FenceOperation(void* user, retained::Fence* fence) noexcept
{
    auto* b = static_cast<Binding*>(user);
    return b && b->validated && ReadFence(*b, fence);
}
Result Release(void* user, const NativeState& expected) noexcept
{
    auto* b = static_cast<Binding*>(user);
    if (!b || !b->validated || !b->ledger || !b->original_destroy ||
        expected.kind != Kind::Texture || expected.manager != b->engine_base + kTextureManagerRva)
        return Result::InvalidArgument;
    retained::Fence fence = {};
    if (!ReadFence(*b, &fence) || !fence.authoritative_post_destroy) return Result::FenceUnavailable;
    NativeState state = {};
    Result result = ReadTexture(*b, expected.manager, expected.object, &state);
    if (result != Result::Succeeded) return result;
    if (state.references) return Result::NativeReferencesLive;
    if (state.deferred) return Result::DeferredResourceLive;
    if (state.callback || state.callback_context) return Result::CallbackRetained;
    if (state.vtable != expected.vtable || state.backend != expected.backend || state.flags != expected.flags ||
        std::memcmp(state.name, expected.name, sizeof(state.name))) return Result::InventoryChanged;
    Identity identity = {};
    result = FindIdentity(b->ledger, state.object, Kind::Texture, &identity);
    if (result != Result::Succeeded) return result;
    bool owned = false;
    for (std::size_t i = 0; i < b->ledger->count; ++i) {
        const Record& record = b->ledger->records[i];
        if (record.identity.object == identity.object && record.identity.generation == identity.generation)
            owned = record.life == Life::Live && record.project && !record.global && !record.baseline;
    }
    if (!owned || b->ledger->coverage_lost) return Result::BaselineChanged;
    result = BeginDestruction(b->ledger, identity);
    if (result != Result::Succeeded) return result;
    // Exact native Destroy unregisters by name+flags, releases the low-level
    // renderer interface, releases the owned name and deletes the texture.
    // Packed resources require their separate completed OnDemand owner path.
    b->original_destroy(reinterpret_cast<void*>(state.manager), reinterpret_cast<void*>(state.object));
    return EndDestruction(b->ledger, identity);
}
}
Result Validate(Binding* b) noexcept
{
    if (!b) return Result::InvalidArgument;
    if (b->gate_thread) return Result::InvalidArgument;
    b->validated = false;
    if (!b->engine_base || b->engine_base > UINTPTR_MAX - 0xE8D000 || !b->read || !b->fence ||
        !b->ledger || std::memcmp(b->engine_sha256, kPlayerEngineSha256, sizeof(kPlayerEngineSha256)))
        return Result::CoverageIncomplete;
    constexpr unsigned char prefix[] = {0x48, 0x85, 0xd2, 0x0f, 0x84, 0xb5, 0x00, 0x00, 0x00,
        0x48, 0x89, 0x5c, 0x24, 0x10, 0x56, 0x48, 0x83, 0xec, 0x30};
    unsigned char bytes[sizeof(prefix)] = {};
    if (!Read(*b, b->engine_base + kTextureDestroyRva, bytes, sizeof(bytes)) ||
        std::memcmp(bytes, prefix, sizeof(bytes))) return Result::CoverageIncomplete;
    std::uintptr_t vtable = 0;
    if (!Field(*b, b->engine_base + kTextureManagerRva, 0, &vtable) ||
        vtable != b->engine_base + kTextureManagerVtableRva) return Result::CoverageIncomplete;
    constexpr unsigned char acquire_prefix[] = {0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x20};
    constexpr unsigned char release_prefix[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x80,0xb9,0x68,0x01,0x00,0x00,0x00};
    if (!Read(*b, b->engine_base + kRendererAcquireRva, bytes, sizeof(acquire_prefix)) ||
        std::memcmp(bytes, acquire_prefix, sizeof(acquire_prefix)) ||
        !Read(*b, b->engine_base + kRendererReleaseRva, bytes, sizeof(release_prefix)) ||
        std::memcmp(bytes, release_prefix, sizeof(release_prefix))) return Result::CoverageIncomplete;
    if (!b->original_destroy)
        b->original_destroy = reinterpret_cast<DestroyTextureFn>(b->engine_base + kTextureDestroyRva);
    b->validated = true; return Result::Succeeded;
}
Result EnterTextureGate(Binding* b) noexcept
{
    if (!b || !b->validated || b->gate_thread || !b->fence) return Result::InvalidArgument;
    retained::Fence authority = {};
    if (!b->fence(b->fence_user, &authority) || !authority.owner_generation ||
        authority.thread_id != GetCurrentThreadId() || !authority.producers_closed ||
        !authority.readers_drained || !authority.worker_completions_drained ||
        (authority.epoch && (!authority.transaction || !authority.authoritative_post_destroy)))
        return Result::FenceUnavailable;
    if (!b->ledger || b->ledger->revision > UINT64_MAX - authority.mutation_revision)
        return Result::FenceUnavailable;
    authority.mutation_revision += b->ledger->revision;
    const auto enter = KernelExport<TryCriticalSectionFn>("TryEnterCriticalSection");
    const auto leave = KernelExport<LeaveCriticalSectionFn>("LeaveCriticalSection");
    std::uintptr_t renderer = 0, vtable = 0, acquire = 0, release = 0;
    unsigned char locked_mode = 0, manager_flags = 0;
    if (!enter || !leave || !Field(*b, b->engine_base, kRendererGlobalRva, &renderer) || !renderer ||
        !Field(*b,b->engine_base+kTextureManagerRva,156,&manager_flags)||(manager_flags&8)||
        !Field(*b, renderer, 0, &vtable) || vtable != b->engine_base + kRendererVtableRva ||
        !Field(*b, vtable, 344, &acquire) || acquire != b->engine_base + kRendererAcquireRva ||
        !Field(*b, vtable, 352, &release) || release != b->engine_base + kRendererReleaseRva ||
        !Field(*b, renderer, 360, &locked_mode) || locked_mode != 1)
        return Result::FenceUnavailable;
    // Match the native renderer-before-texture lock order. The preliminary
    // TryEnter makes the native recursive Acquire call nonblocking on its CS.
    if (!enter(reinterpret_cast<void*>(renderer + 272))) return Result::FenceUnavailable;
    b->gate_thread = GetCurrentThreadId(); b->gate_renderer = renderer; b->renderer_section_owned = true;
    reinterpret_cast<AcquireRendererFn>(acquire)(reinterpret_cast<void*>(renderer), "DLUMA retained resource reset");
    b->renderer_acquired = true;
    if (!enter(reinterpret_cast<void*>(b->engine_base + kTextureManagerRva + 32))) {
        LeaveTextureGate(b); return Result::FenceUnavailable;
    }
    b->texture_section_owned = true;
    retained::Fence current = {};
    if (!ReadFence(*b, &current) || current.transaction != authority.transaction ||
        current.epoch != authority.epoch || current.owner_generation != authority.owner_generation ||
        current.mutation_revision != authority.mutation_revision) {
        LeaveTextureGate(b); return Result::FenceUnavailable;
    }
    return Result::Succeeded;
}
void LeaveTextureGate(Binding* b) noexcept
{
    if (!b || !b->gate_thread || b->gate_thread != GetCurrentThreadId()) return;
    const auto leave = KernelExport<LeaveCriticalSectionFn>("LeaveCriticalSection");
    if (!leave) return;
    if (b->texture_section_owned) leave(reinterpret_cast<void*>(b->engine_base + kTextureManagerRva + 32));
    if (b->renderer_acquired)
        reinterpret_cast<ReleaseRendererFn>(b->engine_base + kRendererReleaseRva)(reinterpret_cast<void*>(b->gate_renderer));
    if (b->renderer_section_owned) leave(reinterpret_cast<void*>(b->gate_renderer + 272));
    b->texture_section_owned = false; b->renderer_acquired = false; b->renderer_section_owned = false;
    b->gate_thread = 0; b->gate_renderer = 0;
}
Result CaptureTextures(Binding* b, Inventory* out) noexcept
{
    if (!out) return Result::InvalidArgument;
    out->count = 0; out->complete = false;
    if (!b || !b->validated || !out->objects || !out->capacity || out->capacity > kMaximumResources)
        return Result::InvalidArgument;
    retained::Fence before = {}, after = {};
    if (!ReadFence(*b, &before)) return Result::FenceUnavailable;
    const std::uintptr_t manager = b->engine_base + kTextureManagerRva;
    std::uintptr_t vtable = 0, data = 0, final_data = 0;
    std::uint32_t count = 0, capacity = 0, final_count = 0, final_capacity = 0;
    if (!Field(*b, manager, 0, &vtable) || vtable != b->engine_base + kTextureManagerVtableRva ||
        !Field(*b, manager, 8, &data) || !Field(*b, manager, 16, &count) || !Field(*b, manager, 20, &capacity) ||
        count > capacity || count > out->capacity || (count && !data)) return Result::Capacity;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uintptr_t object = 0;
        if (!Field(*b, data, sizeof(object) * i, &object) || !object) return Result::CoverageIncomplete;
        for (std::size_t j = 0; j < out->count; ++j)
            if (out->objects[j].object == object) return Result::AddressReused;
        Result result = ReadTexture(*b, manager, object, &out->objects[out->count]);
        if (result != Result::Succeeded) return result;
        ++out->count;
    }
    if (!Field(*b, manager, 8, &final_data) || !Field(*b, manager, 16, &final_count) ||
        !Field(*b, manager, 20, &final_capacity) || data != final_data || count != final_count ||
        capacity != final_capacity || !ReadFence(*b, &after) || !SameFence(before, after))
        return Result::InventoryChanged;
    out->manager = manager; out->kind = Kind::Texture; out->fence = after; out->complete = true;
    return Result::Succeeded;
}
Operations MakeTextureOperations(Binding* b) noexcept
{ return {b, &Capture, &FenceOperation, &Release}; }
} // namespace
