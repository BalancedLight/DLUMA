#include "../src/controller.h"
#include "../src/camera_motion.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace thirdperson;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 0.0001f;
}

struct Plane {
    Vec3 normal{0.0f, 0.0f, 1.0f};
    float offset = -1.0f;
    bool pointValid = true;
    bool oneSided = false;
    bool enabled = true;
    bool endpointOutput = false;
    float minimumZ = -100.0f;
    float maximumZ = 100.0f;
    unsigned queries = 0;
};

TraceHit planeTrace(const Vec3& start, const Vec3& end, void* context)
{
    auto& plane = *static_cast<Plane*>(context);
    ++plane.queries;
    if (!plane.enabled) return {};
    const float a = start.x * plane.normal.x + start.y * plane.normal.y + start.z * plane.normal.z - plane.offset;
    const float b = end.x * plane.normal.x + end.y * plane.normal.y + end.z * plane.normal.z - plane.offset;
    if ((a > 0.0f && b > 0.0f) || (a < 0.0f && b < 0.0f) ||
        std::fabs(a - b) < 0.000001f || (plane.oneSided && a <= b)) return {};
    const float fraction = a / (a - b);
    const Vec3 point{start.x + (end.x - start.x) * fraction,
        start.y + (end.y - start.y) * fraction, start.z + (end.z - start.z) * fraction};
    if (point.z < plane.minimumZ || point.z > plane.maximumZ) return {};
    return {true, plane.pointValid, plane.endpointOutput ? end : point};
}

CameraPose resolve(CameraMotion& motion, Vec3 anchor, Vec3 position, Plane& plane, float dt = 1.0f / 60.0f)
{
    CameraPose pose;
    pose.position = position;
    return motion.resolve(anchor, pose, 0.1f, 1.04719755f, 1.77777778f, dt, planeTrace, &plane);
}

CameraInput gameplay()
{
    CameraInput input;
    input.focused = input.gameplay = input.available = true;
    return input;
}

void stateAndRestoration()
{
    Controller controller;
    auto input = gameplay();
    require(!controller.update(input, 0.016f).applyCamera, "session starts first person");
    input.togglePressed = true;
    require(controller.update(input, 0.016f).state == CameraState::Shoulder, "toggle enters shoulder");
    input.togglePressed = false;
    input.available = false;
    auto out = controller.update(input, 0.016f);
    require(out.state == CameraState::Suspended && out.thirdPersonRequested && !out.applyCamera,
        "player replacement suspends without losing requested view");
    input.available = true;
    require(controller.update(input, 0.016f).state == CameraState::Shoulder, "replacement resumes shoulder");
    input.suspended = true;
    require(!controller.update(input, 0.016f).applyCamera, "scripted camera owns suspended view");
    input.suspended = false;
    input.togglePressed = true;
    require(controller.update(input, 0.016f).state == CameraState::FirstPerson, "toggle restores first person");
    controller.resetSession();
    require(!controller.requested(), "new session clears requested view");

    CameraMotion motion;
    motion.follow({}, 0.016f, true, 0.12f);
    require(motion.follow({2.0f, 0.0f, 0.0f}, 0.016f, true, 0.12f).x < 1.0f,
        "follow retains a smooth positional transition");
    motion.reset();
    require(near(motion.follow({2.0f, 0.0f, 0.0f}, 0.016f, true, 0.12f).x, 2.0f),
        "session reset seeds follow from the new player anchor");
    Plane plane;
    resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    motion.reset();
    plane.enabled = false;
    require(near(resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane).position.z, -3.0f),
        "reset clears the previous collision distance");
}

