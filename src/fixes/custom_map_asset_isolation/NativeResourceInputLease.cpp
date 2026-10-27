#include "NativeResourceInputLease.h"
#include <cstring>
extern "C" {
__declspec(dllimport) BOOL WINAPI DuplicateHandle(HANDLE,HANDLE,HANDLE,HANDLE*,DWORD,BOOL,DWORD);
}
namespace dluma::fixes::custom_map_asset_isolation::resource_input {
namespace {
bool Valid(HANDLE handle)noexcept{return handle&&handle!=reinterpret_cast<HANDLE>(~std::uintptr_t{0});}
bool SameFile(const file_lifetime::FileRecord& a,const file_lifetime::FileRecord& b)noexcept{
 return a.file==b.file&&a.generation==b.generation&&a.kind==b.kind&&a.source.address==b.source.address&&
 a.source.generation==b.source.generation&&a.life==file_lifetime::Life::Live&&b.life==file_lifetime::Life::Live;
}
bool Same(const Identity& a,const Identity& b)noexcept{
 return a.volume==b.volume&&a.index_high==b.index_high&&a.index_low==b.index_low&&a.size_high==b.size_high&&a.size_low==b.size_low&&
 a.creation.dwLowDateTime==b.creation.dwLowDateTime&&a.creation.dwHighDateTime==b.creation.dwHighDateTime&&
 a.write.dwLowDateTime==b.write.dwLowDateTime&&a.write.dwHighDateTime==b.write.dwHighDateTime;
}
bool Info(HANDLE handle,Identity* out)noexcept{
 BY_HANDLE_FILE_INFORMATION info={};
 if(!out||!Valid(handle)||!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&0x10)||
 (!info.nFileIndexHigh&&!info.nFileIndexLow))return false;
 *out={info.dwFileAttributes,info.ftCreationTime,info.ftLastAccessTime,info.ftLastWriteTime,info.dwVolumeSerialNumber,
 info.nFileSizeHigh,info.nFileSizeLow,info.nNumberOfLinks,info.nFileIndexHigh,info.nFileIndexLow};return true;
}
bool Fail(Pin* p,Failure why)noexcept{if(p){p->failure=why;p->state=State::Failed;}return false;}
bool Fail(Job* j,Failure why)noexcept{if(j){j->failure=why;j->state=State::Failed;}return false;}
}
bool Capture(file_lifetime::bridge::State* bridge,const file_lifetime::FileRecord& expected,
 std::uint64_t generation,Pin* out)noexcept{
 struct LastError {DWORD value=GetLastError();~LastError(){SetLastError(value);}} last_error;
 if(!out||out->state!=State::Empty||out->handle||!bridge||!bridge->binding.validated||!generation)
 return Fail(out,Failure::InvalidIdentity);
 file_lifetime::FileRecord actual={};
 if(!file_lifetime::bridge::FileIdentity(bridge,expected.file,&actual)||!SameFile(actual,expected))return Fail(out,Failure::InvalidIdentity);
 std::size_t offset=0;std::uintptr_t expected_vtable=0;
 if(actual.kind==file_lifetime::FileKind::Win32){offset=0x20;expected_vtable=bridge->binding.base+0x3F908;}
 else if(actual.kind==file_lifetime::FileKind::AsyncWin32){offset=0x28;expected_vtable=bridge->binding.base+0x3F978;}
 else return Fail(out,Failure::UnsupportedClass);
 std::uintptr_t vtable=0;HANDLE original=nullptr;
 if(actual.file>UINTPTR_MAX-offset-sizeof(original)||!bridge->read||
 !bridge->read(bridge->read_user,actual.file,&vtable,8)||vtable!=expected_vtable||
 !bridge->read(bridge->read_user,actual.file+offset,&original,sizeof(original))||!Valid(original))
 return Fail(out,Failure::InvalidIdentity);
 HANDLE duplicate=nullptr;const auto process=GetCurrentProcess();
 if(!DuplicateHandle(process,original,process,&duplicate,0,FALSE,2)||!Valid(duplicate))return Fail(out,Failure::DuplicateFailed);
 out->handle=duplicate;
 file_lifetime::FileRecord after={};
 if(!file_lifetime::bridge::FileIdentity(bridge,expected.file,&after)||!SameFile(after,expected)){
  if(!CloseHandle(duplicate))return Fail(out,Failure::CloseFailed);
  out->handle=nullptr;return Fail(out,Failure::IdentityChanged);}
 out->handle=duplicate;out->file=actual;out->input_generation=generation;out->state=State::Pinned;return true;
}
bool Begin(const Pin& pin,std::uint64_t maximum,Job* job)noexcept{
 if(!job||job->state!=State::Empty||job->reader||pin.state!=State::Pinned||!maximum)return Fail(job,Failure::InvalidIdentity);
 Identity before={};if(!Info(pin.handle,&before))return Fail(job,Failure::InvalidIdentity);
 const auto bytes=(static_cast<std::uint64_t>(before.size_high)<<32)|before.size_low;
 if(bytes>maximum)return Fail(job,Failure::InvalidSize);
 job->reader=ReOpenFile(pin.handle,GENERIC_READ,FILE_SHARE_READ,0x08000000);
 if(!Valid(job->reader)){job->reader=nullptr;return Fail(job,Failure::ReopenFailed);}
 if(!Info(job->reader,&job->identity)||!Same(before,job->identity))return Fail(job,Failure::IdentityChanged);
 job->bytes=bytes;job->position=0;dluma_sha256_init(&job->hash);job->state=State::Reading;return true;
}
State Step(const Pin& pin,Job* job)noexcept{
 if(!job||job->state!=State::Reading)return job?job->state:State::Failed;
 if(pin.state!=State::Pinned||!Valid(pin.handle)||!Valid(job->reader)){Fail(job,Failure::InvalidIdentity);return job->state;}
 const auto remaining=job->bytes-job->position;const DWORD wanted=static_cast<DWORD>(remaining<kChunkBytes?remaining:kChunkBytes);
 DWORD got=0;if(!ReadFile(job->reader,job->buffer,wanted?wanted:1,&got,nullptr)||got>wanted||(wanted&&!got)){
  Fail(job,Failure::ReadFailed);return job->state;}
 if(got){dluma_sha256_update(&job->hash,job->buffer,got);job->position+=got;return job->state;}
 Identity current={},pinned={};
 if(!Info(job->reader,&current)||!Info(pin.handle,&pinned)||!Same(current,job->identity)||!Same(pinned,job->identity)){
  Fail(job,Failure::IdentityChanged);return job->state;}
 dluma_sha256_final(&job->hash,job->sha256);job->state=State::Complete;return job->state;
}
bool Close(Pin* pin,Job* job)noexcept{
 if(!pin||!job)return false;
 bool closed=true;
 if(Valid(job->reader)){if(!CloseHandle(job->reader)){Fail(job,Failure::CloseFailed);closed=false;}else job->reader=nullptr;}
 if(Valid(pin->handle)){if(!CloseHandle(pin->handle)){Fail(pin,Failure::CloseFailed);closed=false;}else pin->handle=nullptr;}
 if(closed){pin->state=State::Closed;if(job->state!=State::Complete)job->state=State::Closed;}
 return closed;
}
bool ReleasePin(Pin* pin)noexcept{
 if(!pin)return false;
 if(Valid(pin->handle)&&!CloseHandle(pin->handle))return Fail(pin,Failure::CloseFailed);
 pin->handle=nullptr;pin->state=State::Closed;return true;
}
} // namespace
