#define _HAS_EXCEPTIONS 0
#include "PlayerProcessOwnerRebuild.h"
#include "../../dlw_win32.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::player_process_rebuild {
namespace {
bool Fail(Rebuild* r,Failure f)noexcept{if(r)r->failure=f;return false;}
bool Digest(const Protection& p)noexcept{unsigned char bits=0;for(auto b:p.sha256)bits|=b;return bits&&p.complete&&(p.fields&kRequiredProtection)==kRequiredProtection;}
bool EnvironmentValid(const Environment& e)noexcept{
 if(!e.exact_process_owner_graph||!e.player_readers_closed||!e.player_writers_closed||
 e.thread_id!=GetCurrentThreadId()||!e.transaction||!e.admission_lease||!e.game_di||!e.cache||e.current_session||e.local_player_di||
 e.live_actor_state_count||e.owner_count>kOwnerCapacity||e.owner_count==1)return false;
 for(unsigned i=0;i<e.owner_count;++i){const auto& o=e.owners[i];if(!o.state||!o.lifetime_generation||!o.alias_slot||!o.game_di_member||!o.row_table||(o.kind!=OwnerKind::LocalProcess&&o.kind!=OwnerKind::CharacterProcess))return false;
  for(unsigned j=0;j<i;++j)if(o.kind==e.owners[j].kind||o.state==e.owners[j].state||o.row_table==e.owners[j].row_table)return false;}
 return true;
}
bool SameOwners(const Environment& a,const Environment& b)noexcept{
 if(!EnvironmentValid(b)||a.owner_count!=b.owner_count||a.thread_id!=b.thread_id||a.transaction!=b.transaction||
 a.admission_lease!=b.admission_lease||a.game_di!=b.game_di||a.cache!=b.cache||a.inventory_revision!=b.inventory_revision)return false;
 for(unsigned i=0;i<a.owner_count;++i){const auto& x=a.owners[i];const auto& y=b.owners[i];
  if(x.state!=y.state||x.lifetime_generation!=y.lifetime_generation||x.alias_slot!=y.alias_slot||x.game_di_member!=y.game_di_member||x.row_table!=y.row_table||x.kind!=y.kind)return false;}
 return true;
}
bool Current(Rebuild* r)noexcept{Environment e={};return r->operations.capture_environment(r->operations.user,&e)&&SameOwners(r->plan.owners_before,e);}
bool ReadyOps(const Operations& o)noexcept{return o.exact_native_routes&&o.capture_environment&&o.capture_protection&&o.verify_baseline_definition_owner&&o.begin_exact_permit&&o.end_exact_permit&&o.reset_derived_cache&&o.materialize_owner_presets&&o.resolve_seed&&o.seed_rows&&o.reset_appearance_defaults&&o.initialize_variables&&o.verify_derived_baseline;}
bool Permit(Rebuild* r,Operation op,const Owner* owner)noexcept{
 if(!Current(r))return Fail(r,Failure::OwnersChanged);
 if(!r->operations.begin_exact_permit(r->operations.user,&r->plan,op,owner))return Fail(r,Failure::PermitRejected);
 return true;
}
bool End(Rebuild* r,bool result)noexcept{const bool transcript=r->operations.end_exact_permit(r->operations.user,result);return result&&transcript?true:Fail(r,Failure::NativeFailure);}
bool Read(const NativeBinding& b,std::uintptr_t base,std::size_t offset,void* out,std::size_t size)noexcept{
 return base&&offset<=UINTPTR_MAX-base&&size<=UINTPTR_MAX-(base+offset)&&b.read(b.user,base+offset,out,size);
}
bool NativeOwner(const NativeBinding& b,const Owner& owner)noexcept{
 if(owner.kind!=OwnerKind::LocalProcess&&owner.kind!=OwnerKind::CharacterProcess)return false;
 std::uintptr_t game=0,game_vtable=0,state_vtable=0,alias=0,member=0;
 const auto slot=owner.kind==OwnerKind::LocalProcess?0x1D07698U:0x1D078F0U;
 const auto offset=owner.kind==OwnerKind::LocalProcess?0x520U:0x528U;
 return Read(b,b.gamedll_base,0x1D07F88,&game,sizeof(game))&&game&&game<=UINTPTR_MAX-offset&&
 owner.alias_slot==b.gamedll_base+slot&&owner.game_di_member==game+offset&&
 Read(b,game,0,&game_vtable,sizeof(game_vtable))&&game_vtable==b.gamedll_base+0x157D5F8U&&
 Read(b,owner.state,0,&state_vtable,sizeof(state_vtable))&&state_vtable==b.gamedll_base+0x17E2FF8U&&
 Read(b,owner.alias_slot,0,&alias,sizeof(alias))&&Read(b,owner.game_di_member,0,&member,sizeof(member))&&alias==owner.state&&member==owner.state;
}
struct NativeString {std::uintptr_t data;std::uint32_t length,capacity;};
bool StringValue(const NativeBinding& b,std::uintptr_t address,NativeString* value,unsigned char (&bytes)[256])noexcept{
 return Read(b,address,0,value,sizeof(*value))&&value->length<=255&&value->length<=value->capacity&&
  (!value->length||(value->data&&Read(b,value->data,0,bytes,value->length)));
}
bool SameSelector(const NativeBinding& b,std::uintptr_t record,std::uintptr_t selected)noexcept{
 NativeString left={},right={};unsigned char a[256]={},c[256]={};
 if(!StringValue(b,record,&left,a))return false;
 if(!selected)return left.length==0;
 return StringValue(b,selected,&right,c)&&left.length==right.length&&!std::memcmp(a,c,left.length);
}
bool ActualCache(const NativeBinding& b,std::uintptr_t cache)noexcept{
 std::uintptr_t actual=0;return cache&&Read(b,b.gamedll_base,0x1D07210,&actual,sizeof(actual))&&actual==cache;
}
std::uintptr_t FindOwner(const NativeBinding& b,std::uintptr_t cache,const Owner& owner,std::uint32_t type)noexcept{
 return reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,const std::uint32_t*)>(b.gamedll_base+0x45AB80)(cache,owner.state+0xB0,owner.state+0xC0,&type);
}
}
void Initialize(Rebuild* r,const Operations* ops)noexcept{if(!r)return;std::memset(r,0,sizeof(*r));if(ops)r->operations=*ops;}
bool Prepare(Rebuild* r,BaselineKind kind,std::uint64_t fingerprint)noexcept{
 if(!r||r->prepared||r->mutation_started||r->executing||!fingerprint||!ReadyOps(r->operations)||(kind!=BaselineKind::NoStateAtStartup&&kind!=BaselineKind::StateSnapshot))return Fail(r,Failure::InvalidPlan);
 Environment first={},second={};if(!r->operations.capture_environment(r->operations.user,&first)||!EnvironmentValid(first))return Fail(r,Failure::EnvironmentUnqualified);
 if(!r->operations.verify_baseline_definition_owner(r->operations.user,&first,fingerprint))return Fail(r,Failure::BaselineDefinitionsUnavailable);
 r->plan.startup_kind=kind;r->plan.baseline_definition_fingerprint=fingerprint;r->plan.owners_before=first;
 for(unsigned i=0;i<first.owner_count;++i){Protection p={};if(!r->operations.capture_protection(r->operations.user,&first.owners[i],&r->plan.protected_before[i])||!Digest(r->plan.protected_before[i])||
 !r->operations.capture_protection(r->operations.user,&first.owners[i],&p)||!Digest(p)||p.fields!=r->plan.protected_before[i].fields||std::memcmp(p.sha256,r->plan.protected_before[i].sha256,32))return Fail(r,Failure::ProtectionIncomplete);}
 if(!r->operations.capture_environment(r->operations.user,&second)||!SameOwners(first,second))return Fail(r,Failure::OwnersChanged);
 r->prepared=true;r->failure=Failure::None;return true;
}
bool Execute(Rebuild* r)noexcept{
 if(!r||!r->prepared||r->executing||r->mutation_started||r->verified)return Fail(r,Failure::AlreadyStarted);
 struct Scope{bool& active;explicit Scope(bool& b):active(b){active=true;}~Scope(){active=false;}}scope(r->executing);
 if(!Current(r))return Fail(r,Failure::OwnersChanged);
 if(!r->operations.verify_baseline_definition_owner(r->operations.user,&r->plan.owners_before,r->plan.baseline_definition_fingerprint))return Fail(r,Failure::BaselineDefinitionsUnavailable);
 if(!Permit(r,Operation::ResetDerivedCache,nullptr))return false;
 r->mutation_started=true;
 if(!r->operations.reset_derived_cache(r->operations.user,r->plan.owners_before.cache)){(void)End(r,false);return false;}
 /* Native cache insertion uses realloc on inline records. Finish every
  * selector pair before retaining ANY final pointer. Otherwise another
  * owner's first missing selector could invalidate already seeded rows. */
 for(unsigned i=0;i<r->plan.owners_before.owner_count;++i)
  if(!r->operations.materialize_owner_presets(r->operations.user,r->plan.owners_before.cache,&r->plan.owners_before.owners[i])){(void)End(r,false);return Fail(r,Failure::SeedUnavailable);}
 if(!r->operations.verify_baseline_definition_owner(r->operations.user,&r->plan.owners_before,r->plan.baseline_definition_fingerprint)){(void)End(r,false);return Fail(r,Failure::BaselineDefinitionsUnavailable);}
 /* Keep the explicit reset permit across these planned native operations.
  * A closed external-reader gate is not bypassed by an inherited depth. */
 for(unsigned i=0;i<r->plan.owners_before.owner_count;++i)
  for(unsigned row=0;row<kRowCount;++row){auto& seed=r->seeds[i][row];if(!r->operations.resolve_seed(r->operations.user,r->plan.owners_before.cache,&r->plan.owners_before.owners[i],row,&seed)||
   !seed.preset||seed.row_type!=row||!seed.fresh_cache_membership||!seed.baseline_provider||seed.baseline_definition_fingerprint!=r->plan.baseline_definition_fingerprint){(void)End(r,false);return Fail(r,Failure::SeedUnavailable);}}
 if(!End(r,true))return false;
 /* No InitializeVariables call is allowed in this loop. It evaluates all
  * eight row pointers even while it is rebinding its first individual row. */
 for(unsigned i=0;i<r->plan.owners_before.owner_count;++i){const auto& owner=r->plan.owners_before.owners[i];
  if(!Permit(r,Operation::SeedAllRows,&owner))return false;
  const bool ok=r->operations.seed_rows(r->operations.user,&owner,r->seeds[i],kRowCount)&&r->operations.reset_appearance_defaults(r->operations.user,&owner);
  if(!End(r,ok))return Fail(r,Failure::SeedWriteFailed);++r->seeded_owners;}
 for(unsigned i=0;i<r->plan.owners_before.owner_count;++i){const auto& owner=r->plan.owners_before.owners[i];
  if(!Permit(r,Operation::InitializeVariables,&owner))return false;
  if(!End(r,r->operations.initialize_variables(r->operations.user,&owner)))return false;++r->initialized_owners;
  Protection p={};if(!r->operations.capture_protection(r->operations.user,&owner,&p)||!Digest(p)||
    p.fields!=r->plan.protected_before[i].fields||std::memcmp(p.sha256,r->plan.protected_before[i].sha256,32))return Fail(r,Failure::ProtectionChanged);
  if(!r->operations.verify_derived_baseline(r->operations.user,&owner,r->plan.baseline_definition_fingerprint))return Fail(r,Failure::DerivedStateMismatch);}
 if(!Current(r))return Fail(r,Failure::OwnersChanged);
 r->verified=true;return true;
}
bool ValidateFreshSeed(const NativeBinding& b,std::uintptr_t cache,std::uint32_t type,std::uintptr_t preset,const Owner* owner)noexcept{
 if(!b.exact_layout||!b.read||!cache||!preset||type>=kRowCount)return false;
 if(owner&&(!NativeOwner(b,*owner)||owner->state>UINTPTR_MAX-0xD0))return false;
 std::uintptr_t rows=0;struct Header{std::uintptr_t data;std::uint32_t size,capacity;}header={};
 if(!Read(b,cache,0,&rows,sizeof(rows))||!Read(b,rows,type*24,&header,sizeof(header))||header.size>4096||header.size>header.capacity||(header.size&&!header.data))return false;
 for(unsigned i=0;i<header.size;++i){const auto entry=header.data+64ULL*i;if(entry<header.data||entry>UINTPTR_MAX-64)return false;
  if(entry+32!=preset)continue;
  std::uint32_t recorded_type=UINT32_MAX;Header levels={};
  return Read(b,entry,60,&recorded_type,4)&&recorded_type==type&&
   SameSelector(b,entry,owner?owner->state+0xB0:0)&&SameSelector(b,entry+16,owner?owner->state+0xC0:0)&&
   Read(b,preset,0,&levels,sizeof(levels))&&levels.size<=512&&levels.size<=levels.capacity&&(!levels.size||levels.data);
 }
 return false;
}
bool ValidateNativeBinding(NativeBinding* b,const char* sha)noexcept{
 if(!b)return false;b->exact_layout=false;
 constexpr char expected[]="cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95";
 if(!sha)return false;
 for(unsigned i=0;i<64;++i){const char value=sha[i]>='A'&&sha[i]<='F'?static_cast<char>(sha[i]+('a'-'A')):sha[i];if(value!=expected[i])return false;}
 if(sha[64]||!b->gamedll_base||b->gamedll_base>UINTPTR_MAX-0x1D07F90||!b->read||!b->write)return false;
 constexpr std::uintptr_t rvas[]={0x45AC60,0x45AB80,0xBB86E0,0x45A920};
 constexpr unsigned char prefixes[][16]={
 {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},
 {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,0x49,0x63,0x01,0x45,0x33,0xd2},
 {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57},
 {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}};
 unsigned char bytes[16];for(unsigned i=0;i<4;++i)if(!b->read(b->user,b->gamedll_base+rvas[i],bytes,16)||std::memcmp(bytes,prefixes[i],16))return false;
 b->exact_layout=true;return true;
}
bool ResetNativeDerivedCache(const NativeBinding& b,std::uintptr_t cache)noexcept{
 if(!b.exact_layout||!ActualCache(b,cache))return false;
 reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(b.gamedll_base+0x45AC60)(cache);return true;
}
std::uintptr_t FindNativeEmptySeed(const NativeBinding& b,std::uintptr_t cache,std::uint32_t type)noexcept{
 if(!b.exact_layout||!ActualCache(b,cache)||type>=kRowCount)return 0;
 struct String{const char* data;std::uint32_t length,capacity;};const String empty={};
 const auto preset=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,const String*,const String*,const std::uint32_t*)>(b.gamedll_base+0x45AB80)(cache,&empty,&empty,&type);
 return ValidateFreshSeed(b,cache,type,preset)?preset:0;
}
bool MaterializeNativeOwnerPresets(const NativeBinding& b,std::uintptr_t cache,const Owner& owner)noexcept{
 if(!b.exact_layout||!ActualCache(b,cache)||!NativeOwner(b,owner)||owner.state>UINTPTR_MAX-0xD0)return false;
 NativeString selector={};unsigned char bytes[256]={};
 if(!StringValue(b,owner.state+0xB0,&selector,bytes)||!StringValue(b,owner.state+0xC0,&selector,bytes))return false;
 unsigned found=0;
 for(unsigned i=0;i<kRowCount;++i){const auto p=FindOwner(b,cache,owner,i);if(p){if(!ValidateFreshSeed(b,cache,i,p,&owner))return false;++found;}}
 /* A partial tuple is not a normal native cache publication. Do not append
  * duplicate rows in an attempt to repair an incompletely observed owner. */
 if(found&&found!=kRowCount)return false;
 if(!found)reinterpret_cast<void(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t)>(b.gamedll_base+0x45A920)(cache,owner.state+0xB0,owner.state+0xC0);
 for(unsigned i=0;i<kRowCount;++i)if(!ValidateFreshSeed(b,cache,i,FindOwner(b,cache,owner,i),&owner))return false;
 return true;
}
std::uintptr_t FindNativeOwnerSeed(const NativeBinding& b,std::uintptr_t cache,const Owner& owner,std::uint32_t type)noexcept{
 if(!b.exact_layout||!ActualCache(b,cache)||!NativeOwner(b,owner)||type>=kRowCount||owner.state>UINTPTR_MAX-0xD0)return 0;
 const auto preset=FindOwner(b,cache,owner,type);return ValidateFreshSeed(b,cache,type,preset,&owner)?preset:0;
}
bool InitializeNativeVariables(const NativeBinding& b,const Owner& owner)noexcept{
 if(!b.exact_layout||!owner.state||!NativeOwner(b,owner))return false;
 std::uintptr_t cache=0,rows=0;
 if(!Read(b,b.gamedll_base,0x1D07210,&cache,sizeof(cache))||!Read(b,owner.state,64,&rows,sizeof(rows))||rows!=owner.row_table)return false;
 for(unsigned i=0;i<kRowCount;++i){std::uintptr_t preset=0;if(!Read(b,rows,32*i,&preset,sizeof(preset))||!ValidateFreshSeed(b,cache,i,preset,&owner))return false;}
 reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(b.gamedll_base+0xBB86E0)(owner.state);return true;
}
bool SeedNativeRows(const NativeBinding& b,const Owner& owner,const Seed* seeds,std::size_t count)noexcept{
 if(!b.exact_layout||!b.read||!b.write||!seeds||count!=kRowCount||!owner.state||!owner.row_table||!owner.lifetime_generation||!NativeOwner(b,owner))return false;
 std::uintptr_t alias=0,member=0,rows=0,cache=0;
 if(!Read(b,owner.alias_slot,0,&alias,sizeof(alias))||!Read(b,owner.game_di_member,0,&member,sizeof(member))||alias!=owner.state||member!=owner.state||
 !Read(b,owner.state,64,&rows,sizeof(rows))||rows!=owner.row_table||rows>UINTPTR_MAX-256||!Read(b,b.gamedll_base,0x1D07210,&cache,sizeof(cache)))return false;
 for(unsigned i=0;i<kRowCount;++i)if(!seeds[i].preset||seeds[i].row_type!=i||!seeds[i].fresh_cache_membership||!seeds[i].baseline_provider||!seeds[i].baseline_definition_fingerprint||
  !ValidateFreshSeed(b,cache,i,seeds[i].preset,&owner))return false;
 for(unsigned i=0;i<kRowCount;++i)if(!b.write(b.user,rows+32*i,&seeds[i].preset,sizeof(seeds[i].preset)))return false;
 for(unsigned i=0;i<kRowCount;++i){std::uintptr_t value=0;if(!Read(b,rows,32*i,&value,sizeof(value))||value!=seeds[i].preset)return false;}
 return true;
}
bool ResetNativeAppearanceDefaults(const NativeBinding& b,const Owner& owner)noexcept{
 if(!b.exact_layout||!b.read||!b.write||!owner.state||owner.state>UINTPTR_MAX-1196||!NativeOwner(b,owner))return false;
 /* Exact ctor0xBB5D40; ParseAppearances0xBBC460 only overwrites present
  * script entries. Without these defaults custom-only omitted fields persist. */
 constexpr std::uint32_t defaults[]={0xFFFFFFFFU,0x3F800000U,0U,0xFFFFFFFFU,0U,0xFFFFFFFFU};
 constexpr std::uint32_t multipliers[]={0x3F800000U,0x3F800000U,0x3F800000U,0x3F800000U,0x3F800000U,0x3F800000U,0x3F800000U,0x3F800000U};
 if(!b.write(b.user,owner.state+1136,defaults,sizeof(defaults))||!b.write(b.user,owner.state+1164,multipliers,sizeof(multipliers)))return false;
 std::uint32_t first[6]={},second[8]={};
 return Read(b,owner.state,1136,first,sizeof(first))&&Read(b,owner.state,1164,second,sizeof(second))&&
  !std::memcmp(first,defaults,sizeof(first))&&!std::memcmp(second,multipliers,sizeof(second));
}
} // namespace dluma::fixes::custom_map_asset_isolation::player_process_rebuild
