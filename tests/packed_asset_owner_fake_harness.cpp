#include "fixes/custom_map_asset_isolation/PackedAssetOwner.h"
#include "fixes/custom_map_asset_isolation/PackedAssetWindows.h"
#include <cstdio>
#include <cstring>

namespace p=dluma::fixes::custom_map_asset_isolation::packed;
namespace r=dluma::fixes::custom_map_asset_isolation::retained;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return false;}}while(0)
struct Fixture {
    p::Origin origins[16]={};p::Ledger ledger={};r::Record texture_records[128]={};r::Ledger textures={};
    p::NativePack native_packs[16]={},scratch_packs[16]={};r::NativeState native_textures[128]={},scratch_textures[128]={};
    p::Snapshot native={},scratch={};p::PlanItem items[16]={};p::Plan plan={};
    std::uint32_t release_calls=0,evict_calls=0;bool empty_release=false,keep_queue=false,skip_unregister=false;
    Fixture(){
        p::Initialize(&ledger,origins,16);r::Initialize(&textures,texture_records,128);
        native.packs=native_packs;native.pack_capacity=16;native.textures=native_textures;native.texture_capacity=128;
        native.runtime=0x8000;native.runtime_vtable=0x9000;native.complete=true;
        native.fence.owner_generation=1;native.fence.thread_id=4;native.fence.mutation_revision=1;
        native.fence.producers_closed=native.fence.consumers_drained=native.fence.renderer_lock_owned=native.fence.worker_lock_owned=true;
        scratch.packs=scratch_packs;scratch.pack_capacity=16;scratch.textures=scratch_textures;scratch.texture_capacity=128;
        plan.items=items;plan.capacity=16;
        native_packs[0].object=0x1000;native_packs[0].metadata=0x2000;native_packs[0].references=1;
        native_packs[0].texture_count=1;native_packs[0].resource_types[0]=UINT64_C(1)<<32;native_packs[0].physical_group_count=1;
        native_textures[0]=Texture(0x3000,0x1000);native_textures[0].references=2;native.pack_count=native.texture_count=1;
    }
    static r::NativeState Texture(std::uintptr_t object,std::uintptr_t pack){
        r::NativeState value={};value.manager=0x7000;value.object=object;value.vtable=0x6000;
        value.kind=r::Kind::Texture;value.deferred=pack;std::snprintf(value.name,sizeof(value.name),"texture_%zu",static_cast<std::size_t>(object));return value;
    }
    static r::Input Input(std::uint64_t generation=1){
        r::Input input={};input.file=0xA000;input.file_generation=generation;input.source=0xB000;input.source_generation=generation;
        input.content_bytes=4096;input.content_sha256[0]=0x32;input.realm=r::Realm::Project;input.epoch=2;return input;
    }
    bool Baseline(){
        r::Inventory inventory={};inventory.objects=native_textures;inventory.capacity=128;inventory.count=1;
        inventory.manager=0x7000;inventory.kind=r::Kind::Texture;inventory.complete=true;
        inventory.fence.owner_generation=1;inventory.fence.thread_id=4;inventory.fence.producers_closed=true;
        inventory.fence.readers_drained=inventory.fence.native_lock_owned=inventory.fence.worker_completions_drained=true;
        if(r::CaptureBaseline(&textures,inventory,1)!=r::Result::Succeeded)return false;
        if(p::CaptureBaseline(&ledger,native,1)!=p::Result::Succeeded)return false;
        native.fence.transaction=3;native.fence.epoch=2;native.fence.authoritative_post_destroy=true;return true;
    }
    p::Key Add(std::uint32_t count,std::uint64_t generation=1){
        p::Key key={};(void)p::Constructed(&ledger,0x1100,&key);(void)p::Registered(&ledger,key);
        (void)p::RetainInput(&ledger,key,Input(generation));(void)p::Loaded(&ledger,key);
        auto& pack=native_packs[native.pack_count++];pack={};pack.object=key.address;pack.metadata=0x2100;
        pack.references=count;pack.texture_begin=native.texture_count;pack.texture_count=count;
        pack.resource_types[0]=(UINT64_C(1)<<32)|(UINT64_C(1)<<33);pack.physical_group_count=2;
        for(std::uint32_t i=0;i<count;++i){
            const std::uintptr_t object=0x4000+0x100*i;native_textures[native.texture_count++]=Texture(object,key.address);
            r::Identity identity={};(void)r::Constructed(&textures,object,r::Kind::Texture,&identity);
            (void)r::RetainInput(&textures,identity,Input(generation));
        }
        return key;
    }
    static p::Result Capture(void* user,p::Snapshot* out)noexcept{
        auto& f=*static_cast<Fixture*>(user);std::memcpy(out->packs,f.native_packs,sizeof(f.native_packs));
        std::memcpy(out->textures,f.native_textures,sizeof(f.native_textures));
        out->pack_count=f.native.pack_count;out->texture_count=f.native.texture_count;
        out->runtime=f.native.runtime;out->runtime_vtable=f.native.runtime_vtable;out->fence=f.native.fence;out->complete=true;
        return p::Result::Succeeded;
    }
    static p::Result Evict(void* user,const p::Key& key)noexcept{
        auto& f=*static_cast<Fixture*>(user);++f.evict_calls;
        for(std::size_t i=0;i<f.native.pack_count;++i)if(f.native_packs[i].object==key.address && !f.keep_queue)
            std::memset(f.native_packs[i].queued_entries,0,sizeof(f.native_packs[i].queued_entries));
        ++f.native.fence.mutation_revision;return p::Result::Succeeded;
    }
    static p::Result Release(void* user,const p::Key& key,bool empty)noexcept{
        auto& f=*static_cast<Fixture*>(user);++f.release_calls;f.empty_release|=empty;
        std::size_t index=0;while(index<f.native.pack_count && f.native_packs[index].object!=key.address)++index;
        if(index==f.native.pack_count)return p::Result::UnknownPack;
        auto& pack=f.native_packs[index];if(!empty && !pack.references)return p::Result::NativeFailure;
        if(!empty)--pack.references;++f.native.fence.mutation_revision;
        if(pack.references)return p::Result::Succeeded;
        for(std::size_t j=0;j<pack.texture_count;++j){
            const auto object=f.native_textures[pack.texture_begin+j].object;r::Identity identity={};
            if(r::FindIdentity(&f.textures,object,r::Kind::Texture,&identity)!=r::Result::Succeeded)return p::Result::UnknownTexture;
            (void)r::BeginDestruction(&f.textures,identity);(void)r::EndDestruction(&f.textures,identity);
        }
        const std::size_t first=pack.texture_begin,count=pack.texture_count;
        for(std::size_t j=first+count;j<f.native.texture_count;++j)f.native_textures[j-count]=f.native_textures[j];
        f.native.texture_count-=count;
        for(std::size_t j=index+1;j<f.native.pack_count;++j){f.native_packs[j-1]=f.native_packs[j];f.native_packs[j-1].texture_begin-=count;}
        --f.native.pack_count;(void)p::DestructorEntered(&f.ledger,key);(void)p::DestructorReturned(&f.ledger,key);
        if(!f.skip_unregister)(void)p::Unregistered(&f.ledger,key);return p::Result::Succeeded;
    }
    p::Result Prepare(){return p::Prepare(ledger,textures,native,&plan);}
    p::Result Reset(){return p::Reset(&ledger,&textures,{this,Capture,Evict,Release},&scratch,&plan);}
};
bool Success(){Fixture f;CHECK(f.Baseline());f.Add(2);f.native_packs[0].queued_entries[3]=1;f.native_packs[1].queued_entries[0]=1;
    CHECK(f.Prepare()==p::Result::Succeeded);CHECK(f.Reset()==p::Result::Succeeded);
    CHECK(f.release_calls==2 && f.evict_calls==1 && !f.empty_release);CHECK(f.native.pack_count==1 && f.native.texture_count==1);
    CHECK(f.native_packs[0].queued_entries[3]==1);CHECK(p::CanRetireSource(f.ledger,0xB000,1,3)==p::Result::Succeeded);return true;}
