#include "engine_adapter.h"
#include "controller.h"
#include "camera_motion.h"
#include "engine_reticle.h"
#include "engine_actions.h"
#include "engine_interactions.h"

#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace thirdperson {
namespace {
constexpr float Radians = 0.01745329251994329577f;

// Player, game, and engine identities are checked together before binding.
constexpr char PlayerHash[] = "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A";
constexpr char GameHash[] = "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
constexpr char EngineHash[] = "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C";
constexpr char ToolsGameHash[] = "8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54";
constexpr char ToolsEngineHash[] = "5B8090BFC3D3A25A24DA1846A3AAFD65C97E0D1E700B5DFEBFC7132737B6BFF6";

template<class T> T& field(void* object, size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}

template<class T> T method(void* object, size_t slot)
{
    return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[slot]);
}

bool fileHash(HMODULE module, const char* expected)
{
    wchar_t filename[32768];
    const DWORD length = GetModuleFileNameW(module, filename, static_cast<DWORD>(std::size(filename)));
    if (!length || length >= std::size(filename)) return false;
    HANDLE file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 32> digest{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{};
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
            ok = false;
            break;
        }
        if (!count) break;
        ok = BCryptHashData(hash, buffer.data(), count, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    constexpr char Hex[] = "0123456789ABCDEF";
    for (size_t i = 0; ok && i < digest.size(); ++i)
        ok = expected[i * 2] == Hex[digest[i] >> 4] && expected[i * 2 + 1] == Hex[digest[i] & 15];
    return ok;
}

struct TablePatch {
    void** slot = nullptr;
    void* original = nullptr;
    void* replacement = nullptr;

    bool install(void** target, void* expected, void* hook)
    {
        if (*target != expected) return false;
        DWORD protection = 0;
        if (!VirtualProtect(target, sizeof(void*), PAGE_READWRITE, &protection)) return false;
        void* observed = InterlockedCompareExchangePointer(target, hook, expected);
        DWORD ignored = 0;
        VirtualProtect(target, sizeof(void*), protection, &ignored);
        if (observed != expected) return false;
        slot = target;
        original = expected;
        replacement = hook;
        return true;
    }

    bool restore()
    {
        if (!slot) return true;
        DWORD protection = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
        void* observed = InterlockedCompareExchangePointer(slot, original, replacement);
        DWORD ignored = 0;
        const bool protectedAgain = VirtualProtect(slot, sizeof(void*), protection, &ignored) != 0;
        if ((observed != replacement && observed != original) || !protectedAgain) return false;
        slot = nullptr;
        return true;
    }
};

bool focused()
{
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

bool down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

int scanCode(int virtualKey)
{
    UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
    return static_cast<int>((scan & 0xFF) | ((scan & 0xFF00) ? 0x80 : 0));
}

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scaled(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
} // namespace

struct EngineAdapter::Impl {
    using CameraUpdate = std::uintptr_t (__fastcall*)(void*);
    using LookUpdate = std::uintptr_t (__fastcall*)(void*, float);
    using Keyboard = char (__fastcall*)(void*, int, unsigned char, unsigned int);
    using MouseButton = std::uintptr_t (__fastcall*)(void*, int, int);
    using ChangeCamera = void (__fastcall*)(void*, int, void*);
    using RefreshAppearance = void (__fastcall*)(void*, unsigned char);
    using GetPlayer = void* (__fastcall*)(void*);
    using GetVector = Vec3* (__fastcall*)(void*, Vec3*);
    using GetInputValue = float (__fastcall*)(void*, int);
    using GetMeshElement = int (__fastcall*)(void*, unsigned char);
    using ElementVisibility = bool (__fastcall*)(void*, int);
    using TransformVector = Vec3* (__fastcall*)(void*, Vec3*, const Vec3*);
    using AssistDirection = Vec3* (__fastcall*)(void*, Vec3*, unsigned char);
    using GetScalar = float (__fastcall*)(void*);
    using SetScalar = void (__fastcall*)(void*, float);
    using GetMatrix = const float* (__fastcall*)(void*);
    using InDuck = bool (__fastcall*)(void*);
    using Transform = void (__fastcall*)(void*, const Vec3&, const Vec3&, const Vec3&);
    using Raytrace = unsigned char (__fastcall*)(void*, unsigned char, void*, const Vec3*, Vec3*,
        unsigned short, void*, unsigned int, unsigned int, int);
    using Sensitivity = float* (__fastcall*)(void*, float*);
    using Multipliers = std::uintptr_t (__fastcall*)(void*, float*, float*);

    static inline std::atomic<Impl*> current{nullptr};
    static inline CameraUpdate stockCamera = nullptr;
    static inline CameraUpdate stockFirstPersonCamera = nullptr;
    static inline LookUpdate stockLook = nullptr;
    static inline Keyboard stockKeyboard = nullptr;
    static inline MouseButton stockMouseButton = nullptr;
    static inline GetVector stockWeaponDirection = nullptr;
    static inline GetVector stockMovementInput = nullptr;
    static inline GetVector stockJumpInput = nullptr;
    static inline AssistDirection stockAssistDirection = nullptr;
    static inline std::atomic<unsigned> callbacks{0};
    static inline thread_local unsigned callbackDepth = 0;

    struct Callback {
        Callback() { ++callbackDepth; callbacks.fetch_add(1, std::memory_order_acquire); }
        ~Callback() { callbacks.fetch_sub(1, std::memory_order_release); --callbackDepth; }
    };

    Controller* controller = nullptr;
    KeyBindings keys{};
    LogFunction log = nullptr;
    unsigned char* gameModule = nullptr;
    HMODULE engineModule = nullptr;
    ChangeCamera changeCamera = nullptr;
    RefreshAppearance refreshAppearance = nullptr;
    GetPlayer getPlayer = nullptr;
    GetVector getPosition = nullptr;
    GetVector getCameraPosition = nullptr;
    GetVector getCameraForward = nullptr;
    GetVector getCameraUp = nullptr;
    GetMeshElement getMeshElement = nullptr;
    ElementVisibility isElementHidden = nullptr;
    ElementVisibility hideElement = nullptr;
    ElementVisibility unhideElement = nullptr;
    TransformVector vectorWorldToLocal = nullptr;
    GetPlayer getActiveLook = nullptr;
    InDuck inDuck = nullptr;
    GetScalar getNearClip = nullptr;
    GetScalar getFov = nullptr;
    SetScalar setFov = nullptr;
    GetScalar getAspect = nullptr;
    GetMatrix getProjection = nullptr;
    Transform setTransform = nullptr;
    Raytrace raytrace = nullptr;
    Sensitivity sensitivity = nullptr;
    Multipliers multipliers = nullptr;
    TablePatch cameraPatch, firstPersonCameraPatch, lookPatch, keyboardPatch, mouseButtonPatch, weaponDirectionPatch;
    std::recursive_mutex mutex;
    CameraOutput output{};
    CameraInput input{};
    CameraMotion motion{};
    ActionVisualClassBridge actionVisualClass{};
    std::atomic<bool> actionClassEnabled{false};
    LARGE_INTEGER motionFrequency{};
    LARGE_INTEGER previousMotionTime{};
    void* lookPlayer = nullptr;
    void* lookObject = nullptr;
    bool moveSampleAvailable = false;
    float stableYaw = 0.0f;
    float stablePitch = 0.0f;
    bool stableLookReady = false;
    CameraPose resolvedPose{};
    CameraPose actionPose{};
    void* actionPosePlayer = nullptr;
    void* actionPoseCamera = nullptr;
    CameraPose firstPersonNativePose{};
    void* firstPersonNativeCamera = nullptr;
    bool firstPersonPoseOverridden = false;
    bool nativeActionCamera = false;
    void* actionStatePlayer = nullptr;
    void* hiddenHeadPlayer = nullptr;
    void* hiddenHeadVisual = nullptr;
    void* hiddenHeadModel = nullptr;
    int hiddenHeadElement = -1;
    bool hiddenHeadOwned = false;
    void* resolvedPlayer = nullptr;
    void* resolvedCamera = nullptr;
    Vec3 previousAnchor{};
    bool haveAnchor = false;
    bool assistHookCreated = false;
    bool assistHookEnabled = false;
    bool assistHookInitialized = false;
    bool movementHookCreated = false;
    bool movementHookEnabled = false;
    unsigned char* movementHookModule = nullptr;
    bool jumpHookCreated = false;
    bool jumpHookEnabled = false;
    unsigned char* jumpHookModule = nullptr;
    bool assistHookPublished = false;
    unsigned char* assistHookModule = nullptr;
    void* fovCamera = nullptr;
    float originalFov = 0.0f;
    std::array<bool, 256> swallowed{};
    std::array<bool, 256> forwarded{};
    std::array<bool, 5> swallowedMouse{};
    std::array<bool, 5> forwardedMouse{};
    bool installed = false;
    bool shutdownFailed = false;
    bool enabled = false;
    bool toggleHeld = false;
    bool resetHeld = false;
    bool deferredActionToggle = false;
    bool ownedView = false;
    bool ownsVisualSelection = false;
    bool visualRestorePending = false;
    bool refreshingVisual = false;
    unsigned char previousVisualSelection = 0;
    void* visualPlayer = nullptr;
    void* ownerPlayer = nullptr;
    void* ownerManager = nullptr;
    void* lastVisual = nullptr;
    ULONGLONG lastTick = 0;
    ULONGLONG lastLook = 0;

    void* game() const { return *reinterpret_cast<void**>(gameModule + 0x1D07F88); }
    void* player() const { void* g = game(); return g ? getPlayer(g) : nullptr; }
    static void* actionLocalPlayer(void* context) { return static_cast<Impl*>(context)->player(); }
    static bool actionClassRequested(void* context)
    {
        return static_cast<Impl*>(context)->actionClassEnabled.load(std::memory_order_acquire);
    }
    void* manager() const
    {
        void* g = game();
        if (!g) return nullptr;
        void* session = field<void*>(g, 1288);
        return session ? field<void*>(session, 192) : nullptr;
    }

    bool gameplay() const;

    bool normalLookOwnsInput(void* p) const
    {
        return p && lookObject && getActiveLook &&
            getActiveLook(p) == static_cast<unsigned char*>(lookObject) + 24;
    }

    bool edgeLookOwnsInput(void* p) const
    {
        void* active = p && getActiveLook ? getActiveLook(p) : nullptr;
        if (!active || *reinterpret_cast<void***>(active) !=
                reinterpret_cast<void**>(gameModule + 0x1808F28)) return false;
        auto* edge = static_cast<unsigned char*>(active) - 24;
        return field<unsigned char>(edge, 16) && method<GetPlayer>(edge, 4)(edge) == p;
    }

    void resetMotion()
    {
        interactions::clear();
        motion.reset();
        resolvedPlayer = resolvedCamera = nullptr;
        actionPosePlayer = actionPoseCamera = nullptr;
        moveSampleAvailable = false;
        previousMotionTime = {};
        haveAnchor = false;
    }

    float motionSeconds()
    {
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now) || motionFrequency.QuadPart <= 0) return 0.0f;
        const float dt = previousMotionTime.QuadPart
            ? static_cast<float>(static_cast<double>(now.QuadPart - previousMotionTime.QuadPart) /
                static_cast<double>(motionFrequency.QuadPart)) : 0.0f;
        previousMotionTime = now;
        return std::clamp(dt, 0.0f, 0.1f);
    }

    bool consumeVirtualKey(int key) const
    {
        if (key == keys.toggle) return true;
        if (!controller->requested()) return false;
        if (key == keys.orbit) return true;
        return down(keys.orbit) && (key == keys.left || key == keys.right ||
            key == keys.up || key == keys.down || key == keys.closer ||
            key == keys.farther || key == keys.reset);
    }

    void restoreHiddenHead()
    {
        if (hiddenHeadOwned && hiddenHeadPlayer && player() == hiddenHeadPlayer) {
            void* visual = method<GetPlayer>(hiddenHeadPlayer, 194)(hiddenHeadPlayer);
            if (visual && visual == hiddenHeadVisual) {
                void* modelHost = static_cast<unsigned char*>(visual) + 64;
                void* model = method<GetPlayer>(modelHost, 1)(modelHost);
                if (model && model == hiddenHeadModel && isElementHidden && unhideElement &&
                    isElementHidden(model, hiddenHeadElement) && !unhideElement(model, hiddenHeadElement))
                    return;
            }
        }
        hiddenHeadPlayer = hiddenHeadVisual = hiddenHeadModel = nullptr;
        hiddenHeadElement = -1;
        hiddenHeadOwned = false;
    }

    bool captureActionPose(void* camera, void* p)
    {
        actionPosePlayer = actionPoseCamera = nullptr;
        if (!camera || !p || !getMeshElement || !isElementHidden || !hideElement || !unhideElement)
            return false;
        void* visual = method<GetPlayer>(p, 194)(p);
        if (!visual || visual != lastVisual) return false;
        void* target = field<void*>(camera, 80);
        if (target != static_cast<unsigned char*>(visual) + 11552 ||
            *reinterpret_cast<void***>(target) != reinterpret_cast<void**>(gameModule + 0x17E2048))
            return false;
        void* modelHost = static_cast<unsigned char*>(visual) + 64;
        void* model = method<GetPlayer>(modelHost, 1)(modelHost);
        if (!model || getMeshElement(model, 8) < 0) return false;
        CameraPose candidate{};
        Vec3* position = method<GetVector>(target, 0)(target, &candidate.position);
        Vec3* forward = method<GetVector>(target, 1)(target, &candidate.forward);
        Vec3* up = method<GetVector>(target, 2)(target, &candidate.up);
        if (!position || !forward || !up || !finite(*position) || !finite(*forward) || !finite(*up) ||
            length(*forward) < 0.001f || length(*up) < 0.001f) return false;
        candidate.position = *position;
        candidate.forward = *forward;
        candidate.up = *up;
        actionPose = candidate;
        actionPosePlayer = p;
        actionPoseCamera = camera;
        return true;
    }

    bool captureActiveFirstPersonPose(void* camera, void* p)
    {
        actionPosePlayer = actionPoseCamera = nullptr;
        if (!camera || !p || !getCameraPosition || !getCameraForward || !getCameraUp) return false;
        CameraPose candidate{};
        Vec3* position = getCameraPosition(camera, &candidate.position);
        Vec3* forward = getCameraForward(camera, &candidate.forward);
        Vec3* up = getCameraUp(camera, &candidate.up);
        if (!position || !forward || !up || !finite(*position) || !finite(*forward) || !finite(*up) ||
            length(*forward) < 0.001f || length(*up) < 0.001f) return false;
        candidate.position = *position;
        candidate.forward = scaled(*forward, -1.0f);
        candidate.up = *up;
        actionPose = candidate;
        actionPosePlayer = p;
        actionPoseCamera = camera;
        return true;
    }

    bool hideCurrentHead(void* p)
    {
        void* visual = p ? method<GetPlayer>(p, 194)(p) : nullptr;
        if (!visual || visual != lastVisual || !getMeshElement || !isElementHidden || !hideElement)
            return false;
        void* modelHost = static_cast<unsigned char*>(visual) + 64;
        void* model = method<GetPlayer>(modelHost, 1)(modelHost);
        if (!model) return false;
        const int element = getMeshElement(model, 8);
        if (element < 0) return false;
        if (hiddenHeadVisual && (hiddenHeadVisual != visual || hiddenHeadModel != model ||
                hiddenHeadElement != element)) {
            restoreHiddenHead();
            if (hiddenHeadVisual) return false;
        }
        if (hiddenHeadVisual && isElementHidden(model, element)) return true;
        if (hiddenHeadVisual) restoreHiddenHead();
        const bool alreadyHidden = isElementHidden(model, element);
        if (!alreadyHidden && !hideElement(model, element)) return false;
        hiddenHeadPlayer = p;
        hiddenHeadVisual = visual;
        hiddenHeadModel = model;
        hiddenHeadElement = element;
        hiddenHeadOwned = !alreadyHidden;
        return true;
    }

    bool selectThirdPersonVisual(void* p)
    {
        if (refreshingVisual) return false;
        void* appearance = field<void*>(p, 4968);
        if (!appearance || !field<void*>(appearance, 16)) return false;
        auto& selection = *reinterpret_cast<unsigned char*>(gameModule + 0x1D075A2);
        const bool refresh = !ownsVisualSelection || visualPlayer != p || selection == 0;
        if (!ownsVisualSelection) {
            previousVisualSelection = selection;
            ownsVisualSelection = true;
        }
        actionClassEnabled.store(true, std::memory_order_release);
        selection = 1;
        visualRestorePending = false;
        if (refresh) {
            reticle::setEligiblePlayer(nullptr);
            interactions::clear();
            refreshingVisual = true;
            refreshAppearance(appearance, 1);
            refreshingVisual = false;
            visualPlayer = p;
        }
        return true;
    }

    void restoreVisual(void* p)
    {
        if (refreshingVisual) return;
        actionClassEnabled.store(false, std::memory_order_release);
        if (ownsVisualSelection) {
            *reinterpret_cast<unsigned char*>(gameModule + 0x1D075A2) = previousVisualSelection;
            ownsVisualSelection = false;
            visualPlayer = nullptr;
            visualRestorePending = true;
        }
        if (!visualRestorePending) return;
        // The native action owns the current visual until it finishes.
        if (p && specialActionActive(p)) return;
        void* appearance = p ? field<void*>(p, 4968) : nullptr;
        if (appearance && field<void*>(appearance, 16)) {
            interactions::clear();
            refreshingVisual = true;
            refreshAppearance(appearance, 1);
            refreshingVisual = false;
            visualRestorePending = false;
        }
    }

    void restoreView()
    {
        interactions::clear();
        reticle::setEligiblePlayer(nullptr);
        restoreHiddenHead();
        void* p = player();
        void* m = manager();
        if (firstPersonPoseOverridden && m && field<void*>(m, 80) == firstPersonNativeCamera) {
            setTransform(firstPersonNativeCamera, scaled(firstPersonNativePose.forward, -1.0f),
                firstPersonNativePose.up, firstPersonNativePose.position);
        }
        firstPersonPoseOverridden = false;
        firstPersonNativeCamera = nullptr;
        nativeActionCamera = false;
        if (fovCamera && m && field<void*>(m, 80) == fovCamera)
            setFov(fovCamera, originalFov);
        fovCamera = nullptr;
        restoreVisual(p);
        if (ownedView && p && m == ownerManager && p == ownerPlayer && field<int>(m, 88) == 1)
            changeCamera(m, 2, p);
        ownedView = false;
        ownerPlayer = ownerManager = lastVisual = nullptr;
        resetMotion();
    }

    void sample(float seconds, bool updatePresentation = false)
    {
        if (refreshingVisual) return;
        input.focused = focused();
        void* observedPlayer = player();
        input.available = observedPlayer != nullptr;
        input.gameplay = enabled && gameplay();
        input.suspended = false;
        if (enabled && observedPlayer) {
            input.specialActionActive = specialActionActive(observedPlayer);
            actionStatePlayer = observedPlayer;
        } else if (!enabled || !observedPlayer || observedPlayer != actionStatePlayer) {
            input.specialActionActive = false;
            actionStatePlayer = nullptr;
        }
        const bool currentLook = stableLookReady && lookPlayer == player();
        input.bodyYaw = currentLook ? stableYaw : 0.0f;
        input.bodyPitch = currentLook ? stablePitch : 0.0f;
        const bool normalLook = currentLook && normalLookOwnsInput(lookPlayer);
        input.freeControlAvailable = currentLook &&
            (normalLook || edgeLookOwnsInput(lookPlayer));
        if (!normalLook) moveSampleAvailable = false;
        const bool toggle = down(keys.toggle);
        const bool reset = down(keys.reset);
        input.togglePressed = input.focused && input.gameplay && toggle && !toggleHeld;
        input.resetPressed = input.focused && input.gameplay && reset && !resetHeld;
        toggleHeld = toggle;
        resetHeld = reset;
        input.orbitHeld = down(keys.orbit);
        input.aimHeld = down(VK_RBUTTON) && !consumeVirtualKey(VK_RBUTTON);
        input.aimOrFire = (down(VK_LBUTTON) && !consumeVirtualKey(VK_LBUTTON)) || input.aimHeld;
        input.lateralAdjust = static_cast<float>(down(keys.right)) - static_cast<float>(down(keys.left));
        input.heightAdjust = static_cast<float>(down(keys.up)) - static_cast<float>(down(keys.down));
        input.distanceAdjust = static_cast<float>(down(keys.farther)) - static_cast<float>(down(keys.closer));
        void* m = manager();
        if (m) {
            const int type = field<int>(m, 88);
            input.suspended = type != 1 && type != 2;
            if (type == 2 && input.specialActionActive && output.firstPersonActions)
                captureActiveFirstPersonPose(field<void*>(m, 80), player());
        }
        const bool acceptsToggle = input.focused && input.gameplay && input.available && !input.suspended;
        if (input.specialActionActive) {
            if (input.togglePressed && acceptsToggle) deferredActionToggle = !deferredActionToggle;
            input.togglePressed = false;
        } else if (deferredActionToggle && acceptsToggle) {
            input.togglePressed = !input.togglePressed;
            deferredActionToggle = false;
        }
        input.actionCameraAvailable = m && actionPosePlayer == player() && actionPosePlayer &&
            field<void*>(m, 80) == actionPoseCamera;
        output = controller->update(input, seconds);
        input.mouseYawDelta = input.mousePitchDelta = 0.0f;
        if (m && field<int>(m, 88) == 1) {
            firstPersonPoseOverridden = false;
            firstPersonNativeCamera = nullptr;
            if (!input.specialActionActive && output.specialActionBlend <= 0.0f)
                nativeActionCamera = false;
        }
        if (!input.freeControlAvailable || !input.focused || !input.gameplay ||
            !output.applyCamera || !output.freeCamera || output.temporaryFirstPerson ||
            input.specialActionActive) moveSampleAvailable = false;
        void* reticlePlayer = player();
        void* reticleManager = manager();
        const bool showReticle = enabled && input.focused && input.gameplay &&
            input.aimHeld && !input.specialActionActive && output.specialActionBlend <= 0.0f && output.applyCamera &&
            !output.temporaryFirstPerson && ownedView && reticlePlayer &&
            reticlePlayer == ownerPlayer && reticleManager && reticleManager == ownerManager &&
            field<int>(reticleManager, 88) == 1;
        reticle::setEligiblePlayer(showReticle ? reticlePlayer : nullptr);
        if (!output.freeCamera || !output.applyCamera || output.temporaryFirstPerson ||
            input.specialActionActive || output.specialActionBlend > 0.0f || !input.focused || !input.gameplay)
            interactions::clear();
        // Input hooks may run within player action processing. Defer appearance
        // replacement and native camera switching to the loader update.
        if (!updatePresentation) return;
        // Paired actions own their animation state until the native controller releases it.
        if (input.specialActionActive) {
            if (input.suspended || !input.focused || !input.gameplay || !output.firstPersonActions)
                restoreHiddenHead();
            return;
        }
        if (m && field<int>(m, 88) == 2 && ownedView && nativeActionCamera &&
            output.applyCamera && !output.temporaryFirstPerson && output.specialActionBlend > 0.0f)
            return;
        if (!output.applyCamera || output.temporaryFirstPerson) {
            swallowed.fill(false);
            swallowedMouse.fill(false);
            restoreView();
            return;
        }
        void* p = player();
        if (!m || !p) return;
        if (!selectThirdPersonVisual(p)) {
            output.applyCamera = false;
            return;
        }
        if (ownerPlayer != p || ownerManager != m) {
            ownedView = false;
            lastVisual = nullptr;
        }
        void* visual = method<GetPlayer>(p, 194)(p);
        if (!ownedView || field<int>(m, 88) != 1 || visual != lastVisual) {
            if (!ownedView) resetMotion();
            // Reattach the outfit's current visual after its mesh refresh.
            changeCamera(m, 1, p);
            nativeActionCamera = false;
            firstPersonPoseOverridden = false;
            firstPersonNativeCamera = nullptr;
            ownedView = true;
            ownerPlayer = p;
            ownerManager = m;
            lastVisual = visual;
        }
    }

    Vec3 trace(void* p, Vec3 start, Vec3 end, bool* collided = nullptr)
    {
        const bool hit = raytrace(static_cast<unsigned char*>(p) + 40, 0x37, nullptr, &start, &end,
            0, static_cast<unsigned char*>(p) + 24, 0, 0, 0);
        if (collided) *collided = hit;
        return end;
    }

    struct ProbeContext {
        Impl* adapter;
        void* player;
    };

    static TraceHit traceCamera(const Vec3& start, const Vec3& end, void* opaque)
    {
        const auto& context = *static_cast<const ProbeContext*>(opaque);
        bool hit = false;
        const Vec3 point = context.adapter->trace(context.player, start, end, &hit);
        // The solver validates the returned point and searches hit-only results.
        return {hit, hit && finite(point), point};
    }

    void updateCamera(void* camera, bool stockUpdated = false, bool nativeFirstPerson = false)
    {
        void* p = player();
        void* m = manager();
        if (refreshingVisual || !enabled || !output.applyCamera || output.temporaryFirstPerson || !p || !m ||
            field<void*>(m, 80) != camera || !stableLookReady || lookPlayer != p) return;
        if (fovCamera == camera) {
            setFov(camera, originalFov);
            fovCamera = nullptr;
        }
        Vec3 root{};
        getPosition(static_cast<unsigned char*>(p) + 24, &root);
        if (!finite(root)) return;
        void* duck = field<void*>(p, 0x1408);
        const float shoulder = duck && inDuck(duck) ? 0.9f : 1.5f;
        const Vec3 target{root.x, root.y + shoulder, root.z};
        if (haveAnchor && length(add(target, scaled(previousAnchor, -1.0f))) > 5.0f)
            resetMotion();
        previousAnchor = target;
        haveAnchor = true;
        const float dt = motionSeconds();
        const Vec3 followed = motion.follow(target, dt, output.smoothCamera, output.smoothingSeconds);
        const float yaw = stableYaw;
        const float pitch = stablePitch;
        CameraOutput framing = output;
        framing.offsets.distance *= output.aimDistanceScale;
        CameraPose pose = makeCameraPose(followed, yaw, pitch, framing);
        if (!nativeFirstPerson && output.firstPersonActions &&
            (input.specialActionActive || output.specialActionBlend > 0.0f) && stockUpdated)
            captureActionPose(camera, p);
        else if (!input.specialActionActive && output.specialActionBlend <= 0.0f) {
            restoreHiddenHead();
            actionPosePlayer = actionPoseCamera = nullptr;
        }
        const bool validActionPose = actionPosePlayer == p && actionPoseCamera == camera;
        float actionBlend = validActionPose ? output.specialActionBlend : 0.0f;
        if (actionBlend >= 0.75f && !hideCurrentHead(p)) {
            restoreHiddenHead();
            actionPosePlayer = actionPoseCamera = nullptr;
            actionBlend = 0.0f;
        } else if (actionBlend < 0.75f) restoreHiddenHead();
        if (actionBlend > 0.0f) pose = blendCameraPoses(pose, actionPose, actionBlend);
        const float baseFov = getFov(camera);
        const float aimMagnification = 1.0f + (output.aimMagnification - 1.0f) * (1.0f - actionBlend);
        if (std::isfinite(baseFov) && baseFov > 1.0f && baseFov < 179.0f &&
            std::isfinite(aimMagnification) && aimMagnification > 1.0001f) {
            originalFov = baseFov;
            fovCamera = camera;
            const float zoomed = 2.0f * std::atan(std::tan(baseFov * Radians * 0.5f) /
                aimMagnification) / Radians;
            setFov(camera, zoomed);
        }
        const float nearClip = getNearClip(camera);
        float aspect = getAspect(camera);
        if (!std::isfinite(aspect) || aspect <= 0.0f) aspect = 1.77777778f;
        const float fov = std::clamp(getFov(camera) * Radians, 0.1f, 3.0f);
        // Cover both native FOV modes if a projection has not been built yet.
        float verticalFov = 2.0f * std::atan(std::tan(fov * 0.5f) *
            (std::max)(aspect, 1.77777778f) / aspect);
        const float* projection = getProjection(camera);
        if (projection && std::isfinite(projection[0]) && std::isfinite(projection[5]) &&
            std::fabs(projection[0]) > 0.00001f && std::fabs(projection[5]) > 0.00001f) {
            verticalFov = 2.0f * std::atan(1.0f / std::fabs(projection[5]));
            aspect = std::fabs(projection[5] / projection[0]);
        }
        ProbeContext context{this, p};
        if (actionBlend >= 1.0f) motion.seedResolved(pose.position);
        else pose = motion.resolve(target, pose, nearClip, verticalFov, aspect, dt, traceCamera, &context);
        if (finite(pose.position) && finite(pose.forward)) {
            setTransform(camera, scaled(pose.forward, -1.0f), pose.up, pose.position);
            resolvedPose = pose;
            resolvedPlayer = p;
            resolvedCamera = camera;
            interactions::Frame interactionFrame{};
            interactionFrame.player = p;
            interactionFrame.camera = camera;
            interactionFrame.position = pose.position;
            interactionFrame.forward = pose.forward;
            interactionFrame.verticalFovRadians = verticalFov;
            interactionFrame.aspect = aspect;
            interactionFrame.eligible = output.freeCamera && output.applyCamera &&
                !output.temporaryFirstPerson && !input.specialActionActive &&
                output.specialActionBlend <= 0.0f && input.focused && input.gameplay;
            interactions::setFrame(interactionFrame);
        }
    }

    Vec3* remapMovementInput(void* move, Vec3* original, bool recordSample)
    {
        void* p = player();
        void* m = manager();
        if (!original || refreshingVisual || !enabled || !output.freeCamera || !output.applyCamera ||
            output.temporaryFirstPerson || input.specialActionActive ||
            !input.freeControlAvailable || !focused() || !gameplay() ||
            !p || method<GetPlayer>(move, 4)(move) != p || p != resolvedPlayer ||
            !m || field<void*>(m, 80) != resolvedCamera || !normalLookOwnsInput(p) ||
            !finite(*original)) return original;

        const float side = -original->x;
        const float forward = original->z;
        const float horizontal = std::hypot(side, forward);
        if (recordSample) moveSampleAvailable = true;
        if (horizontal < 0.00001f) return original;
        const float cameraYaw = std::atan2(resolvedPose.forward.x, resolvedPose.forward.z);
        const Vec3 world = cameraRelativeMovement(side, forward, cameraYaw);
        Vec3 local{};
        const Vec3* transformed = vectorWorldToLocal(static_cast<unsigned char*>(p) + 24, &local, &world);
        if (!transformed || !finite(*transformed)) return original;
        const float magnitude = std::hypot(transformed->x, transformed->z);
        if (magnitude < 0.00001f) return original;
        original->x = transformed->x * horizontal / magnitude;
        original->z = transformed->z * horizontal / magnitude;
        return original;
    }

    static Vec3* __fastcall movementInputHook(void* move, Vec3* result)
    {
        const void* caller = _ReturnAddress();
        Callback callback;
        Vec3* original = stockMovementInput(move, result);
        Impl* self = current.load(std::memory_order_acquire);
        if (!self || (caller != self->gameModule + 0xCD81C1 &&
                caller != self->gameModule + 0xCE14CA)) return original;
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        return self->remapMovementInput(move, original, caller == self->gameModule + 0xCD81C1);
    }

    static Vec3* __fastcall jumpInputHook(void* move, Vec3* result)
    {
        const void* caller = _ReturnAddress();
        Callback callback;
        Vec3* original = stockJumpInput(move, result);
        Impl* self = current.load(std::memory_order_acquire);
        if (!self || caller != self->gameModule + 0xCDCDA6) return original;
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        return self->remapMovementInput(move, original, false);
    }

    static Vec3* __fastcall weaponDirectionHook(void* p, Vec3* result)
    {
        const void* caller = _ReturnAddress();
        Callback callback;
        Vec3* original = stockWeaponDirection(p, result);
        Impl* self = current.load(std::memory_order_acquire);
        if (!self || !original) return original;
        const bool edgeInput = caller == self->gameModule + 0xCA4E31;
        const bool edgeLaunch = caller == self->gameModule + 0xC9A1C7;
        if (!edgeInput && !edgeLaunch && caller != self->gameModule + 0xD36BD4) return original;
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        void* m = self->manager();
        if (edgeInput || edgeLaunch) {
            // These native callers own ledge-jump acceptance and launch. The
            // edge look controller may have released ownership after acceptance.
            if (!self->refreshingVisual && self->enabled && self->output.freeCamera &&
                self->output.applyCamera && !self->output.temporaryFirstPerson &&
                !self->input.specialActionActive && self->output.specialActionBlend <= 0.0f &&
                focused() && (edgeLaunch || self->gameplay()) && self->ownedView &&
                p == self->player() && p == self->resolvedPlayer &&
                m && m == self->ownerManager && field<int>(m, 88) == 1 &&
                field<void*>(m, 80) == self->resolvedCamera && finite(self->resolvedPose.forward))
                *original = self->resolvedPose.forward;
            return original;
        }
        if (self->refreshingVisual || self->input.specialActionActive || self->output.specialActionBlend > 0.0f ||
            !self->enabled || !self->output.applyCamera || self->output.temporaryFirstPerson ||
            !focused() || !self->gameplay() || !self->ownedView || p != self->player() ||
            p != self->resolvedPlayer || !m || field<int>(m, 88) != 1 ||
            field<void*>(m, 80) != self->resolvedCamera || !self->lastVisual ||
            method<GetPlayer>(p, 194)(p) != self->lastVisual) return original;
        Vec3 origin{};
        Vec3* position = method<GetVector>(p, 230)(p, &origin);
        if (!position || !finite(*position)) return original;
        const Vec3 target = self->trace(p, self->resolvedPose.position,
            add(self->resolvedPose.position, scaled(self->resolvedPose.forward, 1000.0f)));
        if (finite(target)) *original = weaponAimDirection(*position, target, self->resolvedPose.forward);
        return original;
    }

    static Vec3* __fastcall assistDirectionHook(void* dispatcher, Vec3* result, unsigned char mode)
    {
        const void* caller = _ReturnAddress();
        Callback callback;
        Vec3* original = stockAssistDirection(dispatcher, result, mode);
        Impl* self = current.load(std::memory_order_acquire);
        if (!self || !original || caller != self->gameModule + 0xB2ED56) return original;
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        void* p = self->player();
        void* m = self->manager();
        if (self->refreshingVisual || self->input.specialActionActive || self->output.specialActionBlend > 0.0f ||
            !self->enabled || !self->output.applyCamera ||
            self->output.temporaryFirstPerson || !focused() || !self->gameplay() ||
            !self->ownedView || !p || p != self->resolvedPlayer || !m ||
            field<int>(m, 88) != 1 || field<void*>(m, 80) != self->resolvedCamera ||
            !self->lastVisual || method<GetPlayer>(p, 194)(p) != self->lastVisual) return original;
        Vec3 origin{};
        Vec3* position = method<GetVector>(p, 230)(p, &origin);
        if (!position || !finite(*position)) return original;
        const Vec3 target = self->trace(p, self->resolvedPose.position,
            add(self->resolvedPose.position, scaled(self->resolvedPose.forward, 1000.0f)));
        if (finite(target)) *original = weaponAimDirection(*position, target, self->resolvedPose.forward);
        return original;
    }

    static std::uintptr_t __fastcall cameraHook(void* camera)
    {
        Callback callback;
        Impl* self = current.load(std::memory_order_acquire);
        if (!self) return stockCamera(camera);
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        if (self->refreshingVisual) return 0;
        if (self->ownedView || (self->output.applyCamera && !self->output.temporaryFirstPerson)) {
            void* p = self->player();
            void* m = self->manager();
            // A previous visual may have been destroyed since the last SDK tick.
            // Reattach before allowing the stock camera to read its target.
            if (!p || !m) return 0;
            if (field<void*>(m, 80) == camera && field<int>(m, 88) == 1) {
                void* visual = method<GetPlayer>(p, 194)(p);
                if (!visual) return 0;
                if (p != self->ownerPlayer || m != self->ownerManager || visual != self->lastVisual) {
                    // The current visual may have been destroyed. The loader
                    // update will reattach it outside the native callback.
                    return 0;
                }
            }
        }
        if (self->fovCamera == camera) {
            self->setFov(camera, self->originalFov);
            self->fovCamera = nullptr;
        } else if (self->fovCamera) {
            self->fovCamera = nullptr;
        }
        const auto result = stockCamera(camera);
        self->updateCamera(camera, true);
        return result;
    }

    static std::uintptr_t __fastcall firstPersonCameraHook(void* camera)
    {
        Callback callback;
        Impl* self = current.load(std::memory_order_acquire);
        if (!self) return stockFirstPersonCamera(camera);
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        if (self->refreshingVisual) return 0;
        if (self->firstPersonPoseOverridden && self->firstPersonNativeCamera == camera) {
            self->setTransform(camera, scaled(self->firstPersonNativePose.forward, -1.0f),
                self->firstPersonNativePose.up, self->firstPersonNativePose.position);
        }
        self->firstPersonPoseOverridden = false;
        self->firstPersonNativeCamera = nullptr;
        if (self->fovCamera == camera) {
            self->setFov(camera, self->originalFov);
            self->fovCamera = nullptr;
        }
        const auto result = stockFirstPersonCamera(camera);
        if (self->refreshingVisual || !self->enabled || !self->ownedView) return result;
        void* p = self->player();
        void* m = self->manager();
        if (!p || !m || p != self->ownerPlayer || m != self->ownerManager ||
            field<int>(m, 88) != 2 || field<void*>(m, 80) != camera) return result;
        // Refresh accepted action state before the first native FP action frame.
        self->sample(0.0f);
        if (self->input.specialActionActive) self->nativeActionCamera = true;
        if (!self->nativeActionCamera || !self->input.focused || !self->input.gameplay ||
            !self->output.applyCamera || self->output.temporaryFirstPerson ||
            !self->captureActiveFirstPersonPose(camera, p)) return result;
        self->firstPersonNativePose = self->actionPose;
        self->firstPersonNativeCamera = camera;
        self->input.actionCameraAvailable = true;
        self->input.togglePressed = self->input.resetPressed = false;
        self->output = self->controller->update(self->input, 0.0f);
        self->updateCamera(camera, false, true);
        if (self->resolvedCamera == camera && self->resolvedPlayer == p)
            self->firstPersonPoseOverridden = true;
        return result;
    }

    static std::uintptr_t __fastcall lookHook(void* look, float seconds)
    {
        Callback callback;
        Impl* self = current.load(std::memory_order_acquire);
        if (!self) return stockLook(look, seconds);
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        void* p = self->player();
        if (!p || method<GetPlayer>(look, 4)(look) != p) return stockLook(look, seconds);
        self->lookObject = look;
        self->lastLook = GetTickCount64();
        self->sample(0.0f);
        if (self->output.captureMouse) {
            float x = field<float>(look, 124);
            float y = field<float>(look, 128);
            const float radius = std::hypot(x, y);
            const float threshold = *reinterpret_cast<float*>(self->gameModule + 0x1DF5190);
            if (radius > 0.0f && std::isfinite(radius)) {
                const float magnitude = threshold > 0.0f
                    ? (radius < 2.0f * threshold ? radius * radius / (4.0f * threshold) : radius - threshold)
                    : radius;
                float scale[2]{1.0f, 1.0f};
                float pitchScale = 1.0f, yawScale = 1.0f;
                self->sensitivity(look, scale);
                self->multipliers(look, &pitchScale, &yawScale);
                x *= magnitude / radius;
                y *= magnitude / radius;
                self->input.mouseYawDelta = -x * scale[0] * yawScale * Radians;
                self->input.mousePitchDelta = y * scale[1] * pitchScale * Radians;
                self->output = self->controller->update(self->input, 0.0f);
                self->input.mouseYawDelta = self->input.mousePitchDelta = 0.0f;
            }
            field<float>(look, 124) = 0.0f;
            field<float>(look, 128) = 0.0f;
            if (self->output.freeCamera && self->input.freeControlAvailable &&
                !self->input.specialActionActive && self->stableLookReady &&
                !self->output.temporaryFirstPerson && self->normalLookOwnsInput(p)) {
                const float viewYaw = self->stableYaw + self->output.framingYaw + self->output.orbitYaw;
                const float viewPitch = self->stablePitch + self->output.framingPitch + self->output.orbitPitch;
                float desiredYaw = self->stableYaw;
                const bool movementAvailable = self->moveSampleAvailable;
                self->moveSampleAvailable = false;
                if (self->input.aimHeld) desiredYaw = viewYaw;
                else if (movementAvailable) {
                    const auto getInput = method<GetInputValue>(p, 324);
                    const float side = getInput(p, 6) - getInput(p, 7);
                    const float forward = getInput(p, 4) - getInput(p, 5);
                    if (std::isfinite(side) && std::isfinite(forward) && std::hypot(side, forward) > 0.05f) {
                        const Vec3 movement = cameraRelativeMovement(side, forward, viewYaw);
                        desiredYaw = std::atan2(movement.x, movement.z);
                    }
                }
                const float limit = 9.42477796f * std::clamp(seconds, 0.0f, 0.1f);
                const float yawDelta = std::clamp(std::remainder(desiredYaw - self->stableYaw,
                    6.28318530718f), -limit, limit);
                const float pitchDelta = self->input.aimHeld
                    ? std::clamp(viewPitch - self->stablePitch, -limit, limit) : 0.0f;
                float scales[2]{1.0f, 1.0f};
                float pitchScale = 1.0f, yawScale = 1.0f;
                self->sensitivity(look, scales);
                self->multipliers(look, &pitchScale, &yawScale);
                const float yawFactor = scales[0] * yawScale * Radians;
                const float pitchFactor = scales[1] * pitchScale * Radians;
                const float steerThreshold = *reinterpret_cast<float*>(self->gameModule + 0x1DF5190);
                if (std::isfinite(yawFactor) && std::isfinite(pitchFactor) &&
                    std::isfinite(steerThreshold) && steerThreshold >= 0.0f) {
                    const float shapedX = std::fabs(yawFactor) > 0.000001f ? -yawDelta / yawFactor : 0.0f;
                    const float shapedY = std::fabs(pitchFactor) > 0.000001f ? pitchDelta / pitchFactor : 0.0f;
                    const float shapedRadius = std::hypot(shapedX, shapedY);
                    if (std::isfinite(shapedRadius) && shapedRadius > 0.000001f) {
                        const float steerRadius = steerThreshold > 0.0f && shapedRadius < steerThreshold
                            ? std::sqrt(4.0f * steerThreshold * shapedRadius) : shapedRadius + steerThreshold;
                        field<float>(look, 124) = shapedX * steerRadius / shapedRadius;
                        field<float>(look, 128) = shapedY * steerRadius / shapedRadius;
                    }
                }
            }
        }
        const auto result = stockLook(look, seconds);
        if (self->player() == p) {
            const float yaw = field<float>(look, 100);
            const float pitch = field<float>(look, 104);
            self->stableLookReady = std::isfinite(yaw) && std::isfinite(pitch);
            if (self->stableLookReady) {
                self->stableYaw = std::remainder(1.57079632679f - yaw * Radians, 6.28318530718f);
                self->stablePitch = pitch * Radians;
                self->lookPlayer = p;
                if (self->output.freeCamera) {
                    self->input.bodyYaw = self->stableYaw;
                    self->input.bodyPitch = self->stablePitch;
                    self->input.togglePressed = self->input.resetPressed = false;
                    self->output = self->controller->update(self->input, 0.0f);
                }
            }
        }
        return result;
    }

    bool consume(int key) const
    {
        if (key == scanCode(keys.toggle)) return true;
        if (!controller->requested()) return false;
        if (key == scanCode(keys.orbit)) return true;
        if (!down(keys.orbit)) return false;
        return key == scanCode(keys.left) || key == scanCode(keys.right) ||
            key == scanCode(keys.up) || key == scanCode(keys.down) ||
            key == scanCode(keys.closer) || key == scanCode(keys.farther) || key == scanCode(keys.reset);
    }

    static char __fastcall keyboardHook(void* inputManager, int key, unsigned char pressed, unsigned int converter)
    {
        Callback callback;
        Impl* self = current.load(std::memory_order_acquire);
        if (!self || key < 0 || key > 255) return stockKeyboard(inputManager, key, pressed, converter);
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        const auto index = static_cast<size_t>(key);
        const bool eligible = focused() && self->enabled && self->gameplay();
        if (!eligible) self->swallowed.fill(false);
        const bool capture = eligible && self->consume(key);
        if (capture || self->swallowed[index]) {
            if (self->forwarded[index]) {
                stockKeyboard(inputManager, key, 0, converter);
                self->forwarded[index] = false;
            }
            self->swallowed[index] = pressed != 0;
            return 0;
        }
        self->forwarded[index] = pressed != 0;
        return stockKeyboard(inputManager, key, pressed, converter);
    }

    static std::uintptr_t __fastcall mouseButtonHook(void* inputManager, int button, int pressed)
    {
        Callback callback;
        Impl* self = current.load(std::memory_order_acquire);
        if (!self) return stockMouseButton(inputManager, button, pressed);
        std::lock_guard<std::recursive_mutex> lock(self->mutex);
        if (button >= 0xF000 && button <= 0xF004) {
            const int virtualKeys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            const auto index = static_cast<size_t>(button - 0xF000);
            const bool eligible = focused() && self->enabled && self->gameplay();
            if (!eligible) self->swallowedMouse.fill(false);
            const bool capture = eligible && self->consumeVirtualKey(virtualKeys[index]);
            if (capture || self->swallowedMouse[index]) {
                if (self->forwardedMouse[index]) {
                    stockMouseButton(inputManager, button, 0);
                    self->forwardedMouse[index] = false;
                }
                self->swallowedMouse[index] = pressed != 0;
                if (capture) self->sample(0.0f);
                return 0;
            }
            self->forwardedMouse[index] = pressed != 0;
        }
        if (pressed && (button == 0xF000 || button == 0xF001) && self->enabled &&
            focused() && self->gameplay() && self->controller->requested()) {
            self->sample(0.0f);
            self->input.togglePressed = false;
            self->input.resetPressed = false;
            self->input.aimOrFire = true;
            self->output = self->controller->update(self->input, 0.0f);
            void* m = self->manager();
            if (m && field<int>(m, 88) == 1 && field<void*>(m, 80))
                self->updateCamera(field<void*>(m, 80));
        }
        return stockMouseButton(inputManager, button, pressed);
    }

    void reportHookReleaseFailure()
    {
        // A remaining or chained callback must remain executable until exit.
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&cameraHook), &module);
        shutdownFailed = true;
        if (log) log("ERROR", "Third Person could not release a camera hook. Restart Player before enabling it again.");
    }

    void restoreHooks()
    {
        const bool interaction = interactions::disable();
        const bool reticleDisabled = reticle::disable();
        const bool movement = !movementHookEnabled || MH_DisableHook(gameModule + 0xCDBCD0) == MH_OK;
        if (movement) movementHookEnabled = false;
        const bool jump = !jumpHookEnabled || MH_DisableHook(gameModule + 0xCDBBB0) == MH_OK;
        if (jump) jumpHookEnabled = false;
        const bool assist = !assistHookEnabled || MH_DisableHook(gameModule + 0x3BA380) == MH_OK;
        if (assist) assistHookEnabled = false;
        const bool weaponDirection = weaponDirectionPatch.restore();
        const bool mouse = mouseButtonPatch.restore();
        const bool keyboard = keyboardPatch.restore();
        const bool look = lookPatch.restore();
        const bool firstPersonCamera = firstPersonCameraPatch.restore();
        const bool camera = cameraPatch.restore();
        if (!(interaction && reticleDisabled && movement && jump && assist && weaponDirection && mouse && keyboard && look && firstPersonCamera && camera))
            reportHookReleaseFailure();
    }

    void releaseAssistHook()
    {
        // A thread may have entered the detour before incrementing callbacks.
        // Keep a published trampoline alive until process exit.
        if (shutdownFailed || assistHookPublished) return;
        if (assistHookCreated) {
            if (MH_RemoveHook(gameModule + 0x3BA380) != MH_OK) {
                reportHookReleaseFailure();
                return;
            }
            assistHookCreated = false;
            assistHookModule = nullptr;
            stockAssistDirection = nullptr;
        }
        if (assistHookInitialized) {
            if (MH_Uninitialize() != MH_OK) {
                reportHookReleaseFailure();
                return;
            }
            assistHookInitialized = false;
        }
    }

    bool install();
};

