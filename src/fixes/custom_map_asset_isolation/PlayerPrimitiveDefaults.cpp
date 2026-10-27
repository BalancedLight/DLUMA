#define _HAS_EXCEPTIONS 0
#include "PlayerPrimitiveDefaults.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::player_primitive_defaults {
namespace {
bool NativeNumber(void* user,std::uint32_t id,std::uint32_t* out)noexcept{
 auto* b=static_cast<NativeBinding*>(user);if(!b||!b->validated||!out||id<1||id>895)return false;
 const float value=reinterpret_cast<float(__fastcall*)(int)>(b->gamedll_base+0xBDA670)(static_cast<int>(id));
 std::memcpy(out,&value,sizeof(value));return true;
}
bool NativeBoolean(void* user,std::uint32_t id,bool* out)noexcept{
 auto* b=static_cast<NativeBinding*>(user);if(!b||!b->validated||!out||id<898||id>1033)return false;
 *out=reinterpret_cast<bool(__fastcall*)(int)>(b->gamedll_base+0xBDB830)(static_cast<int>(id));return true;
}
bool NativeString(void* user,std::uint32_t id,char* out,std::size_t capacity)noexcept{
 auto* b=static_cast<NativeBinding*>(user);if(!b||!b->validated||!b->native_free||!out||!capacity||id<1036||id>1040)return false;
 struct String {std::uintptr_t data;std::uint32_t size,capacity;};String value={};
 const auto returned=reinterpret_cast<String*(__fastcall*)(String*,int)>(b->gamedll_base+0xBDB6D0)(&value,static_cast<int>(id));
 bool ok=returned==&value&&value.size<capacity&&value.size<=value.capacity;
 if(ok&&value.size)ok=value.data&&b->read(b->user,value.data,out,value.size+1)&&out[value.size]==0;
 else if(ok)out[0]=0;
 if(value.data)reinterpret_cast<void(__cdecl*)(void*)>(b->native_free)(reinterpret_cast<void*>(value.data));
 return ok;
}
}
bool Capture(const Operations& o,Snapshot* out)noexcept{
 if(!out)return false;*out={};if(!o.exact_native_targets||!o.number||!o.boolean||!o.string)return false;
 DlumaSha256Context hash;dluma_sha256_init(&hash);
 for(std::uint32_t id=0;id<kPropertyCount;++id){auto& value=out->values[id];
  if(id>=1&&id<=895){value.kind=Kind::Number;if(!o.number(o.user,id,&value.bits)||(value.bits&0x7F800000U)==0x7F800000U)return false;}
  else if(id>=898&&id<=1033){value.kind=Kind::Boolean;bool v=false;if(!o.boolean(o.user,id,&v))return false;value.bits=v?1U:0U;}
  else if(id>=1036&&id<=1040){value.kind=Kind::String;if(!o.string(o.user,id,value.text,sizeof(value.text))||!std::memchr(value.text,0,sizeof(value.text)))return false;}
  else continue;
  ++out->captured_values;dluma_sha256_update(&hash,&id,sizeof(id));dluma_sha256_update(&hash,&value.kind,sizeof(value.kind));
  dluma_sha256_update(&hash,&value.bits,sizeof(value.bits));
  if(value.kind==Kind::String)dluma_sha256_update(&hash,value.text,std::strlen(value.text)+1);
 }
 dluma_sha256_final(&hash,out->sha256);out->compiled_primitive_defaults_complete=true;
 /* Effective values also require provider-bound class/progression presets,
  * current levels and buffs. Compiled constants alone do not prove them. */
 return true;
}
bool Validate(NativeBinding* b,const char* sha)noexcept{
 if(!b)return false;b->validated=false;
 constexpr char expected[]="cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95";
 if(!sha)return false;
 for(unsigned i=0;i<64;++i){const char value=sha[i]>='A'&&sha[i]<='F'?static_cast<char>(sha[i]+('a'-'A')):sha[i];if(value!=expected[i])return false;}
 if(sha[64]||!b->gamedll_base||b->gamedll_base>UINTPTR_MAX-0x13CC1A0||!b->read)return false;
 constexpr std::uintptr_t rvas[]={0xBDA670,0xBDB830,0xBDB6D0};
 constexpr unsigned char prefixes[][16]={
 {0xff,0xc9,0x81,0xf9,0x7e,0x03,0x00,0x00,0x0f,0x87,0x44,0x02,0x00,0x00,0x48,0x8d},
 {0x81,0xc1,0x7e,0xfc,0xff,0xff,0x81,0xf9,0x87,0x00,0x00,0x00,0x77,0x19,0x48,0x8d},
 {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}};
 unsigned char actual[16];for(unsigned i=0;i<3;++i)if(!b->read(b->user,b->gamedll_base+rvas[i],actual,16)||std::memcmp(actual,prefixes[i],16))return false;
 if(!b->read(b->user,b->gamedll_base+0x13CC198,&b->native_free,sizeof(b->native_free))||!b->native_free)return false;
 b->validated=true;return true;
}
Operations MakeOperations(NativeBinding* b)noexcept{if(!b||!b->validated)return {};return {b,NativeNumber,NativeBoolean,NativeString,true};}
} // namespace dluma::fixes::custom_map_asset_isolation::player_primitive_defaults
