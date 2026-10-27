#define _HAS_EXCEPTIONS 0
#include "PlayerXmlInputDigest.h"
extern "C" {
#include "../../dlw_common.h"
}
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::player_xml_input {
bool Capture(std::uintptr_t base,std::uintptr_t returned,std::uintptr_t input,std::uint64_t length,unsigned char owns,ReadFn read,void* user,Digest* out)noexcept{
 if(!out)return false;*out={};
 if(!base||base>UINTPTR_MAX-kCallerReturnRva||returned!=base+kCallerReturnRva||owns!=1||!input||!read||length>kMaximumInputBytes||length>=UINTPTR_MAX-input)return false;
 unsigned char terminator=1;if(!read(user,input+static_cast<std::uintptr_t>(length),&terminator,1)||terminator)return false;
 DlumaSha256Context hash={};dluma_sha256_init(&hash);constexpr char tag[]="DLW_NATIVE_XML_CSTRING_V1";dluma_sha256_update(&hash,tag,sizeof(tag));
 unsigned char scratch[4096];std::uint64_t offset=0;bool stopped=false;
 while(offset<length){const auto amount=static_cast<std::size_t>(length-offset<sizeof(scratch)?length-offset:sizeof(scratch));if(!read(user,input+static_cast<std::uintptr_t>(offset),scratch,amount))return false;
  std::size_t prefix=0;while(prefix<amount&&scratch[prefix])++prefix;dluma_sha256_update(&hash,scratch,prefix);offset+=prefix;
  if(prefix<amount){stopped=true;break;}
 }
 /* Delimit the input format and length, rather than confusing this with a
  * raw file SHA or with the engine's UTF-16 script-definition format. */
 dluma_sha256_update(&hash,&offset,sizeof(offset));dluma_sha256_final(&hash,out->sha256);
 out->file_length=length;out->consumed_bytes=offset;out->embedded_nul=stopped;out->format=Format::NativeXmlCStringV1;out->complete=true;return true;
}
}
