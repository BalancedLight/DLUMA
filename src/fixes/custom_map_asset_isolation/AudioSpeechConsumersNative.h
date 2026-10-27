#ifndef DLUMA_AUDIO_SPEECH_CONSUMERS_NATIVE_H
#define DLUMA_AUDIO_SPEECH_CONSUMERS_NATIVE_H
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::audio_speech_consumers {
constexpr std::size_t kMaximumCachedSpeech = 4096;
constexpr std::uintptr_t kUserStreamFactoryRva = 0xF9850;
constexpr std::uintptr_t kUserStreamLookupRva = 0xF9B50;
constexpr std::uintptr_t kStartSpeechRva = 0x32B7B0;
constexpr std::uintptr_t kLoadSpeechDataRva = 0x32C350;
constexpr std::uintptr_t kClearCachedSpeechRva = 0x32CFB0;
enum class Result : std::uint8_t { Succeeded, InvalidArgument, FenceUnavailable,
    ReadFailed, Changed, InvalidGraph, ActiveSpeech, UserStreams, NativeFailure };
struct Fence {
    std::uintptr_t audio_owner, speech_manager;
    std::uint64_t owner_generation, transaction;
    std::uint32_t epoch, thread;
    bool sound_lock_owned, speech_gate_owned, producers_closed, post_c10;
};
using ReadFn = bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
using FenceFn = bool(*)(void*,Fence*) noexcept;
struct Access { std::uintptr_t engine_base; ReadFn read; FenceFn fence; void* user; bool exact_image; };
struct Snapshot {
    Fence fence;
    std::uintptr_t stream_data,stream_head,speech_data,cache_head,cache_tail;
    std::uint32_t streams,stream_capacity,active_speech,speech_capacity,cached_speech;
    bool complete;
};
// A complete snapshot is observational. Empty() is required before removing
// cached phoneme data or SPB tracks; ordinary voice release does not imply it.
Result Capture(const Access&,Snapshot*) noexcept;
Result Empty(const Snapshot&) noexcept;
// Only this native cache-list owner releases cached CSpeechData borrowers.
// It deliberately never stops stale IModelObjects or destroys global streams.
Result ClearCached(const Access&,Snapshot*) noexcept;
bool ValidateNativeTargets(const Access&) noexcept;
} // namespace
#endif
