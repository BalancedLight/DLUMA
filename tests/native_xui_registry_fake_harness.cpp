#include "fixes/custom_map_asset_isolation/NativeXuiRegistrySnapshot.h"
#include <cstdio>
#include <cstring>
namespace x = dluma::fixes::custom_map_asset_isolation::xui;
using R = x::Result;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d %s\n",__LINE__,#c); return false; } } while(0)
namespace {
struct Header { std::uintptr_t data; std::uint32_t count,capacity; };
template<class T> void Put(unsigned char* p,const T& value) { std::memcpy(p,&value,sizeof(value)); }
struct Storage {
    x::Entry entries[8] = {}; std::uint32_t packs[16] = {}; char strings[2048] = {};
    x::Snapshot snapshot = {};
    Storage() { snapshot.entries=entries; snapshot.entry_capacity=8; snapshot.pack_ids=packs;
        snapshot.pack_capacity=16; snapshot.strings=strings; snapshot.string_capacity=sizeof(strings); }
};
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    alignas(8) unsigned char manager[224]={}, pack[32]={}, skin[64]={}, screen[80]={};
    char names[4][24] = {"MenuPack", "MenuSkin", "MainScreen", "ScreenBase"};
    std::uint32_t ids[2]={7,9}; x::Access access={}; x::Fence fence={};
    bool change_name=false,change_fence=false,deny_fence=false; unsigned name_reads=0,fence_reads=0;
    Fixture();
};
bool Copy(std::uintptr_t p,void* out,std::size_t n,const void* object,std::size_t size) {
    const auto b=reinterpret_cast<std::uintptr_t>(object);
    if (p<b || n>size || p-b>size-n) return false;
    std::memcpy(out,reinterpret_cast<const void*>(p),n); return true;
}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {
    auto& f=*static_cast<Fixture*>(user);
    if (p==reinterpret_cast<std::uintptr_t>(f.names[0]) && f.change_name && ++f.name_reads==2)
        f.names[0][0]='X';
    return Copy(p,out,n,f.manager,sizeof(f.manager)) || Copy(p,out,n,f.pack,sizeof(f.pack)) ||
        Copy(p,out,n,f.skin,sizeof(f.skin)) || Copy(p,out,n,f.screen,sizeof(f.screen)) ||
        Copy(p,out,n,f.names,sizeof(f.names)) || Copy(p,out,n,f.ids,sizeof(f.ids));
}
bool Fence(void* user,x::Fence* out) noexcept {
    auto& f=*static_cast<Fixture*>(user); if (f.deny_fence) return false;
    *out=f.fence; if (f.change_fence && ++f.fence_reads>1) ++out->generation; return true;
}
Fixture::Fixture() {
    Put(manager,base+0x9173E8); Put(manager+144,base+0x918F98);
    Put(manager+136,static_cast<std::uintptr_t>(0x5000));
    unsigned char* rows[]={pack,skin,screen}; const std::uintptr_t tables[]={0x918F80,0x918F68,0x918F40};
    for (unsigned i=0;i<3;++i) {
        Put(rows[i],base+tables[i]);
        Header h={reinterpret_cast<std::uintptr_t>(rows[i]),1,1}; Put(manager+152+24*i,h);
        h={reinterpret_cast<std::uintptr_t>(names[i]),static_cast<std::uint32_t>(std::strlen(names[i])),23};
        Put(rows[i]+8,h);
    }
    Put(pack+24,static_cast<std::int16_t>(7));
    Header h={reinterpret_cast<std::uintptr_t>(ids),2,2}; Put(skin+24,h); Put(screen+24,h);
    Put(screen+48,static_cast<std::int32_t>(42));
    h={reinterpret_cast<std::uintptr_t>(names[3]),static_cast<std::uint32_t>(std::strlen(names[3])),23};
    Put(screen+56,h); Put(screen+72,static_cast<std::int16_t>(3)); screen[74]=1; skin[60]=1;
    fence={reinterpret_cast<std::uintptr_t>(manager),3,9,17,1};
    access.engine_base=base; std::memcpy(access.sha256,x::kPlayerEngineSha256,65);
    access.read=Read; access.fence=Fence; access.user=this;
}
bool CapturesDeepCatalog() { Fixture f; Storage s; CHECK(x::Capture(f.access,&s.snapshot)==R::Succeeded);
    CHECK(s.snapshot.complete && s.snapshot.entry_count==3 && s.snapshot.pack_count==4);
    CHECK(s.entries[0].id==7 && s.entries[2].id==42 && s.entries[2].category==3);
    CHECK(!std::strcmp(s.strings+s.entries[2].template_offset,"ScreenBase"));
    f.names[2][0]='X'; CHECK(!std::strcmp(s.strings+s.entries[2].name_offset,"MainScreen")); return true; }
