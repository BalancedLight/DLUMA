#ifndef DLUMA_NATIVE_ARCHIVE_CONTENT_H
#define DLUMA_NATIVE_ARCHIVE_CONTENT_H
#include "NativeFileHookBridge.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::archive_content {
constexpr std::size_t kChunkBytes=65536;
enum class Result : std::uint8_t {
    Succeeded, InvalidArgument, Unauthorized, WrongType, Changed,
    NotFresh, TooLarge, ReadFailed, RestoreFailed,
};
using ReadMemoryFn=file_lifetime::windows::ReadFn;
using AuthorizeFn=bool(*)(void*,const file_lifetime::FileRecord&)noexcept;
using ReadFileFn=std::int64_t(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
using SeekFileFn=bool(*)(void*,std::uintptr_t,std::uint64_t)noexcept;
struct Access {
    std::uintptr_t filesystem_base;
    ReadMemoryFn read;
    AuthorizeFn authorize;
    ReadFileFn read_file;
    SeekFileFn seek_file;
    void* user;
    bool exact_filesystem_profile;
};
struct Digest {
    file_lifetime::FileRecord file;
    std::uint64_t bytes;
    unsigned char sha256[32];
    Result result;
    bool cursor_restored,complete;
};
struct Cache {
    Digest entries[file_lifetime::kFiles]={};
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
};
// A cache slot is the actual native file-ledger slot supplied by the fresh
// return observer. Consumers copy the result by the complete file identity.
// These are value receipts; the cache never retains a native file or provider.
bool Publish(Cache*,std::uint32_t file_slot,const Digest&)noexcept;
bool Copy(Cache*,const file_lifetime::FileRecord&,Digest*)noexcept;
// Only a newly constructed native ZIP file, still inside its actual factory
// return, may be read here. The caller provides bounded scratch storage.
// Every path after the first native read restores and checks the original
// zero cursor before publication. No filename lookup or second open occurs.
Result CaptureFresh(const Access&,const file_lifetime::FileRecord&,
    std::uint64_t maximum_bytes,void* scratch,std::size_t scratch_bytes,Digest*)noexcept;
// These adapters call the qualified unmodified filesystem entries. Readiness
// also requires the actual loaded-image qualification and fresh-return fence.
bool ValidateNativeAccess(const Access&)noexcept;
std::int64_t NativeRead(std::uintptr_t filesystem_base,std::uintptr_t,void*,std::size_t)noexcept;
bool NativeSeek(std::uintptr_t filesystem_base,std::uintptr_t,std::uint64_t)noexcept;
} // namespace
#endif
