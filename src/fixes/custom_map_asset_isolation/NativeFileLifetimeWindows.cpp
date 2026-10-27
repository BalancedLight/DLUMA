#define _HAS_EXCEPTIONS 0
#include "NativeFileLifetimeWindows.h"
#include <cstring>
extern "C" {
dluma::fixes::custom_map_asset_isolation::file_lifetime::CompletionRing* g_dluma_file_completion_ring=nullptr;
volatile LONG g_dluma_file_completion_probe_active_calls=0;
void* g_dluma_file_completion_cancel_relay=nullptr;
void* g_dluma_file_completion_skipped_relay=nullptr;
void* g_dluma_file_completion_read_relay=nullptr;
}
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::windows {
namespace {
constexpr HookSpec specs[]={
 {Route::PublicOpen,Role::NullableFactory,0x166B0,16,{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x41,0x54,0x48,0x8d,0xac,0x24,0x30,0xff,0xff}},
 {Route::NativeOpen,Role::NullableFactory,0x3C80,16,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
 {Route::Win32Factory,Role::NullableFactory,0xA5E0,16,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x8b}},
 {Route::AsyncFactory,Role::NullableFactory,0xAA00,16,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x41,0x54,0x41,0x55,0x48}},
 {Route::DirectoryRead,Role::NullableFactory,0x67B0,16,{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x48,0x81,0xec,0x40}},
 {Route::DirectoryWrite,Role::NullableFactory,0x6C60,16,{0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x55,0x41,0x56,0x48,0x8d,0xac,0x24,0x70,0xff}},
 {Route::ZipFactory,Role::NullableFactory,0x256A0,16,{0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x81,0xec,0x90,0x00}},
 {Route::MemoryFactory,Role::NullableFactory,0x28B0,16,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xf9,0xff,0x15,0x4d}},
 {Route::AsyncConstructor,Role::ConstructorObserver,0xAAE0,16,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33}},
 {Route::MemoryConstructor,Role::ConstructorObserver,0x29B0,16,{0x40,0x53,0x48,0x83,0xec,0x30,0x45,0x33,0xd2,0x48,0x8d,0x05,0xc8,0xc4,0x03,0x00}},
 {Route::Win32Destructor,Role::Destructor,0xA690,16,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x67,0x52,0x03}},
 {Route::AsyncDestructor,Role::Destructor,0xABE0,16,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}},
 {Route::GzipDestructor,Role::Destructor,0x1D00,16,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x77,0xcd,0x03}},
 {Route::ZipDestructor,Role::Destructor,0x248D0,16,{0x40,0x53,0x48,0x83,0xec,0x20,0x83,0x79,0x28,0x00,0x48,0x8d,0x05,0xf7,0x00,0x02}},
 {Route::MemoryDestructor,Role::Destructor,0x2A10,16,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x67,0xc4,0x03}},
 {Route::BaseDestructor,Role::Destructor,0x1820,16,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x17,0xd1,0x03}},
 {Route::QueueSchedule,Role::Schedule,0x1C960,16,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57}},
 {Route::QueueCancelComplete,Role::InteriorCompletion,0x1CC01,16,{0xf0,0xff,0x4b,0x28,0x75,0x1b,0x48,0x8b,0x4b,0x10,0xff,0x15,0x5f,0x14,0x02,0x00}},
 {Route::QueueSkippedComplete,Role::InteriorCompletion,0x1CED8,14,{0xf0,0xff,0x4f,0x28,0x0f,0x85,0xbf,0xfd,0xff,0xff,0x48,0x8b,0x4f,0x10}},
 {Route::QueueReadComplete,Role::InteriorCompletion,0x1D070,14,{0xf0,0xff,0x4f,0x28,0x0f,0x85,0x27,0xfc,0xff,0xff,0x48,0x8b,0x4f,0x10}},
 {Route::DirectorySourceDestructor,Role::SourceDestructor,0x4240,16,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0xe8}},
 {Route::PackSourceDestructor,Role::SourceDestructor,0x24C80,16,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0xe8}},
 {Route::FallbackSourceDestructor,Role::SourceDestructor,0x9770,16,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}},
};
struct KindSpec {std::uintptr_t vtable,destructor;FileKind kind;};
constexpr KindSpec kinds[]={{0x3F908,0xA690,FileKind::Win32},{0x3F978,0xABB0,FileKind::AsyncWin32},
 {0x3EA88,0x1D00,FileKind::Gzip},{0x449D8,0x24BA0,FileKind::Zip},{0x3EE88,0x2B20,FileKind::OwnedMemory}};
