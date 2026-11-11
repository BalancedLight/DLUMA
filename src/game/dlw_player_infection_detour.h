#ifndef DLW_PLAYER_INFECTION_DETOUR_H
#define DLW_PLAYER_INFECTION_DETOUR_H
#include "../fixes/custom_map_asset_isolation/NativeEntryUnwind.h"
namespace dluma::game::infection {
enum class PlanHost{Unknown,MainGame,PlayerStandalone,EditorPlaytest};
struct RespawnDetourPlan {
    unsigned char original[17],patch[16],relay[29],unwind[64];
    std::uint32_t code_size,unwind_size;
};
// Inactive copied-data planner only. exact_profile must come from the shared
// classified full-image profile authority. No allocation, patch, executable
// pointer publication, registration or native game call occurs here. Caller
// must qualify cold install, module retention and routing ownership separately.
// On failure the output remains unchanged. Handler-bearing prologues reject.
bool PrepareRespawnDetourPlan(
    const fixes::custom_map_asset_isolation::detour::EntryUnwindImage&,PlanHost,bool exact_profile,
    std::uintptr_t hook,std::uintptr_t relay,RespawnDetourPlan*)noexcept;
}
#endif
