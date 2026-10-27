#include "fixes/custom_map_asset_isolation/NativeXuiContentOwner.h"
#include <cstdio>
#include <cstring>
namespace x=dluma::fixes::custom_map_asset_isolation::xui_content;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d %s\n",__LINE__,#c);return false;}}while(0)
namespace {
struct Fixture {
    x::State* state=new x::State; x::Key key={0x1000,1};x::Input input={};
    Fixture(){x::SealStartup(state);input.file={0x2000,{0x3000,1},1,1,f::FileKind::Zip,f::Life::Live};
        input.selected_source=input.file.source;std::memcpy(input.path,"ui/menu.xui",12);
        input.sha256[0]=1;input.bytes=128;input.realm=x::Realm::Global;input.complete=true;}
    ~Fixture(){delete state;}
    bool Baseline(){x::Load load={};bool dirty=false;
        return x::Begin(state,key,0,x::Route::Xml,true,&load)&&x::Observe(state,&load,input)&&
            x::End(state,&load,false,&dirty)&&!dirty&&x::CaptureBaseline(state,1);}
    bool Load(const x::Input& value,bool expected,x::Key owner={0x1000,1}){
        x::Load load={};bool dirty=false;return x::Begin(state,owner,7,x::Route::Xml,false,&load)&&
            x::Observe(state,&load,value)&&x::End(state,&load,false,&dirty)&&dirty==expected;}
};
bool OriginalContent(){Fixture t;CHECK(t.Baseline());auto input=t.input;
    input.file.file=0x4000;input.file.generation=9;input.file.root=12;
    CHECK(t.Load(input,false));CHECK(x::VerifyEndedOwners(t.state,1,7));return true;}
bool ProjectSource(){Fixture t;CHECK(t.Baseline());auto input=t.input;
    input.realm=x::Realm::Project;input.source_epoch=7;CHECK(t.Load(input,true));
    CHECK(!x::VerifyEndedOwners(t.state,1,7));CHECK(x::OwnerDestroyed(t.state,t.key));
    CHECK(x::VerifyEndedOwners(t.state,1,7));return true;}
bool ChangedContent(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.sha256[0]=2;
    CHECK(t.Load(input,true));CHECK(!x::VerifyEndedOwners(t.state,1,7));return true;}
bool SourceGeneration(){Fixture t;CHECK(t.Baseline());auto input=t.input;
    ++input.file.source.generation;++input.selected_source.generation;CHECK(t.Load(input,true));return true;}
bool DifferentSource(){Fixture t;CHECK(t.Baseline());auto input=t.input;
    input.file.source.address=0x5000;input.selected_source=input.file.source;CHECK(t.Load(input,true));return true;}
bool DifferentPath(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.path[3]='M';CHECK(t.Load(input,true));return true;}
bool DifferentSize(){Fixture t;CHECK(t.Baseline());auto input=t.input;++input.bytes;CHECK(t.Load(input,true));return true;}
bool IncompleteDigest(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.complete=false;CHECK(t.Load(input,true));return true;}
bool UnknownSource(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.realm=x::Realm::Unknown;CHECK(t.Load(input,true));return true;}
bool JoinedSourceMismatch(){Fixture t;CHECK(t.Baseline());auto input=t.input;
    ++input.selected_source.generation;CHECK(t.Load(input,true));return true;}
bool ReusedFileGeneration(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.file.generation=0;
    CHECK(t.Load(input,true));return true;}
bool PartialStartup(){Fixture t;t.input.complete=false;CHECK(t.Baseline());
    CHECK(t.state->input_count==0);t.input.complete=true;CHECK(t.Load(t.input,true));return true;}
bool FrozenBaseline(){Fixture t;CHECK(t.Baseline());CHECK(x::CaptureBaseline(t.state,1));
    CHECK(!x::CaptureBaseline(t.state,2));auto changed=t.input;changed.sha256[1]=1;
    CHECK(t.Load(changed,true));CHECK(t.state->input_count==1);return true;}
bool MissingOpen(){Fixture t;CHECK(t.Baseline());x::Load load={};bool dirty=false;
    CHECK(x::Begin(t.state,t.key,7,x::Route::BinaryPack,false,&load));CHECK(x::End(t.state,&load,false,&dirty));
    CHECK(dirty);CHECK(!x::VerifyEndedOwners(t.state,1,7));return true;}
bool NativeNoop(){Fixture t;CHECK(t.Baseline());x::Load load={};bool dirty=true;
    CHECK(x::Begin(t.state,t.key,7,x::Route::BinaryFile,false,&load));CHECK(x::End(t.state,&load,true,&dirty));
    CHECK(!dirty);CHECK(x::VerifyEndedOwners(t.state,1,7));return true;}
bool WrongManagerGeneration(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.complete=false;
    CHECK(t.Load(input,true));CHECK(x::OwnerDestroyed(t.state,{t.key.manager,2}));
    CHECK(!x::VerifyEndedOwners(t.state,1,7));CHECK(x::OwnerDestroyed(t.state,t.key));
    CHECK(t.Load(input,true,{t.key.manager,2}));CHECK(!x::VerifyEndedOwners(t.state,1,7));return true;}
bool OtherGlobalOwnerSurvives(){Fixture t;CHECK(t.Baseline());CHECK(t.Load(t.input,false,{0x5000,2}));
    auto input=t.input;input.complete=false;CHECK(t.Load(input,true));CHECK(x::OwnerDestroyed(t.state,t.key));
    CHECK(x::VerifyEndedOwners(t.state,1,7));return true;}
bool ActiveLoadBlocks(){Fixture t;CHECK(t.Baseline());x::Load load={};bool dirty=false;
    CHECK(x::Begin(t.state,t.key,7,x::Route::Xml,false,&load));CHECK(!x::VerifyEndedOwners(t.state,1,7));
    CHECK(!x::CaptureBaseline(t.state,1));CHECK(x::End(t.state,&load,true,&dirty));return true;}
bool CopiedTokenRejected(){Fixture t;CHECK(t.Baseline());x::Load load={};bool dirty=false;
    CHECK(x::Begin(t.state,t.key,7,x::Route::Xml,false,&load));auto copy=load;
    CHECK(!x::End(t.state,&copy,true,&dirty));CHECK(t.state->lost.load());CHECK(!x::VerifyEndedOwners(t.state,1,7));return true;}
bool EndedSlotsReused(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.complete=false;
    for(std::uint64_t n=1;n<1024;++n){x::Key key={0x5000,n};CHECK(t.Load(input,true,key));
        CHECK(x::OwnerDestroyed(t.state,key));CHECK(x::VerifyEndedOwners(t.state,1,7));}
    CHECK(t.state->owner_count<=2);return true;}
bool WrongBaseline(){Fixture t;CHECK(t.Baseline());CHECK(!x::VerifyEndedOwners(t.state,2,7));
    CHECK(!x::VerifyEndedOwners(t.state,1,0));return true;}
bool WrongEpochCannotHideLiveOwner(){Fixture t;CHECK(t.Baseline());auto input=t.input;input.complete=false;
    CHECK(t.Load(input,true));CHECK(!x::VerifyEndedOwners(t.state,1,8));return true;}
bool ReturnedMenuCannotBecomeBaseline(){Fixture t;auto input=t.input;input.complete=false;
    CHECK(t.Load(input,true));CHECK(x::OwnerDestroyed(t.state,t.key));CHECK(!x::CaptureBaseline(t.state,1));return true;}
struct Pair{unsigned attempts=0,releases=0;bool close=false;};
bool Close(void*,void* p)noexcept{auto* pair=static_cast<Pair*>(p);++pair->attempts;return pair->close;}
void Free(void*,void* p)noexcept{++static_cast<Pair*>(p)->releases;}
bool FailedCloseRetained(){x::InputLeasePool pool;Pair pair;x::InputLeaseToken token={};
    CHECK(x::TrackInputLease(&pool,&pair,&token));CHECK(!x::CompleteInputLease(&pool,&token,&Close,&Free,nullptr));
    CHECK(pair.attempts==1&&!pair.releases&&!token.active);CHECK(!x::DrainInputLeases(&pool,&Close,&Free,nullptr));
    CHECK(pair.attempts==2&&!pair.releases);pair.close=true;CHECK(x::DrainInputLeases(&pool,&Close,&Free,nullptr));
    CHECK(pair.attempts==3&&pair.releases==1);CHECK(x::DrainInputLeases(&pool,&Close,&Free,nullptr));
    CHECK(pair.releases==1);return true;}
bool ActiveLeaseNotClosed(){x::InputLeasePool pool;Pair pair;pair.close=true;x::InputLeaseToken token={};
    CHECK(x::TrackInputLease(&pool,&pair,&token));CHECK(!x::DrainInputLeases(&pool,&Close,&Free,nullptr));
    CHECK(!pair.attempts&&!pair.releases);CHECK(x::CompleteInputLease(&pool,&token,&Close,&Free,nullptr));
    CHECK(pair.releases==1);CHECK(!x::CompleteInputLease(&pool,&token,&Close,&Free,nullptr));return true;}
bool LeaseIdentityAndCapacity(){x::InputLeasePool pool;Pair pairs[x::kInputLeases+1];x::InputLeaseToken tokens[x::kInputLeases+1]={};
    for(std::size_t n=0;n<x::kInputLeases;++n)CHECK(x::TrackInputLease(&pool,pairs+n,tokens+n));
    CHECK(!x::TrackInputLease(&pool,pairs+x::kInputLeases,tokens+x::kInputLeases));
    x::InputLeaseToken duplicate={};CHECK(!x::TrackInputLease(&pool,pairs,&duplicate));
    auto stale=tokens[0];++stale.generation;CHECK(!x::CompleteInputLease(&pool,&stale,&Close,&Free,nullptr));
    CHECK(!pairs[0].attempts);return true;}
}
int main(){unsigned passed=0;
    bool(*tests[])()={OriginalContent,ProjectSource,ChangedContent,SourceGeneration,DifferentSource,DifferentPath,
        DifferentSize,IncompleteDigest,UnknownSource,JoinedSourceMismatch,ReusedFileGeneration,PartialStartup,FrozenBaseline,
        MissingOpen,NativeNoop,WrongManagerGeneration,OtherGlobalOwnerSurvives,ActiveLoadBlocks,CopiedTokenRejected,
        EndedSlotsReused,WrongBaseline,WrongEpochCannotHideLiveOwner,ReturnedMenuCannotBecomeBaseline,
        FailedCloseRetained,ActiveLeaseNotClosed,LeaseIdentityAndCapacity};
    for(auto test:tests){if(!test())return 1;++passed;}
    std::printf("native XUI content owner: %u cases passed\n",passed);return 0;}
