#include "NativeResourceInputCapture.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::resource_input_capture {
namespace {
void Lock(State* s)noexcept{while(s->lock.test_and_set(std::memory_order_acquire)){} }
void Unlock(State* s)noexcept{s->lock.clear(std::memory_order_release);}
bool Valid(HANDLE handle)noexcept{return handle&&handle!=INVALID_HANDLE_VALUE;}
void FinishCurrent(State* s,bool success)noexcept{
    const auto slot=s->current;Entry* entry=&s->entries[slot];
    const auto input=entry->input;
    unsigned char sha256[32]={};std::memcpy(sha256,s->job.sha256,32);const auto bytes=s->job.bytes;
    // Release every actual OS handle before removing the gateway's input debt.
    const bool closed=resource_input::Close(&entry->pin,&s->job);
    if(success&&closed)(void)resource_gateway::CompleteInput(s->gateway,input,sha256,bytes);
    else resource_gateway::FailInput(s->gateway,input);
    Lock(s);
    if(closed){*entry={};s->current=resource_gateway::kPendingInputs;s->job={};}
    else{s->failed.store(true);s->accepting.store(false);s->stopping.store(true);}
    Unlock(s);
}
bool Step(State* s)noexcept{
    if(s->failed.load())return false;
    if(s->current==resource_gateway::kPendingInputs){
        Lock(s);
        for(std::size_t i=0;i<resource_gateway::kPendingInputs;++i)
            if(s->entries[i].occupied&&!s->entries[i].reading){s->current=i;s->entries[i].reading=true;break;}
        Unlock(s);
        if(s->current==resource_gateway::kPendingInputs)return false;
        if(s->stopping.load()||!resource_input::Begin(s->entries[s->current].pin,s->maximum_file_bytes,&s->job)){
            FinishCurrent(s,false);return true;
        }
    }
    if(s->stopping.load()){FinishCurrent(s,false);return true;}
    const auto result=resource_input::Step(s->entries[s->current].pin,&s->job);
    if(result==resource_input::State::Complete)FinishCurrent(s,true);
    else if(result!=resource_input::State::Reading)FinishCurrent(s,false);
    return true;
}
DWORD WINAPI Worker(LPVOID user){
    auto* s=static_cast<State*>(user);
    for(;;){
        if(Step(s))continue;
        if(s->stopping.load())return 0;
        (void)WaitForSingleObject(s->wake,100);
    }
}
}
bool Start(State* s,resource_gateway::State* gateway,file_lifetime::bridge::State* files,std::uint64_t maximum)noexcept{
    if(!s||!gateway||!files||!maximum||s->thread||s->wake||s->gateway)return false;
    s->gateway=gateway;s->files=files;s->maximum_file_bytes=maximum;
    s->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!Valid(s->wake)){s->wake=nullptr;s->gateway=nullptr;s->files=nullptr;s->maximum_file_bytes=0;return false;}
    s->thread=CreateThread(nullptr,0,&Worker,s,0,nullptr);
    if(!Valid(s->thread)){
        s->thread=nullptr;
        if(CloseHandle(s->wake)){s->wake=nullptr;s->gateway=nullptr;s->files=nullptr;s->maximum_file_bytes=0;}
        else{s->failed.store(true);s->stopping.store(true);}
        return false;
    }
    // Publish admission only after the worker and its wake event both exist.
    s->accepting.store(true);
    return true;
}
bool Queue(State* s,resource_gateway::InputKey input,const file_lifetime::FileRecord& file)noexcept{
    if(!s||!s->accepting.load()||!input.generation||input.slot>=resource_gateway::kPendingInputs)return false;
    Lock(s);
    if(!s->accepting.load()){Unlock(s);return false;}
    std::size_t slot=0;while(slot<resource_gateway::kPendingInputs&&s->entries[slot].occupied)++slot;
    if(slot==resource_gateway::kPendingInputs){Unlock(s);return false;}
    auto& entry=s->entries[slot];entry={};entry.input=input;entry.occupied=true;
    const bool pinned=resource_input::Capture(s->files,file,input.generation,&entry.pin);
    if(!pinned){
        if(resource_input::ReleasePin(&entry.pin))entry={};
        else{entry.reading=true;s->failed.store(true);s->accepting.store(false);s->stopping.store(true);}
        Unlock(s);SetEvent(s->wake);return false;
    }
    Unlock(s);SetEvent(s->wake);return true;
}
bool Stop(State* s,std::uint32_t timeout)noexcept{
    if(!s)return false;s->accepting.store(false);s->stopping.store(true);
    if(Valid(s->wake))SetEvent(s->wake);
    if(Valid(s->thread)&&WaitForSingleObject(s->thread,timeout)!=WAIT_OBJECT_0)return false;
    bool closed=true;
    // Once the worker has ended, retry actual handles retained by a close
    // failure. Never discard their identities or remove input debt first.
    Lock(s);
    for(std::size_t i=0;i<resource_gateway::kPendingInputs;++i){
        auto& entry=s->entries[i];if(!entry.occupied)continue;
        resource_gateway::FailInput(s->gateway,entry.input);
        const bool released=i==s->current?resource_input::Close(&entry.pin,&s->job):resource_input::ReleasePin(&entry.pin);
        if(released){entry={};if(i==s->current){s->current=resource_gateway::kPendingInputs;s->job={};}}
        else closed=false;
    }
    Unlock(s);
    if(Valid(s->thread)){if(CloseHandle(s->thread))s->thread=nullptr;else closed=false;}
    if(Valid(s->wake)){if(CloseHandle(s->wake))s->wake=nullptr;else closed=false;}
    return closed&&Outstanding(s)==0;
}
std::size_t Outstanding(State* s)noexcept{
    if(!s)return resource_gateway::kPendingInputs;Lock(s);std::size_t count=0;
    for(const auto& entry:s->entries)if(entry.occupied)++count;Unlock(s);return count;
}
} // namespace
