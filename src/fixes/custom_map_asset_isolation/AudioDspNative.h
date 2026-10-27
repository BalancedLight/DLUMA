#ifndef DLUMA_AUDIO_DSP_NATIVE_H
#define DLUMA_AUDIO_DSP_NATIVE_H
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_dsp_native {
constexpr unsigned kMaximumBuses=14, kDspSlots=3, kMaximumParameters=32;
enum class Result : std::uint8_t { Succeeded, InvalidArgument, Unauthorized, ReadFailed, Changed, NativeFailure };
struct Identity {
    std::uintptr_t owner=0, system=0, groups[kMaximumBuses]={}, dsp[kMaximumBuses][kDspSlots]={},master_effect=0;
    std::uint64_t generation=0;
    unsigned buses=0;
};
struct Dsp { int type=0, count=0; float parameters[kMaximumParameters]={}; bool active=false,bypass=false; };
struct Snapshot {
    Identity identity={};
    Dsp dsp[kMaximumBuses][kDspSlots]={};
    Dsp master_effect={};float master_volume=0;
    float volume[kMaximumBuses]={};
    float mix[kMaximumBuses]={};
    std::uintptr_t master=0,head=0,connections[kMaximumBuses]={};
    std::uint32_t reverb[4][20]={};
    bool complete=false;
};
using ResolveFn=std::uintptr_t(*)(void*,const char*) noexcept;
using FenceFn=bool(*)(void*,const Identity&,bool mutation) noexcept;
struct Binding {
    std::uintptr_t module=0;
    ResolveFn resolve=nullptr;
    FenceFn fence=nullptr;
    void* user=nullptr;
    bool exact_image=false,validated=false;
    std::uintptr_t functions[20]={};
};
bool Validate(Binding*) noexcept;
Result Capture(const Binding&,const Identity&,Snapshot*) noexcept;
bool CanRestore(const Snapshot&,const Snapshot&) noexcept;
Result Restore(const Binding&,const Snapshot&,const Identity* current=nullptr) noexcept;
// Current settings own the first three DSP connection gains. Group volumes
// remain authored bus state; both layers are checked independently.
bool Same(const Snapshot&,const Snapshot&,const float user_volume[3]) noexcept;
} // namespace
#endif