bool EntryOverrideIsVisible() { Fixture f; Storage a,b; CHECK(x::Capture(f.access,&a.snapshot)==R::Succeeded);
    f.names[2][0]='X'; CHECK(x::Capture(f.access,&b.snapshot)==R::Succeeded);
    CHECK(!x::Equal(a.snapshot,b.snapshot,false)); return true; }
bool PackLinkChangeIsVisible() { Fixture f; Storage a,b; CHECK(x::Capture(f.access,&a.snapshot)==R::Succeeded);
    f.ids[1]=77; CHECK(x::Capture(f.access,&b.snapshot)==R::Succeeded); CHECK(!x::Equal(a.snapshot,b.snapshot,false)); return true; }
bool LoadedStateIsExplicit() { Fixture f; Storage a,b; CHECK(x::Capture(f.access,&a.snapshot)==R::Succeeded);
    f.skin[60]=0; Put(f.skin+48,17U); CHECK(x::Capture(f.access,&b.snapshot)==R::Succeeded);
    CHECK(!x::Equal(a.snapshot,b.snapshot,true)); CHECK(x::Equal(a.snapshot,b.snapshot,false));
    f.screen[74]=3; CHECK(x::Capture(f.access,&b.snapshot)==R::Succeeded);
    CHECK(!x::Equal(a.snapshot,b.snapshot,false)); return true; }
bool WrongProfileRejected() { Fixture f; Storage s; f.access.sha256[0]='0';
    CHECK(x::Capture(f.access,&s.snapshot)==R::ProfileMismatch && !s.snapshot.complete); return true; }
bool WrongTypeRejected() { Fixture f; Storage s; Put(f.screen,Fixture::base+0x918F80);
    CHECK(x::Capture(f.access,&s.snapshot)==R::InvalidEntry); return true; }
bool BoundsRejectBadVector() { Fixture f; Storage s; Put(f.manager+160,2U);
    CHECK(x::Capture(f.access,&s.snapshot)==R::InvalidVector); return true; }
bool UnterminatedAndEmbeddedNulRejected() { Fixture f; Storage s; f.names[0][8]='x';
    CHECK(x::Capture(f.access,&s.snapshot)==R::InvalidString); f.names[0][8]=0; f.names[0][2]=0;
    CHECK(x::Capture(f.access,&s.snapshot)==R::InvalidString); return true; }
bool CapacityDoesNotPublishPartial() { Fixture f; Storage s; s.snapshot.entry_capacity=2;
    CHECK(x::Capture(f.access,&s.snapshot)==R::CapacityExceeded && !s.snapshot.complete);
    s.snapshot.entry_capacity=8; s.snapshot.string_capacity=2;
    CHECK(x::Capture(f.access,&s.snapshot)==R::CapacityExceeded && !s.snapshot.complete); return true; }
bool ChangedSecondReadRejected() { Fixture f; Storage s; f.change_name=true;
    CHECK(x::Capture(f.access,&s.snapshot)==R::Changed && !s.snapshot.complete); return true; }
