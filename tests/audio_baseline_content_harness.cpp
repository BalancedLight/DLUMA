#include "fixes/custom_map_asset_isolation/AudioBaselineContent.h"
#include <cstdio>
#include <cstring>
namespace c=dluma::fixes::custom_map_asset_isolation::audio_baseline_content;
namespace l=dluma::fixes::custom_map_asset_isolation::speech_content_lease;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
namespace a=dluma::fixes::custom_map_asset_isolation::audio_startup;
namespace raw=dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot;
namespace {
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
struct File {std::uintptr_t vtable=0x2003F978,words[4]={};HANDLE handle=nullptr;};
bool Read(void* user,std::uintptr_t address,void* out,std::size_t bytes) noexcept {
 const auto begin=reinterpret_cast<std::uintptr_t>(user);
 if(address<begin||bytes>sizeof(File)||address-begin>sizeof(File)-bytes)return false;
 std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;
}
c::State state;
a::AudioStartupObservationSnapshot observed;
raw::AudioBaselineWindowsSnapshot native;
c::NaturalBank Natural(const char* path,File& file){
 c::NaturalBank n={};n.open.engine_base=0x10000000;n.open.filesystem_base=0x20000000;
 n.open.native_file=reinterpret_cast<std::uintptr_t>(&file);n.open.caller_return=0x10100C49;
 n.open.speech_scope_generation=3;n.open.provider_identity=0x4000;n.open.provider_generation=7;
 n.open.owner_thread_id=GetCurrentThreadId();n.open.exact_player_images=true;std::strcpy(n.open.resolved_path,path);
 n.file={n.open.native_file,{},11,9,f::FileKind::AsyncWin32,f::Life::Live};n.contextual_source={0x4000,7};
 n.manager=0x5000;n.load_enter_sequence=20;n.kind=l::NativeBankFile::Fmod;return n;
}
void Setup(const c::NaturalBank& n){
 std::memset(&state,0,sizeof(state));std::memset(&observed,0,sizeof(observed));std::memset(&native,0,sizeof(native));
 observed.revision=8;observed.hook_plan_complete=true;observed.coverage_started_before_audio_singletons=true;
 observed.bank_count=1;auto& o=observed.banks[0];o.enter_sequence=20;o.exit_sequence=25;o.scope_generation=3;o.manager=n.manager;
 o.owner=a::AudioStartupBankOwner::Fmod;o.result=0;o.bank_name_complete=true;o.localized_observed=true;o.observation_complete=true;
 o.native_file=n.file.file;o.native_file_instance_generation=n.file.generation;
 o.provider_outcome=a::AudioStartupProviderOutcome::NaturalOpenResolvedPath;o.provider.source=n.contextual_source.address;
 o.provider.instance_generation=7;o.provider.source_registry_generation=4;o.provider.source_registry_generation_observed=true;
 o.provider.exact_source=true;o.provider.path_complete=true;std::strcpy(o.provider.path,n.open.resolved_path);std::strcpy(o.bank_name,"audio/baseline");
 native.observation_revision=8;native.raw_snapshot_complete=true;native.bank_count=1;native.bank_manager=n.manager;
 native.banks[0].native_file=n.file.file;std::strcpy(native.banks[0].name,o.bank_name);
}
bool Hash(){for(unsigned i=0;i<1000;++i){auto p=c::Step(&state,1U<<20);if(p==c::Phase::ContentCaptured)return true;if(p==c::Phase::Failed)return false;}return false;}
bool Load(const char* path,File& file,c::NaturalBank& n){
 file.handle=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file.handle==INVALID_HANDLE_VALUE)return false;n=Natural(path,file);Setup(n);return true;
}
bool NativePinJoin(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));
 CHECK(c::ObserveNatural(&state,n,&Read,&file));CHECK(CloseHandle(file.handle));CHECK(c::Freeze(&state,1,8));CHECK(Hash());
 CHECK(state.banks[0].bytes>65536);CHECK(c::Join(&state,observed,native));CHECK(c::AllBankContentHeld(state));
 CHECK(state.inputs[0].source.generation==7&&state.inputs[0].native_file_generation==11);
 CHECK(!c::ObserveNatural(&state,n,&Read,&file));CHECK(c::AllBankContentHeld(state));CHECK(c::Close(&state));return true;}
bool WrongContextRejected(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));n.contextual_source.generation++;
 CHECK(!c::ObserveNatural(&state,n,&Read,&file));CHECK(state.failure==static_cast<LONG>(c::Failure::IncompleteOpen));
 CHECK(state.count==0);CHECK(CloseHandle(file.handle));CHECK(c::Close(&state));return true;}
bool MissingFileGeneration(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));n.file.generation=0;
 CHECK(!c::ObserveNatural(&state,n,&Read,&file));CHECK(!state.banks[0].pin.handle);CHECK(CloseHandle(file.handle));CHECK(c::Close(&state));return true;}
bool WrongRawInstance(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));CHECK(c::ObserveNatural(&state,n,&Read,&file));
 CHECK(c::Freeze(&state,1,8));CHECK(Hash());native.banks[0].native_file++;
 CHECK(!c::Join(&state,observed,native));CHECK(state.failure==static_cast<LONG>(c::Failure::BankJoinMissing));
 CHECK(c::Close(&state));CHECK(CloseHandle(file.handle));return true;}
bool ReplacementProviderRejected(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));CHECK(c::ObserveNatural(&state,n,&Read,&file));
 CHECK(c::Freeze(&state,1,8));CHECK(Hash());observed.banks[0].provider.instance_generation++;
 CHECK(!c::Join(&state,observed,native));CHECK(state.failure==static_cast<LONG>(c::Failure::ProviderChanged));
 CHECK(c::Close(&state));CHECK(CloseHandle(file.handle));return true;}
bool ParsedNestedInput(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));CHECK(c::ObserveNatural(&state,n,&Read,&file));
 observed.script_count=1;auto& s=observed.scripts[0];s.enter_sequence=10;s.exit_sequence=30;s.loader=0x114100;s.parse_succeeded=true;
 s.provider=observed.banks[0].provider;s.parser_input_complete=true;s.parser_input_observations=1;s.parser_input_bytes=8;s.parser_input_sha256[0]=9;
 std::strcpy(s.logical_path,"data/audio.scr");CHECK(c::Freeze(&state,1,8));CHECK(Hash());CHECK(c::Join(&state,observed,native));
 CHECK(state.input_count==2&&state.inputs[0].script&&state.inputs[1].parent_sequence==10);
 CHECK(c::Close(&state));CHECK(CloseHandle(file.handle));return true;}
bool AsyncQueueIsNotHandle(const char* path){File file;c::NaturalBank n;CHECK(Load(path,file,n));file.words[3]=reinterpret_cast<std::uintptr_t>(file.handle);
 const HANDLE valid=file.handle;file.handle=INVALID_HANDLE_VALUE;CHECK(!c::ObserveNatural(&state,n,&Read,&file));CHECK(!state.banks[0].pin.handle);
 CHECK(CloseHandle(valid));CHECK(c::Close(&state));return true;}
}
int main(int argc,char** argv){if(argc!=2)return 2;bool(*tests[])(const char*)={NativePinJoin,WrongContextRejected,MissingFileGeneration,WrongRawInstance,ReplacementProviderRejected,ParsedNestedInput,AsyncQueueIsNotHandle};
 for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);++i){if(!tests[i](argv[1]))return 1;std::printf("PASS bank_content_%u\n",i);}return 0;}