bool Empty(){Fixture f;CHECK(f.Baseline());f.Add(0);CHECK(f.Prepare()==p::Result::Succeeded);CHECK(f.Reset()==p::Result::Succeeded);
    CHECK(f.empty_release && f.release_calls==1);return true;}
bool TextureRefs(){Fixture f;CHECK(f.Baseline());f.Add(1);f.native_textures[1].references=1;
    CHECK(f.Prepare()==p::Result::NativeReferencesLive);CHECK(!f.release_calls);return true;}
bool ExtraPackRefs(){Fixture f;CHECK(f.Baseline());f.Add(1);++f.native_packs[1].references;
    CHECK(f.Prepare()==p::Result::NativeReferencesLive);return true;}
bool MeshType(){Fixture f;CHECK(f.Baseline());f.Add(1);f.native_packs[1].resource_types[0]|=UINT64_C(1)<<16;
    CHECK(f.Prepare()==p::Result::UnsupportedResourceType);return true;}
bool Patch(){Fixture f;CHECK(f.Baseline());f.Add(1);f.native_packs[1].patch=0x800000;
    CHECK(f.Prepare()==p::Result::UnsupportedPatch);return true;}
bool Queue(){Fixture f;CHECK(f.Baseline());f.Add(1);f.native_packs[1].queued_entries[1]=1;f.keep_queue=true;
    CHECK(f.Prepare()==p::Result::Succeeded);CHECK(f.Reset()==p::Result::QueueStillRetained);CHECK(!f.release_calls);return true;}
bool Destruction(){Fixture f;CHECK(f.Baseline());f.Add(1);f.skip_unregister=true;
    CHECK(f.Prepare()==p::Result::Succeeded);CHECK(f.Reset()==p::Result::VerificationFailed);
    CHECK(p::CanRetireSource(f.ledger,0xB000,1,3)!=p::Result::Succeeded);return true;}
