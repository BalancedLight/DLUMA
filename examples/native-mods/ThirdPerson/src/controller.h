#pragma once

namespace thirdperson {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Offsets {
    float lateral = 0.45f;
    float height = 0.0f;
    float distance = 2.5f;
};

enum class CameraState { FirstPerson, Shoulder, Orbit, Suspended };

struct ControllerConfig {
    float adjustmentSpeed = 0.75f;
    float returnSeconds = 0.25f;
    bool smoothCamera = true;
    float smoothingSeconds = 0.12f;
    bool aimFirstPerson = false;
    bool freeCamera = false;
    bool firstPersonActions = false;
    float aimDistanceReduction = 0.20f;
    float aimMagnification = 1.25f;
    // Camera boom angles in radians; positive verticalAngle raises the camera.
    float horizontalAngle = 0.0f;
    float verticalAngle = 0.0f;
};

// The native adapter supplies edge-triggered buttons and mouse angles in radians.
// A positive distance adjustment moves the camera farther from the player.
struct CameraInput {
    bool focused = false;
    bool gameplay = false;
    bool available = false;
    bool suspended = false;
    bool togglePressed = false;
    bool orbitHeld = false;
    bool aimOrFire = false;
    bool aimHeld = false;
    bool freeControlAvailable = false;
    float bodyYaw = 0.0f;
    float bodyPitch = 0.0f;
    bool specialActionActive = false;
    bool actionCameraAvailable = false;
    float mouseYawDelta = 0.0f;
    float mousePitchDelta = 0.0f;
    float lateralAdjust = 0.0f;
    float heightAdjust = 0.0f;
    float distanceAdjust = 0.0f;
    bool resetPressed = false;
};

struct CameraOutput {
    CameraState state = CameraState::FirstPerson;
    bool thirdPersonRequested = false;
    bool applyCamera = false;
    bool captureMouse = false;
    bool smoothCamera = true;
    float smoothingSeconds = 0.12f;
    Offsets offsets{};
    float aimDistanceScale = 1.0f;
    float aimMagnification = 1.0f;
    bool temporaryFirstPerson = false;
    bool freeCamera = false;
    bool firstPersonActions = false;
    float specialActionBlend = 0.0f;
    float framingYaw = 0.0f;
    float framingPitch = 0.0f;
    float orbitYaw = 0.0f;
    float orbitPitch = 0.0f;
    bool settingsChanged = false;
    // Persist offsets on this update. Edits are coalesced until adjustment ends.
    bool saveSettings = false;
};

struct CameraPose {
    Vec3 position{};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

class Controller {
public:
    Controller() = default;
    explicit Controller(const ControllerConfig& config);

    void configure(const ControllerConfig& config);
    // Invalid stored values leave the current offsets unchanged.
    bool setOffsets(const Offsets& offsets);
    const Offsets& offsets() const { return offsets_; }
    bool requested() const { return requested_; }
    CameraOutput update(const CameraInput& input, float seconds);
    // Poll completed edits; include unfinished edits on deactivation/shutdown.
    bool takePendingSave(bool flushUnfinished = false);
    // A new game session starts in first person and retains saved framing.
    void resetSession();

private:
    ControllerConfig config_{};
    Offsets offsets_{};
    bool requested_ = false;
    bool orbitBlocked_ = false;
    bool orbiting_ = false;
    bool returning_ = false;
    bool dirty_ = false;
    bool pendingSave_ = false;
    float orbitYaw_ = 0.0f;
    float orbitPitch_ = 0.0f;
    float returnYaw_ = 0.0f;
    float returnPitch_ = 0.0f;
    float returnElapsed_ = 0.0f;
    float aimBlend_ = 0.0f;
    float actionBlend_ = 0.0f;
    float freeYaw_ = 0.0f;
    float freePitch_ = 0.0f;
    bool freeSeeded_ = false;

    void clearOrbit(bool blockUntilRelease);
};

// Y is up; zero yaw faces +Z, positive yaw turns toward +X. The target is a
// stable shoulder-height world point supplied by the native adapter.
CameraPose makeCameraPose(
    const Vec3& target, float baseYaw, float basePitch,
    const CameraOutput& output);

// Interpolate positions and the shortest view arc, keeping an orthonormal frame.
CameraPose blendCameraPoses(const CameraPose& third, const CameraPose& first, float amount);

// The adapter supplies the native weapon origin and camera ray's world target.
Vec3 aimDirection(
    const Vec3& origin, const Vec3& aimTarget,
    const Vec3& fallback = Vec3{0.0f, 0.0f, 1.0f});

// Camera hits behind the firing origin must not reverse the shot.
Vec3 weaponAimDirection(
    const Vec3& origin, const Vec3& aimTarget, const Vec3& cameraForward);

// Horizontal input in world space; full diagonal input is normalized.
Vec3 cameraRelativeMovement(float side, float forward, float cameraYaw);

} // namespace thirdperson
