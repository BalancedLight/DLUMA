#ifndef DLUMA_PLAYER_PROCESS_OWNER_REBUILD_H
#define DLUMA_PLAYER_PROCESS_OWNER_REBUILD_H
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::player_process_rebuild {
constexpr std::size_t kOwnerCapacity=2,kRowCount=8;
enum class OwnerKind:std::uint8_t { LocalProcess,CharacterProcess };
enum class BaselineKind:std::uint8_t { NoStateAtStartup,StateSnapshot };
enum class Operation:std::uint8_t { ResetDerivedCache,SeedAllRows,InitializeVariables };
enum class Failure:std::uint8_t { None,InvalidPlan,EnvironmentUnqualified,OwnersChanged,
 BaselineDefinitionsUnavailable,ProtectionIncomplete,ProtectionChanged,PermitRejected,
 NativeFailure,SeedUnavailable,SeedWriteFailed,DerivedStateMismatch,AlreadyStarted };
struct Owner {
 std::uintptr_t state,alias_slot,game_di_member,row_table;
 std::uint64_t lifetime_generation;
 OwnerKind kind;
};
struct Environment {
 Owner owners[kOwnerCapacity];
 std::uint32_t owner_count,live_actor_state_count,thread_id;
 std::uint64_t inventory_revision,admission_lease,transaction;
 std::uintptr_t game_di,current_session,local_player_di,cache;
 bool exact_process_owner_graph,player_readers_closed,player_writers_closed;
};
enum ProtectionField:std::uint32_t { OwnerAliases=1,Progression=2,SkillsAndBuffs=4,
 StoredProgress=8,SelectedIdentity=16,OtherOwnedUserData=32 };
constexpr std::uint32_t kRequiredProtection=63;
struct Protection {unsigned char sha256[32];std::uint32_t fields;bool complete;};
struct Seed {
 std::uintptr_t preset;
 std::uint32_t row_type;
 std::uint64_t baseline_definition_fingerprint;
 bool fresh_cache_membership,baseline_provider;
};
struct Plan {
 BaselineKind startup_kind;
 Environment owners_before;
 Protection protected_before[kOwnerCapacity];
 std::uint64_t baseline_definition_fingerprint;
};
using CaptureEnvironmentFn=bool(*)(void*,Environment*)noexcept;
/* The callback owns a typed preservation READ scope, including any native
 * item/curve borrows. A reset-depth counter is not reader authorization. */
using ProtectionFn=bool(*)(void*,const Owner*,Protection*)noexcept;
using BaselineFn=bool(*)(void*,const Environment*,std::uint64_t)noexcept;
using BeginPermitFn=bool(*)(void*,const Plan*,Operation,const Owner*)noexcept;
using EndPermitFn=bool(*)(void*,bool)noexcept;
using ResetFn=bool(*)(void*,std::uintptr_t)noexcept;
using MaterializeFn=bool(*)(void*,std::uintptr_t,const Owner*)noexcept;
using ResolveSeedFn=bool(*)(void*,std::uintptr_t,const Owner*,std::uint32_t,Seed*)noexcept;
using SeedFn=bool(*)(void*,const Owner*,const Seed*,std::size_t)noexcept;
using ResetAppearanceFn=bool(*)(void*,const Owner*)noexcept;
using InitializeFn=bool(*)(void*,const Owner*)noexcept;
using VerifyFn=bool(*)(void*,const Owner*,std::uint64_t)noexcept;
struct Operations {
 void* user;CaptureEnvironmentFn capture_environment;ProtectionFn capture_protection;
 BaselineFn verify_baseline_definition_owner;BeginPermitFn begin_exact_permit;
 EndPermitFn end_exact_permit;ResetFn reset_derived_cache;MaterializeFn materialize_owner_presets;ResolveSeedFn resolve_seed;
 SeedFn seed_rows;ResetAppearanceFn reset_appearance_defaults;InitializeFn initialize_variables;VerifyFn verify_derived_baseline;
 bool exact_native_routes;
};
struct Rebuild {
 Operations operations;Plan plan;Seed seeds[kOwnerCapacity][kRowCount];Failure failure;
 std::uint32_t seeded_owners,initialized_owners;
 bool prepared,mutation_started,verified,executing;
};
void Initialize(Rebuild*,const Operations*)noexcept;
/* NoStateAtStartup does NOT authorize deletion of later process-lifetime
 * aliases. Both baseline forms use provider-bound derived-state equivalence. */
bool Prepare(Rebuild*,BaselineKind,std::uint64_t baseline_definition_fingerprint)noexcept;
bool Execute(Rebuild*)noexcept;

using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
using WriteFn=bool(*)(void*,std::uintptr_t,const void*,std::size_t)noexcept;
struct NativeBinding {std::uintptr_t gamedll_base;ReadFn read;WriteFn write;void* user;bool exact_layout;};
bool ValidateNativeBinding(NativeBinding*,const char* actual_gamedll_sha256)noexcept;
bool ResetNativeDerivedCache(const NativeBinding&,std::uintptr_t cache)noexcept;
std::uintptr_t FindNativeEmptySeed(const NativeBinding&,std::uintptr_t cache,std::uint32_t type)noexcept;
/* Cache insertion can relocate inline records. Materialize all surviving
 * selector pairs before resolving any final seed pointer or evaluating an owner. */
bool MaterializeNativeOwnerPresets(const NativeBinding&,std::uintptr_t cache,const Owner&)noexcept;
std::uintptr_t FindNativeOwnerSeed(const NativeBinding&,std::uintptr_t cache,const Owner&,std::uint32_t type)noexcept;
bool InitializeNativeVariables(const NativeBinding&,const Owner&)noexcept;
/* Fresh preset identity is checked against its actual row/type vector and
 * selector pair, not only against a non-null returned pointer. A null owner
 * selects the native empty-selector baseline record. */
bool ValidateFreshSeed(const NativeBinding&,std::uintptr_t cache,std::uint32_t type,
                       std::uintptr_t preset,const Owner* owner=nullptr)noexcept;
/* Data-only stores of eight real, prevalidated fresh preset pointers. All
 * process owners must complete this step before any derived evaluation. */
bool SeedNativeRows(const NativeBinding&,const Owner&,const Seed*,std::size_t)noexcept;
/* Restore only ctor-initialized fields written by ParseAppearances. Selected
 * appearance IDs, the adjacent runtime byte and structure padding stay intact. */
bool ResetNativeAppearanceDefaults(const NativeBinding&,const Owner&)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::player_process_rebuild
#endif
