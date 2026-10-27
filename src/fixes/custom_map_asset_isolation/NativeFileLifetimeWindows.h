#ifndef DLUMA_NATIVE_FILE_LIFETIME_WINDOWS_H
#define DLUMA_NATIVE_FILE_LIFETIME_WINDOWS_H
#include "NativeFileLifetimeLedger.h"
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::windows {
enum class Route : std::uint8_t { PublicOpen,NativeOpen,Win32Factory,AsyncFactory,
 DirectoryRead,DirectoryWrite,ZipFactory,MemoryFactory,AsyncConstructor,MemoryConstructor,
 Win32Destructor,AsyncDestructor,GzipDestructor,ZipDestructor,MemoryDestructor,BaseDestructor,
 QueueSchedule,QueueCancelComplete,QueueSkippedComplete,QueueReadComplete,
 DirectorySourceDestructor,PackSourceDestructor,FallbackSourceDestructor,Count };
enum class Role : std::uint8_t { NullableFactory,ConstructorObserver,Destructor,Schedule,InteriorCompletion,SourceDestructor };
struct HookSpec {Route route;Role role;std::uintptr_t rva;std::uint8_t prefix_size;unsigned char prefix[16];};
constexpr std::uint64_t kRequiredHookMask=(std::uint64_t{1}<<static_cast<unsigned>(Route::Count))-1;
constexpr char kFilesystemSha256[]="CB6FF4B0C9AA62C8A35742D463AE3C34FE803767E8E2AC9B33E4604886B9846D";
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
using SourceFn=bool(*)(void*,std::uintptr_t,SourceKey*)noexcept;
struct Binding {std::uintptr_t base;ReadFn read;SourceFn resolve_source;void* user;bool validated;};
struct CompletionRelay {
    std::uintptr_t patch_address;
    std::uintptr_t site;
    std::uintptr_t resume;
    std::uintptr_t conditional_target;
    unsigned char original_envelope[16];
    unsigned char patched_envelope[16];
    unsigned char code[64];
    unsigned char unwind[24];
    std::uint8_t code_size;
    std::uint8_t unwind_size;
    std::uint8_t original_relay_offset;
    std::uint8_t displaced_size;
};
struct EntryRelocation { std::uint8_t stolen_size,rip_offset,rip_end; };
bool EntryRelocationFor(Route,EntryRelocation*)noexcept;
bool CopyEntryUnwind(const Binding*,Route,unsigned char*,std::size_t,std::size_t*)noexcept;
/* Pure preparation: emits a nearby jump island plus a branch-aware original
 * relay. The caller owns atomic 16-byte installation and process-pinned code. */
bool PrepareCompletionRelay(const Binding*,Route,std::uintptr_t allocation,
                            std::uintptr_t hook,CompletionRelay*)noexcept;
const HookSpec* HookPlan(std::size_t*)noexcept;
/* Input SHA must come from the independently verified loaded image. Validate
 * all routes before modifying their prologues, then install the complete set. */
bool Validate(Binding*,const char* actual_sha256)noexcept;
bool ReadFileIdentity(const Binding*,std::uintptr_t,SourceKey*,FileKind*)noexcept;
Token ObserveReturnedFile(const Binding*,Ledger*,const ThreadScope&,std::uintptr_t)noexcept;
Token ObserveDestructorEntry(const Binding*,Ledger*,std::uintptr_t)noexcept;
/* QueueSchedule prototype: void(queue,file,offset,length,buffer,asyncOp,priority,kind).
 * Observe BEFORE the one original call can publish work to its worker. */
Token ObserveQueueSchedule(const Binding*,Ledger*,std::uintptr_t file,std::uintptr_t control)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::windows

extern "C" {
extern dluma::fixes::custom_map_asset_isolation::file_lifetime::CompletionRing* g_dluma_file_completion_ring;
extern volatile LONG g_dluma_file_completion_probe_active_calls;
extern void* g_dluma_file_completion_cancel_relay;
extern void* g_dluma_file_completion_skipped_relay;
extern void* g_dluma_file_completion_read_relay;
void dluma_file_completion_cancel_probe();
void dluma_file_completion_skipped_probe();
void dluma_file_completion_read_probe();
}
#endif