void orbitAndCombat()
{
    Controller controller;
    auto input = gameplay();
    input.togglePressed = true;
    controller.update(input, 0.0f);
    input.togglePressed = false;
    input.orbitHeld = true;
    input.mouseYawDelta = 1.0f;
    input.mousePitchDelta = 0.4f;
    auto out = controller.update(input, 0.016f);
    require(out.captureMouse && out.state == CameraState::Orbit, "Alt captures independent orbit");
    require(near(out.orbitYaw, 1.0f) && near(out.orbitPitch, 0.4f), "orbit consumes calibrated angles");
    input.orbitHeld = false;
    input.mouseYawDelta = input.mousePitchDelta = 0.0f;
    out = controller.update(input, 0.125f);
    require(!out.captureMouse && near(out.orbitYaw, 0.5f), "release returns smoothly halfway");
    out = controller.update(input, 0.125f);
    require(near(out.orbitYaw, 0.0f) && near(out.orbitPitch, 0.0f), "return completes at configured duration");
    input.orbitHeld = true;
    input.mouseYawDelta = 1.0f;
    controller.update(input, 0.01f);
    input.aimOrFire = true;
    out = controller.update(input, 0.01f);
    require(!out.captureMouse && out.applyCamera && near(out.orbitYaw, 1.0f), "combat retains orbit framing while releasing mouse capture");
    input.aimOrFire = false;
    require(!controller.update(input, 0.01f).captureMouse, "combat blocks orbit until Alt release");
    input.orbitHeld = false;
    controller.update(input, 0.01f);
    input.orbitHeld = true;
    require(controller.update(input, 0.01f).captureMouse, "new Alt press enables orbit");
    input.focused = false;
    require(!controller.update(input, 0.01f).captureMouse, "focus loss releases capture");
    input.focused = true;
    require(!controller.update(input, 0.01f).captureMouse, "focus regain cannot recapture held Alt");

    CameraMotion motion;
    Plane plane;
    auto pose = resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(pose.position.z > -1.0f && pose.position.z < -0.9f,
        "wall collision contracts immediately with clearance");
    motion.reset();
    plane.pointValid = false;
    pose = resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(pose.position.z > -1.0f && pose.position.z < -0.9f,
        "boolean collision queries locate the nearest wall");
    motion.reset();
    plane.pointValid = true;
    plane.endpointOutput = true;
    pose = resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(pose.position.z > -1.0f && pose.position.z < -0.9f,
        "unchanged query endpoints use the bounded collision search");
    plane.endpointOutput = false;
    plane.enabled = false;
    const float contracted = pose.position.z;
    pose = resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(pose.position.z < contracted && pose.position.z > -3.0f,
        "camera eases outward when the wall clears");
    plane.enabled = true;
    plane.offset = -0.5f;
    pose = resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(pose.position.z > -0.5f, "a new obstruction overrides outward easing");

    motion.reset();
    Plane sideWall;
    sideWall.normal = {1.0f, 0.0f, 0.0f};
    sideWall.offset = 0.0f;
    sideWall.minimumZ = -3.0f;
    sideWall.maximumZ = -1.0f;
    sideWall.oneSided = true;
    sideWall.pointValid = false;
    resolve(motion, {}, {-1.0f, 0.0f, -2.0f}, sideWall);
    pose = resolve(motion, {}, {1.0f, 0.0f, -2.0f}, sideWall);
    require(pose.position.x < -0.1f, "rapid orbit cannot cross a one-sided wall between frames");
}

