#ifndef DLUMA_PLAYER_STORED_PROGRESS_SNAPSHOT_H
#define DLUMA_PLAYER_STORED_PROGRESS_SNAPSHOT_H
extern "C" {
#include "../../dlw_common.h"
}
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::player_stored_progress {
constexpr std::size_t kContextBytes=4360,kMaximumVectorCount=4096;
enum class Section:std::uint8_t { Inventory,QuickSlots,Equipment,Wallet,Levels,
 Quests,Rewards,BringItems,Appearances,Count };
enum class Failure:std::uint8_t { None,InvalidArgument,WrongThread,ActiveSession,
 OwnerChanged,UnsupportedImage,Unreadable,InvalidHeader,OutputLimit,NativeFailure,
 UnsupportedStreamOperation,StateChanged,QuiescenceUnavailable };
struct Digest {unsigned char sha256[32];std::uint64_t bytes;bool complete;};
struct Snapshot {
 std::uintptr_t player_state,context;
 std::uint64_t owner_generation;
 Digest sections[static_cast<unsigned>(Section::Count)];
 Failure failure;
 std::uint32_t available_section_mask;
 bool cached_context_absent,stored_sections_complete;
 /* Deliberately distinct: this module does not infer live inventory/quest or
  * selected-character owners from a missing cached save context. */
 bool complete_player_preservation;
};
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
struct Fence {
 std::uintptr_t player_state,current_session,local_player_di;
 std::uint64_t owner_generation,mutation_revision;
 std::uint32_t thread_id;
 bool exclusive_preservation_owner;
};
using FenceFn=bool(*)(void*,Fence*)noexcept;
struct Stream {
 void** vtable;
 DlumaSha256Context hash;
 std::uint64_t bytes,limit;
 ReadFn read;
 void* user;
 Failure failure;
 unsigned char scratch[4096];
};
using SerializeFn=bool(*)(void*,Section,std::uintptr_t context,Stream*)noexcept;
struct Reader {
 std::uintptr_t gamedll_base;
 ReadFn read;
 FenceFn read_fence;
 SerializeFn serialize_section;
 void* user;
 std::uint64_t maximum_section_bytes;
 bool exact_image_and_section_routes;
};
void InitializeStream(Stream*,std::uint64_t limit,ReadFn,void*)noexcept;
std::uint64_t WriteStream(Stream*,const void*,std::uint64_t)noexcept;
bool FinishStream(Stream*,Digest*)noexcept;
bool Capture(const Reader&,std::uintptr_t state,std::uint64_t generation,Snapshot*)noexcept;
/* Captures five direct scalar/vector sections without native dispatch. This
 * remains a partial snapshot when inventory/quest/reward roots are unqualified. */
bool CaptureDirect(const Reader&,std::uintptr_t state,std::uint64_t generation,Snapshot*)noexcept;
bool Equal(const Snapshot&,const Snapshot&)noexcept;
/* The only native invocations are the four inspected sectional serializers.
 * Full PlayerState::Save and PlayerSaveContext::Save are intentionally absent. */
struct NativeTargets {std::uintptr_t gamedll_base;ReadFn read;void* user;bool validated;};
bool ValidateNativeTargets(NativeTargets*,const char* exact_image_sha256)noexcept;
bool SerializeNativeSection(const NativeTargets&,Section,std::uintptr_t,Stream*)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::player_stored_progress
#endif
