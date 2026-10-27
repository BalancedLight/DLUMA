#define _HAS_EXCEPTIONS 0
#include "StoredBlueprintOwner.h"
#include "CurveBaselineSnapshot.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::stored_blueprints {
namespace {
struct Header {std::uintptr_t data;std::uint32_t count,capacity;};
struct VectorStamp {std::uintptr_t address;Header header;};
struct ContextStamp {std::uintptr_t state,context;};
bool Read(const Access& a,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool Same(const Fence& x,const Fence& y) noexcept {
    if(x.game_di!=y.game_di || x.session!=y.session || x.blueprint_store!=y.blueprint_store || x.player_count!=y.player_count ||
        x.epoch!=y.epoch || x.thread_id!=y.thread_id || x.teardown_generation!=y.teardown_generation ||
        x.frame_route_token!=y.frame_route_token || x.owner_revision!=y.owner_revision)return false;
    for(std::size_t i=0;i<x.player_count;++i)if(x.players[i].state!=y.players[i].state || x.players[i].generation!=y.players[i].generation)return false;
    return true;
}
bool FenceValid(const Access& a,const Fence& f) noexcept {
    if(!f.game_di || f.game_di>UINTPTR_MAX-0x510 || !f.session || f.session>UINTPTR_MAX-0x7BE8 || !f.epoch || !f.teardown_generation || !f.frame_route_token ||
        f.player_count>kMaximumOwners || f.thread_id!=GetCurrentThreadId() || !a.teardown_scope ||
        a.teardown_scope->caller!=curves::TypedCurveCaller::NativeSessionTeardown || a.teardown_scope->transaction!=f.teardown_generation ||
        !curves::IsTypedCurveNativeScopeActive(a.borrowers,f.thread_id,a.teardown_scope))return false;
    std::uintptr_t game=0,session=0,store=0;
    if(!Read(a,a.gamedll_base+0x1D07F88,&game,8) || game!=f.game_di || !Read(a,game+0x508,&session,8) || session!=f.session ||
        !Read(a,session+0x7BE0,&store,8) || store!=f.blueprint_store)return false;
    for(std::size_t i=0;i<f.player_count;++i){if(!f.players[i].state || f.players[i].state>UINTPTR_MAX-0x470 || !f.players[i].generation)return false;
        for(std::size_t j=0;j<i;++j)if(f.players[i].state==f.players[j].state)return false;}
    return true;
}
bool SourceOwns(const Access& a,std::uintptr_t store,std::uint64_t id,std::uintptr_t object) noexcept {
    if(!store || !id)return false;Header h={};
    if(!Read(a,store,&h,sizeof(h)) || !h.data || !h.count || h.count>kMaximumCopies)return false;
    std::uintptr_t node=h.data,parent=0,visited[128]={};std::size_t used=0;
    while(node){
        if(node<16 || used>=128)return false;
        for(std::size_t i=0;i<used;++i)if(visited[i]==node)return false;visited[used++]=node;
        std::uint64_t key=0;std::uintptr_t value=0,links[3]={};
        if(!Read(a,node-16,&key,8) || !Read(a,node-8,&value,8) || !Read(a,node,links,sizeof(links)) || links[1]!=parent)return false;
        if(key==id)return value==object;
        parent=node;node=key>id?links[0]:links[2];
    }
    return false;
}
bool Less(const Binding& a,const Binding& b) noexcept {return a.expected<b.expected || (a.expected==b.expected && a.address<b.address);}
void Swap(Binding& a,Binding& b) noexcept {const auto value=a;a=b;b=value;}
void Sift(Binding* a,std::size_t root,std::size_t n) noexcept {
    while(root<n/2){std::size_t child=2*root+1;if(child+1<n && Less(a[child],a[child+1]))++child;
        if(!Less(a[root],a[child]))return;Swap(a[root],a[child]);root=child;}
}
void Sort(Binding* a,std::size_t n) noexcept {
    for(std::size_t i=n/2;i>0;--i)Sift(a,i-1,n);
    for(std::size_t i=n;i>1;--i){Swap(a[0],a[i-1]);Sift(a,0,i-1);}
}
bool Vector(const Access& a,std::uintptr_t p,Header* h) noexcept {
    return Read(a,p,h,sizeof(*h)) && h->count<=h->capacity && h->count<=4096 && h->capacity<=1048576 &&
        (!h->count || (h->data && h->count<=(UINTPTR_MAX-h->data)/192));
}
Result Recheck(const Access& a,const Fence& before,const VectorStamp* vectors,std::size_t vector_count,
    const ContextStamp* contexts,std::size_t context_count) noexcept {
    Fence after={};if(!a.fence(a.user,&after) || !Same(before,after) || !FenceValid(a,after))return Result::OwnerChanged;
    for(std::size_t i=0;i<context_count;++i){std::uintptr_t pointer=0;
        if(!Read(a,contexts[i].state+0x468,&pointer,8) || pointer!=contexts[i].context)return Result::OwnerChanged;}
    for(std::size_t i=0;i<vector_count;++i){Header current={};
        if(!Read(a,vectors[i].address,&current,sizeof(current)) || std::memcmp(&current,&vectors[i].header,sizeof(current)))return Result::OwnerChanged;}
    return Result::Succeeded;
}
Result Finish(Owner* o,Result result,std::size_t initial,bool may_have_published) noexcept {
    if(result!=Result::Succeeded && !may_have_published)o->copy_count=initial;
    return o->result=result;
}
} // namespace
bool Initialize(Owner* o,BlueprintCopy* copies,std::size_t count,Binding* work,std::size_t work_count) noexcept {
    if(!o || o->initialized || !copies || !count || count>kMaximumCopies || !work || !work_count || work_count>kMaximumBindings)return false;
    const auto c=reinterpret_cast<std::uintptr_t>(copies),w=reinterpret_cast<std::uintptr_t>(work);
    if(c%alignof(BlueprintCopy) || w%alignof(Binding) || c>UINTPTR_MAX-count*sizeof(BlueprintCopy) || w>UINTPTR_MAX-work_count*sizeof(Binding) ||
        (c<w+work_count*sizeof(Binding) && w<c+count*sizeof(BlueprintCopy)))return false;
    *o={};o->copies=copies;o->copy_capacity=count;o->work=work;o->work_capacity=work_count;o->initialized=true;return true;
}
bool ValidateOwnedBlueprint(const Owner* o,std::uintptr_t pointer,std::uint64_t* id) noexcept {
    if(!o || !o->initialized || !pointer)return false;
    const auto begin=reinterpret_cast<std::uintptr_t>(o->copies);
    if(pointer<begin || (pointer-begin)%sizeof(BlueprintCopy))return false;
    const auto index=(pointer-begin)/sizeof(BlueprintCopy);if(index>=o->copy_count)return false;
    const auto& copy=o->copies[index];if(!copy.published || std::memcmp(copy.native,copy.captured,kBlueprintBytes))return false;
    if(id)*id=copy.id;return true;
}
Result PinBeforeSessionDestroy(Owner* o,const Access& a) noexcept {
    if(!o || !o->initialized || !a.gamedll_base || a.gamedll_base>UINTPTR_MAX-0x2082000 || !a.read || !a.fence || !a.compare_pointer || !a.borrowers)return Result::InvalidArgument;
    const auto initial=o->copy_count;o->work_count=0;
    if(std::memcmp(a.sha256,curves::kPlayerGameDllSha256,65))return o->result=Result::ProfileMismatch;
    Fence before={};if(!a.fence(a.user,&before) || !FenceValid(a,before))return o->result=Result::WrongScope;
    ContextStamp contexts[kMaximumOwners]={};std::size_t context_count=0;
    VectorStamp vectors[15*kMaximumOwners]={};std::size_t vector_count=0;
    for(std::size_t p=0;p<before.player_count;++p){const auto state=before.players[p].state;std::uintptr_t vt=0,context=0;
        if(!Read(a,state,&vt,8) || vt!=a.gamedll_base+0x17E2FF8 || !Read(a,state+0x468,&context,8))return Finish(o,Result::ReadFailed,initial,false);
        contexts[context_count++]={state,context};if(!context)continue;if(context>UINTPTR_MAX-4360)return Finish(o,Result::InvalidGraph,initial,false);
        bool duplicate=false;for(std::size_t i=0;i+1<context_count;++i)if(contexts[i].context==context)duplicate=true;if(duplicate)continue;
        for(unsigned group=0;group<5;++group)for(unsigned slot=0;slot<3;++slot){const auto header=context+24+160ULL*group+24ULL*slot;Header h={};
            if(!Vector(a,header,&h))return Finish(o,Result::InvalidGraph,initial,false);
            vectors[vector_count++]={header,h};
            for(std::uint32_t i=0;i<h.count;++i){const auto item=h.data+192ULL*i;std::uintptr_t item_vt=0,params=0;
                if(!Read(a,item+24,&item_vt,8) || item_vt!=a.gamedll_base+kItemContextVtableRva || !Read(a,item+64,&params,8))
                    return Finish(o,Result::InvalidGraph,initial,false);
                if(!params)continue;
                if(ValidateOwnedBlueprint(o,params))continue;
                const auto own_begin=reinterpret_cast<std::uintptr_t>(o->copies);
                if(params>=own_begin && params<own_begin+o->copy_count*sizeof(BlueprintCopy))return Finish(o,Result::OwnedPayloadChanged,initial,false);
                if(o->work_count==o->work_capacity)return Finish(o,Result::CapacityExceeded,initial,false);
                o->work[o->work_count++]={item+64,params,0};
            }
        }
    }
    Sort(o->work,o->work_count);
    std::uintptr_t prior_source=0,desired=0;std::size_t unique=0;
    for(std::size_t i=0;i<o->work_count;++i){auto binding=o->work[i];
        if(i && binding.address==o->work[i-1].address && binding.expected==o->work[i-1].expected)continue;
        if(binding.expected!=prior_source){
            if(o->copy_count==o->copy_capacity)return Finish(o,Result::CapacityExceeded,initial,false);
            auto& copy=o->copies[o->copy_count];copy={};
            if(!Read(a,binding.expected,copy.native,kBlueprintBytes))return Finish(o,Result::ReadFailed,initial,false);
            std::uintptr_t vt=0;std::memcpy(&vt,copy.native,8);std::memcpy(&copy.id,copy.native+8,8);
            if(vt!=a.gamedll_base+kBlueprintVtableRva || !SourceOwns(a,before.blueprint_store,copy.id,binding.expected))
                return Finish(o,Result::UnownedBlueprint,initial,false);
            std::memcpy(copy.captured,copy.native,kBlueprintBytes);copy.original=binding.expected;copy.session=before.session;
            copy.epoch=before.epoch;copy.teardown_generation=before.teardown_generation;
            desired=reinterpret_cast<std::uintptr_t>(copy.native);prior_source=binding.expected;++o->copy_count;
        }
        binding.desired=desired;o->work[unique++]=binding;
    }
    o->work_count=unique;
    auto check=Recheck(a,before,vectors,vector_count,contexts,context_count);
    if(check!=Result::Succeeded)return Finish(o,check,initial,false);
    for(std::size_t i=initial;i<o->copy_count;++i){std::uint8_t current[kBlueprintBytes];const auto& copy=o->copies[i];
        if(!Read(a,copy.original,current,kBlueprintBytes) || std::memcmp(current,copy.captured,kBlueprintBytes) ||
            !SourceOwns(a,before.blueprint_store,copy.id,copy.original))return Finish(o,Result::OwnerChanged,initial,false);}
    for(std::size_t i=0;i<o->work_count;++i){std::uintptr_t value=0;
        if(!Read(a,o->work[i].address,&value,8) || value!=o->work[i].expected)return Finish(o,Result::OwnerChanged,initial,false);}
    // Once stores may have begun, every reserved payload remains owned even if
    // a store or rollback is uncertain. Native teardown still forwards once.
    for(std::size_t i=initial;i<o->copy_count;++i)o->copies[i].published=true;
    for(std::size_t i=0;i<o->work_count;++i){const auto& w=o->work[i];
        if(!a.compare_pointer(a.user,w.address,w.expected,w.desired))return Finish(o,Result::WriteFailed,initial,true);}
    check=Recheck(a,before,vectors,vector_count,contexts,context_count);
    if(check!=Result::Succeeded)return Finish(o,check,initial,true);
    for(std::size_t i=0;i<o->work_count;++i){std::uintptr_t value=0;
        if(!Read(a,o->work[i].address,&value,8) || value!=o->work[i].desired || !ValidateOwnedBlueprint(o,value))
            return Finish(o,Result::WriteFailed,initial,true);}
    o->last_fence=before;return Finish(o,Result::Succeeded,initial,true);
}
bool NativeComparePointer(void*,std::uintptr_t address,std::uintptr_t expected,std::uintptr_t desired) noexcept {
    if(!address || address%alignof(std::uintptr_t))return false;
    __try {return dluma_atomic_pointer_compare_exchange(reinterpret_cast<void* volatile*>(address),reinterpret_cast<void*>(desired),
        reinterpret_cast<void*>(expected))==reinterpret_cast<void*>(expected);}
    __except(1){return false;}
}
} // namespace dluma::fixes::custom_map_asset_isolation::stored_blueprints
