#include "fixes/custom_map_asset_isolation/AudioBaselineReplay.h"
#include <cstdio>
#include <cstring>
namespace replay = dluma::fixes::custom_map_asset_isolation::audio_baseline_replay;
namespace {
#define CHECK(c) do {if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);return false;}}while(0)
struct Fake {
 replay::Replay replay = {};
 replay::Environment env={9,20,true,true,true,true,false};
 replay::Kind invoked[16]={};unsigned calls=0,permits=0,ends=0,children=0;
 bool source_ok=true,permit_ok=true,invoke_ok=true,effects_ok=true,final_ok=true,change_fence=false;
 bool localized_seen=false,reenter=false,reentry_rejected=false;
};
bool Run(Fake& f)noexcept{if(!replay::ClearOwnedDefinitions(&f.replay))return false;f.env.stage=replay::Stage::RestoreBaseline;f.env.outgoing_providers_retired=true;f.env.outgoing_providers_detached_and_pinned=false;f.env.baseline_provider_leases_held=true;return replay::ReplayBaseline(&f.replay);}
bool Env(void* u,replay::Environment* out)noexcept{auto& f=*static_cast<Fake*>(u);if(f.reenter){f.reenter=false;f.reentry_rejected=!Run(f);}*out=f.env;return true;}
bool Resolve(void* u,const replay::Entry* e)noexcept{return static_cast<Fake*>(u)->source_ok&&e->content_sha256[0]==3;}
bool Permit(void* u,const replay::Manifest*,const replay::Entry*,const replay::Entry* children,std::size_t count)noexcept{
 auto& f=*static_cast<Fake*>(u);++f.permits;f.children+=static_cast<unsigned>(count);
 for(std::size_t i=0;i<count;++i)if(children[i].kind==replay::Kind::SpeechBank)f.localized_seen=children[i].localized;
 return f.permit_ok;
}
bool End(void* u,bool ok)noexcept{auto& f=*static_cast<Fake*>(u);++f.ends;return ok&&f.effects_ok;}
bool Invoke(void* u,const replay::Entry* e)noexcept{auto& f=*static_cast<Fake*>(u);f.invoked[f.calls++]=e->kind;if(f.change_fence)++f.env.fence_identity;return f.invoke_ok;}
bool Verify(void* u,const replay::Manifest* m)noexcept{return static_cast<Fake*>(u)->final_ok&&m->preserved_user_settings_fingerprint==10;}
void Init(Fake& f){replay::Operations ops={&f,Env,Resolve,Permit,End,Invoke,Verify,true};replay::Initialize(&f.replay,&ops);}
replay::Entry Entry(replay::Kind kind,unsigned order,unsigned parent=0){replay::Entry e={};e.kind=kind;e.order=order;e.parent_order=parent;e.owner_identity=kind==replay::Kind::Script?100:kind==replay::Kind::FmodBank?200:300;e.provider_identity=2;e.content_sha256[0]=3;std::strcpy(e.logical_path,kind==replay::Kind::Script?"AudioData_base.scr":"baseline_bank");e.source_content_complete=true;e.bounded_side_effects_verified=true;return e;}
replay::Manifest Manifest(){replay::Manifest m={};m.baseline_generation=1;m.epoch=7;m.transaction_generation=8;m.fence_identity=9;m.preserved_user_settings_fingerprint=10;m.script_registry_owner=100;m.fmod_owner=200;m.speech_registry_owner=300;m.owner_thread_id=20;m.side_effect_order_sealed=true;m.rebuild_speech=true;m.count=4;
 m.entries[0]=Entry(replay::Kind::Script,1);m.entries[1]=Entry(replay::Kind::FmodBank,2,1);m.entries[2]=Entry(replay::Kind::SpeechBank,3,1);m.entries[2].localized=true;m.entries[3]=Entry(replay::Kind::FmodBank,4);return m;}
bool OrderedReplay(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));CHECK(Run(f));CHECK(f.calls==4);CHECK(f.invoked[0]==replay::Kind::ClearScripts);CHECK(f.invoked[1]==replay::Kind::ClearSpeech);CHECK(f.invoked[2]==replay::Kind::Script);CHECK(f.invoked[3]==replay::Kind::FmodBank);CHECK(f.children==2&&f.localized_seen);CHECK(f.ends==f.permits);CHECK(!Run(f));return true;}
bool NoClearBeforeCompleteInputs(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.source_ok=false;CHECK(!Run(f));CHECK(f.calls==0&&!f.replay.started);return true;}
bool SoundLockForbidden(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.env.current_thread_holds_audio_lock=true;CHECK(!Run(f));CHECK(f.calls==0&&f.replay.failure==replay::Failure::AudioLockHeld);return true;}
bool ReferencesMustBeReleased(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.env.speech_references_released=false;CHECK(!Run(f));CHECK(f.calls==0);f.env.speech_references_released=true;f.env.banks_and_voices_released=false;CHECK(!Run(f));CHECK(f.calls==0);return true;}
bool ParentGraphRejected(){Fake f;Init(f);auto m=Manifest();m.entries[2].parent_order=2;CHECK(!replay::Prepare(&f.replay,m));m=Manifest();m.entries[2].order=1;CHECK(!replay::Prepare(&f.replay,m));return true;}
bool UnknownContentAndWaitRejected(){Fake f;Init(f);auto m=Manifest();m.entries[0].content_sha256[0]=0;CHECK(!replay::Prepare(&f.replay,m));m=Manifest();m.entries[0].bounded_side_effects_verified=false;CHECK(!replay::Prepare(&f.replay,m));CHECK(f.calls==0);return true;}
bool PermitDeniedNoMutation(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.permit_ok=false;CHECK(!Run(f));CHECK(f.calls==0&&f.ends==0);return true;}
bool NativeFailureClosesPermit(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.invoke_ok=false;CHECK(!Run(f));CHECK(f.calls==1&&f.ends==1);CHECK(!f.replay.verified);return true;}
bool UnexpectedSideEffectStopsReplay(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.effects_ok=false;CHECK(!Run(f));CHECK(f.calls==1&&f.replay.failure==replay::Failure::SideEffectsMismatch);return true;}
bool FenceAndSettingsVerified(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.change_fence=true;CHECK(!Run(f));CHECK(f.calls==1);Fake g;Init(g);CHECK(replay::Prepare(&g.replay,Manifest()));g.final_ok=false;CHECK(!Run(g));CHECK(!g.replay.verified);return true;}
bool PreflightRetryAndReentry(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));f.source_ok=false;CHECK(!Run(f));CHECK(!f.replay.started);f.source_ok=true;f.reenter=true;CHECK(Run(f));CHECK(f.reentry_rejected&&f.replay.failure==replay::Failure::None);return true;}
bool RetirementSeparatesNativePhases(){Fake f;Init(f);CHECK(replay::Prepare(&f.replay,Manifest()));CHECK(!replay::ReplayBaseline(&f.replay));CHECK(f.calls==0);CHECK(replay::ClearOwnedDefinitions(&f.replay));CHECK(f.calls==2&&f.replay.definitions_cleared);CHECK(!replay::ReplayBaseline(&f.replay));CHECK(f.calls==2);f.env.stage=replay::Stage::RestoreBaseline;f.env.outgoing_providers_retired=true;f.env.outgoing_providers_detached_and_pinned=false;CHECK(!replay::ReplayBaseline(&f.replay));CHECK(f.calls==2);f.env.baseline_provider_leases_held=true;CHECK(replay::ReplayBaseline(&f.replay));CHECK(f.calls==4&&f.replay.verified);return true;}
}
int main(){bool(*tests[])()={OrderedReplay,NoClearBeforeCompleteInputs,SoundLockForbidden,ReferencesMustBeReleased,ParentGraphRejected,UnknownContentAndWaitRejected,PermitDeniedNoMutation,NativeFailureClosesPermit,UnexpectedSideEffectStopsReplay,FenceAndSettingsVerified,PreflightRetryAndReentry,RetirementSeparatesNativePhases};for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);++i){if(!tests[i]())return 1;std::printf("PASS replay_%u\n",i);}return 0;}
