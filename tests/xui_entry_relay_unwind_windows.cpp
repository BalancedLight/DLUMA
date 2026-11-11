#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/fixes/custom_map_asset_isolation/NativeEntryUnwind.h"
namespace d=dluma::fixes::custom_map_asset_isolation::detour;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
struct Step {unsigned end,kind,reg;int value;}; // 1 save,2 push,3 alloc,4 change,5 lea
static const Step manager[]={{5,1,3,8},{10,1,5,16},{15,1,6,24},{16,2,7,0}};
static const Step compiled[]={{5,1,3,16},{10,1,6,24},{11,2,7,0},{15,3,0,48},{21,0,0,0}};
static const Step xml[]={{5,1,3,8},{10,1,6,16},{15,1,7,24},{20,1,12,32}};
static const Step create[]={{5,1,3,8},{10,0,0,0},{11,2,5,0},{12,2,6,0},{13,2,7,0},{15,2,12,0},{17,2,13,0}};
static const Step binary[]={{2,2,5,0},{4,2,13,0},{6,2,14,0},{8,2,15,0},{13,5,5,-56},{20,3,0,312}};
static const Step retire[]={{3,0,0,0},{9,0,0,0},{14,1,6,24},{15,2,7,0},{19,3,0,32}};
static const Step collect[]={{5,1,3,16},{10,1,5,24},{11,2,6,0},{15,3,0,32},{18,4,5,0}};
static const Step all[]={{5,1,5,32},{6,2,7,0},{10,3,0,32},{13,4,5,0},{17,0,0,0}};
struct Case {const char* code;const char* unwind;const Step* steps;unsigned count;bool xmm;};
#define STEPS(v) v,static_cast<unsigned>(sizeof(v)/sizeof(v[0]))
static const Case cases[]={
 {"48895c240848896c2410488974241857","013a0e003a6803001c6410001c540f001c340e001c7218f016e014d012c01070",STEPS(manager),true},
 {"48895c240848896c2410488974241857","0114080014640800145407001434060014321070",STEPS(manager),false},
 {"48895c24104889742418574883ec308b81d0000000","010f06000f640a000f3409000f520b70",STEPS(compiled),false},
 {"48895c2408488974241048897c24184c89642420","012e0d002ec40f052e740e052e640d052e340c052e01080519e017d015500000",STEPS(xml),true},
 {"48895c24084c8944241855565741544155","01210b00213430002101280015f013e011d00fc00d700c600b500000",STEPS(create),false},
 {"4055415541564157488d6c24c84881ec38010000","011406001401270008f006e004d00250",STEPS(binary),false},
 {"48895c24084c894c242055565741544155","01190a001934140019b215f013e011d00fc00d700c600b50",STEPS(create),false},
 {"4885d20f845f0100004889742418574883ec20","011304001364080013320f70",STEPS(retire),false},
 {"48895c241048896c2418564883ec20488be9","010f06000f5408000f3407000f320b60",STEPS(collect),false},
 {"48896c2420574883ec20488be94883c128","010a04000a5409000a320670",STEPS(all),false}};
static unsigned Hex(unsigned char c){return c<='9'?c-'0':c-'a'+10;}
static std::size_t Decode(const char* text,unsigned char* out){std::size_t n=0;while(*text){*out++=static_cast<unsigned char>(16*Hex(text[0])+Hex(text[1]));text+=2;++n;}return n;}
static DWORD64& Reg(CONTEXT& c,unsigned reg){switch(reg){case 3:return c.Rbx;case 5:return c.Rbp;case 6:return c.Rsi;case 7:return c.Rdi;
 case 12:return c.R12;case 13:return c.R13;case 14:return c.R14;default:return c.R15;}}
static constexpr unsigned regs[]={3,5,6,7,12,13,14,15};
static bool Read(void* user,std::uintptr_t at,void* out,std::size_t count)noexcept{auto& image=*static_cast<std::vector<unsigned char>*>(user);
 if(at<0x20000000||at-0x20000000>image.size()||count>image.size()-(at-0x20000000))return false;
 std::memcpy(out,image.data()+at-0x20000000,count);return true;}
