#ifndef DLUMA_NATIVE_XUI_INSTANCE_OWNER_H
#define DLUMA_NATIVE_XUI_INSTANCE_OWNER_H
#include "NativeXuiContentOwner.h"
namespace dluma::fixes::custom_map_asset_isolation::xui_instance {
constexpr std::size_t kInstances=32768;
enum class Life : std::uint8_t { Empty, Active, Retired, Destroyed };
enum class Origin : std::uint8_t { ExternalUnknown, CleanStartupExternal, UiLoadPending, UiLoadGlobal, UiLoadProject, UiLoadUnknown };
struct Instance {
    xui_content::Key manager;
    std::uintptr_t object,interface_object,owner_context,vtable;
    std::uint64_t generation,load_serial;
    file_lifetime::SourceKey source;
    unsigned char sha256[32];std::uint32_t epoch;
    Life life;Origin origin;bool exact_input;
};
struct State {
    void* lock=nullptr;Instance records[kInstances]={};
    std::uintptr_t writer_addresses[128]={};
    std::uint64_t generation=0,revision=0;std::uint32_t active_writers=0;
    bool sealed=false;std::atomic<bool> lost{false};
};
struct Writer {std::uintptr_t state;std::uint64_t revision;std::uint32_t slot;bool active;};
bool SealStartup(State*)noexcept;
bool BeginWriter(State*,Writer*)noexcept;
bool EndWriter(State*,Writer*)noexcept;
// Native return observation supplies the actual factory manager generation,
// object/interface identities and the active XUI input scope. The latter is
// dependency provenance; it does not prove absence of external native clients.
bool Created(State*,const Instance&)noexcept;
bool Retired(State*,xui_content::Key,std::uintptr_t object)noexcept;
bool AllRetired(State*,xui_content::Key)noexcept;
// The collector receipt requires the original native collector to return with
// its dead list empty and no other concurrent observed writer.
bool Collected(State*,xui_content::Key,std::uint32_t expected_dead,bool native_dead_empty,std::uint64_t writer_revision)noexcept;
bool ManagerDestroyed(State*,xui_content::Key)noexcept;
bool FinishLoad(State*,std::uint64_t serial,Origin)noexcept;
bool CopyInventory(State*,xui_content::Key,Instance*,std::size_t capacity,std::size_t* count,std::uint64_t* revision)noexcept;
// A load can instantiate an object in another native factory. Its consumed
// project/unknown input association must close there as well as at the loader.
bool ProjectInputInstancesClosed(State*)noexcept;
} // namespace
#endif