void settingsAndGeometry()
{
    Controller controller;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(!controller.setOffsets({nan, 0.0f, 2.0f}), "nonfinite stored settings rejected");
    require(near(controller.offsets().lateral, 0.45f), "rejected storage retains defaults");
    require(controller.setOffsets({10.0f, -10.0f, 20.0f}), "finite settings accepted");
    require(near(controller.offsets().lateral, 1.5f) && near(controller.offsets().height, -1.0f) &&
        near(controller.offsets().distance, 6.0f), "stored offsets clamped");
    controller.setOffsets({});
    auto input = gameplay();
    input.togglePressed = true;
    controller.update(input, 0.0f);
    input.togglePressed = false;
    input.orbitHeld = true;
    input.lateralAdjust = 1.0f;
    auto out = controller.update(input, 0.5f);
    require(out.settingsChanged && !out.saveSettings && near(out.offsets.lateral, 0.825f), "continuous adjustment is timed and coalesced");
    input.gameplay = false;
    out = controller.update(input, 0.016f);
    require(out.saveSettings && !out.captureMouse, "menu entry flushes unfinished edit");
    require(!controller.update(input, 0.016f).saveSettings, "settings saved once");
    require(controller.takePendingSave() && !controller.takePendingSave(), "save remains pending for runtime until consumed");
    input = gameplay();
    input.orbitHeld = true;
    input.heightAdjust = 1.0f;
    controller.update(input, 0.1f);
    require(!controller.takePendingSave(), "runtime does not save an active adjustment");
    require(controller.takePendingSave(true) && !controller.takePendingSave(true), "shutdown flushes pending settings once");

    CameraOutput frame;
    auto pose = makeCameraPose({0.0f, 1.5f, 0.0f}, 0.0f, 0.0f, frame);
    require(near(pose.position.x, -0.45f) && near(pose.position.y, 1.5f) &&
        near(pose.position.z, -2.5f), "default pose has right shoulder offset");
    pose = makeCameraPose({0.0f, 1.5f, 0.0f}, 1.57079632679f, 0.0f, frame);
    require(near(pose.position.x, -2.5f) && near(pose.position.z, 0.45f), "offset follows view orientation");
    ControllerConfig framing;
    framing.horizontalAngle = 1.57079632679f;
    framing.verticalAngle = 0.52359877559f;
    controller.configure(framing);
    frame = controller.update(gameplay(), 0.0f);
    pose = makeCameraPose({0.0f, 1.5f, 0.0f}, 0.0f, 0.0f, frame);
    require(near(frame.framingYaw, framing.horizontalAngle) &&
        near(frame.framingPitch, -framing.verticalAngle) &&
        pose.position.x < -2.0f && pose.position.y > 2.5f,
        "configured angles rotate the camera boom and positive elevation raises it");
    const auto direction = aimDirection({0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 4.0f});
    require(near(direction.x, 0.6f) && near(direction.z, 0.8f), "aim direction normalized");
    const auto fallback = aimDirection({}, {}, {0.0f, 2.0f, 0.0f});
    require(near(fallback.y, 1.0f), "coincident aim target uses normalized fallback");

    const Vec3 weaponOrigin{0.0f, 1.5f, 0.0f};
    const auto closeAim = weaponAimDirection(weaponOrigin, {-0.45f, 1.5f, 1.0f}, {0.0f, 0.0f, 1.0f});
    require(near(closeAim.x, -0.41036468f) && near(closeAim.z, 0.91192151f),
        "close shoulder target converges from the weapon origin");
    const auto farAim = weaponAimDirection(weaponOrigin, {-0.45f, 1.5f, 100.0f}, {0.0f, 0.0f, 1.0f});
    require(std::fabs(farAim.x) < std::fabs(closeAim.x) && farAim.z > 0.999f,
        "shoulder parallax decreases with target distance");
    CameraOutput orbitFrame;
    orbitFrame.orbitYaw = 1.57079632679f;
    const auto orbitPose = makeCameraPose(weaponOrigin, 0.0f, 0.0f, orbitFrame);
    const Vec3 orbitTarget{orbitPose.position.x + 100.0f * orbitPose.forward.x,
        orbitPose.position.y + 100.0f * orbitPose.forward.y,
        orbitPose.position.z + 100.0f * orbitPose.forward.z};
    const auto orbitAim = weaponAimDirection(weaponOrigin, orbitTarget, orbitPose.forward);
    require(orbitAim.x > 0.999f && std::fabs(orbitAim.z) < 0.01f,
        "weapon targets the retained orbit view instead of the original forward direction");
    const auto blockedAim = weaponAimDirection(weaponOrigin, {-0.45f, 1.5f, -0.1f}, {0.0f, 0.0f, 1.0f});
    require(near(blockedAim.x, 0.0f) && near(blockedAim.z, 1.0f),
        "a camera obstruction behind the weapon cannot reverse the shot");
    const auto blockedOrbitAim = weaponAimDirection(weaponOrigin, {-1.0f, 1.5f, 0.0f}, orbitPose.forward);
    require(blockedOrbitAim.x > 0.999f,
        "behind-origin obstruction keeps the retained orbit firing direction");

    CameraMotion slow;
    CameraMotion fast;
    slow.follow({}, 0.0f, true, 0.12f);
    fast.follow({}, 0.0f, true, 0.12f);
    Vec3 slowAnchor;
    Vec3 fastAnchor;
    for (int frameIndex = 0; frameIndex < 30; ++frameIndex)
        slowAnchor = slow.follow({1.0f, 2.0f, 3.0f}, 1.0f / 30.0f, true, 0.12f);
    for (int frameIndex = 0; frameIndex < 120; ++frameIndex)
        fastAnchor = fast.follow({1.0f, 2.0f, 3.0f}, 1.0f / 120.0f, true, 0.12f);
    require(near(slowAnchor.x, fastAnchor.x) && near(slowAnchor.z, fastAnchor.z),
        "exponential follow is independent of frame rate");
    require(near(slow.follow({5.0f, 0.0f, 0.0f}, 0.0f, false, 0.12f).x, 5.0f),
        "disabled smoothing follows the anchor immediately");

    CameraMotion motion;
    Plane plane;
    plane.enabled = false;
    resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    plane.enabled = true;
    const auto blocked = resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(blocked.position.z > -1.0f, "collision runs after the smoothed camera proposal");
    motion.reset();
    plane.enabled = false;
    plane.queries = 0;
    resolve(motion, {}, {0.0f, 0.0f, -3.0f}, plane);
    require(plane.queries == 18, "clear nine-probe sweeps do not run collision bisection");

    motion.reset();
    for (int frameIndex = 0; frameIndex < 120; ++frameIndex) {
        const float t = static_cast<float>(frameIndex) / 60.0f;
        const Vec3 anchor{5.0f * t, 1.5f + 0.3f * std::sin(6.0f * t), 0.0f};
        const Vec3 followed = motion.follow(anchor, 1.0f / 60.0f, true, 0.12f);
        const CameraPose desired = makeCameraPose(followed, 3.14159265f * t, 0.0f, {});
        const CameraPose resolved = motion.resolve(anchor, desired, 0.1f, 1.04719755f,
            1.77777778f, 1.0f / 60.0f, planeTrace, &plane);
        require(near(resolved.position.x, desired.position.x) &&
            near(resolved.position.y, desired.position.y) && near(resolved.position.z, desired.position.z),
            "clear turns and vertical movement retain the smoothed proposal without collision pumping");
    }

    Controller zoom;
    auto aim = gameplay();
    aim.togglePressed = true;
    zoom.update(aim, 0.0f);
    aim.togglePressed = false;
    aim.aimHeld = aim.aimOrFire = true;
    auto zoomed = zoom.update(aim, 0.1f);
    require(zoomed.aimDistanceScale < 1.0f && zoomed.aimDistanceScale > 0.8f,
        "right-click aim moves smoothly toward a closer camera");
    require(zoomed.aimMagnification > 1.0f && zoomed.aimMagnification < 1.25f, "aim magnification transitions smoothly");
    const float closeScale = zoomed.aimDistanceScale;
    aim.aimHeld = aim.aimOrFire = false;
    require(zoom.update(aim, 0.1f).aimDistanceScale > closeScale,
        "releasing aim smoothly restores normal distance");
    ControllerConfig firstPersonConfig;
    firstPersonConfig.aimFirstPerson = true;
    zoom.configure(firstPersonConfig);
    aim.aimHeld = aim.aimOrFire = true;
    require(zoom.update(aim, 0.0f).temporaryFirstPerson && zoom.requested(), "aim can temporarily select first person without toggling the selected view");
    aim.aimHeld = aim.aimOrFire = false;
    require(!zoom.update(aim, 0.0f).temporaryFirstPerson && zoom.requested(), "releasing aim resumes the selected view");
    aim.focused = false;
    require(near(zoom.update(aim, 0.0f).aimDistanceScale, 1.0f),
        "suspension clears temporary aim zoom");
}
} // namespace

int main()
{
    stateAndRestoration();
    orbitAndCombat();
    settingsAndGeometry();
    std::puts("Third Person controller: 3 core scenarios passed.");
    return 0;
}