bool SameSha(const char* value)noexcept{
 if(!value)return false;
 for(unsigned i=0;i<64;++i){char c=value[i];if(c>='a'&&c<='f')c=static_cast<char>(c-'a'+'A');if(c!=kFilesystemSha256[i])return false;}
 return value[64]==0;
}
struct CompletionSpec {
 Route route;std::uintptr_t site,resume,conditional;
 unsigned char envelope[16];
};
constexpr CompletionSpec completions[]={
 {Route::QueueCancelComplete,0x1CC01,0x1CC07,0x1CC22,
  {0x00,0xf0,0xff,0x4b,0x28,0x75,0x1b,0x48,0x8b,0x4b,0x10,0xff,0x15,0x5f,0x14,0x02}},
 {Route::QueueSkippedComplete,0x1CED8,0x1CEE2,0x1CCA1,
  {0x33,0xc0,0x88,0x85,0x80,0x01,0x00,0x00,0xf0,0xff,0x4f,0x28,0x0f,0x85,0xbf,0xfd}},
 {Route::QueueReadComplete,0x1D070,0x1D07A,0x1CCA1,
  {0xf0,0xff,0x4f,0x28,0x0f,0x85,0x27,0xfc,0xff,0xff,0x48,0x8b,0x4f,0x10,0xff,0x15}},
};
bool Relative32(std::uintptr_t from,std::uintptr_t target,std::int32_t* output)noexcept{
 if(target>=from){const auto distance=target-from;if(distance>INT32_MAX)return false;*output=static_cast<std::int32_t>(distance);}
 else {const auto distance=from-target;if(distance>std::uint64_t{INT32_MAX}+1)return false;*output=static_cast<std::int32_t>(-static_cast<std::int64_t>(distance));}
 return true;
}
}
const HookSpec* HookPlan(std::size_t* count)noexcept{if(count)*count=sizeof(specs)/sizeof(specs[0]);return specs;}
bool EntryRelocationFor(Route route,EntryRelocation* output)noexcept{
 if(!output)return false;*output={};
 switch(route){
 case Route::PublicOpen:*output={17,0,0};break;
 case Route::NativeOpen:case Route::QueueSchedule:*output={16,0,0};break;
 case Route::Win32Factory:case Route::AsyncFactory:case Route::DirectoryRead:*output={19,0,0};break;
 case Route::DirectoryWrite:case Route::ZipFactory:*output={18,0,0};break;
 case Route::MemoryFactory:*output={19,15,19};break;
 case Route::AsyncConstructor:*output={17,0,0};break;
 case Route::MemoryConstructor:*output={16,12,16};break;
 case Route::Win32Destructor:case Route::GzipDestructor:case Route::ZipDestructor:
 case Route::MemoryDestructor:case Route::BaseDestructor:*output={17,13,17};break;
 case Route::AsyncDestructor:case Route::FallbackSourceDestructor:*output={22,18,22};break;
 case Route::DirectorySourceDestructor:case Route::PackSourceDestructor:*output={20,16,20};break;
 default:return false;
 }return true;
}
bool CopyEntryUnwind(const Binding* binding,Route route,unsigned char* output,std::size_t capacity,std::size_t* size)noexcept{
 if(size)*size=0;
 if(!binding||!binding->validated||!binding->read||!output||!size)return false;
 constexpr std::uintptr_t addresses[]={0x4afc0,0x4b234,0x48004,0x4a024,0x4b840,0x4b7d0,0x4aebc,0x4b274,
  0x4932c,0x4a170,0x4a1ac,0x4932c,0x4a1ac,0x48280,0x4a1ac,0x4a1ac,0x4af18,0,0,0,0x4a1ac,0x4a1ac,0x4932c};
 const auto index=static_cast<unsigned>(route);
 if(index>=sizeof(addresses)/sizeof(addresses[0])||!addresses[index])return false;
 unsigned char header[4]={};
 if(!binding->read(binding->user,binding->base+addresses[index],header,4)||header[0]!=1||header[2]>14)return false;
 const auto length=4U+((static_cast<unsigned>(header[2])+1U)&~1U)*2U;
 if(length>capacity||!binding->read(binding->user,binding->base+addresses[index],output,length))return false;
 *size=length;return true;
}
bool Validate(Binding* b,const char* sha)noexcept{
 if(!b)return false;b->validated=false;
 if(!SameSha(sha)||!b->base||b->base>UINTPTR_MAX-0x60000||!b->read||!b->resolve_source)return false;
 unsigned char actual[16];
 for(const auto& s:specs)if(!b->read(b->user,b->base+s.rva,actual,s.prefix_size)||std::memcmp(actual,s.prefix,s.prefix_size))return false;
 for(const auto& k:kinds){std::uintptr_t target=0;if(!b->read(b->user,b->base+k.vtable,&target,sizeof(target))||target!=b->base+k.destructor)return false;}
 b->validated=true;return true;
}
bool ReadFileIdentity(const Binding* b,std::uintptr_t file,SourceKey* source,FileKind* kind)noexcept{
 if(!source||!kind)return false;*source={};*kind=FileKind::Unknown;
 if(!b||!b->validated||!file||file>UINTPTR_MAX-0x20)return false;
 std::uintptr_t vtable=0,owner=0;
 if(!b->read(b->user,file,&vtable,sizeof(vtable))||!b->read(b->user,file+0x18,&owner,sizeof(owner)))return false;
 for(const auto& k:kinds)if(vtable==b->base+k.vtable)*kind=k.kind;
 if(*kind==FileKind::Unknown)return false;
 if(!owner)return true; // Exact native reader owns an OS handle/buffer, no CFSSource pointer.
 return b->resolve_source(b->user,owner,source)&&source->address==owner&&source->generation;
}
Token ObserveReturnedFile(const Binding* b,Ledger* l,const ThreadScope& scope,std::uintptr_t file)noexcept{
 if(!file)return {};SourceKey source={};FileKind kind={};
 if(!ReadFileIdentity(b,file,&source,&kind)){RecordCoverageLoss(l,Failure::UnknownClass);return {};}
 return ObserveConstructed(l,scope,file,source,kind);
}
Token ObserveDestructorEntry(const Binding* b,Ledger* l,std::uintptr_t file)noexcept{
 SourceKey source={};FileKind kind={};
 if(!ReadFileIdentity(b,file,&source,&kind)){RecordCoverageLoss(l,Failure::UnknownClass);return {};}
 return BeginDestroy(l,file,source,kind);
}
Token ObserveQueueSchedule(const Binding* b,Ledger* l,std::uintptr_t file,std::uintptr_t control)noexcept{
 if(!file&&!control)return {}; // Native termination marker has no file owner.
 SourceKey source={};FileKind kind={};
 if(!control||!ReadFileIdentity(b,file,&source,&kind)){RecordCoverageLoss(l,Failure::UnknownClass);return {};}
 return ObserveDeferredControl(l,file,control,source);
}
bool PrepareCompletionRelay(const Binding* binding,Route route,std::uintptr_t allocation,
                            std::uintptr_t hook,CompletionRelay* output)noexcept{
 if(output)*output={};
 if(!binding||!binding->validated||!binding->read||!output||!allocation||!hook||
    allocation>UINTPTR_MAX-64||binding->base>UINTPTR_MAX-0x60000)return false;
 const CompletionSpec* spec=nullptr;
 for(const auto& candidate:completions)if(candidate.route==route)spec=&candidate;
 if(!spec)return false;
 CompletionRelay result={};
 result.site=binding->base+spec->site;result.patch_address=result.site&~std::uintptr_t{15};
 result.resume=binding->base+spec->resume;result.conditional_target=binding->base+spec->conditional;
 const auto offset=static_cast<unsigned>(result.site-result.patch_address);
 if(offset+5>16||!binding->read(binding->user,result.patch_address,result.original_envelope,16)||
    std::memcmp(result.original_envelope,spec->envelope,16))return false;
 std::int32_t entry_jump=0,condition_jump=0;
 if(!Relative32(result.site+5,allocation,&entry_jump)||
    !Relative32(allocation+26,result.conditional_target,&condition_jump))return false;
 std::memcpy(result.patched_envelope,result.original_envelope,16);
 result.patched_envelope[offset]=0xE9;
 std::memcpy(result.patched_envelope+offset+1,&entry_jump,4);
 /* Entry island uses no scratch register; the assembly observer supplies the
  * native frame and jumps back to the original relay at allocation+16. */
 result.code[0]=0xff;result.code[1]=0x25;
 std::memcpy(result.code+6,&hook,sizeof(hook));
 result.code[14]=result.code[15]=0x90;
 std::memcpy(result.code+16,result.original_envelope+offset,4); // native LOCK DEC
 result.code[20]=0x0f;result.code[21]=0x85; // preserve JNE, expand cancellation's rel8
 std::memcpy(result.code+22,&condition_jump,4);
 result.code[26]=0xff;result.code[27]=0x25;
 std::memcpy(result.code+32,&result.resume,sizeof(result.resume));
 result.code_size=40;result.original_relay_offset=16;
 result.displaced_size=static_cast<std::uint8_t>(spec->resume-spec->site);
 /* Equivalent full-body unwind operations for these exact native frames.
  * Cancellation's chained range additionally saves RSI at current RSP+0x30. */
 constexpr unsigned char cancel_unwind[]={1,0,6,0,0,0x64,6,0,0,0x34,7,0,0,0x32,0,0x70};
 constexpr unsigned char worker_unwind[]={1,0,10,0,0,1,0x45,0,0,0xf0,0,0xe0,0,0xd0,0,0xc0,0,0x70,0,0x60,0,0x30,0,0x50};
 result.unwind_size=static_cast<std::uint8_t>(route==Route::QueueCancelComplete?sizeof(cancel_unwind):sizeof(worker_unwind));
 std::memcpy(result.unwind,route==Route::QueueCancelComplete?cancel_unwind:worker_unwind,result.unwind_size);
 *output=result;return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::windows
