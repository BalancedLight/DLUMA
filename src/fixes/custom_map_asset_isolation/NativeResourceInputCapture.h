#ifndef DLUMA_NATIVE_RESOURCE_INPUT_CAPTURE_H
#define DLUMA_NATIVE_RESOURCE_INPUT_CAPTURE_H
#include "NativeResourceInputLease.h"
#include "NativeResourceGateway.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::resource_input_capture {
struct Entry {
    resource_gateway::InputKey input;
    resource_input::Pin pin;
    bool occupied,reading;
};
struct State {
    resource_gateway::State* gateway=nullptr;
    file_lifetime::bridge::State* files=nullptr;
    Entry entries[resource_gateway::kPendingInputs]={};
    resource_input::Job job={};
    std::size_t current=resource_gateway::kPendingInputs;
    std::uint64_t maximum_file_bytes=0;
    HANDLE wake=nullptr,thread=nullptr;
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
    std::atomic<bool> accepting{false},stopping{false},failed{false};
};
// The worker does only OS file IO and bounded POD/digest publication. It never
// calls the engine, a mod callback or a provider/path resolver.
bool Start(State*,resource_gateway::State*,file_lifetime::bridge::State*,std::uint64_t maximum_file_bytes)noexcept;
bool Queue(State*,resource_gateway::InputKey,const file_lifetime::FileRecord&)noexcept;
// Bounded join. A false result retains the state/thread; its caller must keep
// the module alive and cannot claim that input leases have been closed.
bool Stop(State*,std::uint32_t timeout_ms)noexcept;
std::size_t Outstanding(State*)noexcept;
} // namespace
#endif