static bool Unwind(const Case& test,unsigned pc,unsigned char* relay,RUNTIME_FUNCTION* function){
 alignas(16) DWORD64 stack[256]={};const auto entry=reinterpret_cast<DWORD64>(stack+192);stack[192]=0x1234567890ULL;
 CONTEXT c={};c.Rip=reinterpret_cast<DWORD64>(relay)+pc;c.Rsp=entry;c.ContextFlags=CONTEXT_FULL;
 for(auto reg:regs)Reg(c,reg)=0x11110000+reg;
 for(unsigned i=0;i<test.count;++i){auto s=test.steps[i];if(s.end>pc)break;
  if(s.kind==1)*reinterpret_cast<DWORD64*>(c.Rsp+s.value)=Reg(c,s.reg);
  if(s.kind==2){c.Rsp-=8;*reinterpret_cast<DWORD64*>(c.Rsp)=Reg(c,s.reg);}
  if(s.kind==3)c.Rsp-=s.value;if(s.kind==4)Reg(c,s.reg)=0x76543210;if(s.kind==5)Reg(c,s.reg)=c.Rsp+s.value;}
 PVOID handler=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,reinterpret_cast<DWORD64>(relay),c.Rip,function,&c,&handler,&frame,nullptr);
 if(c.Rip!=stack[192]||c.Rsp!=entry+8){std::printf("pc%u rsp delta%lld rip%llx\n",pc,static_cast<long long>(c.Rsp-entry),c.Rip);return false;}
 for(auto reg:regs)if(Reg(c,reg)!=0x11110000+reg){std::printf("pc%u reg%u=%llx\n",pc,reg,Reg(c,reg));return false;}return true;
}
int main(){
 for(unsigned index=0;index<sizeof(cases)/sizeof(cases[0]);++index){const auto& test=cases[index];
  std::printf("case%u\n",index);
  std::vector<unsigned char> source(0x4000);const std::uint32_t nt=0x100,directory=0x900,table_bytes=12;
  std::memcpy(source.data()+0x3c,&nt,4);source[nt+24]=0xb;source[nt+25]=2;
  std::memcpy(source.data()+nt+24+112+24,&directory,4);std::memcpy(source.data()+nt+24+112+28,&table_bytes,4);
  const std::uint32_t record[]={0x1000,0x2000,0x3000};std::memcpy(source.data()+directory,record,12);Decode(test.unwind,source.data()+0x3000);
  auto* relay=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(relay);
  unsigned stolen=static_cast<unsigned>(Decode(test.code,relay));
  const auto continuation=reinterpret_cast<std::uintptr_t>(relay)+512;CHECK(d::BuildEntryRelayTransfer(relay+stolen,14,continuation));
  d::EntryUnwindImage image={0x20000000,static_cast<std::uint32_t>(source.size()),16,&Read,&source,test.xmm};
  std::size_t bytes=0;CHECK(d::CopyEntryRelayUnwind(image,0x1000,stolen,stolen+14,relay+128,64,&bytes));
  for(std::size_t i=0;i<bytes;++i)std::printf("%02x",relay[128+i]);std::puts("");
  auto* function=reinterpret_cast<RUNTIME_FUNCTION*>(relay+256);*function={0,stolen+14,128};DWORD64 found=0;
  CHECK(!RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(relay)+stolen,&found,nullptr));
  CHECK(RtlAddFunctionTable(function,1,reinterpret_cast<DWORD64>(relay)));DWORD old=0;CHECK(VirtualProtect(relay,4096,PAGE_EXECUTE_READ,&old));
  CHECK(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(relay)+stolen,&found,nullptr)==function&&found==reinterpret_cast<DWORD64>(relay));
  CHECK(Unwind(test,0,relay,function));for(unsigned n=0;n<test.count;++n)CHECK(Unwind(test,test.steps[n].end,relay,function));
  CHECK(Unwind(test,stolen,relay,function));CHECK(Unwind(test,stolen+10,relay,function));CHECK(Unwind(test,stolen+13,relay,function));
  CHECK(RtlDeleteFunctionTable(function));CHECK(!RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(relay)+stolen,&found,nullptr));CHECK(VirtualFree(relay,0,MEM_RELEASE));
 }
 std::puts("PASS all ten XUI entry relay prefix/suffix Windows unwind contexts");return 0;
}
