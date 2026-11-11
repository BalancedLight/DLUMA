#include "fixes/custom_map_asset_isolation/NativeXuiCompiledSnapshot.h"
#include "fixes/custom_map_asset_isolation/NativeXuiInstanceOwner.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
namespace x=dluma::fixes::custom_map_asset_isolation::xui;
namespace c=dluma::fixes::custom_map_asset_isolation::xui_compiled;
namespace i=dluma::fixes::custom_map_asset_isolation::xui_instance;
namespace xui=x; namespace xui_instance=i;
static bool g_engine_full_profile_valid=true,g_xui_manager_observation_ready=true,g_xui_instance_observation_ready=true,g_hooks_current=true;
static i::State g_xui_instances;static x::ManagerLedger g_xui_manager_ledger;static x::ManagerRecord g_test_manager={};
static bool ProductionXuiHooksCurrent(){return g_hooks_current;}
struct TestLevelInterface {std::uintptr_t vtable=0x5000,native_level=0x4000;};
static TestLevelInterface g_test_level;
[[maybe_unused]] static bool ReadBytes(std::uintptr_t address,void* output,std::size_t bytes){
    if(address!=reinterpret_cast<std::uintptr_t>(&g_test_level.native_level)||bytes!=sizeof(std::uintptr_t))return false;
    std::memcpy(output,&g_test_level.native_level,bytes);return true;
}
namespace dluma::fixes::custom_map_asset_isolation::xui {
bool LookupManager(ManagerLedger*,std::uintptr_t manager,ManagerRecord* out)noexcept {
    if(!out||manager!=g_test_manager.manager)return false;*out=g_test_manager;return true;
}}
#include "fixes/custom_map_asset_isolation/NativeXuiReceiverBridge.inl"
#define CHECK(v) do{if(!(v)){std::printf("FAIL %d %s\n",__LINE__,#v);return false;}}while(0)
namespace {
template<class T>void Put(unsigned char* p,const T& value){std::memcpy(p,&value,sizeof(value));}
struct Header{std::uintptr_t pointer;std::uint32_t count,capacity;};
struct Buffer {c::Table tables[4]={};c::AnimationRange ranges[4]={};c::Record records[8]={};c::Symbol symbols[4]={};char strings[128]={};c::Snapshot snapshot={};
    Buffer(){snapshot.tables=tables;snapshot.table_capacity=4;snapshot.records=records;snapshot.record_capacity=8;
        snapshot.ranges=ranges;snapshot.range_capacity=4;snapshot.symbols=symbols;snapshot.symbol_capacity=4;
        snapshot.strings=strings;snapshot.string_capacity=sizeof(strings);}};
unsigned Hash(const char* name){unsigned char h=1;while(*name)h=static_cast<unsigned char>(static_cast<unsigned char>(*name++)^(33*h));return h;}
struct Fixture {
    alignas(8) unsigned char manager[5320]={},tables[2][40]={},nodes[2][48]={},ranges[2][24]={},range_node[48]={};
    std::uintptr_t table_pointers[2]={},row_pointers[2]={};std::uint32_t rows[2][4]={{1,2,3,4},{5,6,7,8}};
    char names[2][24]={"First","Second"};x::Access access={};x::Fence fence={};unsigned reads=0;bool change=false,deny=false;
    Fixture();void Append();void AddRange();
};
bool Copy(std::uintptr_t address,void* out,std::size_t bytes,const void* storage,std::size_t size){
    const auto start=reinterpret_cast<std::uintptr_t>(storage);if(address<start||bytes>size||address-start>size-bytes)return false;
    std::memcpy(out,reinterpret_cast<const void*>(address),bytes);return true;}
bool Read(void* user,std::uintptr_t address,void* out,std::size_t bytes)noexcept {
    auto& f=*static_cast<Fixture*>(user);
    if(address==reinterpret_cast<std::uintptr_t>(f.rows[0])&&f.change&&++f.reads==3)++f.rows[0][0];
    return Copy(address,out,bytes,f.manager,sizeof(f.manager))||Copy(address,out,bytes,f.tables,sizeof(f.tables))||
        Copy(address,out,bytes,f.nodes,sizeof(f.nodes))||Copy(address,out,bytes,f.table_pointers,sizeof(f.table_pointers))||
        Copy(address,out,bytes,f.row_pointers,sizeof(f.row_pointers))||Copy(address,out,bytes,f.rows,sizeof(f.rows))||
        Copy(address,out,bytes,f.names,sizeof(f.names))||Copy(address,out,bytes,f.ranges,sizeof(f.ranges))||
        Copy(address,out,bytes,f.range_node,sizeof(f.range_node));
}
bool Fence(void* user,x::Fence* out)noexcept{auto& f=*static_cast<Fixture*>(user);*out=f.fence;return !f.deny;}
Fixture::Fixture(){access.engine_base=0x10000000;std::memcpy(access.sha256,x::kPlayerEngineSha256,65);
    access.read=&Read;access.fence=&Fence;access.user=this;fence={reinterpret_cast<std::uintptr_t>(manager),1,2,3,4};
    Put(manager,access.engine_base+0x9173E8);
    for(unsigned n=0;n<2;++n){table_pointers[n]=reinterpret_cast<std::uintptr_t>(tables[n]);row_pointers[n]=reinterpret_cast<std::uintptr_t>(rows[n]);
        Put(tables[n],Header{reinterpret_cast<std::uintptr_t>(row_pointers+n),1,1});Put(tables[n]+24,10U);Put(tables[n]+28,20U);
        Put(tables[n]+32,static_cast<std::int32_t>(n+7));
        Put(nodes[n],Header{reinterpret_cast<std::uintptr_t>(names[n]),static_cast<std::uint32_t>(std::strlen(names[n])),23});
        Put(nodes[n]+16,static_cast<std::int16_t>(-1-static_cast<int>(n)));Put(nodes[n]+24,Hash(names[n]));}
    Put(manager+3160,Header{reinterpret_cast<std::uintptr_t>(table_pointers),1,2});Put(manager+3184,2U);
    const auto node=reinterpret_cast<std::uintptr_t>(nodes[0]+32);Put(manager+3192,node);Put(manager+3200,node);Put(manager+3208,1U);
    Put(manager+3224+8*Hash(names[0]),node);
}
void Fixture::Append(){Put(manager+3160,Header{reinterpret_cast<std::uintptr_t>(table_pointers),2,2});
    const auto a=reinterpret_cast<std::uintptr_t>(nodes[0]+32),b=reinterpret_cast<std::uintptr_t>(nodes[1]+32);
    Put(nodes[0]+32,b);Put(nodes[1]+40,a);Put(manager+3200,b);Put(manager+3208,2U);Put(manager+3184,3U);
    Put(manager+3224+8*Hash(names[1]),b);}
void Fixture::AddRange(){Put(manager+1048,Header{reinterpret_cast<std::uintptr_t>(ranges),1,2});
    Put(ranges[0],static_cast<std::int16_t>(7));Put(ranges[0]+2,static_cast<std::int16_t>(8));Put(ranges[0]+4,static_cast<std::int16_t>(0));
    Put(ranges[0]+8,Header{reinterpret_cast<std::uintptr_t>(names[1]),6,23});
    Put(range_node,Header{reinterpret_cast<std::uintptr_t>(names[1]),6,23});Put(range_node+16,static_cast<std::int16_t>(0));
    Put(range_node+24,Hash(names[1]));const auto node=reinterpret_cast<std::uintptr_t>(range_node+32);
    Put(manager+1072,node);Put(manager+1080,node);Put(manager+1088,1U);Put(manager+1104+8*Hash(names[1]),node);}
bool DeepSnapshot(){Fixture f;Buffer b;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    CHECK(b.snapshot.table_count==1&&b.snapshot.record_count==1&&b.snapshot.symbol_count==1);
    CHECK(b.symbols[0].value==-1&&b.records[0].words[0]==1);f.names[0][0]='X';f.rows[0][0]=9;
    CHECK(b.strings[0]=='F'&&b.records[0].words[0]==1);return true;}
bool TailPreservation(){Fixture f;Buffer b,current;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    f.Append();CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);CHECK(c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool SameIdChangedRecord(){Fixture f;Buffer b,current;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);++f.rows[0][2];
    CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);CHECK(!c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool NegativeValuePreserved(){Fixture f;Buffer b,current;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    Put(f.nodes[0]+16,static_cast<std::int16_t>(0));CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);
    CHECK(!c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool ChangedFence(){Fixture f;Buffer b,current;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);++f.fence.generation;
    CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);CHECK(!c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool CorruptMap(){Fixture f;Buffer b;f.Append();Put(f.nodes[1]+40,std::uintptr_t{0});
    CHECK(c::Capture(f.access,&b.snapshot)==x::Result::InvalidEntry);return true;}
bool WrongBucket(){Fixture f;Buffer b;Put(f.manager+3224+8*Hash(f.names[0]),std::uintptr_t{0});
    CHECK(c::Capture(f.access,&b.snapshot)==x::Result::InvalidVector);return true;}
bool ChangedSecondRead(){Fixture f;Buffer b;f.change=true;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Changed);return true;}
bool Capacity(){Fixture f;Buffer b;f.Append();b.snapshot.table_capacity=1;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::CapacityExceeded);return true;}
bool NativeOverlapRejectedBeforeWrite(){Fixture f;Buffer b;const auto original=f.names[0][0];b.snapshot.strings=f.names[0];b.snapshot.string_capacity=24;
    CHECK(c::Capture(f.access,&b.snapshot)!=x::Result::Succeeded);CHECK(f.names[0][0]==original);return true;}
bool WrongProfile(){Fixture f;Buffer b;f.access.sha256[0]='0';CHECK(c::Capture(f.access,&b.snapshot)==x::Result::ProfileMismatch);return true;}
bool InvalidSnapshotOffsets(){Fixture f;Buffer b,current;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);current.tables[0].record_offset=UINT32_MAX;
    CHECK(!c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool AnimationRangeAndSecondMap(){Fixture f;Buffer b,current;f.AddRange();CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    CHECK(b.snapshot.range_count==1&&b.snapshot.symbol_count==2&&b.ranges[0].start==7&&b.ranges[0].end==8);
    CHECK(b.symbols[0].map==c::MapKind::NamedFrame&&b.symbols[1].map==c::MapKind::AnimationRange);
    Put(f.ranges[0]+2,static_cast<std::int16_t>(9));CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);
    CHECK(!c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool AnimationMapCannotChange(){Fixture f;Buffer b,current;f.AddRange();CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    Put(f.range_node+16,static_cast<std::int16_t>(-1));CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);
    CHECK(!c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
bool BindingCounterIsSeparate(){Fixture f;Buffer b,current;CHECK(c::Capture(f.access,&b.snapshot)==x::Result::Succeeded);
    Put(f.tables[0]+36,4);CHECK(c::Capture(f.access,&current.snapshot)==x::Result::Succeeded);
    CHECK(current.tables[0].binding_count==4&&c::BaselinePreserved(b.snapshot,current.snapshot));return true;}
struct Instances {i::State* state=new i::State;dluma::fixes::custom_map_asset_isolation::xui_content::Key manager={0x1000,1};
    Instances(){i::SealStartup(state);}~Instances(){delete state;}
    bool Create(std::uintptr_t object=0x2000,i::Origin origin=i::Origin::UiLoadPending,std::uint32_t epoch=0,
                std::uintptr_t identity=0){i::Writer w={};i::Instance value={};
        value.manager=manager;value.object=object;value.interface_object=identity?identity:object+16;value.vtable=0x3000;value.life=i::Life::Active;
        value.origin=origin;value.epoch=epoch;value.load_serial=9;return i::BeginWriter(state,&w)&&i::Created(state,value)&&i::EndWriter(state,&w);}};
bool ReceiverObservation() {
    Instances f; std::uint64_t generation=999;
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation)&&generation==0);
    CHECK(f.Create());i::Instance copied={};
    CHECK(i::TryCopyActiveReceiver(f.state,0x2010,0,&copied));
    CHECK(copied.manager.manager==0x1000 && copied.manager.generation==1 && copied.generation==1 && copied.load_serial==9 && copied.origin==i::Origin::UiLoadPending);
    CHECK(i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation));
    CHECK(generation==1);std::uint64_t matched=0;
    CHECK(i::TryMatchActiveReceiver(f.state,f.manager,0x2010,generation,&matched)&&matched==generation);
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,generation+1,&matched)&&matched==0);
    auto wrong=f.manager;++wrong.generation;
    CHECK(!i::TryMatchActiveReceiver(f.state,wrong,0x2010,0,&matched)&&matched==0);
    i::Writer w={};CHECK(i::BeginWriter(f.state,&w));
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&matched)&&matched==0);
    CHECK(i::Retired(f.state,f.manager,0x2000));CHECK(i::EndWriter(f.state,&w));
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&matched));
    CHECK(!i::TryCopyActiveReceiver(f.state,0x2010,0,&copied) && !copied.generation);
    return true;
}
bool ReceiverObservationFailures() {
    i::State* state=new i::State;std::uint64_t generation=1;dluma::fixes::custom_map_asset_isolation::xui_content::Key manager={0x1000,1};CHECK(!i::TryMatchActiveReceiver(state,manager,0x2010,0,&generation)&&!generation);
    delete state;Instances f;CHECK(f.Create());
    CHECK(f.Create(0x3000,i::Origin::UiLoadPending,0,0x2010));
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation)&&!generation);
    i::Instance copied={};CHECK(!i::TryCopyActiveReceiver(f.state,0x2010,0,&copied) && !copied.generation);
    i::Writer w={};CHECK(i::BeginWriter(f.state,&w));CHECK(i::Retired(f.state,f.manager,0x3000));CHECK(i::EndWriter(f.state,&w));
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation)&&!generation);
    i::Writer collect={};CHECK(i::BeginWriter(f.state,&collect));
    CHECK(i::Collected(f.state,f.manager,1,true,collect.revision));CHECK(i::EndWriter(f.state,&collect));
    CHECK(i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation)&&generation==1);
    f.state->lost=true;
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation));return true;
}
std::size_t ReceiverBucket(std::uintptr_t value) {
    value^=value>>17;value*=static_cast<std::uintptr_t>(0xed5ad4bbU);value^=value>>11;
    return static_cast<std::size_t>(value)&(i::kReceiverBuckets-1);
}
bool ReceiverObservationBoundAndReuse() {
    Instances f;std::uint64_t generation=0;
    for(std::size_t n=0;n<4050;++n)
        CHECK(f.Create(0x200000+static_cast<std::uintptr_t>(n)*0x100));
    CHECK(i::TryMatchActiveReceiver(f.state,f.manager,0x200010,0,&generation));
    CHECK(i::ManagerDestroyed(f.state,f.manager));
    ++f.manager.generation;CHECK(f.Create());
    CHECK(i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation));return true;
}
bool ReceiverCollisionBudget() {
    Instances f;std::uint64_t generation=0;std::uintptr_t first=0,last=0;std::size_t created=0;
    for(std::uintptr_t candidate=0x10000;created<=i::kReceiverScanLimit;candidate+=16){
        if(ReceiverBucket(candidate)!=0)continue;
        if(!first)first=candidate;last=candidate;
        CHECK(f.Create(0x10000000+created*0x100,i::Origin::UiLoadPending,0,candidate));++created;
    }
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,first,0,&generation)&&!generation);
    i::Writer w={};CHECK(i::BeginWriter(f.state,&w));CHECK(i::Retired(f.state,f.manager,0x10000000));CHECK(i::EndWriter(f.state,&w));
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,last,0,&generation));
    i::Writer collect={};CHECK(i::BeginWriter(f.state,&collect));
    CHECK(i::Collected(f.state,f.manager,1,true,collect.revision));CHECK(i::EndWriter(f.state,&collect));
    CHECK(i::TryMatchActiveReceiver(f.state,f.manager,last,0,&generation));
    return true;
}
bool ReceiverBridgeChecksManagerAndProvenance(){
    CHECK(i::SealStartup(&g_xui_instances));i::Writer w={};i::Instance entry={};
    entry.manager={0x1000,7};entry.object=0x2000;entry.interface_object=0x2010;entry.vtable=0x3000;
    entry.life=i::Life::Active;entry.origin=i::Origin::UiLoadProject;entry.load_serial=9;entry.epoch=3;entry.exact_input=true;entry.sha256[0]=0x42;
    CHECK(i::BeginWriter(&g_xui_instances,&w));CHECK(i::Created(&g_xui_instances,entry));CHECK(i::EndWriter(&g_xui_instances,&w));
    g_test_manager={0x1000,0x4000,7,3,x::ManagerLife::Live};DlumaXuiReceiverObservation out={};
    CHECK(dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,(void*)0x1000,1,&out));
    CHECK(out.instance_generation==1&&out.manager_generation==7&&out.load_serial==9&&out.exact_input&&out.sha256[0]==0x42);
    CHECK(dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,(void*)0x1000,0,&out)&&out.manager_generation==7);
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,(void*)0x1000,7,&out));
    i::ReceiverDiagnostics diagnostic={};CHECK(i::CopyReceiverDiagnostics(&g_xui_instances,0x2010,&diagnostic));
    CHECK(diagnostic.sealed&&!diagnostic.lost&&!diagnostic.index_lost&&!diagnostic.writers&&diagnostic.matches==1&&
        diagnostic.first.generation==1&&diagnostic.first.manager.generation==7);
    g_test_level.native_level=0x4001;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,0,1,&out)&&!out.instance_generation);
    g_test_level.native_level=0;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,0,1,&out));
    g_test_level.native_level=0x4000;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,nullptr,0,1,&out));
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,(void*)(UINTPTR_MAX-4),0,1,&out));
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,(void*)0x4000,0,1,&out));
    g_test_manager.life=x::ManagerLife::Destroying;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,(void*)0x1000,1,&out)&&!out.instance_generation);
    g_test_manager.life=x::ManagerLife::Live;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,(void*)0x4001,(void*)0x1000,1,&out));
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,(void*)0x1001,1,&out));
    ++g_test_manager.generation;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,0,1,&out));
    --g_test_manager.generation;g_hooks_current=false;
    CHECK(!dluma_xui_observe_borrowed_receiver((void*)0x2010,&g_test_level,0,1,&out));
    return true;
}
bool NativeRetireCollect(){Instances f;CHECK(f.Create());i::Writer retire={};CHECK(i::BeginWriter(f.state,&retire));
    CHECK(i::Retired(f.state,f.manager,0x2000));CHECK(i::EndWriter(f.state,&retire));
    i::Writer collect={};CHECK(i::BeginWriter(f.state,&collect));CHECK(i::Collected(f.state,f.manager,1,true,collect.revision));
    CHECK(i::EndWriter(f.state,&collect));i::Instance out[2]={};std::size_t count=0;std::uint64_t revision=0;
    CHECK(i::CopyInventory(f.state,f.manager,out,2,&count,&revision)&&count==0);return true;}
