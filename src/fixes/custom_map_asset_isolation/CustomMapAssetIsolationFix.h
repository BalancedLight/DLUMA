#ifndef DLUMA_FIXES_CUSTOM_MAP_ASSET_ISOLATION_FIX_H
#define DLUMA_FIXES_CUSTOM_MAP_ASSET_ISOLATION_FIX_H

#include "../FixRegistry.h"

namespace dluma::fixes::custom_map_asset_isolation {

static constexpr const char* kFixId = "custom_map_asset_isolation";
static constexpr const char* kFixDisplayName =
    "Custom Map Content Isolation";

enum class ExitGameFinalizationWaitResult : std::uint8_t {
    Ready,
    SelfCountMismatch,
    OtherActiveCalls,
};

struct ExitGameFinalizationCounts {
    std::uint32_t expected_exit_owner_calls;
    std::uint32_t observed_exit_owner_calls;
    std::uint32_t hook_active_calls;
    std::uint32_t current_thread_hook_calls;
    std::uint32_t consumer_active_calls;
    std::uint32_t current_thread_consumer_calls;
    std::uint32_t unowned_native_active_calls;
};

inline ExitGameFinalizationWaitResult ClassifyExitGameFinalizationCounts(
    const ExitGameFinalizationCounts& counts) noexcept
{
    if (counts.expected_exit_owner_calls != 1 ||
        counts.observed_exit_owner_calls !=
            counts.expected_exit_owner_calls)
        return ExitGameFinalizationWaitResult::SelfCountMismatch;
    if (counts.hook_active_calls < counts.current_thread_hook_calls ||
        counts.consumer_active_calls <
            counts.current_thread_consumer_calls)
        return ExitGameFinalizationWaitResult::OtherActiveCalls;
    return counts.hook_active_calls == counts.current_thread_hook_calls &&
           counts.consumer_active_calls ==
               counts.current_thread_consumer_calls &&
           counts.unowned_native_active_calls == 0
        ? ExitGameFinalizationWaitResult::Ready
        : ExitGameFinalizationWaitResult::OtherActiveCalls;
}

FixStatus Initialize(const RuntimeContext& context, bool enabled);
FixStatus Shutdown();
FixStatus CurrentStatus();

}  // namespace dluma::fixes::custom_map_asset_isolation

#endif
