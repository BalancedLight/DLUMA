#include "camera_motion.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace thirdperson {
namespace {
constexpr float clearance = 0.02f;
constexpr float minimumExtent = 0.15f;

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(Vec3 a, float value) { return {a.x * value, a.y * value, a.z * value}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(Vec3 value) { return std::sqrt(dot(value, value)); }
bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
Vec3 normalized(Vec3 value, Vec3 fallback)
{
    const float magnitude = length(value);
    return std::isfinite(magnitude) && magnitude > 0.00001f ? scale(value, 1.0f / magnitude) : fallback;
}
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float elapsed(float dt) { return std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f; }

bool projectedFraction(const TraceHit& hit, Vec3 start, Vec3 delta, Vec3 suppliedEnd,
    float squaredLength, float& result)
{
    if (!hit.pointValid || !finite(hit.point)) return false;
    const float fraction = dot(subtract(hit.point, start), delta) / squaredLength;
    if (fraction < 0.0f || fraction > 1.0f) return false;
    const Vec3 projected = add(start, scale(delta, fraction));
    // Some trace APIs leave their output at the supplied endpoint on a hit.
    if (length(subtract(hit.point, projected)) > 0.02f ||
        length(subtract(hit.point, suppliedEnd)) < 0.001f) return false;
    result = fraction;
    return true;
}

float safeFraction(Vec3 start, Vec3 end, TraceFunction query, void* context)
{
    const Vec3 delta = subtract(end, start);
    const float squaredLength = dot(delta, delta);
    if (squaredLength < 0.000001f) return 1.0f;
    const TraceHit forward = query(start, end, context);
    const TraceHit reverse = query(end, start, context);
    if (!forward.hit && !reverse.hit) return 1.0f;

    float closest = 1.0f;
    bool needsSearch = false;
    const std::array<TraceHit, 2> hits{forward, reverse};
    for (unsigned index = 0; index < hits.size(); ++index) {
        const TraceHit& hit = hits[index];
        if (!hit.hit) continue;
        float fraction = 1.0f;
        if (projectedFraction(hit, start, delta, index == 0 ? end : start, squaredLength, fraction))
            closest = std::min(closest, fraction);
        else
            needsSearch = true;
    }
    if (needsSearch) {
        float clear = 0.0f;
        float blocked = closest;
        // Search prefixes in both directions so one-sided geometry still blocks.
        for (int iteration = 0; iteration < 8; ++iteration) {
            const float middle = (clear + blocked) * 0.5f;
            const Vec3 endpoint = add(start, scale(delta, middle));
            const bool forwardHit = query(start, endpoint, context).hit;
            const bool reverseHit = query(endpoint, start, context).hit;
            if (forwardHit || reverseHit) blocked = middle;
            else clear = middle;
        }
        closest = clear;
    }
    return std::max(0.0f, closest - clearance / std::sqrt(squaredLength));
}

std::array<Vec3, 9> probeOffsets(const CameraPose& pose, float nearClip, float fov, float aspect)
{
    const Vec3 forward = normalized(pose.forward, {0.0f, 0.0f, 1.0f});
    Vec3 right = cross({0.0f, 1.0f, 0.0f}, forward);
    if (length(right) < 0.001f) right = cross({0.0f, 0.0f, 1.0f}, forward);
    right = normalized(right, {1.0f, 0.0f, 0.0f});
    const Vec3 up = normalized(cross(forward, right), {0.0f, 1.0f, 0.0f});
    const float clip = std::isfinite(nearClip) ? std::clamp(nearClip, 0.0f, 2.0f) : 0.1f;
    const float vertical = std::isfinite(fov) ? std::clamp(fov, 0.1f, 3.0f) : 1.04719755f;
    const float ratio = std::isfinite(aspect) ? std::clamp(aspect, 0.1f, 10.0f) : 1.77777778f;
    const float nearHeight = clip * std::tan(vertical * 0.5f);
    const float halfHeight = std::max(minimumExtent, nearHeight);
    const float halfWidth = std::max(minimumExtent, nearHeight * ratio);
    const Vec3 nearCenter = scale(forward, clip);
    std::array<Vec3, 9> result{};
    unsigned index = 1;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            result[index++] = add(nearCenter,
                add(scale(right, static_cast<float>(x) * halfWidth), scale(up, static_cast<float>(y) * halfHeight)));
        }
    }
    return result;
}

