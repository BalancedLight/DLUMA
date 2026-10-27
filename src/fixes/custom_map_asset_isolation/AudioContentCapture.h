#ifndef DLUMA_AUDIO_CONTENT_CAPTURE_H
#define DLUMA_AUDIO_CONTENT_CAPTURE_H
extern "C" {
#include "../../dlw_common.h"
}
#include "AudioStartupObservationLedger.h"
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_content_capture {
constexpr std::size_t kCapacity = 288U;
constexpr std::size_t kReadChunk = 64U * 1024U;
enum class Kind : std::uint8_t { Script, FmodBank, SpeechBank };
enum class ContentFormat : std::uint8_t { RawFileBytes, EngineParserUtf16DefinitionAndBody };
enum class State : std::uint8_t { Empty, Prepared, Reading, Complete, Failed, Cancelled };
enum class Failure : std::uint8_t {
    None, InvalidInput, ObservationLoss, IncompleteCoverage, Capacity,
    AmbiguousOrder, IncompleteProvider, FenceChanged, LeaseUnavailable,
    OpenFailed, ProviderChanged, InvalidLength, ReadFailed, ContentChanged,
    CloseFailed, Cancelled,
};
struct Target {
    Kind kind;
    std::uint64_t enter_sequence, exit_sequence, parent_sequence;
    std::uint64_t source_generation;
    std::uintptr_t owner, source;
    bool localized;
    /* Never derive these native arguments from a bank basename. The path
     * resolver copies the exact open's consumed path from its observation. */
    char logical_path[256];
    char read_path[1024];
    bool physical_native_path;
};
struct Content {
    Target target;
    ContentFormat format;
    unsigned char sha256[32];
    std::uint64_t length;
    std::uint64_t lease_content_identity;
};
struct ParsedInputScope {
    std::uint64_t audio_load_sequence, source_generation;
    std::uintptr_t source;
    std::uint32_t thread_id;
    char logical_path[256];
    unsigned char sha256[32];
    std::uint64_t bytes;
    std::uint32_t observations;
    bool exact_provider, lost;
};
using ReadableFn = bool (*)(void*, const void*, std::size_t) noexcept;
/* Hook ENGINE Parser_GetTreeFromMemory at0x6B7E90, not the gamedll copy.
 * Audio's exact callsite is0x6B7BB9 (return0x6B7BBE). Its uint32 count is
 * WCHAR elements and includes the generated definition plus both CRLFs.
 * This bounded CPU-only observation does no I/O, allocation or engine call.
 * The digest is semantic parser bytes; never compare it to raw SCR bytes. */
bool ObserveParsedInput(ParsedInputScope*, const void* wide_input,
                        std::uint32_t wide_count, const char* copied_path,
                        std::uint32_t thread_id, std::uintptr_t engine_base,
                        std::uintptr_t caller_return, std::uintptr_t parser_option,
                        std::size_t maximum_bytes, ReadableFn, void*) noexcept;
bool ParsedInputReady(const ParsedInputScope&) noexcept;
bool PublishParsedInput(const ParsedInputScope&,
                        audio_startup::AudioStartupScriptObservation*) noexcept;
struct Fence {
    std::uint64_t observation_revision, source_generation, creator_generation;
    std::uint32_t active_audio_producers;
    bool first_project_seen;
};
/* A lease must pin the source AND the historical loaded bytes until release.
 * For an archive this includes its existing immutable backing handle. For a
 * resident bank it includes the existing native bank file. A loose SCR whose
 * parser closed its handle needs a digest of the actual parser input; a mere
 * path/provider-generation check is deliberately insufficient. */
struct Lease {
    std::uint64_t token, loaded_content_identity;
    unsigned char expected_sha256[32];
    bool compare_loaded_digest;
};
using FenceFn = bool (*)(void*, Fence*) noexcept;
using ResolvePathFn = bool (*)(void*, Target*) noexcept;
using AcquireFn = bool (*)(void*, const Target*, Lease*) noexcept;
using ValidateLeaseFn = bool (*)(void*, const Target*, const Lease*) noexcept;
using ReleaseFn = void (*)(void*, const Lease*) noexcept;
using OpenFn = std::uintptr_t (*)(void*, const Target*, std::uintptr_t* source,
                                std::uint64_t* length) noexcept;
using ReadFn = std::uint64_t (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
using CloseFn = bool (*)(void*, std::uintptr_t) noexcept;
struct Operations {
    void* user;
    FenceFn read_fence;
    ResolvePathFn resolve_observed_path;
    AcquireFn acquire_loaded_content_lease;
    ValidateLeaseFn validate_loaded_content_lease;
    ReleaseFn release_loaded_content_lease;
    OpenFn open_owned;
    ReadFn read_owned;
    CloseFn close_owned;
};
struct Capture {
    Operations operations;
    Fence fence;
    Content entries[kCapacity];
    std::uint32_t count, completed;
    std::uint64_t maximum_file_bytes, read_bytes;
    std::uintptr_t handle;
    Lease lease;
    DlumaSha256Context sha;
    unsigned char buffer[kReadChunk];
    State state;
    Failure failure;
};
void Initialize(Capture*, const Operations*) noexcept;
/* Converts the actual bounded startup observation snapshot, not a caller
 * supplied ready manifest. No native I/O occurs here or in producer hooks. */
bool Prepare(Capture*, const audio_startup::AudioStartupObservationSnapshot&,
             std::uint64_t creator_generation,
             std::uint64_t maximum_file_bytes) noexcept;
/* Call only from a cooperative off-hook owner. At most one read of 64 KiB
 * occurs per step; there is no loop waiting for filesystem/audio progress. */
State Step(Capture*) noexcept;
void Cancel(Capture*) noexcept;
bool RevalidateCompleted(Capture*) noexcept;

using ReadMemoryFn = bool (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
struct NativeFilesystem {
    std::uintptr_t base;
    ReadMemoryFn read_memory;
    void* user;
    bool validated;
};
bool ValidateNativeFilesystem(NativeFilesystem*, bool exact_player_filesystem) noexcept;
/* The caller must hold its source/content lease across Open/Read/Close.
 * These use independent SFsFile objects; engine-owned file positions never
 * change. Native source+0x18 is verified for directory and pack readers. */
std::uintptr_t OpenNativeOwned(NativeFilesystem*, const Target*,
                              std::uintptr_t*, std::uint64_t*) noexcept;
std::uint64_t ReadNativeOwned(NativeFilesystem*, std::uintptr_t,
                            void*, std::size_t) noexcept;
bool CloseNativeOwned(NativeFilesystem*, std::uintptr_t) noexcept;
std::uint64_t LengthNativeOwned(NativeFilesystem*,std::uintptr_t) noexcept;
// Reads one still-listed original B0 provider even while project overrides
// win normal lookup. Native fs::path and vslot160 match public read dispatch.
// Returned SFsFile/source identity must still be joined through FileHookBridge.
std::uintptr_t OpenNativeFromSource(NativeFilesystem*,std::uintptr_t source,
                                    const char* logical_path) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::audio_content_capture
#endif
