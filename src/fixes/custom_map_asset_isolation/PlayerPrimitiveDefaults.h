#ifndef DLUMA_PLAYER_PRIMITIVE_DEFAULTS_H
#define DLUMA_PLAYER_PRIMITIVE_DEFAULTS_H
extern "C" {
#include "../../dlw_common.h"
}
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::player_primitive_defaults {
constexpr std::size_t kPropertyCount=1042,kStringBytes=64;
enum class Kind:std::uint8_t { Reserved,Number,Boolean,String };
struct Value {Kind kind;std::uint32_t bits;char text[kStringBytes];};
struct Snapshot {
 Value values[kPropertyCount];unsigned char sha256[32];std::uint32_t captured_values;
 bool compiled_primitive_defaults_complete;
 bool template_effective_defaults_complete;
};
using NumberFn=bool(*)(void*,std::uint32_t,std::uint32_t*)noexcept;
using BooleanFn=bool(*)(void*,std::uint32_t,bool*)noexcept;
using StringFn=bool(*)(void*,std::uint32_t,char*,std::size_t)noexcept;
struct Operations {void* user;NumberFn number;BooleanFn boolean;StringFn string;bool exact_native_targets;};
bool Capture(const Operations&,Snapshot*)noexcept;
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
struct NativeBinding {std::uintptr_t gamedll_base,native_free;ReadFn read;void* user;bool validated;};
bool Validate(NativeBinding*,const char* exact_image_sha256)noexcept;
Operations MakeOperations(NativeBinding*)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::player_primitive_defaults
#endif
