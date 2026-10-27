#include "ResourceProviderScope.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::resource_provider {
namespace {
bool Read(const Access& a,std::uintptr_t address,void* out,std::size_t bytes)noexcept{
 return a.read&&address&&bytes&&bytes-1<=UINTPTR_MAX-address&&a.read(a.user,address,out,bytes);
}
bool Copy(const Access& a,const char* source,char* out)noexcept{
 if(!source)return false;std::memset(out,0,kPathBytes);const auto address=reinterpret_cast<std::uintptr_t>(source);
 for(std::size_t i=0;i<kPathBytes;++i){if(i>UINTPTR_MAX-address||!Read(a,address+i,out+i,1))return false;if(!out[i])return true;}return false;
}
}
bool Begin(const Access& access,std::uint64_t serial,Scope* out)noexcept{
 if(!out)return false;*out={};out->probe.slot_index=UINT16_MAX;
 if(!serial||!access.probe||!access.files||!access.read||!access.source_revision||
 !access.source_revision(access.user,&out->before_revision)||!out->before_revision)return false;
 out->load_serial=serial;out->thread_id=GetCurrentThreadId();
 if(audio::ArmAudioFullPathProbe(access.probe,out->thread_id,audio::AudioFullPathProbeOwner::Resource,serial,&out->probe)!=
 audio::AudioFullPathProbeArmResult::Armed)return false;
 out->active=true;return true;
}
void Cancel(const Access& access,Scope* scope)noexcept{
 if(!scope||!scope->active)return;
 (void)audio::DisarmAudioFullPathProbe(access.probe,&scope->probe);scope->active=false;
}
bool Finish(const Access& access,Scope* scope,std::uintptr_t native_string,Receipt* out)noexcept{
 if(out)*out={};if(!scope||!scope->active||!out||scope->thread_id!=GetCurrentThreadId())return false;
 std::uintptr_t selected=0;const auto result=audio::DrainAudioFullPathProbe(access.probe,scope->probe,&selected);
 const auto serial=scope->load_serial;const auto before=scope->before_revision;const auto thread=scope->thread_id;
 Cancel(access,scope);
 if(result==audio::AudioFullPathProbeDrainResult::NoObservation){
  // A failed resolution or an already-physical input can legitimately return
  // without selecting a CFSSource. It grants no contextual provider identity;
  // a later source-less retaining open still requires another exact receipt.
  out->load_serial=serial;out->source_revision=before;out->thread_id=thread;return true;
 }
 if(result!=audio::AudioFullPathProbeDrainResult::ExactSource||!selected)return false;
 struct NativeString{std::uintptr_t data;std::uint32_t size,capacity;} value={};
 if(!Read(access,native_string,&value,sizeof(value))||!value.data||!value.size||value.size>=kPathBytes||value.size>value.capacity)
 return false;
 std::uint64_t after=0;file_lifetime::SourceKey source={};
 if(!access.source_revision(access.user,&after)||after!=before||
 !file_lifetime::bridge::SourceIdentity(access.files,selected,&source)||!source.generation||
 !Copy(access,reinterpret_cast<const char*>(value.data),out->resolved_path)||std::strlen(out->resolved_path)!=value.size)
 return false;
 if(!access.source_revision(access.user,&after)||after!=before)return false;
 out->source=source;out->source_revision=before;out->load_serial=serial;out->thread_id=thread;out->exact=true;return true;
}
bool Matches(const Access& access,const Receipt& receipt,std::uint64_t serial,const char* path)noexcept{
 if(!receipt.exact||receipt.load_serial!=serial||receipt.thread_id!=GetCurrentThreadId()||!receipt.source.address||!receipt.source.generation)
 return false;
 std::uint64_t revision=0;file_lifetime::SourceKey current={};char copied[kPathBytes]={};
 if(!access.source_revision||!access.source_revision(access.user,&revision)||revision!=receipt.source_revision||
 !file_lifetime::bridge::SourceIdentity(access.files,receipt.source.address,&current)||current.generation!=receipt.source.generation||
 !Copy(access,path,copied))return false;
 return !std::memcmp(copied,receipt.resolved_path,kPathBytes);
}
} // namespace