bool NativeCollectorNotEmpty(){Instances f;CHECK(f.Create());i::Writer w={};CHECK(i::BeginWriter(f.state,&w));
    CHECK(i::AllRetired(f.state,f.manager));CHECK(i::EndWriter(f.state,&w));std::uint64_t generation=0;
    CHECK(!i::TryMatchActiveReceiver(f.state,f.manager,0x2010,0,&generation));CHECK(i::BeginWriter(f.state,&w));
    CHECK(!i::Collected(f.state,f.manager,1,false,w.revision));return true;}
bool UnobservedDeadObject(){Instances f;i::Writer w={};CHECK(i::BeginWriter(f.state,&w));
    CHECK(!i::Collected(f.state,f.manager,1,true,w.revision));return true;}
bool CollectorInterveningWriter(){Instances f;CHECK(f.Create());i::Writer outer={},inner={};
    CHECK(i::BeginWriter(f.state,&outer));CHECK(i::BeginWriter(f.state,&inner));CHECK(i::EndWriter(f.state,&inner));
    CHECK(!i::Collected(f.state,f.manager,0,true,outer.revision));return true;}
bool CopiedWriterRejected(){Instances f;i::Writer w={};CHECK(i::BeginWriter(f.state,&w));auto copy=w;
    CHECK(!i::EndWriter(f.state,&copy));return true;}
