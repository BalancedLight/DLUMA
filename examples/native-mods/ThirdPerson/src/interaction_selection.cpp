#include "interaction_selection.h"

#include <cmath>

namespace thirdperson {
namespace {
bool valid(const InteractionCandidate& candidate)
{
    return candidate.id && candidate.reachable && candidate.unobstructed &&
        candidate.usable && candidate.visible &&
        std::isfinite(candidate.distanceSquared) && candidate.distanceSquared >= 0.0f &&
        std::isfinite(candidate.viewAngle) && candidate.viewAngle >= 0.0f;
}

bool preferred(const InteractionCandidate& a, const InteractionCandidate& b)
{
    if (a.priority != b.priority) return a.priority > b.priority;
    if (a.distanceSquared != b.distanceSquared) return a.distanceSquared < b.distanceSquared;
    if (a.viewAngle != b.viewAngle) return a.viewAngle < b.viewAngle;
    return a.id < b.id;
}
} // namespace

std::uint64_t selectInteraction(const InteractionCandidate* candidates, std::size_t count,
    std::uint64_t currentId)
{
    if (!candidates) return 0;
    const InteractionCandidate* best = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& candidate = candidates[i];
        if (!valid(candidate)) continue;
        if (currentId && candidate.id == currentId) return currentId;
        if (!best || preferred(candidate, *best)) best = &candidate;
    }
    return best ? best->id : 0;
}

bool canActivateInteraction(std::uint64_t id, InteractionRevalidate revalidate, void* context)
{
    return id && revalidate && revalidate(id, context);
}

} // namespace thirdperson