bool GenerationFenceRejected() { Fixture f; Storage s; f.change_fence=true;
    CHECK(x::Capture(f.access,&s.snapshot)==R::Changed); f.deny_fence=true;
    CHECK(x::Capture(f.access,&s.snapshot)==R::FenceUnavailable); return true; }
bool EmptyCatalogAndNoBorrowedPointers() { Fixture f; Storage a,b;
    for(unsigned i=0;i<3;++i) { Header h={}; Put(f.manager+152+24*i,h); }
    CHECK(x::Capture(f.access,&a.snapshot)==R::Succeeded && !a.snapshot.entry_count);
    CHECK(x::Capture(f.access,&b.snapshot)==R::Succeeded && x::Equal(a.snapshot,b.snapshot,true)); return true; }
bool OverlappingOutputRejected() { Fixture f; Storage s; s.snapshot.strings=reinterpret_cast<char*>(s.entries);
    CHECK(x::Capture(f.access,&s.snapshot)==R::InvalidArgument); return true; }
bool NativeManagerBoundaries() { Fixture f; x::ManagerLedger ledger; x::ManagerToken token={};
    x::ManagerRecord rows[4]={}; std::size_t count=0; std::uint64_t revision=0;
    const auto manager=reinterpret_cast<std::uintptr_t>(f.manager);
    CHECK(x::SealManagerStartup(&ledger));
    CHECK(x::BeginManagerConstruction(&ledger,manager,0x5000,7,&token));
    CHECK(!x::CopyManagerInventory(&ledger,rows,4,&count,&revision));
    CHECK(x::EndManagerConstruction(&ledger,&token,f.access,manager));
    CHECK(x::CopyManagerInventory(&ledger,rows,4,&count,&revision) && count==1 &&
        rows[0].birth_epoch==7 && rows[0].level==0x5000 && rows[0].generation==1);
    CHECK(x::BeginManagerDestruction(&ledger,manager,&token));
    CHECK(!x::CopyManagerInventory(&ledger,rows,4,&count,&revision));
    CHECK(x::EndManagerDestruction(&ledger,&token));
    CHECK(x::CopyManagerInventory(&ledger,rows,4,&count,&revision) && !count); return true; }
bool ManagerSlotsReuseWithFreshGeneration() { Fixture f; x::ManagerLedger ledger;
    const auto manager=reinterpret_cast<std::uintptr_t>(f.manager); CHECK(x::SealManagerStartup(&ledger));
    for(unsigned i=0;i<40;++i) { x::ManagerToken token={};
        CHECK(x::BeginManagerConstruction(&ledger,manager,0x5000,i+1,&token)); CHECK(token.generation==i+1);
        CHECK(x::EndManagerConstruction(&ledger,&token,f.access,manager));
        CHECK(x::BeginManagerDestruction(&ledger,manager,&token)); CHECK(x::EndManagerDestruction(&ledger,&token)); }
    CHECK(!ledger.lost.load() && ledger.next_generation==40); return true; }
bool StaleManagerTokenCannotRetireReusedAddress() { Fixture f; x::ManagerLedger ledger; x::ManagerToken token={};
    const auto manager=reinterpret_cast<std::uintptr_t>(f.manager); CHECK(x::SealManagerStartup(&ledger));
    CHECK(x::BeginManagerConstruction(&ledger,manager,0x5000,1,&token));
    CHECK(x::EndManagerConstruction(&ledger,&token,f.access,manager));
    CHECK(x::BeginManagerDestruction(&ledger,manager,&token)); const auto stale=token;
    CHECK(x::EndManagerDestruction(&ledger,&token));
    CHECK(x::BeginManagerConstruction(&ledger,manager,0x5000,2,&token));
    CHECK(x::EndManagerConstruction(&ledger,&token,f.access,manager));
    auto old=stale; CHECK(!x::EndManagerDestruction(&ledger,&old));
    CHECK(ledger.records[0].life==x::ManagerLife::Live && ledger.records[0].generation==2); return true; }
