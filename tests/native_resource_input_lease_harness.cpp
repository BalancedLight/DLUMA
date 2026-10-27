#include "fixes/custom_map_asset_isolation/NativeResourceInputLease.h"
#include <cstdio>
#include <cstring>
extern "C" __declspec(dllimport) DWORD WINAPI GetTempPathW(DWORD,LPWSTR);
extern "C" __declspec(dllimport) UINT WINAPI GetTempFileNameW(LPCWSTR,LPCWSTR,UINT,LPWSTR);
constexpr DWORD kTemporaryAttribute=0x100,kSharingViolation=32;
namespace r=dluma::fixes::custom_map_asset_isolation::resource_input;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
static f::FileRecord observed={};static unsigned identity_reads=0;static bool change_generation=false;
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::bridge {
bool FileIdentity(State*,std::uintptr_t address,FileRecord* out)noexcept{
 if(address!=observed.file||!out)return false;*out=observed;
 if(change_generation&&++identity_reads>1)++out->generation;return true;
}
}
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s (%lu)\n",__LINE__,#x,GetLastError());return false;}}while(0)
struct Fixture {
 f::bridge::State bridge={};unsigned char native[104]={};WCHAR path[MAX_PATH]={};HANDLE original=INVALID_HANDLE_VALUE;
 r::Pin pin={};r::Job job={};unsigned char content[70013]={};bool ready=false;
 Fixture(bool async=false){
  identity_reads=0;change_generation=false;
  WCHAR directory[MAX_PATH]={};const WCHAR prefix[]={'d','l','w',0};
  if(!GetTempPathW(MAX_PATH,directory)||!GetTempFileNameW(directory,prefix,0,path))return;
  for(std::size_t i=0;i<sizeof(content);++i)content[i]=static_cast<unsigned char>((i*37+11)%251);
  HANDLE writer=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,kTemporaryAttribute,nullptr);
  if(writer==INVALID_HANDLE_VALUE)return;DWORD written=0;const bool ok=WriteFile(writer,content,sizeof(content),&written,nullptr)&&written==sizeof(content);
  CloseHandle(writer);if(!ok)return;
  original=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
  if(original==INVALID_HANDLE_VALUE)return;
  bridge.binding.base=0x180000000;bridge.binding.validated=true;bridge.read=Read;bridge.read_user=this;
  const auto vtable=bridge.binding.base+(async?0x3F978:0x3F908);std::memcpy(native,&vtable,8);
  std::memcpy(native+(async?0x28:0x20),&original,sizeof(original));
  observed={};observed.file=reinterpret_cast<std::uintptr_t>(native);observed.generation=3;observed.life=f::Life::Live;
  observed.kind=async?f::FileKind::AsyncWin32:f::FileKind::Win32;ready=true;
 }
 ~Fixture(){(void)r::Close(&pin,&job);if(original!=INVALID_HANDLE_VALUE)CloseHandle(original);if(path[0])DeleteFileW(path);}
 static bool Read(void* user,std::uintptr_t address,void* output,std::size_t size)noexcept{
  const auto* f=static_cast<Fixture*>(user);const auto begin=reinterpret_cast<std::uintptr_t>(f->native);
  if(address<begin||address-begin>sizeof(f->native)||size>sizeof(f->native)-(address-begin))return false;
  std::memcpy(output,reinterpret_cast<void*>(address),size);return true;
 }
 bool Capture(){return r::Capture(&bridge,observed,9,&pin);}
 bool Digest(){if(!r::Begin(pin,sizeof(content),&job))return false;
  for(unsigned i=0;i<8&&job.state==r::State::Reading;++i)(void)r::Step(pin,&job);
  unsigned char expected[32]={};DlumaSha256Context hash={};dluma_sha256_init(&hash);dluma_sha256_update(&hash,content,sizeof(content));dluma_sha256_final(&hash,expected);
  return job.state==r::State::Complete&&job.bytes==sizeof(content)&&!std::memcmp(job.sha256,expected,32);
 }
};
bool Win32Digest(){Fixture f;CHECK(f.ready);CHECK(f.Capture());CHECK(f.pin.file.source.address==0&&f.pin.file.source.generation==0);CHECK(f.Digest());return true;}
bool AsyncHandleOffset(){Fixture f(true);CHECK(f.ready&&f.Capture());CHECK(f.Digest());return true;}
bool OriginalCursorPreserved(){Fixture f;CHECK(f.ready);unsigned char bytes[3]={};DWORD read=0;CHECK(ReadFile(f.original,bytes,3,&read,nullptr)&&read==3);
 CHECK(f.Capture()&&f.Digest());CHECK(ReadFile(f.original,bytes,3,&read,nullptr)&&read==3);CHECK(!std::memcmp(bytes,f.content+3,3));return true;}
bool CloseOriginalAfterPin(){Fixture f;CHECK(f.ready&&f.Capture());CHECK(CloseHandle(f.original));f.original=INVALID_HANDLE_VALUE;CHECK(f.Digest());return true;}
bool StaleGeneration(){Fixture f;CHECK(f.ready);auto stale=observed;++stale.generation;CHECK(!r::Capture(&f.bridge,stale,9,&f.pin));CHECK(!f.pin.handle);return true;}
bool GenerationChangesWhilePinning(){Fixture f;CHECK(f.ready);change_generation=true;CHECK(!f.Capture());CHECK(f.pin.failure==r::Failure::IdentityChanged&&!f.pin.handle);return true;}
bool UnsupportedArchive(){Fixture f;CHECK(f.ready);observed.kind=f::FileKind::Zip;CHECK(!f.Capture());CHECK(f.pin.failure==r::Failure::UnsupportedClass);return true;}
bool ReaderDeniesWrites(){Fixture f;CHECK(f.ready&&f.Capture());CHECK(r::Begin(f.pin,sizeof(f.content),&f.job));
 HANDLE writer=CreateFileW(f.path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
 if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);CHECK(false);}CHECK(GetLastError()==kSharingViolation);return true;}
bool MaximumSize(){Fixture f;CHECK(f.ready&&f.Capture());CHECK(!r::Begin(f.pin,4,&f.job));CHECK(f.job.failure==r::Failure::InvalidSize&&!f.job.reader);return true;}
bool PreservesLastErrorAndClosesHandles(){Fixture f;CHECK(f.ready);SetLastError(731);CHECK(f.Capture());CHECK(GetLastError()==731);CHECK(f.Digest());
 CHECK(r::Close(&f.pin,&f.job));CHECK(!f.pin.handle&&!f.job.reader);CHECK(r::Close(&f.pin,&f.job));return true;}
int main(){const struct{const char* name;bool(*run)();} tests[]={
 {"Win32 source-zero digest",Win32Digest},{"async native HANDLE offset",AsyncHandleOffset},{"original cursor preserved",OriginalCursorPreserved},
 {"pin outlives original",CloseOriginalAfterPin},{"stale native generation",StaleGeneration},{"generation changes during duplicate",GenerationChangesWhilePinning},
 {"unsupported archive fails closed",UnsupportedArchive},{"reader excludes writers",ReaderDeniesWrites},{"bounded file size",MaximumSize},
 {"last error and actual close",PreservesLastErrorAndClosesHandles}};
 for(const auto& test:tests)if(!test.run()){std::printf("FAILED %s\n",test.name);return 1;}
 std::printf("PASS %zu real-file resource-input scenarios\n",sizeof(tests)/sizeof(tests[0]));return 0;}
