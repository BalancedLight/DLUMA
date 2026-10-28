#pragma once

#include "controller.h"

namespace thirdperson {

struct TraceHit {
    bool hit = false;
    bool pointValid = false;
    Vec3 point{};
};

using TraceFunction = TraceHit (*)(const Vec3& start, const Vec3& end, void* context);

class CameraMotion {
public:
    void reset();
    // Follow history remains intact while the native action camera owns position.
    void seedResolved(Vec3 position);
    Vec3 follow(Vec3 desiredAnchor, float dt, bool smooth, float smoothingSeconds);
    CameraPose resolve(Vec3 currentAnchor, CameraPose proposed, float nearClip,
        float verticalFovRadians, float aspect, float dt, TraceFunction query, void* context);

private:
    Vec3 followedAnchor_{};
    Vec3 previousResolved_{};
    float collisionFraction_ = 1.0f;
    bool haveFollow_ = false;
    bool haveResolved_ = false;
};

} // namespace thirdperson
