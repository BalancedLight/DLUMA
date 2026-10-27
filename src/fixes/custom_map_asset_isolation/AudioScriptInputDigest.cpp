#define _HAS_EXCEPTIONS 0
#include "AudioScriptInputDigest.h"
extern "C" {
#include "../../dlw_common.h"
__declspec(dllimport) UINT WINAPI GetACP();
}
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::audio_script_input {
namespace {
Result Convert(const char* bytes,std::size_t count,std::uint16_t* out,std::size_t* words) noexcept {
    *words=0;if(!count)return Result::Succeeded;
    const int emitted=MultiByteToWideChar(0,0,bytes,static_cast<int>(count),reinterpret_cast<LPWSTR>(out),static_cast<int>(count));
    if(emitted<=0)return Result::ConversionFailed;
    std::size_t used=0;while(used<static_cast<std::size_t>(emitted)&&out[used])++used;
    // Native load_wide_string places the terminator at raw byte length, not
    // the actual conversion length. A shorter DBCS conversion with no earlier
    // NUL exposes uninitialized native memory. No deterministic digest exists.
    if(used==static_cast<std::size_t>(emitted)&&used!=count)return Result::IndeterminateNativeTail;
    *words=used;return Result::Succeeded;
}
}
Result Compute(const char* definition,std::size_t definition_bytes,const unsigned char* raw,std::size_t length,
               std::uint16_t* scratch,std::size_t capacity,Digest* output) noexcept {
    if(output)*output={};
    if(!definition||!scratch||!output||(length&&!raw)||definition_bytes>INT32_MAX||length>INT32_MAX||
       std::memchr(definition,0,definition_bytes))return Result::InvalidArgument;
    if(definition_bytes>SIZE_MAX-length-5||capacity<definition_bytes+length+5)return Result::Capacity;
    std::size_t definition_words=0;
    auto result=Convert(definition,definition_bytes,scratch,&definition_words);
    if(result!=Result::Succeeded)return result;
    if(definition_words!=definition_bytes)return Result::IndeterminateNativeTail;
    std::size_t at=definition_words;scratch[at++]=13;scratch[at++]=10;
    std::size_t body_words=0;
    if(length>=2&&raw[0]==0xff&&raw[1]==0xfe){
        const std::size_t maximum=(length-2)/2;
        while(body_words<maximum){const std::size_t byte=2+body_words*2;
            const auto word=static_cast<std::uint16_t>(raw[byte]|(static_cast<std::uint16_t>(raw[byte+1])<<8));
            if(!word)break;scratch[at+body_words++]=word;
        }
    }else if(length>=2){
        result=Convert(reinterpret_cast<const char*>(raw),length,scratch+at,&body_words);
        if(result!=Result::Succeeded)return result;
    }
    // Exact native single-byte file path never reads that byte and produces
    // an empty wide body. Empty files follow the same resulting representation.
    at+=body_words;scratch[at++]=13;scratch[at++]=10;scratch[at]=0;
    DlumaSha256Context sha={};dluma_sha256_init(&sha);dluma_sha256_update(&sha,scratch,at*2);dluma_sha256_final(&sha,output->sha256);
    output->bytes=at*2;output->ansi_code_page=GetACP();return Result::Succeeded;
}
}
