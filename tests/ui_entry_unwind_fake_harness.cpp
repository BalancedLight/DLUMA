#include "../src/fixes/custom_map_asset_isolation/NativeEntryUnwind.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace d=dluma::fixes::custom_map_asset_isolation::detour;
extern "C" bool CheckUiEntryUnwind(const unsigned char*,std::size_t)noexcept;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Fixture {
 std::vector<unsigned char> image=std::vector<unsigned char>(15257600);
 static constexpr std::uintptr_t base=0x20000000;
 static bool Read(void* u,std::uintptr_t address,void* out,std::size_t size)noexcept{
  auto& f=*static_cast<Fixture*>(u);if(address<base||address-base>f.image.size()||size>f.image.size()-(address-base))return false;
  std::memcpy(out,f.image.data()+address-base,size);return true;}
 void Put(std::size_t at,std::uint32_t value){std::memcpy(image.data()+at,&value,4);}
 Fixture(){
  Put(0x3C,0x100);image[0x118]=0x0B;image[0x119]=2;Put(0x100+24+112+24,0x900);Put(0x100+24+112+28,24);
  const std::uint32_t entries[]={0x60C7E0,0x60C7E6,0x9C7ABC,0x60C7E6,0x60C905,0x9EFFC4};
  std::memcpy(image.data()+0x900,entries,sizeof(entries));
  const unsigned char first[]={1,6,2,0,6,0x92,2,0x70};std::memcpy(image.data()+0x9C7ABC,first,sizeof(first));
  const unsigned char second[]={0x21,26,10,0,26,0x88,2,0,20,0x78,3,0,15,0x68,4,0,10,0xC4,15,0,5,0x54,13,0};
  std::memcpy(image.data()+0x9EFFC4,second,sizeof(second));std::memcpy(image.data()+0x9EFFDC,entries,12);
 }
 d::EntryUnwindImage Binding(){return {base,static_cast<std::uint32_t>(image.size()),65536,Read,this};}
};
int main(){
 Fixture f;auto binding=f.Binding();unsigned char output[64];std::memset(output,0xA5,sizeof(output));std::size_t size=99;
 CHECK(!d::CopyEntryUnwind(binding,0x60C7E0,16,output,sizeof(output),&size)&&!size);
 for(auto b:output)CHECK(b==0xA5);
 binding.allow_skipped_xmm=true;
 CHECK(d::CopyEntryUnwind(binding,0x60C7E0,16,output,sizeof(output),&size));
 const unsigned char expected[]={1,16,6,0,16,0xC4,15,0,11,0x54,13,0,6,0x92,2,0x70};
 CHECK(size==sizeof(expected)&&!std::memcmp(output,expected,size));CHECK(CheckUiEntryUnwind(output,size));
 CHECK(!d::CopyEntryUnwind(binding,0x60C7E0,21,output,sizeof(output),&size)&&!size); // XMM6 now within copied prefix
 f.image[0x9EFFC4+2]=9;
 CHECK(!d::CopyEntryUnwind(binding,0x60C7E0,16,output,sizeof(output),&size)&&!size);
 f.image[0x9EFFC4+2]=10;
 const unsigned char far_codes[]={0x21,26,11,0,26,0x89,32,0,0,0,20,0x78,3,0,15,0x68,4,0,10,0xC4,15,0,5,0x54,13,0};
 std::memcpy(f.image.data()+0x9EFFC4,far_codes,sizeof(far_codes));
 const std::uint32_t parent[]={0x60C7E0,0x60C7E6,0x9C7ABC};std::memcpy(f.image.data()+0x9EFFC4+28,parent,12);
 // Count11 pads to12 slots before the chain descriptor.
 f.image[0x9EFFC4+26]=f.image[0x9EFFC4+27]=0;
 CHECK(d::CopyEntryUnwind(binding,0x60C7E0,16,output,sizeof(output),&size));
 CHECK(size==sizeof(expected)&&!std::memcmp(output,expected,size));
 CHECK(!d::CopyEntryUnwind(binding,0x60C7E0,32,output,sizeof(output),&size)&&!size);
 std::puts("PASS: bounded skipped XMM unwind");return 0;
}
