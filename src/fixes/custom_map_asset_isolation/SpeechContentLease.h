#ifndef DLUMA_SPEECH_CONTENT_LEASE_H
#define DLUMA_SPEECH_CONTENT_LEASE_H
extern "C" {
#include "../../dlw_common.h"
__declspec(dllimport) HANDLE WINAPI CreateFileA(LPCSTR,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
}
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::speech_content_lease {
constexpr std::size_t kPathCapacity=1024,kChunkBytes=65536;
enum class Format : std::uint8_t { PinnedRawSpeechBankFile };
enum class NativeBankFile : std::uint8_t { Speech, Fmod };
enum class State : std::uint8_t { Empty,Pinned,Reading,Hashed,Verified,Failed,Closed };
enum class Failure : std::uint8_t { None,InvalidScope,WrongNativeClass,DuplicateFailed,
 OpenFailed,IdentityMismatch,InvalidSize,ReadFailed,ContentMismatch,CloseFailed };
using ReadMemoryFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
struct NaturalOpen {
 std::uintptr_t engine_base,filesystem_base,native_file,caller_return;
 std::uint64_t speech_scope_generation,provider_identity,provider_generation;
 std::uint32_t owner_thread_id;
 bool exact_player_images;
 char resolved_path[kPathCapacity];
};
struct Pin {
 HANDLE handle;
 std::uint64_t scope_generation,provider_identity,provider_generation;
 char path[kPathCapacity];
 State state;Failure failure;
};
struct Identity {
 DWORD volume,index_high,index_low,size_high,size_low;
 FILETIME creation,write;
};
struct ReadJob {
 HANDLE reader;
 Identity identity;
 std::uint64_t total,position,maximum_bytes;
 std::uint64_t provider_identity,provider_generation,pin_scope_generation;
 DlumaSha256Context hash;
 unsigned char bytes[kChunkBytes],sha256[32];
 Format format;
 State state;Failure failure;
};
/* At the exact natural Speech open return, while its original SFsFile is
 * alive: duplicate only the underlying OS handle. No file read/seek, path
 * lookup, heap allocation or waiting occurs in this hook helper. The duplicate
 * shares file position, so it is a pin ONLY and is never read or repositioned. */
bool CaptureNaturalPin(Pin*,const NaturalOpen&,ReadMemoryFn,void*)noexcept;
// FMOD's native-open mode 0/flags 0x40 returns Win32FileHandleAsync;
// its owned HANDLE is +0x28, while Speech's Win32FileHandle uses +0x20.
bool CaptureNaturalBankPin(Pin*,const NaturalOpen&,NativeBankFile,
                           ReadMemoryFn,void*)noexcept;
/* Off-hook replay preflight after the caller has resolved/verified its native
 * winning provider. Keep this pin/reader through the native SpeechLoad call. */
bool OpenReplayPin(Pin*,const char* resolved_path,std::uint64_t provider_identity,
                   std::uint64_t provider_generation,std::uint64_t scope_generation)noexcept;
/* Open an independent file position and compare it to the pinned file object
 * by volume/file index/size/timestamps before hashing any bytes. */
bool BeginRead(ReadJob*,const Pin&,std::uint64_t maximum_bytes)noexcept;
State Step(ReadJob*)noexcept;
bool VerifyExpected(ReadJob*,const unsigned char expected[32])noexcept;
bool ReadyForNativeSpeechLoad(const ReadJob&,const Pin&)noexcept;
// A historical baseline digest has no expected digest yet. This checks the
// independent hashed reader against the still-held natural pin without
// manufacturing a VerifyExpected comparison against its own output.
bool HistoricalContentHeld(const ReadJob&,const Pin&)noexcept;
bool PinStillNamesContent(const Pin&,const Identity&)noexcept;
bool CloseRead(ReadJob*)noexcept;
bool ReleasePin(Pin*)noexcept;
bool TransferPin(Pin* from,Pin* empty_destination)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::speech_content_lease
#endif
