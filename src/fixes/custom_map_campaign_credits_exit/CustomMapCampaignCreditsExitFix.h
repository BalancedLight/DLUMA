#ifndef DLUMA_FIXES_CUSTOM_MAP_CAMPAIGN_CREDITS_EXIT_FIX_H
#define DLUMA_FIXES_CUSTOM_MAP_CAMPAIGN_CREDITS_EXIT_FIX_H

#include "../FixRegistry.h"

namespace dluma::fixes::custom_map_campaign_credits_exit {

static constexpr const char* kFixId = "custom_map_campaign_credits_exit";
static constexpr const char* kFixDisplayName =
    "Custom Map Campaign Credits Exit Crash Fix";

/* The campaign-finished call creates a pending proof. The game returns from
 * it before its later SessionDI::OnDestroy work reaches the faulting tail, so
 * only the matching session-destroy call makes the MASM guard live. The
 * engine may schedule those two operations on different threads. */
bool BeginVerifiedCustomCampaignFinishedTeardown(
    int destroy_mode, bool project_complete, const char* map,
    const char* project) noexcept;
bool EnterVerifiedCustomCampaignFinishedSessionDestroy(
    bool project_complete, const char* map, const char* project) noexcept;
void EndVerifiedCustomCampaignFinishedSessionDestroy(bool armed) noexcept;

FixStatus Initialize(const RuntimeContext& context, bool enabled,
                     bool ownership_tracking_enabled);
FixStatus Shutdown();
FixStatus CurrentStatus();

}  // namespace dluma::fixes::custom_map_campaign_credits_exit

#endif
