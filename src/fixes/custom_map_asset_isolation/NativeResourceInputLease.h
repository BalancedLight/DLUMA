#ifndef DLUMA_NATIVE_RESOURCE_INPUT_LEASE_H
#define DLUMA_NATIVE_RESOURCE_INPUT_LEASE_H
#include "NativeFileHookBridge.h"
extern "C" {
#include "../../dlw_common.h"
}

namespace dluma::fixes::custom_map_asset_isolation::resource_input {
constexpr std::size_t kChunkBytes=65536;
enum class State : std::uint8_t { Empty, Pinned, Reading, Complete, Failed, Closed };
enum class Failure : std::uint8_t { None, InvalidIdentity, UnsupportedClass, DuplicateFailed,
    ReopenFailed, IdentityChanged, InvalidSize, ReadFailed, CloseFailed };
struct Identity {
    DWORD attributes;FILETIME creation,access,write;DWORD volume,size_high,size_low,links,index_high,index_low;
};
struct Pin {
    HANDLE handle;file_lifetime::FileRecord file;std::uint64_t input_generation;
    State state;Failure failure;
};
struct Job {
    HANDLE reader;Identity identity;DlumaSha256Context hash;
    std::uint64_t bytes,position;unsigned char buffer[kChunkBytes],sha256[32];
    State state;Failure failure;
};
// Actual opened native object only. Capture duplicates its handle without any
// read, seek or filename lookup. Both supported classes are exact-file-ledger
// classes; the asynchronous class owns its HANDLE at+0x28, synchronous at+0x20.
bool Capture(file_lifetime::bridge::State*,const file_lifetime::FileRecord&,
             std::uint64_t input_generation,Pin*)noexcept;
// Off-hook. ReOpenFile creates an independent file position from the pinned
// object and denies writers; no path resolution can choose another object.
bool Begin(const Pin&,std::uint64_t maximum_bytes,Job*)noexcept;
// One bounded chunk per call. Checks the pinned object and reader again at EOF.
State Step(const Pin&,Job*)noexcept;
// Close both actual handles before publishing the completed digest to a
// retained-owner ledger, so a cleared pending-input count implies no lease IO.
bool Close(Pin*,Job*)noexcept;
bool ReleasePin(Pin*)noexcept;
} // namespace
#endif
