#ifndef DLUMA_PLAYER_XML_INPUT_DIGEST_H
#define DLUMA_PLAYER_XML_INPUT_DIGEST_H
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::player_xml_input {
constexpr std::uintptr_t kParseRva=0x12CE870,kCallerReturnRva=0x12CE802;
constexpr std::uint64_t kMaximumInputBytes=16U*1024U*1024U;
enum class Format:std::uint32_t { Unavailable,NativeXmlCStringV1 };
struct Digest {unsigned char sha256[32];std::uint64_t file_length,consumed_bytes;Format format;bool complete,embedded_nul;};
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
/* length is the observed caller RBX from the one qualified native callsite.
 * The helper performs only bounded memory reads and incremental SHA. */
bool Capture(std::uintptr_t gamedll_base,std::uintptr_t actual_return,
 std::uintptr_t input,std::uint64_t length,unsigned char transfer_ownership,
 ReadFn,void*,Digest*)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::player_xml_input
#endif
