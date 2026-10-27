#define _HAS_EXCEPTIONS 0
#include "SpeechContentLease.h"
#include <cstring>
extern "C" {
__declspec(dllimport) BOOL WINAPI DuplicateHandle(HANDLE,HANDLE,HANDLE,HANDLE*,DWORD,BOOL,DWORD);
}
namespace dluma::fixes::custom_map_asset_isolation::speech_content_lease {
namespace {
bool Text(const char* p)noexcept{if(!p||!*p)return false;for(std::size_t i=0;i<kPathCapacity;++i)if(!p[i])return true;return false;}
bool Valid(HANDLE h)noexcept{return h&&h!=INVALID_HANDLE_VALUE;}
bool FileIdentity(HANDLE handle,Identity* out)noexcept{
 BY_HANDLE_FILE_INFORMATION info={};
 if(!Valid(handle)||!out||!GetFileInformationByHandle(handle,&info))return false;
 *out={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,info.nFileSizeHigh,info.nFileSizeLow,info.ftCreationTime,info.ftLastWriteTime};
 return info.nFileIndexHigh||info.nFileIndexLow;
}
bool Same(const Identity& a,const Identity& b)noexcept{
 return a.volume==b.volume&&a.index_high==b.index_high&&a.index_low==b.index_low&&
 a.size_high==b.size_high&&a.size_low==b.size_low&&
 a.creation.dwHighDateTime==b.creation.dwHighDateTime&&a.creation.dwLowDateTime==b.creation.dwLowDateTime&&
 a.write.dwHighDateTime==b.write.dwHighDateTime&&a.write.dwLowDateTime==b.write.dwLowDateTime;
}
bool PinFail(Pin* p,Failure failure)noexcept{if(p){p->failure=failure;p->state=State::Failed;}return false;}
bool JobFail(ReadJob* j,Failure failure)noexcept{if(j){j->failure=failure;j->state=State::Failed;}return false;}
bool InitPin(Pin* p,const char* path,std::uint64_t provider,std::uint64_t generation,std::uint64_t scope)noexcept{
 if(!p||p->state!=State::Empty||p->handle||!Text(path)||!provider||!scope)return false;
 p->provider_identity=provider;p->provider_generation=generation;p->scope_generation=scope;
 std::memcpy(p->path,path,std::strlen(path)+1);return true;
}
}
bool CaptureNaturalPin(Pin* p,const NaturalOpen& o,ReadMemoryFn read,void* user)noexcept{
 return CaptureNaturalBankPin(p,o,NativeBankFile::Speech,read,user);
}
bool CaptureNaturalBankPin(Pin* p,const NaturalOpen& o,NativeBankFile kind,ReadMemoryFn read,void* user)noexcept{
 struct LastErrorScope {DWORD value;LastErrorScope()noexcept:value(GetLastError()){}~LastErrorScope(){SetLastError(value);}} last_error;
 if(!InitPin(p,o.resolved_path,o.provider_identity,o.provider_generation,o.speech_scope_generation))return false;
 if(!read||!o.exact_player_images||!o.engine_base||!o.filesystem_base||!o.native_file||
 o.engine_base>UINTPTR_MAX-0x32A8F8U||o.filesystem_base>UINTPTR_MAX-0x3F978U||o.native_file>UINTPTR_MAX-0x30U||
 (kind!=NativeBankFile::Speech&&kind!=NativeBankFile::Fmod)||
 o.caller_return!=o.engine_base+(kind==NativeBankFile::Speech?0x32A8F8U:0x100C49U)||
 !o.owner_thread_id||o.owner_thread_id!=GetCurrentThreadId())
 return PinFail(p,Failure::InvalidScope);
 std::uintptr_t vtable=0;HANDLE original=nullptr;
 if(!read(user,o.native_file,&vtable,sizeof(vtable))||
 vtable!=o.filesystem_base+(kind==NativeBankFile::Speech?0x3F908U:0x3F978U)||
 !read(user,o.native_file+(kind==NativeBankFile::Speech?0x20U:0x28U),&original,sizeof(original))||
 !Valid(original))return PinFail(p,Failure::WrongNativeClass);
 HANDLE duplicate=nullptr;const HANDLE process=GetCurrentProcess();
 /* DUPLICATE_SAME_ACCESS, never DUPLICATE_CLOSE_SOURCE. */
 if(!DuplicateHandle(process,original,process,&duplicate,0,FALSE,2)||!Valid(duplicate))return PinFail(p,Failure::DuplicateFailed);
 p->handle=duplicate;p->state=State::Pinned;return true;
}
bool OpenReplayPin(Pin* p,const char* path,std::uint64_t provider,std::uint64_t generation,std::uint64_t scope)noexcept{
 if(!InitPin(p,path,provider,generation,scope))return false;
 p->handle=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(!Valid(p->handle)){p->handle=nullptr;return PinFail(p,Failure::OpenFailed);}
 p->state=State::Pinned;return true;
}
bool BeginRead(ReadJob* j,const Pin& p,std::uint64_t maximum)noexcept{
 if(!j||j->state!=State::Empty||j->reader||p.state!=State::Pinned||!Valid(p.handle)||!maximum)return false;
 Identity pinned={};if(!FileIdentity(p.handle,&pinned))return JobFail(j,Failure::IdentityMismatch);
 const auto total=(static_cast<std::uint64_t>(pinned.size_high)<<32)|pinned.size_low;
 if(total>maximum)return JobFail(j,Failure::InvalidSize);
 j->reader=CreateFileA(p.path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(!Valid(j->reader)){j->reader=nullptr;return JobFail(j,Failure::OpenFailed);}
 if(!FileIdentity(j->reader,&j->identity)||!Same(pinned,j->identity))return JobFail(j,Failure::IdentityMismatch);
 j->total=total;j->maximum_bytes=maximum;j->position=0;j->format=Format::PinnedRawSpeechBankFile;
 j->provider_identity=p.provider_identity;j->provider_generation=p.provider_generation;j->pin_scope_generation=p.scope_generation;
 dluma_sha256_init(&j->hash);j->state=State::Reading;return true;
}
State Step(ReadJob* j)noexcept{
 if(!j||j->state!=State::Reading)return j?j->state:State::Failed;
 const auto left=j->total-j->position;
 const auto wanted=static_cast<DWORD>(left<kChunkBytes?left:kChunkBytes);DWORD got=0;
 if(!ReadFile(j->reader,j->bytes,wanted?wanted:1,&got,nullptr)||got>wanted||(wanted&&!got)){
  JobFail(j,Failure::ReadFailed);return j->state;}
 if(got){dluma_sha256_update(&j->hash,j->bytes,got);j->position+=got;return j->state;}
 Identity current={};if(!FileIdentity(j->reader,&current)||!Same(j->identity,current)){JobFail(j,Failure::IdentityMismatch);return j->state;}
 dluma_sha256_final(&j->hash,j->sha256);j->state=State::Hashed;return j->state;
}
bool VerifyExpected(ReadJob* j,const unsigned char expected[32])noexcept{
 if(!j||j->state!=State::Hashed||!expected)return false;
 if(std::memcmp(j->sha256,expected,32))return JobFail(j,Failure::ContentMismatch);
 j->state=State::Verified;return true;
}
bool ReadyForNativeSpeechLoad(const ReadJob& j,const Pin& p)noexcept{
 return j.state==State::Verified&&HistoricalContentHeld(j,p);
}
bool HistoricalContentHeld(const ReadJob& j,const Pin& p)noexcept{
 if((j.state!=State::Hashed&&j.state!=State::Verified)||p.state!=State::Pinned||!Valid(j.reader)||!Valid(p.handle)||
 j.provider_identity!=p.provider_identity||j.provider_generation!=p.provider_generation||j.pin_scope_generation!=p.scope_generation)return false;
 Identity a={},b={};return FileIdentity(j.reader,&a)&&FileIdentity(p.handle,&b)&&Same(a,b)&&Same(a,j.identity);
}
bool PinStillNamesContent(const Pin& p,const Identity& identity)noexcept{
 Identity current={};return p.state==State::Pinned&&FileIdentity(p.handle,&current)&&Same(current,identity);
}
bool CloseRead(ReadJob* j)noexcept{
 if(!j)return false;if(Valid(j->reader)&&!CloseHandle(j->reader))return JobFail(j,Failure::CloseFailed);
 j->reader=nullptr;j->state=State::Closed;return true;
}
bool ReleasePin(Pin* p)noexcept{
 if(!p)return false;if(Valid(p->handle)&&!CloseHandle(p->handle))return PinFail(p,Failure::CloseFailed);
 p->handle=nullptr;p->state=State::Closed;return true;
}
bool TransferPin(Pin* from,Pin* to)noexcept{
 if(!from||!to||from==to||from->state!=State::Pinned||!Valid(from->handle)||to->state!=State::Empty||to->handle)return false;
 *to=*from;from->handle=nullptr;from->state=State::Closed;return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::speech_content_lease