bool EngineAdapter::Impl::gameplay() const
{
    void* g = game();
    if (!g || !player()) return false;
    void* inputManager = field<void*>(g, 200);
    void* m = manager();
    return m && field<void*>(m, 64) && inputManager && field<int>(inputManager, 308) == 0 &&
        (field<int>(m, 88) == 1 || field<int>(m, 88) == 2);
}

bool EngineAdapter::Impl::install()
{
    if (shutdownFailed) return false;
    HMODULE executable = GetModuleHandleW(nullptr);
    HMODULE module = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    engineModule = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!module || !engineModule || !fileHash(executable, PlayerHash) ||
        !(fileHash(module, GameHash) || fileHash(module, ToolsGameHash)) ||
        !(fileHash(engineModule, EngineHash) || fileHash(engineModule, ToolsEngineHash)))
        return false;
    gameModule = reinterpret_cast<unsigned char*>(module);
    changeCamera = reinterpret_cast<ChangeCamera>(gameModule + 0x2BFE20);
    refreshAppearance = reinterpret_cast<RefreshAppearance>(gameModule + 0xC62910);
    getPlayer = reinterpret_cast<GetPlayer>(gameModule + 0x3C2D90);
    getActiveLook = reinterpret_cast<GetPlayer>(gameModule + 0xB11B50);
    sensitivity = reinterpret_cast<Sensitivity>(gameModule + 0xCD5460);
    multipliers = reinterpret_cast<Multipliers>(gameModule + 0xCD6880);
    inDuck = reinterpret_cast<InDuck>(gameModule + 0xC652A0);
    getPosition = reinterpret_cast<GetVector>(GetProcAddress(engineModule,
        "?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    getCameraPosition = reinterpret_cast<GetVector>(GetProcAddress(engineModule,
        "?GetPosition@IBaseCamera@@QEBA?BVvec3@@XZ"));
    getCameraForward = reinterpret_cast<GetVector>(GetProcAddress(engineModule,
        "?GetForwardVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    getCameraUp = reinterpret_cast<GetVector>(GetProcAddress(engineModule,
        "?GetUpVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    getMeshElement = reinterpret_cast<GetMeshElement>(GetProcAddress(engineModule,
        "?GetMeshElemFromBoneID@IModelObject@@QEBAHE@Z"));
    isElementHidden = reinterpret_cast<ElementVisibility>(GetProcAddress(engineModule,
        "?IsElementHidden@IModelObject@@QEBA_NH@Z"));
    hideElement = reinterpret_cast<ElementVisibility>(GetProcAddress(engineModule,
        "?HideElement@IModelObject@@QEAA_NH@Z"));
    unhideElement = reinterpret_cast<ElementVisibility>(GetProcAddress(engineModule,
        "?UnhideElement@IModelObject@@QEAA_NH@Z"));
    vectorWorldToLocal = reinterpret_cast<TransformVector>(GetProcAddress(engineModule,
        "?VectorWorldToLocal@IControlObject@@QEBA?AVvec3@@AEBV2@@Z"));
    getNearClip = reinterpret_cast<GetScalar>(GetProcAddress(engineModule,
        "?GetClipNear@IBaseCamera@@QEAAMXZ"));
    getFov = reinterpret_cast<GetScalar>(GetProcAddress(engineModule,
        "?GetFOV@IBaseCamera@@QEAAMXZ"));
    setFov = reinterpret_cast<SetScalar>(GetProcAddress(engineModule,
        "?SetFOV@IBaseCamera@@QEAAXM@Z"));
    getAspect = reinterpret_cast<GetScalar>(GetProcAddress(engineModule,
        "?GetAspect@IBaseCamera@@QEAAMXZ"));
    getProjection = reinterpret_cast<GetMatrix>(GetProcAddress(engineModule,
        "?GetProjectionMatrix@IBaseCamera@@QEAAAEBVmtx44@@XZ"));
    setTransform = reinterpret_cast<Transform>(GetProcAddress(engineModule,
        "?FromForwardUpPos@IBaseCamera@@UEAAXAEBVvec3@@00@Z"));
    raytrace = reinterpret_cast<Raytrace>(GetProcAddress(engineModule,
        "?Raytrace@IGSObject@@QEAAEEPEAUSCollision@@AEBVvec3@@AEAV3@GPEAVIControlObject@@IIH@Z"));
    if (!getPosition || !getCameraPosition || !getCameraForward || !getCameraUp || !vectorWorldToLocal ||
        !setTransform || !raytrace || !getNearClip || !getFov || !setFov ||
        !getAspect || !getProjection || !QueryPerformanceFrequency(&motionFrequency)) return false;
    constexpr unsigned char assistPrologue[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,
        0x24,0x18,0x56,0x48,0x83,0xec,0x40,0x48};
    if (std::memcmp(gameModule + 0x3BA380, assistPrologue, sizeof(assistPrologue)) != 0) return false;
    constexpr unsigned char movementPrologue[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x81,
        0xec,0x80,0x00,0x00,0x00,0x0f,0x29,0x74};
    if (std::memcmp(gameModule + 0xCDBCD0, movementPrologue, sizeof(movementPrologue)) != 0) return false;
    constexpr unsigned char jumpPrologue[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,
        0xec,0x50,0x48,0x8b,0x01,0x0f,0x29,0x74};
    if (std::memcmp(gameModule + 0xCDBBB0, jumpPrologue, sizeof(jumpPrologue)) != 0) return false;
    stockCamera = reinterpret_cast<CameraUpdate>(gameModule + 0x2CB410);
    stockFirstPersonCamera = reinterpret_cast<CameraUpdate>(gameModule + 0x2BB220);
    stockLook = reinterpret_cast<LookUpdate>(gameModule + 0xCD0800);
    stockKeyboard = reinterpret_cast<Keyboard>(gameModule + 0x10C6D10);
    stockMouseButton = reinterpret_cast<MouseButton>(gameModule + 0x10C7390);
    stockWeaponDirection = reinterpret_cast<GetVector>(gameModule + 0xB12950);
    current.store(this, std::memory_order_release);
    bool ok = cameraPatch.install(reinterpret_cast<void**>(gameModule + 0x155C7B8) + 9,
            reinterpret_cast<void*>(stockCamera), reinterpret_cast<void*>(cameraHook)) &&
        firstPersonCameraPatch.install(reinterpret_cast<void**>(gameModule + 0x15590C8) + 9,
            reinterpret_cast<void*>(stockFirstPersonCamera), reinterpret_cast<void*>(firstPersonCameraHook)) &&
        lookPatch.install(reinterpret_cast<void**>(gameModule + 0x180AE48) + 16,
            reinterpret_cast<void*>(stockLook), reinterpret_cast<void*>(lookHook)) &&
        keyboardPatch.install(reinterpret_cast<void**>(gameModule + 0x18FFD78) + 144,
            reinterpret_cast<void*>(stockKeyboard), reinterpret_cast<void*>(keyboardHook)) &&
        mouseButtonPatch.install(reinterpret_cast<void**>(gameModule + 0x18FFD78) + 146,
            reinterpret_cast<void*>(stockMouseButton), reinterpret_cast<void*>(mouseButtonHook)) &&
        weaponDirectionPatch.install(reinterpret_cast<void**>(gameModule + 0x17DD738) + 233,
            reinterpret_cast<void*>(stockWeaponDirection), reinterpret_cast<void*>(weaponDirectionHook));
    if (ok) {
        if (!assistHookInitialized)
            assistHookInitialized = MH_Initialize() == MH_OK;
        if (assistHookInitialized && !assistHookCreated) {
            assistHookCreated = MH_CreateHook(gameModule + 0x3BA380,
                reinterpret_cast<void*>(assistDirectionHook), reinterpret_cast<void**>(&stockAssistDirection)) == MH_OK;
            if (assistHookCreated) assistHookModule = gameModule;
        }
        if (assistHookCreated && assistHookModule == gameModule) {
            HMODULE pinned = nullptr;
            const bool pinnedOk = assistHookPublished || GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&assistDirectionHook), &pinned) != 0;
            if (pinnedOk) {
                // Treat an enable attempt as published: failure may be reported
                // after another thread could reach the detour.
                assistHookPublished = true;
                assistHookEnabled = MH_EnableHook(gameModule + 0x3BA380) == MH_OK;
            }
        }
        ok = assistHookEnabled;
    }
    if (ok) {
        if (!movementHookCreated) {
            movementHookCreated = MH_CreateHook(gameModule + 0xCDBCD0,
                reinterpret_cast<void*>(movementInputHook), reinterpret_cast<void**>(&stockMovementInput)) == MH_OK;
            if (movementHookCreated) movementHookModule = gameModule;
        }
        if (movementHookCreated && movementHookModule == gameModule)
            movementHookEnabled = MH_EnableHook(gameModule + 0xCDBCD0) == MH_OK;
        ok = movementHookEnabled;
    }
    if (ok) {
        if (!jumpHookCreated) {
            jumpHookCreated = MH_CreateHook(gameModule + 0xCDBBB0,
                reinterpret_cast<void*>(jumpInputHook), reinterpret_cast<void**>(&stockJumpInput)) == MH_OK;
            if (jumpHookCreated) jumpHookModule = gameModule;
        }
        if (jumpHookCreated && jumpHookModule == gameModule)
            jumpHookEnabled = MH_EnableHook(gameModule + 0xCDBBB0) == MH_OK;
        ok = jumpHookEnabled;
    }
    if (ok) ok = reticle::install(gameModule);
    if (ok) ok = actionVisualClass.initialize(gameModule, this, actionLocalPlayer, actionClassRequested);
    if (!ok) {
        restoreHooks();
        current.store(nullptr, std::memory_order_release);
        while (callbacks.load(std::memory_order_acquire)) SwitchToThread();
        if (!actionVisualClass.shutdown()) reportHookReleaseFailure();
        releaseAssistHook();
        return false;
    }
    installed = true;
    lastTick = GetTickCount64();
    toggleHeld = down(keys.toggle);
    resetHeld = down(keys.reset);
    return true;
}

