#ifndef DLUMA_PLAYER_INTRINSIC_PROGRESS_SNAPSHOT_H
#define DLUMA_PLAYER_INTRINSIC_PROGRESS_SNAPSHOT_H
#include "PlayerStoredProgressSnapshot.h"
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::player_intrinsic_progress {
constexpr std::size_t kStateBytes=0x4B0,kFieldCount=9,kNodeLimit=4096;
enum class Field:std::uint8_t { Identity,Progression,Skills,Buffs,Statistics,
 AppearanceUnlocks,MapHistory,Attributes,ProtectedBody };
enum UnqualifiedGraph:std::uint32_t { Rewards=1,FogOfWar=2,Pursuit=4,
 AuxiliaryVectors=8,TransientActors=16 };
struct Snapshot {
 std::uintptr_t state,cached_context,attributes,attribute_defaults;
 std::uint64_t owner_generation;
 player_stored_progress::Digest fields[kFieldCount];
 std::uintptr_t timed_buff_definitions[kNodeLimit];
 std::uint32_t timed_buff_definition_count,unqualified_graphs;
 std::uint32_t transient_actor_count;
 std::uint64_t transient_actor_fingerprint;
 bool intrinsic_fields_complete,owned_user_graph_complete;
};
using Reader=player_stored_progress::Reader;
/* Pure bounded reads: no native getter, parser, profile or allocation route. */
bool Capture(const Reader&,std::uintptr_t state,std::uint64_t generation,Snapshot*)noexcept;
bool Equal(const Snapshot&,const Snapshot&)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::player_intrinsic_progress
#endif