bool Pending(){Fixture f;CHECK(f.Baseline());const auto key=f.Add(1);CHECK(p::InputPending(&f.ledger,key)==p::Result::Succeeded);
    CHECK(f.Prepare()==p::Result::PendingInput);return true;}
bool SeparateGpuOwner(){Fixture f;CHECK(f.Baseline());f.Add(1);f.native_packs[1].flags|=0x02000000U;
    CHECK(f.Prepare()==p::Result::PendingNativeCall);CHECK(!f.release_calls&&!f.evict_calls);return true;}
bool ObservedBaselineFitsCapacity(){Fixture f;f.ledger.capacity=1;p::Key key={};
    CHECK(p::Constructed(&f.ledger,0x1000,&key)==p::Result::Succeeded);CHECK(f.Baseline());return true;}
bool WrongOwner(){Fixture f;CHECK(f.Baseline());f.Add(1);f.native_textures[1].deferred=0x1000;
    CHECK(f.Prepare()==p::Result::Incomplete);return true;}
bool Baseline(){Fixture f;CHECK(f.Baseline());p::Key key={};CHECK(p::FindActive(&f.ledger,0x1000,&key)==p::Result::Succeeded);
    CHECK(p::RetainInput(&f.ledger,key,Fixture::Input())==p::Result::Succeeded);CHECK(f.Prepare()==p::Result::BaselineChanged);return true;}
bool Unknown(){Fixture f;CHECK(f.Baseline());f.Add(1);f.origins[1].empty=true;CHECK(f.Prepare()==p::Result::UnknownPack);return true;}
bool Stale(){Fixture f;CHECK(f.Baseline());f.Add(1);CHECK(f.Prepare()==p::Result::Succeeded);CHECK(f.Reset()==p::Result::Succeeded);
    CHECK(p::CanRetireSource(f.ledger,0xB000,1,4)!=p::Result::Succeeded);p::Key unexpected={};
    CHECK(p::Constructed(&f.ledger,0x1200,&unexpected)==p::Result::Succeeded);
    CHECK(p::CanRetireSource(f.ledger,0xB000,1,3)!=p::Result::Succeeded);return true;}
bool MixedInputRecordRecycles(){Fixture f;CHECK(f.Baseline());f.ledger.capacity=2;
    for(unsigned cycle=0;cycle<40;++cycle){const auto key=f.Add(0,cycle+1);auto global=Fixture::Input();global.realm=r::Realm::Global;global.epoch=0;global.source=0xC000;global.file=0xD000;
        CHECK(p::RetainInput(&f.ledger,key,global)==p::Result::Succeeded);CHECK(f.Prepare()==p::Result::Succeeded);CHECK(f.Reset()==p::Result::Succeeded);
        CHECK(p::ReclaimRetired(&f.ledger,0xB000,cycle+1,3)==p::Result::Succeeded);CHECK(f.origins[key.slot].empty);
    }return true;}
bool OtherStreamOwnerBlocksAfterPackDestruction(){Fixture f;CHECK(f.Baseline());const auto key=f.Add(1);
    CHECK(p::ObserveUnclassifiedStream(&f.ledger,key,2)==p::Result::Succeeded);CHECK(f.Prepare()==p::Result::UnclassifiedStreamOwner);
    CHECK(Fixture::Release(&f,key,false)==p::Result::Succeeded);CHECK(f.Prepare()==p::Result::UnclassifiedStreamOwner);return true;}
bool GlobalPackStreamCanConsumeUnknownProjectInput(){Fixture f;CHECK(f.Baseline());p::Key key={};
    CHECK(p::FindActive(&f.ledger,0x1000,&key)==p::Result::Succeeded);CHECK(p::ObserveUnclassifiedStream(&f.ledger,key,2)==p::Result::Succeeded);
    CHECK(f.Prepare()==p::Result::UnclassifiedStreamOwner);return true;}
int main(){
    struct Test{const char* name;bool(*run)();};const Test tests[]={{"exact packed texture release",Success},{"empty registered pack",Empty},
        {"live texture borrower",TextureRefs},{"unaccounted native pack ref",ExtraPackRefs},{"unclassified resource type",MeshType},
        {"patch owner",Patch},{"pending selected queue",Queue},{"missing unregister completion",Destruction},{"pending actual input",Pending},
        {"wrong texture pack owner",WrongOwner},{"baseline override",Baseline},{"unknown constructed pack",Unknown},{"stale source receipt",Stale},
        {"separate GPU streaming owner",SeparateGpuOwner},{"already observed B0 at capacity",ObservedBaselineFitsCapacity},
        {"mixed global input record recycling",MixedInputRecordRecycles},{"other stream owner outlives pack destruction",OtherStreamOwnerBlocksAfterPackDestruction},
        {"global pack streaming project input remains unclassified",GlobalPackStreamCanConsumeUnknownProjectInput}};
    for(const auto& test:tests)if(!test.run()){std::printf("FAILED %s\n",test.name);return 1;}
    std::printf("PASS %zu packed-asset scenarios\n",sizeof(tests)/sizeof(tests[0]));return 0;}
