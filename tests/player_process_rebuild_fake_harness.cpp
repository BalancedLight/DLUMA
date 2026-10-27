#include "fixes/custom_map_asset_isolation/PlayerProcessOwnerRebuild.h"
#include "dlw_win32.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace rebuild=dluma::fixes::custom_map_asset_isolation::player_process_rebuild;
namespace {
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
struct Fake {
 rebuild::Rebuild rebuild={};rebuild::Environment environment={};
 unsigned resets=0,initializes=0,seeds=0,permits=0,ends=0,materialized=0;
 bool baseline=true,allow=true,bindings=true,modify_user=false,seed_failure=false,all_seeded_before_evaluation=true;
 int bad_seed=-1;bool seeded[2]={};bool resolve_before_all_materialized=false,materialize_failure=false;
 Fake(){auto& e=environment;e.owner_count=2;e.thread_id=GetCurrentThreadId();e.inventory_revision=3;e.admission_lease=4;e.transaction=5;e.game_di=0x1000;e.cache=0x2000;e.exact_process_owner_graph=e.player_readers_closed=e.player_writers_closed=true;
 e.owners[0]={0x3000,0x4000,0x5000,0x6000,6,rebuild::OwnerKind::LocalProcess};e.owners[1]={0x7000,0x8000,0x9000,0xA000,7,rebuild::OwnerKind::CharacterProcess};}
};
bool Env(void* u,rebuild::Environment* e)noexcept{*e=static_cast<Fake*>(u)->environment;return true;}
bool Protection(void* u,const rebuild::Owner* o,rebuild::Protection* p)noexcept{auto& f=*static_cast<Fake*>(u);*p={};p->sha256[0]=static_cast<unsigned char>(11+static_cast<unsigned>(o->kind)+(f.modify_user&&f.initializes?1:0));p->fields=rebuild::kRequiredProtection;p->complete=true;return true;}
bool Baseline(void* u,const rebuild::Environment*,std::uint64_t fp)noexcept{return static_cast<Fake*>(u)->baseline&&fp==9;}
bool Begin(void* u,const rebuild::Plan*,rebuild::Operation,const rebuild::Owner*)noexcept{auto& f=*static_cast<Fake*>(u);++f.permits;return f.allow;}
bool End(void* u,bool value)noexcept{++static_cast<Fake*>(u)->ends;return value;}
bool Reset(void* u,std::uintptr_t)noexcept{++static_cast<Fake*>(u)->resets;return true;}
bool Materialize(void* u,std::uintptr_t,const rebuild::Owner*)noexcept{auto& f=*static_cast<Fake*>(u);++f.materialized;return !f.materialize_failure;}
bool Resolve(void* u,std::uintptr_t,const rebuild::Owner* owner,std::uint32_t type,rebuild::Seed* s)noexcept{auto& f=*static_cast<Fake*>(u);if(f.materialized!=f.environment.owner_count)f.resolve_before_all_materialized=true;*s={0xB000+f.materialized*0x1000+static_cast<unsigned>(owner->kind)*0x400+type*64,type,9,true,true};if(static_cast<int>(type)==f.bad_seed)s->preset=0;return true;}
bool Seed(void* u,const rebuild::Owner* o,const rebuild::Seed* s,std::size_t count)noexcept{auto& f=*static_cast<Fake*>(u);++f.seeds;if(f.seed_failure&&o->kind==rebuild::OwnerKind::CharacterProcess)return false;if(count!=8)return false;for(unsigned i=0;i<8;++i)if(!s[i].preset||s[i].row_type!=i)return false;f.seeded[static_cast<unsigned>(o->kind)]=true;return true;}
bool Appearance(void*,const rebuild::Owner*)noexcept{return true;}
bool Init(void* u,const rebuild::Owner*)noexcept{auto& f=*static_cast<Fake*>(u);++f.initializes;if(!f.seeded[0]||!f.seeded[1])f.all_seeded_before_evaluation=false;return true;}
bool Verify(void* u,const rebuild::Owner*,std::uint64_t)noexcept{return static_cast<Fake*>(u)->bindings;}
void Init(Fake& f){rebuild::Operations o={&f,Env,Protection,Baseline,Begin,End,Reset,Materialize,Resolve,Seed,Appearance,Init,Verify,true};rebuild::Initialize(&f.rebuild,&o);}
bool AllOwnersSeedBeforeEvaluation(){Fake f;Init(f);CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(rebuild::Execute(&f.rebuild));CHECK(f.all_seeded_before_evaluation&&!f.resolve_before_all_materialized&&f.materialized==2&&f.resets==1&&f.seeds==2&&f.initializes==2&&f.permits==5&&f.ends==5);return true;}
bool StartupAbsenceIsSemanticNotDeletion(){Fake f;Init(f);CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::NoStateAtStartup,9));CHECK(rebuild::Execute(&f.rebuild));CHECK(f.environment.owner_count==2&&f.rebuild.verified);return true;}
bool MissingRealSeedPreventsEvaluation(){Fake f;Init(f);f.bad_seed=3;CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.seeds==0&&f.initializes==0&&f.rebuild.mutation_started&&!f.rebuild.verified);return true;}
bool PartialSeedNeverEvaluated(){Fake f;Init(f);f.seed_failure=true;CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.seeds==2&&f.initializes==0&&!f.rebuild.verified);return true;}
bool ActualAdmissionRequired(){Fake f;Init(f);f.environment.player_readers_closed=false;CHECK(!rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));f.environment.player_readers_closed=true;f.environment.player_writers_closed=false;CHECK(!rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(f.resets==0);return true;}
bool LiveActorsRejectGlobalDestruction(){Fake f;Init(f);f.environment.live_actor_state_count=1;CHECK(!rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(f.resets==0);return true;}
bool PermitsNotDepthBypass(){Fake f;Init(f);CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));f.allow=false;CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.resets==0&&!f.rebuild.mutation_started);return true;}
bool OwnerGenerationChangesReject(){Fake f;Init(f);CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));++f.environment.owners[1].lifetime_generation;CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.resets==0);return true;}
bool ProtectedUserStateVerified(){Fake f;Init(f);f.modify_user=true;CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.rebuild.failure==rebuild::Failure::ProtectionChanged);return true;}
bool BaselineBindingsVerified(){Fake f;Init(f);f.bindings=false;CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.rebuild.failure==rebuild::Failure::DerivedStateMismatch);return true;}
bool BaselineDefinitionOwnerRequired(){Fake f;Init(f);f.baseline=false;CHECK(!rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(f.resets==0);return true;}
bool MaterializationFailureNeverBinds(){Fake f;Init(f);f.materialize_failure=true;CHECK(rebuild::Prepare(&f.rebuild,rebuild::BaselineKind::StateSnapshot,9));CHECK(!rebuild::Execute(&f.rebuild));CHECK(f.materialized==1&&f.seeds==0&&f.initializes==0&&f.ends==1);return true;}
struct Memory {
 static constexpr std::uintptr_t base=0x10000000;
 std::vector<unsigned char> bytes;rebuild::NativeBinding binding={};rebuild::Owner owner={};rebuild::Seed seeds[8]={};unsigned writes=0;
 template<class T>void Put(std::uintptr_t address,const T& value){std::memcpy(bytes.data()+address-base,&value,sizeof(value));}
 static bool Read(void* user,std::uintptr_t address,void* out,std::size_t size)noexcept{auto& m=*static_cast<Memory*>(user);if(address<base||address-base>m.bytes.size()||size>m.bytes.size()-(address-base))return false;std::memcpy(out,m.bytes.data()+address-base,size);return true;}
 static bool Write(void* user,std::uintptr_t address,const void* value,std::size_t size)noexcept{auto& m=*static_cast<Memory*>(user);if(address<base||address-base>m.bytes.size()||size>m.bytes.size()-(address-base))return false;std::memcpy(m.bytes.data()+address-base,value,size);++m.writes;return true;}
 Memory():bytes(0x1D08000){binding={base,Read,Write,this,true};const auto game=base+0x2000,cache=base+0x1000,types=base+0x1100,state=base+0x3000,rows=base+0x4000;
  owner={state,base+0x1D07698,game+0x520,rows,6,rebuild::OwnerKind::LocalProcess};
  Put(base+0x1D07F88,game);Put(base+0x1D07210,cache);Put(owner.alias_slot,state);Put(owner.game_di_member,state);Put(game,base+0x157D5F8);Put(state,base+0x17E2FF8);Put(state+64,rows);Put(cache,types);
  struct Header{std::uintptr_t data;std::uint32_t size,capacity;};
  for(unsigned i=0;i<8;++i){const auto entry=base+0x6000+64*i;Put(types+24*i,Header{entry,1,1});Put(entry+60,i);seeds[i]={entry+32,i,9,true,true};for(unsigned j=0;j<32;++j)bytes[rows-base+32*i+j]=static_cast<unsigned char>(i*32+j);}
 }
};
bool NativeSeedsPreserveAllNonPointerRowBytes(){Memory m;unsigned char before[256];CHECK(Memory::Read(&m,m.owner.row_table,before,sizeof(before)));CHECK(rebuild::SeedNativeRows(m.binding,m.owner,m.seeds,8));CHECK(m.writes==8);unsigned char after[256];CHECK(Memory::Read(&m,m.owner.row_table,after,sizeof(after)));for(unsigned i=0;i<8;++i){CHECK(!std::memcmp(before+32*i+8,after+32*i+8,24));std::uintptr_t p=0;std::memcpy(&p,after+32*i,8);CHECK(p==m.seeds[i].preset);}return true;}
bool NativeWrongTypeAndStaleSeedsCannotWrite(){Memory m;m.Put(m.seeds[4].preset+28,std::uint32_t{3});CHECK(!rebuild::SeedNativeRows(m.binding,m.owner,m.seeds,8));CHECK(m.writes==0);m.Put(m.seeds[4].preset+28,std::uint32_t{4});m.seeds[4].preset+=64;CHECK(!rebuild::SeedNativeRows(m.binding,m.owner,m.seeds,8));CHECK(m.writes==0);return true;}
bool NativeAliasMismatchCannotWrite(){Memory m;m.Put(m.owner.game_di_member,m.owner.state+16);CHECK(!rebuild::SeedNativeRows(m.binding,m.owner,m.seeds,8));CHECK(m.writes==0);return true;}
bool NativeSelectedTemplateMustMatch(){Memory m;struct String{std::uintptr_t data;std::uint32_t size,capacity;};const auto str=Memory::base+0x7000;m.Put(str,std::uint32_t{0x00636261});m.Put(m.owner.state+0xB0,String{str,3,3});CHECK(!rebuild::SeedNativeRows(m.binding,m.owner,m.seeds,8));CHECK(m.writes==0);for(auto& s:m.seeds)m.Put(s.preset-32,String{str,3,3});CHECK(rebuild::SeedNativeRows(m.binding,m.owner,m.seeds,8));return true;}
bool NativeAppearanceDefaultsRemoveStalePerksOnly(){Memory m;const auto state=m.owner.state;for(unsigned i=1056;i<1200;++i)m.bytes[state-Memory::base+i]=0xA5;CHECK(rebuild::ResetNativeAppearanceDefaults(m.binding,m.owner));CHECK(m.writes==2);std::uint32_t selected=0,gap=0,id=0,one=0,zero=1;CHECK(Memory::Read(&m,state+1060,&selected,4));CHECK(Memory::Read(&m,state+1160,&gap,4));CHECK(Memory::Read(&m,state+1136,&id,4));CHECK(Memory::Read(&m,state+1140,&one,4));CHECK(Memory::Read(&m,state+1144,&zero,4));CHECK(selected==0xA5A5A5A5&&gap==0xA5A5A5A5&&id==0xFFFFFFFF&&one==0x3F800000&&zero==0);for(unsigned i=0;i<8;++i){std::uint32_t multiplier=0;CHECK(Memory::Read(&m,state+1164+i*4,&multiplier,4));CHECK(multiplier==0x3F800000);}return true;}
}
int main(){bool(*tests[])()={AllOwnersSeedBeforeEvaluation,StartupAbsenceIsSemanticNotDeletion,MissingRealSeedPreventsEvaluation,PartialSeedNeverEvaluated,ActualAdmissionRequired,LiveActorsRejectGlobalDestruction,PermitsNotDepthBypass,OwnerGenerationChangesReject,ProtectedUserStateVerified,BaselineBindingsVerified,BaselineDefinitionOwnerRequired,MaterializationFailureNeverBinds,NativeSeedsPreserveAllNonPointerRowBytes,NativeWrongTypeAndStaleSeedsCannotWrite,NativeAliasMismatchCannotWrite,NativeSelectedTemplateMustMatch,NativeAppearanceDefaultsRemoveStalePerksOnly};for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);++i){if(!tests[i]())return 1;std::printf("PASS process_rebuild_%u\n",i);}return 0;}
