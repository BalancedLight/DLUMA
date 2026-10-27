#ifndef DLUMA_AUDIO_BUS_NATIVE_H
#define DLUMA_AUDIO_BUS_NATIVE_H
#include "AudioDspNative.h"
namespace dluma::fixes::custom_map_asset_isolation::audio_bus_native {
enum class Result : std::uint8_t { Succeeded, InvalidArgument, Unauthorized, ReadFailed, InvalidGraph, Changed, WriteFailed };
struct Fence { std::uintptr_t owner=0;std::uint64_t generation=0;std::uint32_t thread=0;bool worker_parked=false,sound_owned=false,definitions_owned=false,producers_closed=false; };
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
using WriteFn=bool(*)(void*,std::uintptr_t,const void*,std::size_t) noexcept;
using FenceFn=bool(*)(void*,Fence*,bool) noexcept;
struct Access { std::uintptr_t engine=0;ReadFn read=nullptr;WriteFn write=nullptr;FenceFn fence=nullptr;void* user=nullptr;bool exact=false; };
struct Vector { std::uintptr_t data=0;std::uint32_t count=0,capacity=0; };
struct Bus {
    // Sparse normalized representation. Only the evidenced field spans are
    // filled or written; native padding and unused effects are never copied.
    unsigned char fields[740]={};
    unsigned char effects[7][56]={};
    std::uint32_t effect_count=0;
};
struct Environment { Vector vector={};unsigned char entries[16][48]={}; };
struct ListenerBuffer { std::uintptr_t address=0;std::uint32_t count=0;unsigned char rows[10][240]={}; };
struct Listener { ListenerBuffer role[3]={}; };
struct Parameter { std::uintptr_t address=0;std::uint32_t type=0,value=0;char name[96]={}; };
struct Opcode {std::uint32_t table=0,execute=0,apply=0;};
struct Snapshot {
    Fence fence={};audio_dsp_native::Identity backend={};
    Bus buses[14]={};Environment environment[2]={};Listener listener[2]={};
    unsigned char reverbs[60][44]={},transition[2][44]={},ambient[28]={},current_reverb[44]={},reverb_input[40]={};
    unsigned char environment_cache[176]={};
    Parameter parameters[128]={};std::uint32_t parameter_count=0;
    unsigned char globals[15][20]={};
    Opcode on_start[4096]={};std::uint32_t on_start_count=0;
    bool complete=false;
};
Result Capture(const Access&,Snapshot*) noexcept;
bool CanRestore(const Snapshot& baseline,const Snapshot& current) noexcept;
bool ReplayProgramSupported(const Snapshot&) noexcept;
bool ReplayProgramUnchanged(const Access&,const Snapshot&) noexcept;
Result Restore(const Access&,const Snapshot&,Snapshot* scratch) noexcept;
bool Same(const Snapshot&,const Snapshot&,const float current_user_maximum[3]) noexcept;
} // namespace
#endif
