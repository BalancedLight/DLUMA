#define _HAS_EXCEPTIONS 0
#include "PlayerIntrinsicProgressSnapshot.h"
#include <cstring>
#include <initializer_list>
namespace dluma::fixes::custom_map_asset_isolation::player_intrinsic_progress {
namespace {
namespace stored=player_stored_progress;
template<class T>T Value(const unsigned char* body,std::size_t offset)noexcept{T value={};std::memcpy(&value,body+offset,sizeof(value));return value;}
bool Read(const Reader& r,std::uintptr_t base,std::size_t offset,void* out,std::size_t size)noexcept{return base&&offset<=UINTPTR_MAX-base&&size<=UINTPTR_MAX-base-offset&&r.read(r.user,base+offset,out,size);}
bool Add(stored::Stream& s,std::uintptr_t base,std::size_t offset,std::size_t size)noexcept{return base&&offset<=UINTPTR_MAX-base&&stored::WriteStream(&s,reinterpret_cast<const void*>(base+offset),size)==size;}
bool AddBytes(stored::Stream& s,const void* bytes,std::size_t size)noexcept{return stored::WriteStream(&s,bytes,size)==size;}
struct Header{std::uintptr_t data;std::uint32_t count,capacity;};
bool Vector(const Reader& r,stored::Stream& s,std::uintptr_t object,std::size_t offset,std::size_t stride,Header* output=nullptr)noexcept{
 Header h={},after={};if(!Read(r,object,offset,&h,sizeof(h))||h.count>h.capacity||h.count>kNodeLimit||(h.count&&!h.data))return false;
 if(!AddBytes(s,&h.count,4)||(h.count&&!Add(s,h.data,0,static_cast<std::size_t>(h.count)*stride))||!Read(r,object,offset,&after,sizeof(after))||std::memcmp(&h,&after,sizeof(h)))return false;
 if(output)*output=h;return true;
}
bool Selector(const Reader& r,stored::Stream& s,std::uintptr_t state,std::size_t offset)noexcept{
 Header h={};if(!Read(r,state,offset,&h,sizeof(h))||h.count>255||h.count>h.capacity||(h.count&&!h.data))return false;
 return AddBytes(s,&h.count,4)&&(!h.count||Add(s,h.data,0,h.count));
}
bool List(const Reader& r,stored::Stream& s,std::uintptr_t state,std::size_t offset,std::size_t payload)noexcept{
 std::uintptr_t first=0,last=0;std::uint32_t count=0;
 if(!Read(r,state,offset,&first,8)||!Read(r,state,offset+8,&last,8)||!Read(r,state,offset+16,&count,4)||count>kNodeLimit||
  ((count==0)!=(first==0))||((count==0)!=(last==0))||!AddBytes(s,&count,4))return false;
 std::uintptr_t previous=0,current=first,seen[kNodeLimit]={};
 for(unsigned i=0;i<count;++i){std::uintptr_t links[2]={};if(current<payload||!Read(r,current,0,links,sizeof(links))||links[1]!=previous)return false;
  for(unsigned j=0;j<i;++j)if(seen[j]==current)return false;
  seen[i]=current;if(!Add(s,current-payload,0,payload))return false;previous=current;current=links[0];}
 return !current&&previous==last;
}
bool Skills(const Reader& r,stored::Stream& s,std::uintptr_t state)noexcept{
 Header h={};if(!Read(r,state,216,&h,sizeof(h))||h.count>kNodeLimit||((h.count==0)!=(h.data==0))||!AddBytes(s,&h.count,4))return false;
 std::uintptr_t stack[kNodeLimit]={},parents[kNodeLimit]={},seen[kNodeLimit]={};unsigned pending=0,visited=0;
 if(h.data){stack[pending]=h.data;parents[pending++]=0;}
 while(pending){--pending;const auto link=stack[pending],parent=parents[pending];if(link<16||visited>=h.count)return false;
  for(unsigned i=0;i<visited;++i)if(seen[i]==link)return false;seen[visited++]=link;
  unsigned char node[40];if(!Read(r,link-16,0,node,sizeof(node))||Value<std::uintptr_t>(node,24)!=parent||!AddBytes(s,node,16))return false;
  for(const auto offset:{16U,32U}){const auto child=Value<std::uintptr_t>(node,offset);if(child){if(pending>=kNodeLimit)return false;stack[pending]=child;parents[pending++]=link;}}
 }
 return visited==h.count;
}
bool AttributeArrays(const Reader& r,stored::Stream& s,std::uintptr_t object,std::uintptr_t* defaults)noexcept{
 if(!object||!Read(r,object,32,defaults,8)||!*defaults||!AddBytes(s,defaults,8))return false;
 for(unsigned index=0;index<2;++index){unsigned char h[16],after[16];const unsigned offset=index*16;
  if(!Read(r,object,offset,h,sizeof(h)))return false;
  const auto data=Value<std::uintptr_t>(h,0);const auto count=Value<std::uint16_t>(h,8),capacity=Value<std::uint16_t>(h,10);
  if(count>capacity||count>kNodeLimit||(count&&!data)||!AddBytes(s,&count,2)||(count&&!Add(s,data,0,count*(index?1U:8U)))||
   !Read(r,object,offset,after,sizeof(after))||std::memcmp(h,after,sizeof(h)))return false;
 }
 return true;
}
enum class TreeKind { RewardMaps,RewardSpeakers,Fog,Pursuit };
bool UserTree(const Reader& r,stored::Stream& s,std::uintptr_t header,TreeKind kind,unsigned* budget)noexcept{
 Header h={};if(!Read(r,header,0,&h,sizeof(h))||h.count>kNodeLimit||((h.count==0)!=(h.data==0))||!AddBytes(s,&h.count,4))return false;
 std::uintptr_t stack[kNodeLimit]={},parents[kNodeLimit]={},seen[kNodeLimit]={};unsigned pending=0,visited=0;
 if(h.data){stack[pending]=h.data;parents[pending++]=0;}
 const unsigned payload=kind==TreeKind::Pursuit?24U:40U;
 while(pending){--pending;const auto link=stack[pending],parent=parents[pending];if(link<payload||visited>=h.count||!*budget)return false;--*budget;
  for(unsigned i=0;i<visited;++i)if(seen[i]==link)return false;seen[visited++]=link;
  std::uintptr_t links[3]={};if(!Read(r,link,0,links,sizeof(links))||links[1]!=parent)return false;
  switch(kind){
  case TreeKind::RewardMaps:
   /* Exact copied MapId contains its integer at +8. Its nested speaker map
    * starts at payload+16. No virtual ID getter is needed for this value. */
   if(!Add(s,link-32,0,4)||!UserTree(r,s,link-24,TreeKind::RewardSpeakers,budget))return false;break;
  case TreeKind::RewardSpeakers:
   if(!Selector(r,s,link-40,0)||!Vector(r,s,link-24,0,20))return false;break;
  case TreeKind::Fog:{std::uint32_t bits=0;std::uintptr_t bytes=0;
   if(!Read(r,link-32,0,&bits,4)||bits>128U*1024U*1024U||!Read(r,link-24,0,&bytes,8)||!Add(s,link-40,0,4)||!AddBytes(s,&bits,4)||
    (bits&&!Add(s,bytes,0,(static_cast<std::size_t>(bits)+7)/8)))return false;break;}
  case TreeKind::Pursuit:
   if(!Selector(r,s,link-24,0)||!Add(s,link-8,0,3))return false;break;
  }
  for(const auto child:{links[0],links[2]})if(child){if(pending>=kNodeLimit)return false;stack[pending]=child;parents[pending++]=link;}
 }
 Header after={};return visited==h.count&&Read(r,header,0,&after,sizeof(after))&&!std::memcmp(&h,&after,sizeof(h));
}
bool Once(const Reader& r,std::uintptr_t state,std::uint64_t generation,Snapshot* out)noexcept{
 *out={};out->state=state;out->owner_generation=generation;unsigned char body[kStateBytes],after[kStateBytes];
 if(!Read(r,state,0,body,sizeof(body))||Value<std::uintptr_t>(body,0)!=r.gamedll_base+0x17E2FF8||Value<std::uintptr_t>(body,8)!=r.gamedll_base+0x17E3788||
  Value<std::uintptr_t>(body,16)!=r.gamedll_base+0x17E37B0||Value<std::uintptr_t>(body,48)!=r.gamedll_base+0x17E37C0)return false;
 const auto rows=Value<std::uintptr_t>(body,64);if(!rows||Value<std::uint32_t>(body,72)!=8||Value<std::uint32_t>(body,76)<8)return false;
 out->cached_context=Value<std::uintptr_t>(body,1128);out->attributes=Value<std::uintptr_t>(body,1080);
 for(unsigned field=0;field<kFieldCount;++field){stored::Stream s={};stored::InitializeStream(&s,r.maximum_section_bytes,r.read,r.user);if(!AddBytes(s,&field,4))return false;bool ok=true;
  switch(static_cast<Field>(field)){
  case Field::Identity:ok=Add(s,state,88,4)&&Add(s,state,1056,20)&&Selector(r,s,state,176)&&Selector(r,s,state,192);break;
  case Field::Progression:
   for(unsigned row=0;row<8&&ok;++row)ok=Add(s,rows,row*32+8,4)&&Add(s,rows,row*32+20,2)&&Add(s,rows,row*32+24,8);
   ok=ok&&Add(s,state,288,2)&&Add(s,state,292,16)&&Add(s,state,1112,8);break;
  case Field::Skills:ok=Skills(r,s,state);break;
  case Field::Buffs:{Header timed={};ok=Vector(r,s,state,240,4)&&Vector(r,s,state,264,24,&timed);
   for(unsigned i=0;ok&&i<timed.count;++i){std::uintptr_t ptr=0;ok=Read(r,timed.data,24*i+8,&ptr,8);if(ptr)out->timed_buff_definitions[out->timed_buff_definition_count++]=ptr;}break;}
  case Field::Statistics:ok=Add(s,state,308,0x238);break;
  case Field::AppearanceUnlocks:ok=Vector(r,s,state,96,4)&&Vector(r,s,state,120,4)&&Vector(r,s,state,1008,2);break;
  case Field::MapHistory:ok=List(r,s,state,144,16)&&List(r,s,state,928,8)&&Vector(r,s,state,1088,4);break;
  case Field::Attributes:ok=AttributeArrays(r,s,out->attributes,&out->attribute_defaults);break;
  case Field::ProtectedBody:{unsigned char protected_body[kStateBytes];std::memcpy(protected_body,body,sizeof(body));protected_body[290]&=0xFC;
   std::memset(protected_body+1136,0,24);std::memset(protected_body+1164,0,32);unsigned budget=kNodeLimit;
   ok=AddBytes(s,protected_body,sizeof(protected_body))&&UserTree(r,s,state+880,TreeKind::RewardMaps,&budget)&&
    UserTree(r,s,state+960,TreeKind::Fog,&budget)&&UserTree(r,s,state+984,TreeKind::Pursuit,&budget);break;}
  }
  if(!ok||!stored::FinishStream(&s,&out->fields[field]))return false;
 }
 if(Value<std::uint32_t>(body,1040))out->unqualified_graphs|=AuxiliaryVectors;
 Header transient={};stored::Stream transient_stream={};stored::Digest transient_digest={};stored::InitializeStream(&transient_stream,r.maximum_section_bytes,r.read,r.user);
 if(!Vector(r,transient_stream,state,904,8,&transient)||!stored::FinishStream(&transient_stream,&transient_digest))return false;
 out->transient_actor_count=transient.count;std::memcpy(&out->transient_actor_fingerprint,transient_digest.sha256,8);
 if(transient.count)out->unqualified_graphs|=TransientActors;
 if(!Read(r,state,0,after,sizeof(after))||std::memcmp(body,after,sizeof(body)))return false;
 out->intrinsic_fields_complete=true;out->owned_user_graph_complete=out->unqualified_graphs==0;return true;
}
}
bool Equal(const Snapshot& a,const Snapshot& b)noexcept{
 if(!a.intrinsic_fields_complete||!b.intrinsic_fields_complete||a.state!=b.state||a.owner_generation!=b.owner_generation||a.cached_context!=b.cached_context||a.attributes!=b.attributes||a.attribute_defaults!=b.attribute_defaults||
  a.unqualified_graphs!=b.unqualified_graphs||a.owned_user_graph_complete!=b.owned_user_graph_complete||a.timed_buff_definition_count!=b.timed_buff_definition_count||
  a.transient_actor_count!=b.transient_actor_count||a.transient_actor_fingerprint!=b.transient_actor_fingerprint||
  std::memcmp(a.timed_buff_definitions,b.timed_buff_definitions,a.timed_buff_definition_count*sizeof(std::uintptr_t)))return false;
 for(unsigned i=0;i<kFieldCount;++i)if(!a.fields[i].complete||!b.fields[i].complete||a.fields[i].bytes!=b.fields[i].bytes||std::memcmp(a.fields[i].sha256,b.fields[i].sha256,32))return false;
 return true;
}
bool Capture(const Reader& r,std::uintptr_t state,std::uint64_t generation,Snapshot* out)noexcept{
 if(!out)return false;*out={};if(!r.read||!r.read_fence||!r.exact_image_and_section_routes||!state||!generation||!r.maximum_section_bytes)return false;
 stored::Fence before={},after={};if(!r.read_fence(r.user,&before)||!before.exclusive_preservation_owner||before.player_state!=state||before.owner_generation!=generation||
  before.current_session||before.local_player_di||before.thread_id!=GetCurrentThreadId())return false;
 Snapshot second={};if(!Once(r,state,generation,out)||!Once(r,state,generation,&second)||!Equal(*out,second)||!r.read_fence(r.user,&after)||
  !after.exclusive_preservation_owner||after.player_state!=before.player_state||after.owner_generation!=before.owner_generation||after.mutation_revision!=before.mutation_revision||
  after.current_session||after.local_player_di||after.thread_id!=before.thread_id){*out={};return false;}return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::player_intrinsic_progress