bool ManagerTypeAndLevelMustMatch() { Fixture f; x::ManagerLedger ledger; x::ManagerToken token={};
    const auto manager=reinterpret_cast<std::uintptr_t>(f.manager); CHECK(x::SealManagerStartup(&ledger));
    CHECK(x::BeginManagerConstruction(&ledger,manager,0x5000,1,&token)); Put(f.manager+136,static_cast<std::uintptr_t>(0x6000));
    CHECK(!x::EndManagerConstruction(&ledger,&token,f.access,manager) && ledger.lost.load()); return true; }
bool MissingManagerHistoryCannotBecomeBaseline() { x::ManagerLedger ledger; x::ManagerToken token={};
    CHECK(!x::BeginManagerConstruction(&ledger,0x100,0x200,0,&token));
    CHECK(!x::SealManagerStartup(&ledger)); return true; }
bool UnexpectedOrConcurrentManagerObservationInvalidatesInventory() { x::ManagerLedger ledger; x::ManagerToken token={};
    CHECK(x::SealManagerStartup(&ledger)); CHECK(!x::BeginManagerDestruction(&ledger,0x100,&token));
    x::ManagerLedger other; CHECK(x::SealManagerStartup(&other)); other.lock.test_and_set();
    CHECK(!x::BeginManagerConstruction(&other,0x100,0x200,0,&token)); other.lock.clear();
    CHECK(other.lost.load()); return true; }
bool BusyManagerReadDoesNotLoseConstructorHistory() { x::ManagerLedger ledger;
    CHECK(x::SealManagerStartup(&ledger)); x::ManagerRecord rows[2]={}; std::size_t count=0; std::uint64_t revision=0;
    ledger.lock.test_and_set(); CHECK(!x::CopyManagerInventory(&ledger,rows,2,&count,&revision)); ledger.lock.clear();
    CHECK(!ledger.lost.load()); CHECK(x::CopyManagerInventory(&ledger,rows,2,&count,&revision) && !count); return true; }

struct ConsumerFixture {
    Fixture catalog;
    unsigned char first[72]={},second[72]={},dead[72]={},input_owner[18872]={};
    std::uintptr_t input_node[3]={};
    x::Consumer rows[4]={};
    x::Consumers output={rows,4};
    x::Access access={};
    unsigned owner_reads=0;
    bool mutate=false;
    ConsumerFixture() {
        access=catalog.access; access.user=this; access.read=&Read; access.fence=&Fence;
        Put(first,Fixture::base+0x900000); Put(second,Fixture::base+0x900000); Put(dead,Fixture::base+0x900000);
        Put(first+16,std::uintptr_t{0x101}); Put(second+16,std::uintptr_t{0x102}); Put(dead+16,std::uintptr_t{0x103});
        Put(first+48,reinterpret_cast<std::uintptr_t>(second)); Put(second+56,reinterpret_cast<std::uintptr_t>(first));
        Put(catalog.manager+24,reinterpret_cast<std::uintptr_t>(first)); Put(catalog.manager+32,2U);
        Put(catalog.manager+40,reinterpret_cast<std::uintptr_t>(dead)); Put(catalog.manager+48,1U); dead[64]=2;
        input_node[0]=reinterpret_cast<std::uintptr_t>(catalog.manager)+8;
        Put(input_owner+18864,reinterpret_cast<std::uintptr_t>(&input_node[1]));
    }
    static bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {
        auto& f=*static_cast<ConsumerFixture*>(user);
        if(p==Fixture::base+0xACA848 && n==8) {
            const auto owner=reinterpret_cast<std::uintptr_t>(f.input_owner); std::memcpy(out,&owner,8); return true;
        }
        if(p==reinterpret_cast<std::uintptr_t>(f.catalog.manager) && ++f.owner_reads==2 && f.mutate)
            Put(f.first+16,std::uintptr_t{0x999});
        std::memcpy(out,reinterpret_cast<const void*>(p),n); return true;
    }
    static bool Fence(void* user,x::Fence* out) noexcept {
        auto& f=*static_cast<ConsumerFixture*>(user); return f.catalog.access.fence(&f.catalog,out);
    }
};
bool NativeConsumersIncludeDeferredObjects() { ConsumerFixture f;
    CHECK(x::CaptureConsumers(f.access,&f.output)==R::Succeeded && f.output.complete);
    CHECK(f.output.count==3 && f.output.active_count==2 && f.output.dead_count==1);
    CHECK(f.rows[2].awaiting_destruction && f.rows[2].interface_object==0x103);
    CHECK(f.output.input_receiver_references==1); return true; }
