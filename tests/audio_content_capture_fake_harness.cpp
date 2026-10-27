#include "fixes/custom_map_asset_isolation/AudioContentCapture.h"
#include <cstdio>
#include <cstring>
namespace cap=dluma::fixes::custom_map_asset_isolation::audio_content_capture;
namespace obs=dluma::fixes::custom_map_asset_isolation::audio_startup;
namespace {
#define CHECK(c) do { if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);return false;} }while(0)
struct Fake {
 cap::Capture capture={}; cap::Fence fence={12,7,5,0,false};
 unsigned opened=0,closed=0,released=0,reads=0;std::size_t position=0;
 bool lease_ok=true,lease_valid=true,open_ok=true,wrong_provider=false,close_ok=true;
 bool short_file=false,grown=false,mutate_after_read=false,loaded_digest=false,bad_digest=false;
 std::uint64_t length=3;unsigned char data[cap::kReadChunk+5]={};
 Fake(){std::memcpy(data,"abc",3);}
};
bool Fence(void* u,cap::Fence* f)noexcept{*f=static_cast<Fake*>(u)->fence;return true;}
bool Path(void*,cap::Target* t)noexcept{std::strcpy(t->read_path,t->logical_path);return true;}
bool Lease(void* u,const cap::Target*,cap::Lease* l)noexcept{
 auto& f=*static_cast<Fake*>(u);if(!f.lease_ok)return false;l->token=9;l->loaded_content_identity=11;
 l->compare_loaded_digest=f.loaded_digest;if(f.loaded_digest){DlumaSha256Context c;dluma_sha256_init(&c);dluma_sha256_update(&c,f.data,static_cast<SIZE_T>(f.length));dluma_sha256_final(&c,l->expected_sha256);if(f.bad_digest)++l->expected_sha256[0];}return true;}
bool Valid(void* u,const cap::Target*,const cap::Lease*)noexcept{return static_cast<Fake*>(u)->lease_valid;}
void Release(void* u,const cap::Lease*)noexcept{++static_cast<Fake*>(u)->released;}
std::uintptr_t Open(void* u,const cap::Target* t,std::uintptr_t* source,std::uint64_t* length)noexcept{
 auto& f=*static_cast<Fake*>(u);if(!f.open_ok)return 0;++f.opened;f.position=0;*source=t->source+(f.wrong_provider?1:0);*length=f.length;return 77;}
std::uint64_t Read(void* u,std::uintptr_t,void* out,std::size_t wanted)noexcept{
 auto& f=*static_cast<Fake*>(u);++f.reads;if(f.mutate_after_read)++f.fence.source_generation;
 if(f.short_file)return 0;const auto actual=f.length+(f.grown?1:0);const auto left=actual-f.position;
 const auto size=left<wanted?static_cast<std::size_t>(left):wanted;std::memcpy(out,f.data+f.position,size);f.position+=size;return size;}
bool Close(void* u,std::uintptr_t)noexcept{auto& f=*static_cast<Fake*>(u);++f.closed;return f.close_ok;}
void Init(Fake& f){cap::Operations o={&f,Fence,Path,Lease,Valid,Release,Open,Read,Close};cap::Initialize(&f.capture,&o);}
obs::AudioStartupObservationSnapshot Snapshot(){obs::AudioStartupObservationSnapshot s={};s.revision=12;s.coverage_started_before_audio_singletons=true;s.hook_plan_complete=true;s.script_count=1;
 auto& o=s.scripts[0];o.enter_sequence=10;o.exit_sequence=40;o.loader=100;o.request_complete=true;o.parse_succeeded=true;std::strcpy(o.logical_path,"audio/startup.scr");o.provider.source=33;o.provider.source_registry_generation=7;o.provider.exact_source=true;o.provider.path_complete=true;o.provider.source_registry_generation_observed=true;std::strcpy(o.provider.path,"base.pak");return s;}
bool Run(Fake& f){for(unsigned i=0;i<100 && (f.capture.state==cap::State::Prepared||f.capture.state==cap::State::Reading);++i)cap::Step(&f.capture);return f.capture.state==cap::State::Complete;}
bool HashExactBytes(){Fake f;Init(f);CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(f.opened==0);CHECK(Run(f));char hex[65];dluma_sha256_hex(f.capture.entries[0].sha256,hex);CHECK(std::strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")==0);CHECK(f.reads==2&&f.closed==1&&f.released==1);CHECK(cap::RevalidateCompleted(&f.capture));return true;}
bool ChunkBounded(){Fake f;Init(f);f.length=sizeof(f.data);CHECK(cap::Prepare(&f.capture,Snapshot(),5,100000));cap::Step(&f.capture);CHECK(f.position==cap::kReadChunk&&f.reads==1);CHECK(Run(f));CHECK(f.reads==3);return true;}
bool ParentFromNaturalIntervals(){Fake f;Init(f);auto s=Snapshot();s.bank_count=2;for(unsigned i=0;i<2;++i){auto& o=s.banks[i];o.enter_sequence=20+i*5;o.exit_sequence=22+i*5;o.manager=200+i;o.owner=i?obs::AudioStartupBankOwner::Fmod:obs::AudioStartupBankOwner::Speech;o.provider_outcome=obs::AudioStartupProviderOutcome::NaturalOpenResolvedPath;o.provider=s.scripts[0].provider;o.bank_name_complete=true;o.localized_observed=true;o.localized=i==0;o.observation_complete=true;std::strcpy(o.bank_name,i?"common":"speech.spb");}CHECK(cap::Prepare(&f.capture,s,5,1024));CHECK(f.capture.entries[1].target.parent_sequence==10&&f.capture.entries[2].target.parent_sequence==10);CHECK(f.capture.entries[1].target.localized);CHECK(Run(f));return true;}
bool MissingHistoryRejected(){Fake f;Init(f);auto s=Snapshot();s.hook_plan_complete=false;CHECK(!cap::Prepare(&f.capture,s,5,1024));CHECK(f.opened==0);return true;}
bool CrossingRejected(){Fake f;Init(f);auto s=Snapshot();s.script_count=2;s.scripts[1]=s.scripts[0];s.scripts[1].enter_sequence=30;s.scripts[1].exit_sequence=50;CHECK(!cap::Prepare(&f.capture,s,5,1024));CHECK(f.opened==0);return true;}
bool NoHistoricalBytesLease(){Fake f;Init(f);f.lease_ok=false;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(!Run(f));CHECK(f.capture.failure==cap::Failure::LeaseUnavailable&&f.opened==0);return true;}
bool ProviderSwitchRejected(){Fake f;Init(f);f.wrong_provider=true;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(!Run(f));CHECK(f.closed==1&&f.released==1&&f.reads==0);return true;}
bool GenerationChangeCloses(){Fake f;Init(f);f.mutate_after_read=true;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(!Run(f));CHECK(f.closed==1&&f.released==1);return true;}
bool FirstProjectCancelsCapture(){Fake f;Init(f);CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));cap::Step(&f.capture);f.fence.first_project_seen=true;CHECK(!Run(f));CHECK(f.closed==1&&f.released==1);return true;}
bool CancellationCloses(){Fake f;Init(f);CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));cap::Step(&f.capture);cap::Cancel(&f.capture);CHECK(f.closed==1&&f.released==1&&f.capture.state==cap::State::Cancelled);cap::Cancel(&f.capture);CHECK(f.closed==1);return true;}
bool InvalidSizeCloses(){Fake f;Init(f);f.length=2048;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(!Run(f));CHECK(f.closed==1&&f.released==1&&f.reads==0);return true;}
bool GrowthAndTruncationRejected(){Fake f;Init(f);f.grown=true;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(!Run(f));Fake g;Init(g);g.short_file=true;CHECK(cap::Prepare(&g.capture,Snapshot(),5,1024));CHECK(!Run(g));CHECK(f.closed==1&&g.closed==1);return true;}
bool LoadedDigestChecked(){Fake f;Init(f);f.loaded_digest=true;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(Run(f));Fake g;Init(g);g.loaded_digest=true;g.bad_digest=true;CHECK(cap::Prepare(&g.capture,Snapshot(),5,1024));CHECK(!Run(g));CHECK(g.capture.failure==cap::Failure::ContentChanged);return true;}
bool FailedClosePinsLease(){Fake f;Init(f);f.close_ok=false;CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(!Run(f));CHECK(f.closed==1&&f.released==0&&f.capture.handle==77&&f.capture.lease.token==9);return true;}
bool PublicationRechecksLifetime(){Fake f;Init(f);CHECK(cap::Prepare(&f.capture,Snapshot(),5,1024));CHECK(Run(f));f.lease_valid=false;CHECK(!cap::RevalidateCompleted(&f.capture));return true;}
bool Readable(void*,const void*,std::size_t)noexcept{return true;}
cap::ParsedInputScope InputScope(){cap::ParsedInputScope s={};s.audio_load_sequence=10;s.source_generation=2;s.source=33;s.thread_id=8;s.exact_provider=true;std::strcpy(s.logical_path,"audio/startup.scr");return s;}
bool ActualWideInput(){auto s=InputScope();const char16_t input[]=u"definition\r\nAudioProc()\r\n";const auto count=static_cast<std::uint32_t>(sizeof(input)/2-1);CHECK(cap::ObserveParsedInput(&s,input,count,s.logical_path,8,0x10000000,0x106B7BBE,0,1024,Readable,nullptr));CHECK(cap::ParsedInputReady(s)&&s.bytes==count*2);CHECK(!cap::ObserveParsedInput(&s,input,count,s.logical_path,8,0x10000000,0x106B7BBE,0,1024,Readable,nullptr));CHECK(!cap::ParsedInputReady(s));return true;}
bool OtherParserAndOversizeRejected(){auto s=InputScope();const char16_t input[]=u"abc";CHECK(!cap::ObserveParsedInput(&s,input,3,s.logical_path,8,0x10000000,0x112C6320,0,1024,Readable,nullptr));s=InputScope();CHECK(!cap::ObserveParsedInput(&s,input,3,s.logical_path,8,0x10000000,0x106B7BBE,0,2,Readable,nullptr));CHECK(!cap::ParsedInputReady(s));return true;}
bool ParsedBytesDoNotBecomeDiskHash(){Fake f;Init(f);auto snapshot=Snapshot();auto s=InputScope();s.source_generation=7;const char16_t input[]=u"AudioProc()";CHECK(cap::ObserveParsedInput(&s,input,11,s.logical_path,8,0x10000000,0x106B7BBE,0,1024,Readable,nullptr));CHECK(cap::PublishParsedInput(s,&snapshot.scripts[0]));CHECK(cap::Prepare(&f.capture,snapshot,5,1024));CHECK(Run(f));CHECK(f.opened==0&&f.reads==0&&f.capture.entries[0].format==cap::ContentFormat::EngineParserUtf16DefinitionAndBody);CHECK(std::memcmp(f.capture.entries[0].sha256,s.sha256,32)==0);return true;}
}
int main(){bool(*tests[])()={HashExactBytes,ChunkBounded,ParentFromNaturalIntervals,MissingHistoryRejected,CrossingRejected,NoHistoricalBytesLease,ProviderSwitchRejected,GenerationChangeCloses,FirstProjectCancelsCapture,CancellationCloses,InvalidSizeCloses,GrowthAndTruncationRejected,LoadedDigestChecked,FailedClosePinsLease,PublicationRechecksLifetime,ActualWideInput,OtherParserAndOversizeRejected,ParsedBytesDoNotBecomeDiskHash};for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);++i){if(!tests[i]())return 1;std::printf("PASS capture_%u\n",i);}return 0;}