EngineAdapter::EngineAdapter() : impl_(std::make_unique<Impl>()) {}
EngineAdapter::~EngineAdapter() { shutdown(); }

bool EngineAdapter::initialize(Controller* controller, const KeyBindings& keys, LogFunction log)
{
    shutdown();
    impl_->controller = controller;
    impl_->keys = keys;
    impl_->log = log;
    if (!controller || !impl_->install()) {
        if (log) log("WARN", "Third Person is unavailable for this Player build.");
        return false;
    }
    impl_->enabled = true;
    return true;
}

void EngineAdapter::setBindings(const KeyBindings& keys)
{
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    impl_->keys = keys;
    impl_->toggleHeld = down(keys.toggle);
    impl_->resetHeld = down(keys.reset);
}

void EngineAdapter::activate()
{
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    reticle::setEligiblePlayer(nullptr);
    impl_->restoreHiddenHead();
    impl_->deferredActionToggle = false;
    impl_->enabled = true;
    impl_->lastTick = GetTickCount64();
    impl_->resetMotion();
    impl_->stableLookReady = false;
    impl_->lookPlayer = nullptr;
    impl_->lookObject = nullptr;
    impl_->ownedView = false;
    impl_->ownerPlayer = impl_->ownerManager = impl_->lastVisual = nullptr;
    impl_->toggleHeld = down(impl_->keys.toggle);
    impl_->resetHeld = down(impl_->keys.reset);
}