bool SourceOriginAndExternal(){Instances f;CHECK(f.Create());CHECK(f.Create(0x2100,i::Origin::ExternalUnknown));
    CHECK(i::FinishLoad(f.state,9,i::Origin::UiLoadProject));i::Instance out[2]={};std::size_t count=0;std::uint64_t revision=0;
    CHECK(i::CopyInventory(f.state,f.manager,out,2,&count,&revision)&&count==2);
    CHECK(out[0].origin==i::Origin::UiLoadProject&&out[1].origin==i::Origin::ExternalUnknown);return true;}
bool AddressReuseRequiresNativeEnd(){Instances f;CHECK(f.Create());CHECK(i::ManagerDestroyed(f.state,f.manager));
    ++f.manager.generation;CHECK(f.Create());i::Instance out[2]={};std::size_t count=0;std::uint64_t revision=0;
    CHECK(i::CopyInventory(f.state,f.manager,out,2,&count,&revision)&&count==1);CHECK(out[0].generation==2);return true;}
bool ForeignFactoryProjectInput(){Instances f;f.manager={0x5000,2};CHECK(f.Create(0x2000,i::Origin::UiLoadPending,7));
    CHECK(i::FinishLoad(f.state,9,i::Origin::UiLoadProject));CHECK(!i::ProjectInputInstancesClosed(f.state));
    CHECK(i::ManagerDestroyed(f.state,{0x1000,1}));CHECK(!i::ProjectInputInstancesClosed(f.state));
    CHECK(i::ManagerDestroyed(f.state,f.manager));CHECK(i::ProjectInputInstancesClosed(f.state));return true;}
