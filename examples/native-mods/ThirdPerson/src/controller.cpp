#include "controller.h"

#include <algorithm>
#include <cmath>

namespace thirdperson {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kPitchLimit = kPi * (85.0f / 180.0f);

float finiteOrZero(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

float axis(float value)
{
    return std::clamp(finiteOrZero(value), -1.0f, 1.0f);
}

float wrap(double value)
{
    return static_cast<float>(std::remainder(value, 2.0 * kPi));
}

Offsets clampOffsets(Offsets value)
{
    value.lateral = std::clamp(value.lateral, -1.5f, 1.5f);
    value.height = std::clamp(value.height, -1.0f, 1.5f);
    value.distance = std::clamp(value.distance, 0.4f, 6.0f);
    return value;
}

bool different(const Offsets& a, const Offsets& b)
{
    return a.lateral != b.lateral || a.height != b.height ||
        a.distance != b.distance;
}

Vec3 normalized(const Vec3& value)
{
    const double length = std::sqrt(
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z);
    if (!std::isfinite(length) || length < 0.000001)
        return {};
    return {
        static_cast<float>(value.x / length),
        static_cast<float>(value.y / length),
        static_cast<float>(value.z / length)};
}

bool nonzero(const Vec3& value)
{
    return value.x != 0.0f || value.y != 0.0f || value.z != 0.0f;
}
} // namespace

Controller::Controller(const ControllerConfig& config)
{
    configure(config);
}

void Controller::configure(const ControllerConfig& config)
{
    config_.adjustmentSpeed = std::isfinite(config.adjustmentSpeed)
        ? std::clamp(config.adjustmentSpeed, 0.01f, 10.0f) : 0.75f;
    config_.returnSeconds = std::isfinite(config.returnSeconds)
        ? std::clamp(config.returnSeconds, 0.01f, 2.0f) : 0.25f;
    config_.smoothCamera = config.smoothCamera;
    config_.smoothingSeconds = std::isfinite(config.smoothingSeconds)
        ? std::clamp(config.smoothingSeconds, 0.02f, 0.3f) : 0.12f;
    config_.aimFirstPerson = config.aimFirstPerson;
    config_.aimDistanceReduction = std::isfinite(config.aimDistanceReduction)
        ? std::clamp(config.aimDistanceReduction, 0.0f, 0.6f) : 0.20f;
    config_.aimMagnification = std::isfinite(config.aimMagnification)
        ? std::clamp(config.aimMagnification, 1.0f, 2.0f) : 1.25f;
    config_.horizontalAngle = std::clamp(
        finiteOrZero(config.horizontalAngle), -kPi, kPi);
    config_.verticalAngle = std::clamp(
        finiteOrZero(config.verticalAngle), -kPitchLimit, kPitchLimit);
}

bool Controller::setOffsets(const Offsets& offsets)
{
    if (!std::isfinite(offsets.lateral) || !std::isfinite(offsets.height) ||
        !std::isfinite(offsets.distance))
        return false;
    offsets_ = clampOffsets(offsets);
    return true;
}

void Controller::clearOrbit(bool blockUntilRelease)
{
    orbiting_ = false;
    returning_ = false;
    orbitYaw_ = 0.0f;
    orbitPitch_ = 0.0f;
    returnElapsed_ = 0.0f;
    orbitBlocked_ = blockUntilRelease;
}

CameraOutput Controller::update(const CameraInput& input, float seconds)
{
    const float dt = std::max(finiteOrZero(seconds), 0.0f);
    const bool active = input.focused && input.gameplay && input.available &&
        !input.suspended;
    CameraOutput result;

    if (!input.orbitHeld)
        orbitBlocked_ = false;
    if (active && input.togglePressed) {
        requested_ = !requested_;
        clearOrbit(input.orbitHeld);
    }

    if (!active || !requested_) {
        clearOrbit(input.orbitHeld);
        result.state = requested_ ? CameraState::Suspended : CameraState::FirstPerson;
    } else {
        result.state = CameraState::Shoulder;
        if (input.aimOrFire) {
            // Keep the current framing while releasing mouse capture for combat.
            if (input.orbitHeld) orbitBlocked_ = true;
        } else if (input.orbitHeld && !orbitBlocked_) {
            orbiting_ = true;
            returning_ = false;
            orbitYaw_ = wrap(static_cast<double>(orbitYaw_) + finiteOrZero(input.mouseYawDelta));
            orbitPitch_ = std::clamp(
                orbitPitch_ + finiteOrZero(input.mousePitchDelta),
                -kPitchLimit, kPitchLimit);
            result.state = CameraState::Orbit;
        } else {
            if (orbiting_) {
                orbiting_ = false;
                returning_ = true;
                returnYaw_ = orbitYaw_;
                returnPitch_ = orbitPitch_;
                returnElapsed_ = 0.0f;
            }
            if (returning_) {
                returnElapsed_ += dt;
                const float t = std::clamp(
                    returnElapsed_ / config_.returnSeconds, 0.0f, 1.0f);
                const float blend = 1.0f - t * t * (3.0f - 2.0f * t);
                orbitYaw_ = returnYaw_ * blend;
                orbitPitch_ = returnPitch_ * blend;
                if (t >= 1.0f)
                    clearOrbit(orbitBlocked_);
            }
        }

        const Offsets before = offsets_;
        if (input.orbitHeld) {
            if (input.resetPressed) {
                offsets_ = {};
            } else {
                const float movement = static_cast<float>(std::min(
                    static_cast<double>(config_.adjustmentSpeed) * dt, 100.0));
                offsets_.lateral += axis(input.lateralAdjust) * movement;
                offsets_.height += axis(input.heightAdjust) * movement;
                offsets_.distance += axis(input.distanceAdjust) * movement;
                offsets_ = clampOffsets(offsets_);
            }
        }
        result.settingsChanged = different(before, offsets_);
        dirty_ = dirty_ || result.settingsChanged;
    }

    const bool adjusting = active && requested_ && input.orbitHeld &&
        !input.resetPressed &&
        (axis(input.lateralAdjust) != 0.0f || axis(input.heightAdjust) != 0.0f ||
         axis(input.distanceAdjust) != 0.0f);
    if (dirty_ && !adjusting) {
        result.saveSettings = true;
        pendingSave_ = true;
        dirty_ = false;
    }
    if (!active || !requested_) aimBlend_ = 0.0f;
    else {
        const float target = input.aimHeld ? 1.0f : 0.0f;
        aimBlend_ += (target - aimBlend_) * -std::expm1(-dt / 0.10f);
    }
    result.aimDistanceScale = 1.0f - config_.aimDistanceReduction * aimBlend_;
    result.aimMagnification = 1.0f + (config_.aimMagnification - 1.0f) * aimBlend_;
    result.temporaryFirstPerson = active && requested_ && input.aimHeld && config_.aimFirstPerson;
    result.thirdPersonRequested = requested_;
    result.applyCamera = active && requested_;
    result.captureMouse = result.state == CameraState::Orbit;
    result.smoothCamera = config_.smoothCamera;
    result.smoothingSeconds = config_.smoothingSeconds;
    result.offsets = offsets_;
    result.framingYaw = config_.horizontalAngle;
    result.framingPitch = -config_.verticalAngle;
    result.orbitYaw = orbitYaw_;
    result.orbitPitch = orbitPitch_;
    return result;
}

bool Controller::takePendingSave(bool flushUnfinished)
{
    const bool result = pendingSave_ || (flushUnfinished && dirty_);
    if (flushUnfinished)
        dirty_ = false;
    pendingSave_ = false;
    return result;
}

void Controller::resetSession()
{
    requested_ = false;
    aimBlend_ = 0.0f;
    clearOrbit(true);
}

CameraPose makeCameraPose(
    const Vec3& target, float baseYaw, float basePitch,
    const CameraOutput& output)
{
    const float yaw = wrap(static_cast<double>(finiteOrZero(baseYaw)) +
        finiteOrZero(output.framingYaw) + finiteOrZero(output.orbitYaw));
    const float pitch = std::clamp(
        finiteOrZero(basePitch) + finiteOrZero(output.framingPitch) +
            finiteOrZero(output.orbitPitch),
        -kPitchLimit, kPitchLimit);
    const float sineYaw = std::sin(yaw);
    const float cosineYaw = std::cos(yaw);
    const float sinePitch = std::sin(pitch);
    const float cosinePitch = std::cos(pitch);
    CameraPose pose;
    pose.forward = {sineYaw * cosinePitch, sinePitch, cosineYaw * cosinePitch};
    pose.position = {
        target.x - cosineYaw * output.offsets.lateral -
            pose.forward.x * output.offsets.distance,
        target.y + output.offsets.height - pose.forward.y * output.offsets.distance,
        target.z + sineYaw * output.offsets.lateral -
            pose.forward.z * output.offsets.distance};
    return pose;
}

Vec3 aimDirection(
    const Vec3& origin, const Vec3& aimTarget, const Vec3& fallback)
{
    const Vec3 toward = normalized({
        aimTarget.x - origin.x,
        aimTarget.y - origin.y,
        aimTarget.z - origin.z});
    if (nonzero(toward))
        return toward;
    const Vec3 alternate = normalized(fallback);
    return nonzero(alternate) ? alternate : Vec3{0.0f, 0.0f, 1.0f};
}

Vec3 weaponAimDirection(const Vec3& origin, const Vec3& aimTarget, const Vec3& cameraForward)
{
    const Vec3 forward = aimDirection({}, cameraForward);
    const double advance =
        (static_cast<double>(aimTarget.x) - origin.x) * forward.x +
        (static_cast<double>(aimTarget.y) - origin.y) * forward.y +
        (static_cast<double>(aimTarget.z) - origin.z) * forward.z;
    if (!std::isfinite(advance) || advance <= 0.001)
        return forward;
    return aimDirection(origin, aimTarget, forward);
}

} // namespace thirdperson
