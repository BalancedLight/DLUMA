#pragma once

#include <cstddef>
#include <cstdint>

namespace thirdperson {

// IDs are opaque, generation-aware handles supplied by the native adapter.
struct InteractionCandidate {
    std::uint64_t id = 0;
    int priority = 0;
    float distanceSquared = 0.0f;
    float viewAngle = 0.0f;
    bool reachable = false;
    bool unobstructed = false;
    bool usable = false;
    bool visible = false;
};

using InteractionRevalidate = bool (*)(std::uint64_t id, void* context);

std::uint64_t selectInteraction(const InteractionCandidate* candidates, std::size_t count,
    std::uint64_t currentId);
bool canActivateInteraction(std::uint64_t id, InteractionRevalidate revalidate, void* context);

} // namespace thirdperson