bool PureDynamicExternalBirth(){Instances f;CHECK(f.Create(0x2000,i::Origin::ExternalUnknown,7));
    CHECK(i::ProjectInputInstancesClosed(f.state));return true;}
bool BirthPrecedesRetirementAndCollection(){
    Instances f;i::Writer create={};CHECK(i::BeginWriter(f.state,&create));i::Instance value={};
    value.manager=f.manager;value.object=0x2000;value.interface_object=0x2010;value.vtable=0x3000;value.life=i::Life::Provisional;
    std::uint64_t generation=0;CHECK(i::Born(f.state,value,&generation)&&generation==1);
    i::Writer retire={};CHECK(i::BeginWriter(f.state,&retire));CHECK(i::Retired(f.state,f.manager,value.object));CHECK(i::EndWriter(f.state,&retire));
    i::Writer collect={};CHECK(i::BeginWriter(f.state,&collect));CHECK(i::CollectedNested(f.state,f.manager,1,true,collect));CHECK(i::EndWriter(f.state,&collect));
    CHECK(i::FinishBirth(f.state,f.manager,value.object,generation,true,value.object));CHECK(i::EndWriter(f.state,&create));
    CHECK(!f.state->lost&&f.state->records[0].life==i::Life::Destroyed&&!f.state->records[0].birth_pending);
    CHECK(f.Create(0x2200));return true;
}
bool NestedBirthAndUnwoundBirth(){
    Instances f;i::Instance value={};value.manager=f.manager;value.object=0x2000;value.interface_object=0x2010;value.vtable=0x3000;value.life=i::Life::Provisional;
    i::Writer outer={},inner={};std::uint64_t parent=0,child=0;CHECK(i::BeginWriter(f.state,&outer));CHECK(i::Born(f.state,value,&parent));
    CHECK(i::BeginWriter(f.state,&inner));value.object=0x2200;value.interface_object=0x2210;CHECK(i::Born(f.state,value,&child));
    CHECK(i::FinishBirth(f.state,f.manager,value.object,child,true,value.object));CHECK(i::EndWriter(f.state,&inner));
    CHECK(i::FinishBirth(f.state,f.manager,0x2000,parent,false,0));CHECK(i::EndWriter(f.state,&outer));
    i::Instance result={};CHECK(!i::TryCopyActiveReceiver(f.state,0x2010,0,&result));CHECK(i::TryCopyActiveReceiver(f.state,0x2210,child,&result));
    CHECK(!f.state->lost&&!f.state->active_writers);return true;
}
bool CollectorRejectsPreexistingOtherThread(){
    Instances f;std::atomic<unsigned> phase{0};bool other_ok=false;
    std::thread other([&]{i::Writer writer={};other_ok=i::BeginWriter(f.state,&writer);phase.store(1);
        while(phase.load()!=2)std::this_thread::yield();other_ok=i::EndWriter(f.state,&writer)&&other_ok;phase.store(3);});
    while(phase.load()!=1)std::this_thread::yield();i::Writer collect={};const bool started=i::BeginWriter(f.state,&collect);
    phase.store(2);other.join();CHECK(started&&other_ok);
    CHECK(!i::CollectedNested(f.state,f.manager,0,true,collect));
    CHECK(f.state->first_loss.reason==i::Loss::CollectorFence);return true;
}
bool TenRetainedDocumentCycles(){
    const auto start=std::chrono::steady_clock::now();
    Instances f;i::Writer writer={};CHECK(i::BeginWriter(f.state,&writer));i::Instance value={};
    value.manager=f.manager;value.vtable=0x3000;value.life=i::Life::Active;
    const unsigned baseline=24000,per_document=4500,cycles=10;
    for(unsigned n=0;n<baseline+2*cycles*per_document;++n){
        value.object=0x10000000+static_cast<std::uintptr_t>(n)*256;value.interface_object=value.object+16;
        value.life=n<baseline?i::Life::Active:i::Life::Retired;
        CHECK(i::Created(f.state,value));
    }
    CHECK(i::EndWriter(f.state,&writer));CHECK(!f.state->lost&&f.state->generation==114000);
    std::uint64_t generation=0;CHECK(i::TryMatchActiveReceiver(f.state,f.manager,0x10000010,0,&generation)&&generation==1);
    CHECK(i::BeginWriter(f.state,&writer));CHECK(i::Collected(f.state,f.manager,90000,true,writer.revision));CHECK(i::EndWriter(f.state,&writer));
    CHECK(f.Create(0x10000000+static_cast<std::uintptr_t>(baseline)*256));
    CHECK(!f.state->lost&&f.state->generation==114001);
    std::printf("ten retained document cycles: 114000 births + native collection/reuse, %lld ms, State=%zu bytes\n",
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()),sizeof(i::State));return true;
}
}
int main(){bool(*tests[])()={DeepSnapshot,TailPreservation,SameIdChangedRecord,NegativeValuePreserved,ChangedFence,CorruptMap,
    WrongBucket,ChangedSecondRead,Capacity,NativeOverlapRejectedBeforeWrite,WrongProfile,InvalidSnapshotOffsets,
    AnimationRangeAndSecondMap,AnimationMapCannotChange,BindingCounterIsSeparate,ReceiverObservation,ReceiverObservationFailures,ReceiverObservationBoundAndReuse,ReceiverCollisionBudget,ReceiverBridgeChecksManagerAndProvenance,NativeRetireCollect,NativeCollectorNotEmpty,
    UnobservedDeadObject,CollectorInterveningWriter,CopiedWriterRejected,SourceOriginAndExternal,AddressReuseRequiresNativeEnd,
    ForeignFactoryProjectInput,PureDynamicExternalBirth,BirthPrecedesRetirementAndCollection,NestedBirthAndUnwoundBirth,CollectorRejectsPreexistingOtherThread,TenRetainedDocumentCycles};
    unsigned passed=0;for(auto test:tests){if(!test())return 1;++passed;}std::printf("native XUI instances and compiled state: %u cases passed\n",passed);return 0;}
