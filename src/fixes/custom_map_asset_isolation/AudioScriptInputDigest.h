#ifndef DLUMA_AUDIO_SCRIPT_INPUT_DIGEST_H
#define DLUMA_AUDIO_SCRIPT_INPUT_DIGEST_H
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::audio_script_input {
enum class Result : std::uint8_t { Succeeded,InvalidArgument,Capacity,ConversionFailed,IndeterminateNativeTail };
struct Digest {
    unsigned char sha256[32];
    std::uint64_t bytes;
    std::uint32_t ansi_code_page;
};
// The definition must be the actual current native generated definition, and
// raw_bytes must come from an independently opened/identified current provider.
// Caller owns scratch storage; no native parser, registry or engine call occurs.
// CP_ACP is captured in the output because it belongs to the input identity.
Result Compute(const char* definition,std::size_t definition_bytes,
               const unsigned char* raw_bytes,std::size_t raw_length,
               std::uint16_t* scratch,std::size_t scratch_words,Digest*) noexcept;
}
#endif
