#define _HAS_EXCEPTIONS 0
#include "PlayerStoredProgressSnapshot.h"
#include <cstring>
#include <initializer_list>
namespace dluma::fixes::custom_map_asset_isolation::player_stored_progress {
namespace {
constexpr std::uintptr_t kStateVtable=0x17E2FF8,kCachedContext=0x468,kGameDiGlobal=0x1D07F88;
constexpr std::uintptr_t kSectionRvas[]={0xBB39C0,0xBB24E0,0xBB2950,0xBB2E10};
std::uint64_t __fastcall Unsupported(Stream* s)noexcept{if(s)s->failure=Failure::UnsupportedStreamOperation;return 0;}
std::uint64_t __fastcall NativeWrite(Stream* s,const void* p,std::uint64_t n)noexcept{return WriteStream(s,p,n);}
void* stream_vtable[]={reinterpret_cast<void*>(&Unsupported),reinterpret_cast<void*>(&Unsupported),
 reinterpret_cast<void*>(&Unsupported),reinterpret_cast<void*>(&Unsupported),reinterpret_cast<void*>(&Unsupported),
 reinterpret_cast<void*>(&Unsupported),reinterpret_cast<void*>(&Unsupported),reinterpret_cast<void*>(&NativeWrite)};
bool Fail(Snapshot* s,Failure f)noexcept{if(s){s->failure=f;s->stored_sections_complete=false;}return false;}
bool Read(const Reader& r,std::uintptr_t base,std::size_t offset,void* out,std::size_t size)noexcept{
 return base&&offset<=UINTPTR_MAX-base&&size<=UINTPTR_MAX-(base+offset)&&r.read(r.user,base+offset,out,size);
}
bool SameFence(const Fence& a,const Fence& b)noexcept{
 return a.exclusive_preservation_owner&&b.exclusive_preservation_owner&&a.player_state==b.player_state&&
 a.owner_generation==b.owner_generation&&a.mutation_revision==b.mutation_revision&&a.thread_id==b.thread_id&&
 !a.current_session&&!b.current_session&&!a.local_player_di&&!b.local_player_di;
}
struct Header {std::uintptr_t data;std::uint32_t count,capacity;};
bool Vector(const Reader& r,std::uintptr_t context,std::size_t offset,std::size_t stride,Header* h)noexcept{
 if(!Read(r,context,offset,h,sizeof(*h))||h->count>h->capacity||h->count>kMaximumVectorCount||h->capacity>1048576||
 (h->count&&!h->data))return false;
 if(!h->count)return true;
 const auto bytes=static_cast<std::uint64_t>(h->count)*stride;
 if(bytes>UINTPTR_MAX-h->data)return false;
 unsigned char scratch[4096];
 for(std::uint64_t at=0;at<bytes;){const auto n=static_cast<std::size_t>(bytes-at<sizeof(scratch)?bytes-at:sizeof(scratch));
  if(!r.read(r.user,h->data+static_cast<std::uintptr_t>(at),scratch,n))return false;at+=n;}
 return true;
}
bool Preflight(const Reader& r,std::uintptr_t context)noexcept{
 Header h={};
 for(unsigned group=0;group<5;++group){
  for(unsigned slot=0;slot<3;++slot)
   if(!Vector(r,context,24+160*group+24*slot,192,&h)||!Vector(r,context,824+120*group+24*slot,120,&h))return false;
  if(!Vector(r,context,96+160*group,8,&h)||!Vector(r,context,896+120*group,8,&h)||
     !Vector(r,context,120+160*group,4,&h)||!Vector(r,context,920+120*group,4,&h)||
     !Vector(r,context,144+160*group,2,&h))return false;
 }
 for(const auto offset:{1424U,1448U,1472U,1496U,1520U,1544U,3520U,3544U,3568U})
  if(!Vector(r,context,offset,2,&h))return false;
 if(!Vector(r,context,1568,1,&h)||!Vector(r,context,1592,4,&h)||!Vector(r,context,1616,4,&h))return false;
 /* Top-level intrusive maps must have a coherent header. Native section
  * traversal additionally requires the caller's existing validated owner
  * graph and closed writer gate; a count alone is not that graph proof. */
 for(const auto offset:{0U,2640U,2664U}){
  if(!Read(r,context,offset,&h,sizeof(h))||h.count>kMaximumVectorCount||((h.count==0)!=(h.data==0)))return false;
 }
 return true;
}
bool AddMemory(Stream* stream,std::uintptr_t base,std::size_t offset,std::size_t n)noexcept{
 return offset<=UINTPTR_MAX-base&&WriteStream(stream,reinterpret_cast<void*>(base+offset),n)==n;
}
bool AddVector(const Reader& r,Stream* stream,std::uintptr_t context,std::size_t offset,std::size_t stride)noexcept{
 Header h={};if(!Vector(r,context,offset,stride,&h))return false;
 const auto size=static_cast<std::uint64_t>(h.count)*stride;
 return WriteStream(stream,&h.count,sizeof(h.count))==sizeof(h.count)&&WriteStream(stream,reinterpret_cast<void*>(h.data),size)==size;
}
bool CaptureSection(const Reader& r,Section section,std::uintptr_t context,Digest* digest)noexcept{
 Stream stream={};InitializeStream(&stream,r.maximum_section_bytes,r.read,r.user);
 bool ok=true;
 switch(section){
 case Section::Inventory:case Section::Quests:case Section::Rewards:case Section::BringItems:
  ok=r.serialize_section(r.user,section,context,&stream);break;
 case Section::QuickSlots:
  ok=AddMemory(&stream,context,2622,4)&&AddMemory(&stream,context,2636,4);
  for(unsigned i=0;i<6&&ok;++i)ok=AddVector(r,&stream,context,1424+24*i,2);
  if(ok)ok=AddVector(r,&stream,context,1568,1);break;
 case Section::Equipment:
  for(unsigned i=0;i<3&&ok;++i)ok=AddVector(r,&stream,context,3520+24*i,2);break;
 case Section::Wallet:ok=AddMemory(&stream,context,3480,40)&&AddMemory(&stream,context,3592,24);break;
 case Section::Levels:ok=AddMemory(&stream,context,2480,24)&&AddMemory(&stream,context,2504,120)&&AddMemory(&stream,context,3720,8);break;
 case Section::Appearances:ok=AddMemory(&stream,context,2472,8)&&AddVector(r,&stream,context,1592,4)&&AddVector(r,&stream,context,1616,4);break;
 default:ok=false;break;
 }
 return ok&&FinishStream(&stream,digest);
}
}
void InitializeStream(Stream* s,std::uint64_t limit,ReadFn read,void* user)noexcept{
 if(!s)return;std::memset(s,0,sizeof(*s));s->vtable=stream_vtable;s->limit=limit;s->read=read;s->user=user;dluma_sha256_init(&s->hash);
}
std::uint64_t WriteStream(Stream* s,const void* data,std::uint64_t size)noexcept{
 if(!s||s->failure!=Failure::None)return 0;
 if(!s->read||s->bytes>s->limit||size>s->limit-s->bytes){s->failure=Failure::OutputLimit;return 0;}
 const auto p=reinterpret_cast<std::uintptr_t>(data);
 if(size&&(!p||size>UINTPTR_MAX-p)){s->failure=Failure::Unreadable;return 0;}
 for(std::uint64_t at=0;at<size;){const auto n=static_cast<std::size_t>(size-at<sizeof(s->scratch)?size-at:sizeof(s->scratch));
  if(!s->read(s->user,p+static_cast<std::uintptr_t>(at),s->scratch,n)){s->failure=Failure::Unreadable;return 0;}
  dluma_sha256_update(&s->hash,s->scratch,n);at+=n;}
 s->bytes+=size;return size;
}
bool FinishStream(Stream* s,Digest* out)noexcept{
 if(!s||!out||s->failure!=Failure::None)return false;*out={};dluma_sha256_final(&s->hash,out->sha256);out->bytes=s->bytes;out->complete=true;return true;
}
bool CaptureInternal(const Reader& r,std::uintptr_t state,std::uint64_t generation,Snapshot* out,bool native_sections)noexcept{
 if(!out)return false;*out={};out->player_state=state;out->owner_generation=generation;
 if(!r.read||!r.read_fence||(native_sections&&!r.serialize_section)||!state||!generation||!r.maximum_section_bytes||!r.exact_image_and_section_routes)return Fail(out,Failure::InvalidArgument);
 Fence before={},after={};
 if(!r.read_fence(r.user,&before)||!before.exclusive_preservation_owner)return Fail(out,Failure::QuiescenceUnavailable);
 if(before.thread_id!=GetCurrentThreadId())return Fail(out,Failure::WrongThread);
 if(before.current_session||before.local_player_di)return Fail(out,Failure::ActiveSession);
 if(before.player_state!=state||before.owner_generation!=generation)return Fail(out,Failure::OwnerChanged);
 std::uintptr_t vtable=0,context=0;
 if(!Read(r,state,0,&vtable,sizeof(vtable))||vtable!=r.gamedll_base+kStateVtable||!Read(r,state,kCachedContext,&context,sizeof(context)))return Fail(out,Failure::Unreadable);
 out->context=context;
 if(!context){out->cached_context_absent=true;if(!r.read_fence(r.user,&after)||!SameFence(before,after))return Fail(out,Failure::StateChanged);return true;}
 unsigned char body_before[kContextBytes],body_after[kContextBytes];
 if(!Read(r,context,0,body_before,sizeof(body_before))||(native_sections&&!Preflight(r,context)))return Fail(out,Failure::InvalidHeader);
 for(unsigned i=0;i<static_cast<unsigned>(Section::Count);++i){
  const auto section=static_cast<Section>(i);
  if(!native_sections&&(section==Section::Inventory||section==Section::Quests||section==Section::Rewards||section==Section::BringItems))continue;
  Digest second={};if(!CaptureSection(r,static_cast<Section>(i),context,&out->sections[i])||!CaptureSection(r,static_cast<Section>(i),context,&second))return Fail(out,Failure::NativeFailure);
  if(second.bytes!=out->sections[i].bytes||std::memcmp(second.sha256,out->sections[i].sha256,32))return Fail(out,Failure::StateChanged);
  out->available_section_mask|=1U<<i;
 }
 std::uintptr_t current=0;
 if(!Read(r,state,kCachedContext,&current,sizeof(current))||current!=context||!Read(r,context,0,body_after,sizeof(body_after))||
    std::memcmp(body_before,body_after,sizeof(body_before))||!r.read_fence(r.user,&after)||!SameFence(before,after))return Fail(out,Failure::StateChanged);
 out->stored_sections_complete=out->available_section_mask==((1U<<static_cast<unsigned>(Section::Count))-1U);return true;
}
bool Capture(const Reader& r,std::uintptr_t state,std::uint64_t generation,Snapshot* out)noexcept{return CaptureInternal(r,state,generation,out,true);}
bool CaptureDirect(const Reader& r,std::uintptr_t state,std::uint64_t generation,Snapshot* out)noexcept{return CaptureInternal(r,state,generation,out,false);}
bool Equal(const Snapshot& a,const Snapshot& b)noexcept{
 if(a.player_state!=b.player_state||a.context!=b.context||a.owner_generation!=b.owner_generation||a.failure!=Failure::None||b.failure!=Failure::None||
    a.cached_context_absent!=b.cached_context_absent||a.stored_sections_complete!=b.stored_sections_complete||a.available_section_mask!=b.available_section_mask)return false;
 if(a.cached_context_absent)return true;if(!a.available_section_mask)return false;
 for(unsigned i=0;i<static_cast<unsigned>(Section::Count);++i)if((a.available_section_mask&(1U<<i))&&(!a.sections[i].complete||!b.sections[i].complete||a.sections[i].bytes!=b.sections[i].bytes||std::memcmp(a.sections[i].sha256,b.sections[i].sha256,32)))return false;
 return true;
}
bool ValidateNativeTargets(NativeTargets* t,const char* sha)noexcept{
 if(!t)return false;t->validated=false;
 constexpr char expected[]="cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95";
 if(!sha)return false;
 for(unsigned i=0;i<64;++i){const char value=sha[i]>='A'&&sha[i]<='F'?static_cast<char>(sha[i]+('a'-'A')):sha[i];if(value!=expected[i])return false;}
 if(sha[64]||!t->gamedll_base||!t->read||t->gamedll_base>UINTPTR_MAX-kGameDiGlobal-8)return false;
 constexpr unsigned char inventory[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57};
 constexpr unsigned char tree[]={0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xfa};
 unsigned char bytes[16];for(unsigned i=0;i<4;++i)if(!t->read(t->user,t->gamedll_base+kSectionRvas[i],bytes,16)||std::memcmp(bytes,i?tree:inventory,16))return false;
 t->validated=true;return true;
}
bool SerializeNativeSection(const NativeTargets& t,Section section,std::uintptr_t context,Stream* stream)noexcept{
 if(!t.validated||!context||!stream)return false;
 if(section==Section::Inventory){using Fn=bool(__fastcall*)(std::uintptr_t,Stream*,int);const auto fn=reinterpret_cast<Fn>(t.gamedll_base+kSectionRvas[0]);
  for(int group=0;group<5;++group)if(!fn(context,stream,group)||stream->failure!=Failure::None)return false;return true;}
 unsigned index=section==Section::Quests?1:section==Section::Rewards?2:section==Section::BringItems?3:0;
 if(!index)return false;
 return reinterpret_cast<bool(__fastcall*)(std::uintptr_t,Stream*)>(t.gamedll_base+kSectionRvas[index])(context,stream)&&stream->failure==Failure::None;
}
} // namespace dluma::fixes::custom_map_asset_isolation::player_stored_progress
