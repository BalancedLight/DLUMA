#include "fixes/custom_map_asset_isolation/LocalizedTextObservationGate.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64();
extern "C" __declspec(dllimport) void __stdcall RaiseException(unsigned long,unsigned long,unsigned long,const std::uintptr_t*);
namespace text=dluma::fixes::custom_map_asset_isolation::localized_texts;
using text::TextHook;using text::TextGateResult;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)

namespace {
struct Image {
    static constexpr std::uintptr_t base=0x10000000;
    std::uint8_t entries[text::kTextHookCount][16]={};
    std::uintptr_t startup_callback=0;
    std::uintptr_t owner=0;
    Image(){
        std::size_t count=0;auto* c=text::TextHookCatalog(&count);
        for(std::size_t i=0;i<count;++i) {
            if(c[i].pointer_slot_rva){auto p=base+c[i].function_rva;std::memcpy(entries[i],&p,8);}
            else std::memcpy(entries[i],c[i].original_prefix,16);
        }
    }
    void install(){
        std::size_t count=0;auto* c=text::TextHookCatalog(&count);
        for(std::size_t i=0;i<count;++i) {
            auto p=text::TextHookWrapper(c[i].kind);std::memset(entries[i],0,16);
            if(c[i].pointer_slot_rva)std::memcpy(entries[i],&p,8);
            else {entries[i][0]=0xff;entries[i][1]=0x25;std::memcpy(entries[i]+6,&p,8);}
        }
    }
};
bool Read(void* user,std::uintptr_t at,void* out,std::size_t n) noexcept {
    auto* image=static_cast<Image*>(user);std::size_t count=0;auto* c=text::TextHookCatalog(&count);
    if(at==image->base+0xACCC58 && n==8){std::memcpy(out,&image->startup_callback,8);return true;}
    if((at==image->base+0xACA818 || at==image->base+0xACCC68 || at==image->base+0xACCC70) && n==8){std::memcpy(out,&image->owner,8);return true;}
    if(at==image->base+0x24B9A9 && n==2){const std::uint8_t call[]={0xff,0xd7};std::memcpy(out,call,2);return true;}
    for(std::size_t i=0;i<count;++i)if(at==image->base+(c[i].pointer_slot_rva?c[i].pointer_slot_rva:c[i].function_rva) && n<=16) {
        std::memcpy(out,image->entries[i],n);return true;
    }
    return false;
}
std::atomic<unsigned> clear_calls{0},insert_calls{0},wrapped_calls{0};
std::atomic<bool> fault_clear{false};
text::LocalizedTextObservationGate* current=nullptr;
std::uintptr_t __fastcall Noop(){return 0x1234;}
std::uintptr_t __fastcall ClearFake(){++clear_calls;if(fault_clear.load())RaiseException(0xe0424242,0,0,nullptr);return 0x1234;}
void __fastcall InsertFake(const char* key,const std::uint16_t* value,unsigned char replace) {
    if(std::strcmp(key,"fixture_key") || value[0]!=42 || replace)std::abort();
    text::TextCallToken child={};text::BeginTextCall(current,TextHook::EntryLookup,&child);
    text::EndTextCall(current,&child);++insert_calls;
}
std::uintptr_t __fastcall ValueFake(void*,const char*) {return 0x4321;}
std::uintptr_t __fastcall SubstituteFake(void* table,void*,const std::uint16_t*,int depth) {
    auto raw=reinterpret_cast<std::uintptr_t(__fastcall*)(void*,const char*)>(text::TextHookWrapper(TextHook::ValueLookup));
    if(raw(table,"fixture_key")!=0x4321)std::abort();
    if(depth){auto nested=reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,const std::uint16_t*,int)>(text::TextHookWrapper(TextHook::Substitute));nested(table,nullptr,nullptr,depth-1);}
    ++wrapped_calls;return 0x8765;
}
struct Fixture {
    Image image;text::LocalizedTextObservationGate gate;
    std::uintptr_t originals[text::kTextHookCount]={};
    text::ObservedTextStartup startup={};
    Fixture(){for(auto& o:originals)o=reinterpret_cast<std::uintptr_t>(&Noop);
        originals[static_cast<unsigned>(TextHook::Clear)]=reinterpret_cast<std::uintptr_t>(&ClearFake);
        originals[static_cast<unsigned>(TextHook::Insert)]=reinterpret_cast<std::uintptr_t>(&InsertFake);
        originals[static_cast<unsigned>(TextHook::Substitute)]=reinterpret_cast<std::uintptr_t>(&SubstituteFake);
        originals[static_cast<unsigned>(TextHook::ValueLookup)]=reinterpret_cast<std::uintptr_t>(&ValueFake);
        current=&gate;
        image.startup_callback=reinterpret_cast<std::uintptr_t>(&Noop);
        startup={image.base+0x24B9AB,image.startup_callback,GetCurrentThreadId()};
    }
    bool start(){
        CHECK(text::InitializeTextGate(&gate,image.base,text::kPlayerEngineSha256,Read,&image,originals,text::kTextHookCount,&startup)==TextGateResult::Succeeded);
        CHECK(text::BindTextHookBridge(&gate)==TextGateResult::Succeeded);image.install();
        CHECK(text::VerifyTextHookCoverage(&gate)==TextGateResult::Succeeded);
        CHECK(text::SealTextHookStartup(&gate)==TextGateResult::Succeeded);return true;
    }
};
bool Wait(std::atomic<bool>& flag,unsigned milliseconds=1000){
    auto start=GetTickCount64();while(!flag.load()){if(GetTickCount64()-start>milliseconds)return false;Sleep(1);}return true;
}
bool NestedBorrowCountsWholeCopy(){
    Fixture f;CHECK(f.start());auto get=reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,const std::uint16_t*,int)>(text::TextHookWrapper(TextHook::Substitute));
    CHECK(get(nullptr,nullptr,nullptr,2)==0x8765);CHECK(wrapped_calls==3);
    auto s=text::InspectTextGate(&f.gate);CHECK(!s.unknown_borrow && !s.active_calls && s.revision==1);
    text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::Succeeded);return true;
}
bool UnexpectedBorrow(TextHook kind){
    Fixture f;CHECK(f.start());text::TextCallToken t={};text::BeginTextCall(&f.gate,kind,&t);text::EndTextCall(&f.gate,&t);
    CHECK(text::InspectTextGate(&f.gate).unknown_borrow);text::TextGateLease lease={};
    CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::UnsafeBorrow);
    CHECK(text::InitializeTextGate(&f.gate,f.image.base,text::kPlayerEngineSha256,Read,&f.image,f.originals,text::kTextHookCount,&f.startup)==TextGateResult::AlreadyInitialized);return true;
}
bool LoadBeforeFirstInsertDrains(){
    Fixture f;CHECK(f.start());std::atomic<bool> started{false},finish{false};
    std::thread worker([&]{text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::FromFile,&t);started=true;
        while(!finish.load())Sleep(1);
        text::TextCallToken i={};text::BeginTextCall(&f.gate,TextHook::Insert,&i);text::EndTextCall(&f.gate,&i);text::EndTextCall(&f.gate,&t);});
    bool ok=Wait(started);text::TextGateLease lease={};auto result=text::CloseTextGate(&f.gate,5,&lease);
    finish=true;worker.join();CHECK(ok);CHECK(result==TextGateResult::TimedOut);
    CHECK(!text::InspectTextGate(&f.gate).admission_closed);CHECK(text::InspectTextGate(&f.gate).revision==5);
    CHECK(text::CloseTextGate(&f.gate,100,&lease)==TextGateResult::Succeeded);CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::Succeeded);return true;
}
bool NestedCallFinishesWhileClosing(){
    Fixture f;CHECK(f.start());std::atomic<bool> started{false},nested{false};
    std::thread worker([&]{text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::Substitute,&t);started=true;
        auto start=GetTickCount64();while(!text::InspectTextGate(&f.gate).admission_closed && GetTickCount64()-start<1000)Sleep(1);
        text::TextCallToken c={};text::BeginTextCall(&f.gate,TextHook::ValueLookup,&c);nested=true;text::EndTextCall(&f.gate,&c);text::EndTextCall(&f.gate,&t);});
    bool ok=Wait(started);text::TextGateLease lease={};auto result=text::CloseTextGate(&f.gate,1000,&lease);worker.join();
    CHECK(ok && nested.load());CHECK(result==TextGateResult::Succeeded);CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::Succeeded);return true;
}
bool NewCallsWaitWithoutFakeReturn(){
    Fixture f;CHECK(f.start());text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    std::atomic<bool> started{false},entered{false};
    std::thread worker([&]{started=true;text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::Exists,&t);entered=true;text::EndTextCall(&f.gate,&t);});
    bool ok=Wait(started);Sleep(10);bool was_waiting=!entered.load();auto result=text::OpenTextGate(&f.gate,&lease);worker.join();
    CHECK(ok && was_waiting && entered.load());CHECK(result==TextGateResult::Succeeded);return true;
}
bool CoverageIsActual(){
    Fixture f;CHECK(f.start());f.image.entries[static_cast<unsigned>(TextHook::SaveBinary)][6]^=1;
    text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::IncompleteCoverage);
    CHECK(!text::InspectTextGate(&f.gate).admission_closed);return true;
}
bool ReusedTokenPoisons(){
    Fixture f;CHECK(f.start());text::TextCallToken a={};text::BeginTextCall(&f.gate,TextHook::Exists,&a);auto copied=a;text::EndTextCall(&f.gate,&a);
    text::TextCallToken b={};text::BeginTextCall(&f.gate,TextHook::Exists,&b);text::EndTextCall(&f.gate,&copied);
    CHECK(text::InspectTextGate(&f.gate).active_calls==1);text::EndTextCall(&f.gate,&b);
    text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::LostObservation);return true;
}
bool Context(void*,text::TextFence* out) noexcept {*out={7,999,0,"En",true,false,true,true,true};return true;}
text::TextReplayAuthority authority={12,text::TextReplayOperation::Clear};
bool ReplayAuthority(void*,text::TextReplayAuthority* out) noexcept {*out=authority;return true;}
bool ObservedFenceAndReplay(){
    Fixture f;CHECK(f.start());text::ObservedTextAccess bridge={&f.gate,Context,nullptr,ReplayAuthority};text::NativeTextAccess access={};
    CHECK(text::MakeObservedTextAccess(&bridge,&access));text::TextFence fence={};CHECK(access.fence(access.user,&fence));
    CHECK(!fence.producer_admission_closed && fence.mutation_revision==1);std::uint16_t value[]={42,0};
    CHECK(!access.clear(access.user,f.image.base+text::kClearTextsRva));CHECK(clear_calls==0);
    text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    CHECK(text::BindObservedTextReplay(&bridge,&lease,12));
    CHECK(access.fence(access.user,&fence) && fence.producer_admission_closed && fence.readers_drained);
    CHECK(access.clear(access.user,f.image.base+text::kClearTextsRva));
    CHECK(!access.insert(access.user,f.image.base+text::kInsertTextRva,"fixture_key",value));
    authority.operation=text::TextReplayOperation::Insert;
    CHECK(access.insert(access.user,f.image.base+text::kInsertTextRva,"fixture_key",value));
    CHECK(clear_calls==1 && insert_calls==1);CHECK(access.fence(access.user,&fence));CHECK(fence.mutation_revision==1 && fence.readers_drained);
    CHECK(!access.clear(access.user,f.image.base+text::kClearTextsRva+1));CHECK(clear_calls==1);
    CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::Succeeded);return true;
}
bool OwnerUnexpectedCallDoesNotDeadlock(){
    Fixture f;CHECK(f.start());text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::Clear,&t);text::EndTextCall(&f.gate,&t);
    CHECK(text::InspectTextGate(&f.gate).lost_observation);CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::LostObservation);
    text::TextGateLease next={};CHECK(text::CloseTextGate(&f.gate,0,&next)==TextGateResult::LostObservation);return true;
}
bool WrongThreadCannotOpen(){
    Fixture f;CHECK(f.start());text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    TextGateResult r=TextGateResult::Succeeded;std::thread worker([&]{r=text::OpenTextGate(&f.gate,&lease);});worker.join();
    CHECK(r==TextGateResult::WrongThread);CHECK(text::InspectTextGate(&f.gate).admission_closed);
    CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::Succeeded);text::TextGateLease next={};CHECK(text::CloseTextGate(&f.gate,0,&next)==TextGateResult::Succeeded);
    CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::WrongLease);CHECK(text::OpenTextGate(&f.gate,&next)==TextGateResult::Succeeded);return true;
}
bool OriginalPrefixMustMatch(){
    Fixture f;f.image.entries[1][0]^=1;
    CHECK(text::InitializeTextGate(&f.gate,f.image.base,text::kPlayerEngineSha256,Read,&f.image,f.originals,text::kTextHookCount,&f.startup)==TextGateResult::ProfileMismatch);return true;
}
bool FaultUnwindsTheNativeScope(){
    Fixture f;CHECK(f.start());text::ObservedTextAccess bridge={&f.gate,Context,nullptr,ReplayAuthority};text::NativeTextAccess access={};
    CHECK(text::MakeObservedTextAccess(&bridge,&access));text::TextGateLease lease={};
    CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);CHECK(text::BindObservedTextReplay(&bridge,&lease,12));fault_clear=true;
    CHECK(!access.clear(access.user,f.image.base+text::kClearTextsRva));
    CHECK(text::InspectTextGate(&f.gate).active_calls==0 && text::InspectTextGate(&f.gate).lost_observation);
    CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::LostObservation);return true;
}
bool LateStartupIsRejected(){
    Fixture f;f.image.owner=0x1234;
    CHECK(text::InitializeTextGate(&f.gate,f.image.base,text::kPlayerEngineSha256,Read,&f.image,f.originals,text::kTextHookCount,&f.startup)==TextGateResult::StartupUnproven);return true;
}
bool WrongReturnIsRejected(){
    Fixture f;f.startup.callback_return_address++;
    CHECK(text::InitializeTextGate(&f.gate,f.image.base,text::kPlayerEngineSha256,Read,&f.image,f.originals,text::kTextHookCount,&f.startup)==TextGateResult::StartupUnproven);return true;
}
bool UnsealedAndLateSealRejected(){
    Fixture f;CHECK(text::InitializeTextGate(&f.gate,f.image.base,text::kPlayerEngineSha256,Read,&f.image,f.originals,text::kTextHookCount,&f.startup)==TextGateResult::Succeeded);
    CHECK(text::BindTextHookBridge(&f.gate)==TextGateResult::Succeeded);f.image.install();
    text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::StartupUnproven);
    f.image.owner=0x1234;CHECK(text::SealTextHookStartup(&f.gate)==TextGateResult::StartupUnproven);return true;
}
bool WrongReplayGenerationIsRejected(){
    Fixture f;CHECK(f.start());text::ObservedTextAccess bridge={&f.gate,Context,nullptr,ReplayAuthority};text::NativeTextAccess access={};
    CHECK(text::MakeObservedTextAccess(&bridge,&access));text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    CHECK(text::BindObservedTextReplay(&bridge,&lease,13));CHECK(!access.clear(access.user,f.image.base+text::kClearTextsRva));CHECK(clear_calls==0);
    authority.transaction_generation=13;CHECK(access.clear(access.user,f.image.base+text::kClearTextsRva));CHECK(clear_calls==1);
    CHECK(text::OpenTextGate(&f.gate,&lease)==TextGateResult::Succeeded);text::TextGateLease next={};CHECK(text::CloseTextGate(&f.gate,0,&next)==TextGateResult::Succeeded);
    CHECK(!access.clear(access.user,f.image.base+text::kClearTextsRva));CHECK(clear_calls==1);
    CHECK(text::OpenTextGate(&f.gate,&next)==TextGateResult::Succeeded);return true;
}
bool LateWriterCannotProduceAFalseSuccess(){
    Fixture f;CHECK(f.start());text::TextGateLease lease={};CHECK(text::CloseTextGate(&f.gate,0,&lease)==TextGateResult::Succeeded);
    std::atomic<bool> started{false},entered{false};
    std::thread worker([&]{started=true;text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::Insert,&t);entered=true;text::EndTextCall(&f.gate,&t);});
    bool ok=Wait(started);auto start=GetTickCount64();while(!text::InspectTextGate(&f.gate).late_producer && GetTickCount64()-start<1000)Sleep(1);
    bool poisoned=text::InspectTextGate(&f.gate).late_producer;bool was_waiting=!entered.load();
    auto result=text::OpenTextGate(&f.gate,&lease);worker.join();CHECK(ok && poisoned && was_waiting && entered.load());
    CHECK(result==TextGateResult::LateProducer);text::TextGateLease next={};CHECK(text::CloseTextGate(&f.gate,0,&next)==TextGateResult::LateProducer);return true;
}
bool CommitWatchLeavesReadersOpen(){
    Fixture f;CHECK(f.start());text::TextGateLease first={};CHECK(text::CloseTextGate(&f.gate,0,&first)==TextGateResult::Succeeded);
    CHECK(text::BeginTextCommitWatch(&f.gate,&first,12)==TextGateResult::Succeeded);
    CHECK(text::OpenTextGate(&f.gate,&first)==TextGateResult::Succeeded);
    std::atomic<bool> read_finished{false};std::thread reader([&]{text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::Exists,&t);text::EndTextCall(&f.gate,&t);read_finished=true;});
    bool ok=Wait(read_finished);reader.join();CHECK(ok);CHECK(!text::InspectTextGate(&f.gate).late_producer);
    text::TextGateLease final={};CHECK(text::CloseTextGate(&f.gate,0,&final)==TextGateResult::Succeeded);
    CHECK(text::CompleteTextCommitWatch(&f.gate,&final,13)==TextGateResult::WrongLease);
    CHECK(text::CompleteTextCommitWatch(&f.gate,&final,12)==TextGateResult::Succeeded);
    CHECK(text::OpenTextGate(&f.gate,&final)==TextGateResult::Succeeded);
    text::TextCallToken later={};text::BeginTextCall(&f.gate,TextHook::Insert,&later);text::EndTextCall(&f.gate,&later);
    CHECK(!text::InspectTextGate(&f.gate).late_producer);return true;
}
bool CommitWatchDoesNotDeadlockProducer(){
    Fixture f;CHECK(f.start());text::TextGateLease first={};CHECK(text::CloseTextGate(&f.gate,0,&first)==TextGateResult::Succeeded);
    CHECK(text::BeginTextCommitWatch(&f.gate,&first,12)==TextGateResult::Succeeded);
    CHECK(text::OpenTextGate(&f.gate,&first)==TextGateResult::Succeeded);
    std::atomic<bool> finished{false};std::thread writer([&]{text::TextCallToken t={};text::BeginTextCall(&f.gate,TextHook::Insert,&t);text::EndTextCall(&f.gate,&t);finished=true;});
    bool ok=Wait(finished);writer.join();CHECK(ok);CHECK(text::InspectTextGate(&f.gate).late_producer);
    text::TextGateLease final={};CHECK(text::CloseTextGate(&f.gate,0,&final)==TextGateResult::LateProducer);return true;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;int test=std::atoi(argv[1]);bool ok=false;
    switch(test){
    case 0:ok=NestedBorrowCountsWholeCopy();break;
    case 1:ok=UnexpectedBorrow(TextHook::ValueLookup);break;
    case 2:ok=UnexpectedBorrow(TextHook::TableAddress);break;
    case 3:ok=UnexpectedBorrow(TextHook::EntryLookup);break;
    case 4:ok=LoadBeforeFirstInsertDrains();break;
    case 5:ok=NestedCallFinishesWhileClosing();break;
    case 6:ok=NewCallsWaitWithoutFakeReturn();break;
    case 7:ok=CoverageIsActual();break;
    case 8:ok=ReusedTokenPoisons();break;
    case 9:ok=ObservedFenceAndReplay();break;
    case 10:ok=OwnerUnexpectedCallDoesNotDeadlock();break;
    case 11:ok=WrongThreadCannotOpen();break;
    case 12:ok=OriginalPrefixMustMatch();break;
    case 13:ok=FaultUnwindsTheNativeScope();break;
    case 14:ok=LateStartupIsRejected();break;
    case 15:ok=WrongReturnIsRejected();break;
    case 16:ok=UnsealedAndLateSealRejected();break;
    case 17:ok=WrongReplayGenerationIsRejected();break;
    case 18:ok=LateWriterCannotProduceAFalseSuccess();break;
    case 19:ok=CommitWatchLeavesReadersOpen();break;
    case 20:ok=CommitWatchDoesNotDeadlockProducer();break;
    default:return 2;}
    if(ok)std::printf("localized-text observation scenario %d passed\n",test);return ok?0:1;
}