bool EmptyActiveListDoesNotHideRetiredOrInputOwners() { ConsumerFixture f;
    Put(f.catalog.manager+24,std::uintptr_t{0}); Put(f.catalog.manager+32,0U);
    CHECK(x::CaptureConsumers(f.access,&f.output)==R::Succeeded && f.output.active_count==0);
    CHECK(f.output.dead_count==1 && f.output.input_receiver_references==1); return true; }
bool NativeConsumerBacklinksAndFlagsAreChecked() { ConsumerFixture f;
    Put(f.second+56,std::uintptr_t{0}); CHECK(x::CaptureConsumers(f.access,&f.output)==R::InvalidEntry);
    Put(f.second+56,reinterpret_cast<std::uintptr_t>(f.first)); f.dead[64]=0;
    CHECK(x::CaptureConsumers(f.access,&f.output)==R::InvalidEntry && !f.output.complete); return true; }
bool NativeConsumerCycleDoesNotPublishPrefix() { ConsumerFixture f;
    Put(f.second+48,reinterpret_cast<std::uintptr_t>(f.first));
    CHECK(x::CaptureConsumers(f.access,&f.output)==R::InvalidEntry && !f.output.count); return true; }
bool NativeConsumerMutationInvalidatesCapture() { ConsumerFixture f; f.mutate=true;
    CHECK(x::CaptureConsumers(f.access,&f.output)==R::Changed && !f.output.complete && !f.output.count); return true; }
bool NativeInputReferencesAreOwnerSpecific() { ConsumerFixture f; f.input_node[0]=0x123;
    CHECK(x::CaptureConsumers(f.access,&f.output)==R::Succeeded && f.output.input_receiver_references==0);
    f.output.capacity=2; CHECK(x::CaptureConsumers(f.access,&f.output)==R::CapacityExceeded && !f.output.complete); return true; }
}
int main() {
    bool(*cases[])()={CapturesDeepCatalog,EntryOverrideIsVisible,PackLinkChangeIsVisible,LoadedStateIsExplicit,
        WrongProfileRejected,WrongTypeRejected,BoundsRejectBadVector,UnterminatedAndEmbeddedNulRejected,
        CapacityDoesNotPublishPartial,ChangedSecondReadRejected,GenerationFenceRejected,
        EmptyCatalogAndNoBorrowedPointers,OverlappingOutputRejected,NativeManagerBoundaries,
        ManagerSlotsReuseWithFreshGeneration,StaleManagerTokenCannotRetireReusedAddress,ManagerTypeAndLevelMustMatch,
        MissingManagerHistoryCannotBecomeBaseline,UnexpectedOrConcurrentManagerObservationInvalidatesInventory,
        BusyManagerReadDoesNotLoseConstructorHistory,NativeConsumersIncludeDeferredObjects,
        EmptyActiveListDoesNotHideRetiredOrInputOwners,NativeConsumerBacklinksAndFlagsAreChecked,
        NativeConsumerCycleDoesNotPublishPrefix,NativeConsumerMutationInvalidatesCapture,NativeInputReferencesAreOwnerSpecific};
    for(auto test:cases) if(!test()) return 1;
    std::puts("native XUI registry, lifetime and consumers: 26 scenarios passed"); return 0;
}