float sweep(Vec3 start, Vec3 end, const std::array<Vec3, 9>& offsets, TraceFunction query, void* context)
{
    float allowed = 1.0f;
    for (Vec3 offset : offsets) {
        const Vec3 limitedEnd = add(start, scale(subtract(end, start), allowed));
        allowed *= safeFraction(add(start, offset), add(limitedEnd, offset), query, context);
        if (allowed == 0.0f) break;
    }
    return allowed;
}
} // namespace

void CameraMotion::reset()
{
    followedAnchor_ = {};
    previousResolved_ = {};
    collisionFraction_ = 1.0f;
    haveFollow_ = false;
    haveResolved_ = false;
}

void CameraMotion::seedResolved(Vec3 position)
{
    if (!finite(position)) {
        haveResolved_ = false;
        return;
    }
    previousResolved_ = position;
    collisionFraction_ = 1.0f;
    haveResolved_ = true;
}

Vec3 CameraMotion::follow(Vec3 desiredAnchor, float dt, bool smooth, float smoothingSeconds)
{
    if (!finite(desiredAnchor)) return followedAnchor_;
    if (!haveFollow_ || !smooth || !std::isfinite(smoothingSeconds) || smoothingSeconds <= 0.0f) {
        followedAnchor_ = desiredAnchor;
        haveFollow_ = true;
    } else {
        const float alpha = -std::expm1(-elapsed(dt) / smoothingSeconds);
        followedAnchor_ = add(followedAnchor_, scale(subtract(desiredAnchor, followedAnchor_), alpha));
    }
    return followedAnchor_;
}

CameraPose CameraMotion::resolve(Vec3 currentAnchor, CameraPose proposed, float nearClip,
    float verticalFovRadians, float aspect, float dt, TraceFunction query, void* context)
{
    if (!finite(currentAnchor) || !finite(proposed.position)) {
        reset();
        proposed.position = finite(currentAnchor) ? currentAnchor : Vec3{};
        return proposed;
    }
    const Vec3 boom = subtract(proposed.position, currentAnchor);
    const float desiredDistance = length(boom);
    if (!query || desiredDistance < 0.00001f) {
        previousResolved_ = proposed.position;
        collisionFraction_ = 1.0f;
        haveResolved_ = true;
        return proposed;
    }
    const auto offsets = probeOffsets(proposed, nearClip, verticalFovRadians, aspect);
    const float allowedFraction = sweep(currentAnchor, proposed.position, offsets, query, context);
    const float allowedDistance = desiredDistance * allowedFraction;
    // Follow lag changes the boom length during turns and stance transitions.
    // Only obstruction recovery is damped; a clear boom keeps its full length.
    if (!haveResolved_ || allowedFraction <= collisionFraction_) collisionFraction_ = allowedFraction;
    else collisionFraction_ += (allowedFraction - collisionFraction_) * -std::expm1(-12.0f * elapsed(dt));
    collisionFraction_ = std::min(collisionFraction_, allowedFraction);
    Vec3 candidate = add(currentAnchor, scale(boom, collisionFraction_));
    if (haveResolved_ && length(subtract(candidate, previousResolved_)) > 0.001f) {
        const float movementFraction = sweep(previousResolved_, candidate, offsets, query, context);
        if (movementFraction < 1.0f) {
            candidate = add(previousResolved_, scale(subtract(candidate, previousResolved_), movementFraction));
            const Vec3 adjustedBoom = subtract(candidate, currentAnchor);
            const float adjustedDistance = length(adjustedBoom);
            if (adjustedDistance > allowedDistance && adjustedDistance > 0.00001f)
                candidate = add(currentAnchor, scale(adjustedBoom, allowedDistance / adjustedDistance));
            const float finalFraction = sweep(currentAnchor, candidate, offsets, query, context);
            candidate = add(currentAnchor, scale(subtract(candidate, currentAnchor), finalFraction));
            collisionFraction_ = std::min(1.0f, length(subtract(candidate, currentAnchor)) / desiredDistance);
        }
    }
    proposed.position = candidate;
    previousResolved_ = candidate;
    haveResolved_ = true;
    return proposed;
}

} // namespace thirdperson
