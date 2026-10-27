#include "fixes/custom_map_asset_isolation/AudioScriptInputDigest.h"
extern "C" {
#include "dlw_common.h"
}
#include <cstdio>
#include <cstring>
namespace input=dluma::fixes::custom_map_asset_isolation::audio_script_input;
namespace {
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
void Hash(const std::uint16_t* value,std::size_t words,unsigned char digest[32]){DlumaSha256Context c={};dluma_sha256_init(&c);dluma_sha256_update(&c,value,words*2);dluma_sha256_final(&c,digest);}
}
int main(){std::uint16_t scratch[256]={};input::Digest digest={};unsigned char expected[32]={};
 const std::uint16_t want[]={'!', 'X','(',')',13,10,'a','b',13,10};Hash(want,10,expected);
 const unsigned char ansi[]={'a','b'};
 CHECK(input::Compute("!X()",4,ansi,2,scratch,256,&digest)==input::Result::Succeeded);
 CHECK(digest.bytes==20&&!std::memcmp(digest.sha256,expected,32)&&digest.ansi_code_page);
 const unsigned char wide[]={0xff,0xfe,'a',0,'b',0};
 CHECK(input::Compute("!X()",4,wide,6,scratch,256,&digest)==input::Result::Succeeded);CHECK(!std::memcmp(digest.sha256,expected,32));
 const unsigned char odd[]={0xff,0xfe,'a',0,'b',0,0x17};
 CHECK(input::Compute("!X()",4,odd,7,scratch,256,&digest)==input::Result::Succeeded);CHECK(!std::memcmp(digest.sha256,expected,32));
 const unsigned char nul[]={'a','b',0,'x'};
 CHECK(input::Compute("!X()",4,nul,4,scratch,256,&digest)==input::Result::Succeeded);CHECK(!std::memcmp(digest.sha256,expected,32));
 const std::uint16_t empty[]={'!', 'X','(',')',13,10,13,10};Hash(empty,8,expected);
 CHECK(input::Compute("!X()",4,ansi,1,scratch,256,&digest)==input::Result::Succeeded);CHECK(!std::memcmp(digest.sha256,expected,32));
 CHECK(input::Compute("!X()",4,nullptr,0,scratch,256,&digest)==input::Result::Succeeded);CHECK(!std::memcmp(digest.sha256,expected,32));
 CHECK(input::Compute("!X()",4,wide,6,scratch,4,&digest)==input::Result::Capacity);CHECK(!digest.bytes);
 CHECK(input::Compute("!\0()",4,wide,6,scratch,256,&digest)==input::Result::InvalidArgument);
 std::puts("PASS 8 exact script representation scenarios");return 0;}