void EngineAdapter::deactivate()
{
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    impl_->enabled = false;
    if (impl_->installed) impl_->restoreView();
    if (impl_->controller) impl_->output = impl_->controller->update({}, 0.0f);
    impl_->input = {};
    impl_->swallowed.fill(false);
    impl_->swallowedMouse.fill(false);
}

void EngineAdapter::tick()
{
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!impl_->installed) return;
    const ULONGLONG now = GetTickCount64();
    const float seconds = impl_->lastTick ? (std::min)(static_cast<float>(now - impl_->lastTick) * 0.001f, 0.1f) : 0.0f;
    impl_->lastTick = now;
    impl_->sample(seconds, Impl::callbackDepth == 0);
    interactions::drainDiagnostics(impl_->log);
}

void EngineAdapter::shutdown()
{
    if (!impl_ || !impl_->installed) return;
    deactivate();
    impl_->restoreHooks();
    Impl::current.store(nullptr, std::memory_order_release);
    while (Impl::callbacks.load(std::memory_order_acquire)) SwitchToThread();
    if (!impl_->actionVisualClass.shutdown()) impl_->reportHookReleaseFailure();
    impl_->releaseAssistHook();
    impl_->installed = false;
    impl_->controller = nullptr;
    impl_->log = nullptr;
}

bool EngineAdapter::active() const { return impl_->installed && impl_->enabled; }
} // namespace thirdperson
