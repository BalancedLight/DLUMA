#include "engine_interactions.h"
#include "interaction_selection.h"

#include <MinHook.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace thirdperson::interactions {
namespace {
template<class T> T& field(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}
template<class T> T method(void* object, std::size_t slot)
{
    return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[slot]);
}

struct NativeVector {
    void** data = nullptr;
    std::uint32_t size = 0;
    std::uint32_t capacity = 0;
    std::uint32_t allocatorTag = 77;
};
// Native picker context: the only pointer is the owning player at offset 0;
// the remaining fields are action codes, flags, and scalar reach/angle limits.
struct alignas(8) QueryContext { std::array<unsigned char, 40> bytes{}; };
using Pick = void (__fastcall*)(void*, NativeVector*);
using Query = bool (__fastcall*)(void*, const QueryContext*, NativeVector*, unsigned int, const Vec3*);
using SetTarget = void (__fastcall*)(void*, void*, unsigned char);
using Activate = void (__fastcall*)(void*, unsigned int, unsigned char);
using GetPlayer = void* (__fastcall*)(void*);
using GetVector = Vec3* (__fastcall*)(void*, Vec3*);
using GetLevel = void* (__fastcall*)(void*);
using GetParent = void* (__fastcall*)(void*);
using GetObjectId = std::int32_t (__fastcall*)(void*);
using GetObjectById = void* (__fastcall*)(void*, std::int32_t);
using GetArray = const float* (__fastcall*)(void*);
using IsActive = bool (__fastcall*)(void*);
using NearShape = bool (__fastcall*)(void*, const Vec3*);
using IsUsable = bool (__fastcall*)(void*, const QueryContext*, unsigned int);
using GetAction = unsigned int (__fastcall*)(void*, const QueryContext*);
using Free = void (__cdecl*)(void*);
using Raytrace = unsigned char (__fastcall*)(void*, unsigned char, void*, const Vec3*, Vec3*,
    unsigned short, void*, unsigned int, unsigned int, int);

struct QuerySnapshot {
    QueryContext context{};
    void* level = nullptr;
    void* player = nullptr;
    unsigned int flags = 0;
    DWORD thread = 0;
    std::uint64_t generation = 0;
    ULONGLONG validatedAt = 0;
    Vec3 targetPosition{};
    std::array<float, 6> bounds{};
    std::array<float, 12> transform{};
    bool boundsValid = false;
    bool rayCandidate = false;
    bool valid = false;
};
struct PickerScope {
    PickerScope* previous = nullptr;
    Frame frame{};
    NativeVector* candidates = nullptr;
    QuerySnapshot query{};
    std::array<void*,64> reachable{};
    std::size_t reachableCount = 0;
    Vec3 cameraRayHit{};
    bool usedCameraRay = false;
    bool hasCameraRayHit = false;
};

std::recursive_mutex stateMutex;
Frame publishedFrame{};
std::uint64_t generation = 1;
std::uint64_t poseRevision = 1;
QuerySnapshot selectedQuery{};
void* selectedObject = nullptr;
void* selectedReference = nullptr;
std::int32_t selectedObjectId = -1;
void* pendingPlayer = nullptr;
unsigned activePickers = 0;
unsigned activeActivations = 0;
unsigned char* game = nullptr;
HMODULE boundEngine = nullptr;
GetPlayer getPlayer = nullptr;
GetVector getWorldPosition = nullptr;
GetLevel getLevel = nullptr;
GetParent getParent = nullptr;
GetObjectId getObjectId = nullptr;
GetObjectById getObjectById = nullptr;
GetArray getWorldBounds = nullptr;
GetArray getWorldTransform = nullptr;
IsActive isActive = nullptr;
NearShape nearShape = nullptr;
IsUsable isUsable = nullptr;
GetAction getAction = nullptr;
Raytrace raytrace = nullptr;
Raytrace originalRaytrace = nullptr;
Free nativeFree = nullptr;
Pick originalPick = nullptr;
Query originalQuery = nullptr;
SetTarget originalSetTarget = nullptr;
Activate originalActivate = nullptr;
thread_local PickerScope* pickerScope = nullptr;
std::array<bool, 5> created{};
std::array<bool, 5> enabled{};
bool published = false;
bool cleanupFailure = false;

enum Diagnostic : unsigned {
    TargetPending = 1u << 0, TargetChanged = 1u << 1, Ineligible = 1u << 2,
    StaleQuery = 1u << 3, ChangedLevel = 1u << 5,
    NoLongerReachable = 1u << 6, NotVisible = 1u << 7,
    ChangedDuringValidation = 1u << 8, Forwarded = 1u << 9,
    ExpiredSelection = 1u << 10, PositionChanged = 1u << 11
};
std::atomic<unsigned> pendingDiagnostics{0};
std::atomic<unsigned> emittedDiagnostics{0};
thread_local bool suppressDiagnostics = false;
void note(Diagnostic reason) {
    if (!suppressDiagnostics) pendingDiagnostics.fetch_or(reason, std::memory_order_relaxed);
}
struct DiagnosticSuppression {
    bool previous = suppressDiagnostics;
    DiagnosticSuppression() { suppressDiagnostics = true; }
    ~DiagnosticSuppression() { suppressDiagnostics = previous; }
};

struct ScopeGuard {
    explicit ScopeGuard(PickerScope& scope) { scope.previous = pickerScope; pickerScope = &scope; }
    ~ScopeGuard() {
        pickerScope = pickerScope->previous;
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        --activePickers;
    }
};
struct PendingGuard {
    void* player;
    ~PendingGuard() {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (pendingPlayer == player) pendingPlayer = nullptr;
    }
};
struct ActivationGuard {
    ActivationGuard() {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        ++activeActivations;
    }
    ~ActivationGuard() {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        --activeActivations;
    }
};

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
bool finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
Vec3 unit(Vec3 v)
{
    const float length = std::sqrt(dot(v,v));
    return std::isfinite(length) && length > 0.00001f ? Vec3{v.x/length,v.y/length,v.z/length} : Vec3{};
}

template<std::size_t N> bool finiteArray(const std::array<float, N>& values)
{
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

bool captureBounds(void* object, QuerySnapshot& query)
{
    void* control = static_cast<unsigned char*>(object) + 24;
    const float* bounds = getWorldBounds(control);
    if (!bounds) return false;
    std::copy_n(bounds, query.bounds.size(), query.bounds.begin());
    const float* transform = getWorldTransform(control);
    if (!transform) return false;
    std::copy_n(transform, query.transform.size(), query.transform.begin());
    return finiteArray(query.bounds) && finiteArray(query.transform) &&
        query.bounds[0] <= query.bounds[3] && query.bounds[1] <= query.bounds[4] &&
        query.bounds[2] <= query.bounds[5];
}

Frame snapshot(std::uint64_t* epoch = nullptr, std::uint64_t* revision = nullptr)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (epoch) *epoch = generation;
    if (revision) *revision = poseRevision;
    return publishedFrame;
}

bool eligible(const Frame& frame, void* player)
{
    if (!game || !frame.eligible || !frame.player || player != frame.player || !frame.camera ||
        !finite(frame.position) || !finite(frame.forward) || !std::isfinite(frame.verticalFovRadians) ||
        !std::isfinite(frame.aspect)) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    if (process != GetCurrentProcessId()) return false;
    void* instance = *reinterpret_cast<void**>(game + 0x1D07F88);
    if (!instance || getPlayer(instance) != player) return false;
    void* input = field<void*>(instance, 200);
    void* session = field<void*>(instance, 1288);
    void* manager = session ? field<void*>(session, 192) : nullptr;
    return input && field<int>(input, 308) == 0 && manager &&
        field<int>(manager, 88) == 1 && field<void*>(manager, 80) == frame.camera;
}

unsigned char __fastcall pickerRayHook(void* owner, unsigned char mask, void* collision,
    const Vec3* start, Vec3* end, unsigned short material, void* excluded,
    unsigned int flagsA, unsigned int flagsB, int flagsC)
{
    const void* caller = _ReturnAddress();
    PickerScope* scope = pickerScope;
    const bool initial = game && caller == game + 0xB0CFF9;
    const bool continuation = game && caller == game + 0xB0D12D;
    if (!scope || (!initial && !continuation))
        return originalRaytrace(owner,mask,collision,start,end,material,excluded,flagsA,flagsB,flagsC);
    std::uint64_t epoch = 0;
    const Frame latest = snapshot(&epoch);
    if (!scope->query.valid ||
        scope->query.thread != GetCurrentThreadId() || epoch != scope->query.generation ||
        latest.player != scope->frame.player ||
        latest.camera != scope->frame.camera || !eligible(scope->frame,scope->query.player) ||
        !start || !end || !finite(*start) || !finite(*end)) {
        if (continuation && scope->usedCameraRay) return 0;
        return originalRaytrace(owner,mask,collision,start,end,material,excluded,flagsA,flagsB,flagsC);
    }

    const Vec3 forward = unit(scope->frame.forward);
    if (dot(forward,forward) < 0.5f) {
        if (continuation && scope->usedCameraRay) return 0;
        return originalRaytrace(owner,mask,collision,start,end,material,excluded,flagsA,flagsB,flagsC);
    }
    Vec3 rayStart{}, rayEnd{};
    if (initial) {
        const Vec3 nativeSegment = subtract(*end,*start);
        const Vec3 cameraToPlayer = subtract(scope->frame.position,*start);
        const float length = std::sqrt(dot(nativeSegment,nativeSegment)) +
            std::sqrt(dot(cameraToPlayer,cameraToPlayer));
        if (!std::isfinite(length) || length <= 0.0f || length > 100.0f)
            return originalRaytrace(owner,mask,collision,start,end,material,excluded,flagsA,flagsB,flagsC);
        rayStart = scope->frame.position;
        rayEnd = {rayStart.x+forward.x*length,rayStart.y+forward.y*length,rayStart.z+forward.z*length};
    } else {
        if (scope->usedCameraRay && !scope->hasCameraRayHit) return 0;
        if (!scope->hasCameraRayHit)
            return originalRaytrace(owner,mask,collision,start,end,material,excluded,flagsA,flagsB,flagsC);
        rayStart = {scope->cameraRayHit.x+forward.x*0.003f,scope->cameraRayHit.y+forward.y*0.003f,
            scope->cameraRayHit.z+forward.z*0.003f};
        rayEnd = {rayStart.x+forward.x*0.04f,rayStart.y+forward.y*0.04f,rayStart.z+forward.z*0.04f};
    }
    const Vec3 requestedEnd = rayEnd;
    const unsigned char result = originalRaytrace(owner,mask,collision,&rayStart,&rayEnd,
        material,excluded,flagsA,flagsB,flagsC);
    *end = rayEnd;
    if (initial) {
        scope->usedCameraRay = true;
        scope->cameraRayHit = rayEnd;
        const Vec3 segment = subtract(requestedEnd,rayStart);
        const Vec3 travelled = subtract(rayEnd,rayStart);
        const Vec3 remaining = subtract(requestedEnd,rayEnd);
        const float segmentSquared = dot(segment,segment);
        const float progress = segmentSquared > 0.0f ? dot(travelled,segment)/segmentSquared : -1.0f;
        const Vec3 residual{travelled.x-segment.x*progress,travelled.y-segment.y*progress,
            travelled.z-segment.z*progress};
        scope->hasCameraRayHit = result && finite(rayEnd) && std::isfinite(progress) &&
            progress >= 0.0f && progress <= 1.0f && dot(remaining,remaining) > 0.00000001f &&
            dot(residual,residual) <= 0.0001f;
    }
    return result;
}

bool candidatePoint(void* candidate, Vec3& point)
{
    if (!candidate) return false;
    Vec3 buffer{};
    Vec3* value = method<GetVector>(candidate, 181)(candidate, &buffer);
    if (!value || !finite(*value)) return false;
    point = *value;
    return true;
}

bool cameraVisible(const Frame& frame, void* candidate, Vec3 point, float& angle)
{
    const Vec3 direction = unit(frame.forward);
    if (dot(direction,direction) < 0.5f) return false;
    const Vec3 delta = subtract(point, frame.position);
    const float distanceSquared = dot(delta, delta);
    if (!std::isfinite(distanceSquared) || distanceSquared < 0.000001f) return false;
    const float forward = dot(delta, direction);
    if (forward <= 0.0f) return false;
    Vec3 right = unit(cross({0.0f,1.0f,0.0f}, direction));
    if (dot(right,right) < 0.5f) right = unit(cross({0.0f,0.0f,1.0f}, direction));
    const Vec3 up = unit(cross(direction,right));
    const float tangent = std::tan(std::clamp(frame.verticalFovRadians,0.1f,3.0f)*0.5f);
    const float aspect = std::clamp(frame.aspect,0.1f,10.0f);
    if (std::fabs(dot(delta,right)) > forward*tangent*aspect+0.15f ||
        std::fabs(dot(delta,up)) > forward*tangent+0.15f) return false;
    angle = std::acos(std::clamp(forward/std::sqrt(distanceSquared),-1.0f,1.0f));
    alignas(16) std::array<unsigned char, 256> collision{};
    const std::int32_t noElement = -1;
    std::memcpy(collision.data()+32, &noElement, sizeof(noElement));
    Vec3 end = point;
    const bool hit = raytrace(static_cast<unsigned char*>(frame.player)+40,0x27,
        collision.data(),&frame.position,&end,0,static_cast<unsigned char*>(frame.player)+24,0,0,4044024) != 0;
    if (!hit) return true;
    void* hitControl = nullptr;
    std::memcpy(&hitControl, collision.data()+24, sizeof(hitControl));
    void* candidateControl = static_cast<unsigned char*>(candidate)+24;
    for (unsigned i = 0; hitControl && i < 8; ++i) {
        if (hitControl == candidateControl) return true;
        void* parent = getParent(hitControl);
        if (parent == hitControl) break;
        hitControl = parent;
    }
    // Ground-level interaction points can lie exactly on a floor surface.
    // Retest the segment just short of the point: an obstacle earlier in the
    // path still blocks the candidate even if the hit point is unavailable.
    const float distance = std::sqrt(distanceSquared);
    constexpr float endpointClearance = 0.02f;
    if (distance <= endpointClearance) return false;
    const float fraction = (distance - endpointClearance) / distance;
    Vec3 shortened = {frame.position.x + delta.x * fraction,
        frame.position.y + delta.y * fraction, frame.position.z + delta.z * fraction};
    collision.fill(0);
    std::memcpy(collision.data()+32, &noElement, sizeof(noElement));
    return !raytrace(static_cast<unsigned char*>(frame.player)+40,0x27,
        collision.data(),&frame.position,&shortened,0,static_cast<unsigned char*>(frame.player)+24,0,0,4044024);
}

bool validateCachedCandidate(const Frame& frame, void* player, void* object,
    void* level, std::int32_t id, const QuerySnapshot& query)
{
    // The HUD picker runs on a timer slightly longer than 100 ms. Leave room
    // for a missed update while requiring fresh geometry and action checks.
    constexpr ULONGLONG maxAgeMs = 350;
    constexpr float transformTolerance = 0.0001f;
    constexpr float pointToleranceSquared = 0.01f * 0.01f;
    if (GetTickCount64() - query.validatedAt > maxAgeMs) { note(ExpiredSelection); return false; }
    if (!query.boundsValid || id < 0 || getObjectById(level,id) != static_cast<unsigned char*>(object)+40 ||
        !isActive(static_cast<unsigned char*>(object)+40)) { note(NoLongerReachable); return false; }

    void* control = static_cast<unsigned char*>(object)+24;
    const float* matrix = getWorldTransform(control);
    if (!matrix) { note(PositionChanged); return false; }
    std::array<float, 12> currentTransform{};
    std::copy_n(matrix,currentTransform.size(),currentTransform.begin());
    if (!finiteArray(currentTransform)) { note(PositionChanged); return false; }
    for (std::size_t i=0; i<currentTransform.size(); ++i) {
        if (i % 4 != 3 && std::fabs(currentTransform[i]-query.transform[i]) > transformTolerance) {
            note(PositionChanged);
            return false;
        }
    }
    const Vec3 translation{currentTransform[3]-query.transform[3],
        currentTransform[7]-query.transform[7], currentTransform[11]-query.transform[11]};
    Vec3 target{};
    if (!candidatePoint(object,target)) { note(PositionChanged); return false; }
    const Vec3 expected{query.targetPosition.x+translation.x,query.targetPosition.y+translation.y,
        query.targetPosition.z+translation.z};
    const Vec3 difference = subtract(target,expected);
    if (dot(difference,difference) > pointToleranceSquared) { note(PositionChanged); return false; }

    Vec3 eyeBuffer{};
    Vec3* eye = method<GetVector>(player,230)(player,&eyeBuffer);
    if (!eye || !finite(*eye) || !nearShape(object,eye)) {
        note(NoLongerReachable);
        return false;
    }
    QueryContext context = query.context;
    const unsigned int category = getAction(object,&context);
    float radius = 0.0f;
    const std::size_t radiusOffset = (category == 2 || category == 4 || category == 5) ? 16 : 12;
    std::memcpy(&radius,context.bytes.data()+radiusOffset,sizeof(radius));
    if (!std::isfinite(radius) || radius < 0.0f) { note(NoLongerReachable); return false; }
    const float shift[] = {translation.x,translation.y,translation.z};
    const float coordinates[] = {eye->x,eye->y,eye->z};
    float distanceSquared = 0.0f;
    for (std::size_t i=0; i<3; ++i) {
        const float low = query.bounds[i]+shift[i];
        const float high = query.bounds[i+3]+shift[i];
        const float differenceFromBounds = coordinates[i] < low ? low-coordinates[i] :
            (coordinates[i] > high ? coordinates[i]-high : 0.0f);
        distanceSquared += differenceFromBounds*differenceFromBounds;
    }
    if (!std::isfinite(distanceSquared) || distanceSquared > radius*radius) {
        note(NoLongerReachable);
        return false;
    }
    std::uint32_t slot = 0;
    std::memcpy(&slot,context.bytes.data()+8,sizeof(slot));
    bool usable = false;
    if (slot == 4) {
        for (std::uint32_t i=0; i<4 && !usable; ++i) {
            std::memcpy(context.bytes.data()+8,&i,sizeof(i));
            usable = isUsable(object,&context,query.flags);
        }
    } else usable = isUsable(object,&context,query.flags);
    if (!usable) { note(NoLongerReachable); return false; }

    Frame fromPlayer = frame;
    fromPlayer.position = *eye;
    fromPlayer.forward = subtract(target,*eye);
    const bool atTarget = dot(fromPlayer.forward,fromPlayer.forward) < 0.000001f;
    float angle = 0.0f;
    if (!atTarget && !cameraVisible(fromPlayer,object,target,angle)) {
        note(NoLongerReachable);
        return false;
    }
    if (!cameraVisible(frame,object,target,angle)) { note(NotVisible); return false; }
    return true;
}

void* currentTarget(void* player, void** reference = nullptr)
{
    void* control = field<void*>(player,5336);
    if (reference) *reference = control;
    return control ? field<void*>(player,5344) : nullptr;
}

bool listContains(const NativeVector& list, void* candidate)
{
    if (!list.data || list.size > 64 || list.size > list.capacity) return false;
    for (std::uint32_t i=0; i<list.size; ++i) if (list.data[i] == candidate) return true;
    return false;
}

void __fastcall pickHook(void* player, NativeVector* output)
{
    std::uint64_t epoch = 0;
    Frame frame = snapshot(&epoch);
    if (!output || !eligible(frame,player)) {
        originalPick(player,output);
        return;
    }
    PickerScope scope{};
    scope.frame = frame;
    scope.candidates = output;
    scope.query.player = player;
    scope.query.generation = epoch;
    scope.query.thread = GetCurrentThreadId();
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (generation != epoch ||
            !publishedFrame.eligible || publishedFrame.player != player ||
            publishedFrame.camera != frame.camera) {
            // Call the native picker outside this scope below.
            scope.query.valid = false;
        } else {
            ++activePickers;
            scope.query.valid = true;
        }
    }
    if (!scope.query.valid) { originalPick(player,output); return; }
    scope.query.valid = false;
    ScopeGuard guard(scope);
    originalPick(player,output);
}

bool __fastcall queryHook(void* level, const QueryContext* context, NativeVector* output,
    unsigned int flags, const Vec3* direction)
{
    const void* caller = _ReturnAddress();
    PickerScope* scope = pickerScope;
    void* contextPlayer = nullptr;
    if (context) std::memcpy(&contextPlayer, context->bytes.data(), sizeof(contextPlayer));
    bool currentScope = false;
    if (scope) {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        currentScope = scope->query.generation == generation &&
            scope->query.thread == GetCurrentThreadId() && publishedFrame.eligible &&
            publishedFrame.player == scope->query.player && publishedFrame.camera == scope->frame.camera;
    }
    if (!scope || !currentScope || !game || caller != game + 0xB0CD39 || !context || output != scope->candidates ||
        contextPlayer != scope->query.player ||
        !eligible(scope->frame,scope->query.player))
        return originalQuery(level,context,output,flags,direction);
    QueryContext copy = *context;
    Vec3 sentinel{};
    std::memcpy(&sentinel,game+0x1D08330,sizeof(sentinel));
    if (!finite(sentinel)) return originalQuery(level,context,output,flags,direction);
    // The native zero-direction sentinel skips only the cone test. Keep every
    // context flag intact for native object-specific eligibility checks.
    const bool found = originalQuery(level,&copy,output,flags,&sentinel);
    scope->query.context = copy;
    scope->query.level = level;
    scope->query.flags = flags;
    scope->query.validatedAt = GetTickCount64();
    scope->query.valid = output && output->size <= scope->reachable.size() &&
        output->size <= output->capacity && (!output->size || output->data);
    scope->reachableCount = scope->query.valid ? output->size : 0;
    for (std::size_t i = 0; i < scope->reachableCount; ++i)
        scope->reachable[i] = output->data[i];
    return found;
}

void __fastcall setTargetHook(void* player, void* nativeTarget, unsigned char mode)
{
    const void* caller = _ReturnAddress();
    PickerScope* scope = pickerScope;
    if (scope && game && caller == game + 0xB0DA0F && player == scope->query.player && scope->query.valid) {
        std::uint64_t epoch = 0;
        const Frame latest = snapshot(&epoch);
        if (epoch != scope->query.generation ||
            scope->query.thread != GetCurrentThreadId() || latest.camera != scope->frame.camera || !eligible(latest,player))
            return;
    }
    if (!scope || !game || caller != game + 0xB0DA0F || player != scope->query.player ||
        !scope->query.valid || !eligible(scope->frame,player) || !scope->candidates) {
        originalSetTarget(player,nativeTarget,mode);
        // Retire our ownership only after the native picker has actually
        // installed its own target or replaced the old weak reference.
        if (game && caller == game + 0xB0DA0F && player) {
            void* ref = nullptr;
            void* observed = currentTarget(player,&ref);
            std::lock_guard<std::recursive_mutex> lock(stateMutex);
            if (selectedQuery.player == player &&
                ((nativeTarget && observed == nativeTarget) ||
                 observed != selectedObject || ref != selectedReference)) {
                selectedObject = selectedReference = nullptr;
                selectedObjectId = -1;
                selectedQuery = {};
            }
        }
        return;
    }
    Vec3 root{};
    Vec3* rootPoint = getWorldPosition(static_cast<unsigned char*>(player)+24,&root);
    const bool usableList = rootPoint && finite(*rootPoint) &&
        scope->candidates->size <= 64 && scope->candidates->size <= scope->candidates->capacity &&
        (!scope->candidates->size || scope->candidates->data) &&
        getLevel(static_cast<unsigned char*>(player)+40) == scope->query.level;
    void* oldTarget = currentTarget(player);
    std::array<InteractionCandidate,64> candidates{};
    std::array<void*,64> objects{};
    std::array<Vec3,64> points{};
    std::array<bool,64> rayCandidates{};
    std::size_t used = 0;
    std::uint64_t currentId = 0;
    for (std::uint32_t i=0; usableList && i<scope->candidates->size; ++i) {
        void* object = scope->candidates->data[i];
        if (!object) continue;
        const bool nearby = std::find(scope->reachable.begin(), scope->reachable.begin()+scope->reachableCount, object) !=
            scope->reachable.begin()+scope->reachableCount;
        void* scriptObject = static_cast<unsigned char*>(object)+40;
        const std::int32_t objectId = getObjectId(scriptObject);
        if (objectId < 0 || getObjectById(scope->query.level,objectId) != scriptObject) continue;
        Vec3 point{}; float angle = 0.0f;
        if (!candidatePoint(object,point) || !cameraVisible(scope->frame,object,point,angle)) continue;
        if (!nearby) {
            // The native picker may append a ray hit that the nearby query
            // omitted. Its stock type checks already ran; apply the same
            // per-object reach, usability and visibility checks before use.
            QuerySnapshot rayQuery = scope->query;
            rayQuery.targetPosition = point;
            rayQuery.boundsValid = captureBounds(object,rayQuery);
            DiagnosticSuppression quiet;
            if (!validateCachedCandidate(scope->frame,player,object,scope->query.level,objectId,rayQuery))
                continue;
        }
        const Vec3 delta = subtract(point,*rootPoint);
        auto& candidate = candidates[used];
        objects[used] = object;
        rayCandidates[used] = !nearby;
        points[used++] = point;
        candidate.id = static_cast<std::uint64_t>(static_cast<std::uint32_t>(objectId))+1;
        if (object == oldTarget) currentId = candidate.id;
        candidate.priority = 0;
        candidate.distanceSquared = dot(delta,delta);
        candidate.viewAngle = angle;
        candidate.reachable = candidate.unobstructed = candidate.usable = candidate.visible = true;
    }
    std::uint64_t chosenId = 0;
    void* chosen = nullptr;
    QuerySnapshot candidateQuery = scope->query;
    // A displayed target must be validatable on either input thread. If its
    // bounds cannot be captured, try the next visible candidate instead.
    for (std::size_t attempt=0; attempt<used; ++attempt) {
        chosenId = selectInteraction(candidates.data(),used,currentId);
        if (!chosenId) break;
        std::size_t selected = 0;
        while (selected < used && candidates[selected].id != chosenId) ++selected;
        if (selected == used) { chosenId = 0; break; }
        candidateQuery.targetPosition = points[selected];
        candidateQuery.boundsValid = captureBounds(objects[selected],candidateQuery);
        candidateQuery.rayCandidate = rayCandidates[selected];
        if (candidateQuery.boundsValid) { chosen = objects[selected]; break; }
        candidates[selected].usable = false;
        chosenId = 0;
    }
    // Reserve this selection until ownership metadata is published; an action
    // arriving concurrently must never observe a newly selected, untracked target.
    if (!eligible(snapshot(),player)) return;
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (scope->query.generation != generation ||
            scope->query.thread != GetCurrentThreadId() || !publishedFrame.eligible ||
            publishedFrame.player != player || publishedFrame.camera != scope->frame.camera || pendingPlayer)
            return;
        pendingPlayer = player;
    }
    PendingGuard pending{player};
    // Mode 1 bypasses the native retention grace when no visible candidate
    // remains, so a stale prompt is not left active after selection fails.
    originalSetTarget(player,chosen,chosen ? mode : 1);
    void* reference = nullptr;
    void* observed = currentTarget(player,&reference);
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (chosen && observed == chosen && reference) {
        selectedObject = chosen;
        selectedReference = reference;
        selectedObjectId = static_cast<std::int32_t>(chosenId - 1);
        selectedQuery = candidateQuery;
        if (scope->query.generation != generation ||
            scope->query.thread != GetCurrentThreadId() || !publishedFrame.eligible ||
            publishedFrame.player != player || publishedFrame.camera != scope->frame.camera)
            selectedQuery.valid = false;
    } else if (observed && observed == selectedObject && reference == selectedReference &&
        selectedQuery.player == player) {
        // The stock setter can retain its old target briefly after a miss.
        // Keep ownership so activation still forces fresh validation.
        selectedQuery.valid = false;
    } else {
        selectedObject = selectedReference = nullptr;
        selectedObjectId = -1;
        selectedQuery = {};
    }
}

void __fastcall activateHook(void* player, unsigned int action, unsigned char flags)
{
    ActivationGuard activation{};
    QuerySnapshot query{};
    void* expectedObject = nullptr;
    void* expectedReference = nullptr;
    std::int32_t expectedId = -1;
    Frame frame{};
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (pendingPlayer == player) { note(TargetPending); return; }
        frame = publishedFrame;
        query = selectedQuery;
        expectedObject = selectedObject;
        expectedReference = selectedReference;
        expectedId = selectedObjectId;
    }
    void* reference = nullptr;
    void* current = player ? currentTarget(player,&reference) : nullptr;
    if (expectedObject && player == query.player &&
        (current != expectedObject || reference != expectedReference)) {
        note(TargetChanged);
        // A target changed after the prompt was selected. Let the next native
        // picker publish it before any action can use the replacement.
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (selectedObject == expectedObject && selectedReference == expectedReference)
            selectedQuery.valid = false;
        return;
    }
    if (expectedObject && player == query.player && current == expectedObject && reference == expectedReference) {
        bool valid = false;
        std::uint64_t epoch = 0;
        std::uint64_t revision = 0;
        frame = snapshot(&epoch,&revision);
        const bool eligibleNow = eligible(frame,player);
        void* level = eligibleNow ? getLevel(static_cast<unsigned char*>(player)+40) : nullptr;
        const bool sameThread = query.thread == GetCurrentThreadId();
        if (!eligibleNow) note(Ineligible);
        else if (!query.valid || epoch != query.generation) note(StaleQuery);
        else if (!level || level != query.level) note(ChangedLevel);
        if (eligibleNow && query.valid && epoch == query.generation && level && level == query.level && sameThread) {
            NativeVector fresh{};
            QueryContext context = query.context;
            Vec3 sentinel{};
            std::memcpy(&sentinel,game+0x1D08330,sizeof(sentinel));
            if (finite(sentinel)) (void)originalQuery(level,&context,&fresh,query.flags,&sentinel);
            if (expectedId >= 0 && listContains(fresh,current) &&
                getObjectById(level,expectedId) == static_cast<unsigned char*>(current)+40) {
                Vec3 point{}; float angle = 0.0f;
                valid = candidatePoint(current,point) && cameraVisible(frame,current,point,angle);
                if (!valid) note(NotVisible);
            } else if (query.rayCandidate) {
                valid = validateCachedCandidate(frame,player,current,level,expectedId,query);
            } else note(NoLongerReachable);
            if (fresh.data) nativeFree(fresh.data);
        } else if (eligibleNow && query.valid && epoch == query.generation && level && level == query.level) {
            // The original input callback can run on another thread. Validate
            // only the selected candidate here; the full nearby query changes
            // collision flags and belongs to the native picker thread.
            valid = validateCachedCandidate(frame,player,current,level,expectedId,query);
        }
        if (valid && !sameThread) {
            std::uint64_t latestEpoch = 0, latestRevision = 0;
            const Frame updated = snapshot(&latestEpoch,&latestRevision);
            if (latestRevision != revision) {
                Vec3 point{}; float angle = 0.0f;
                valid = latestEpoch == epoch && updated.player == player && updated.camera == frame.camera &&
                    eligible(updated,player) && candidatePoint(current,point) &&
                    cameraVisible(updated,current,point,angle);
                if (!valid) note(NotVisible);
                else { frame = updated; revision = latestRevision; }
            }
        }
        if (valid) {
            Frame latest{};
            {
                std::lock_guard<std::recursive_mutex> lock(stateMutex);
                latest = publishedFrame;
                valid = generation == epoch && poseRevision == revision &&
                    selectedQuery.valid && selectedQuery.generation == epoch &&
                    selectedObject == expectedObject && selectedReference == expectedReference &&
                    selectedObjectId == expectedId && pendingPlayer != player &&
                    latest.player == player && latest.camera == frame.camera;
            }
            void* latestReference = nullptr;
            void* latestTarget = currentTarget(player,&latestReference);
            valid = valid && latestTarget == expectedObject && latestReference == expectedReference &&
                eligible(latest,player) && getLevel(static_cast<unsigned char*>(player)+40) == level &&
                getObjectById(level,expectedId) == static_cast<unsigned char*>(expectedObject)+40;
            if (valid) {
                std::lock_guard<std::recursive_mutex> lock(stateMutex);
                // This locked recheck commits the action. Later revocation
                // stops future selections, while this native action finishes.
                valid = generation == epoch && poseRevision == revision &&
                    selectedQuery.valid && selectedQuery.generation == epoch &&
                    selectedObject == expectedObject && selectedReference == expectedReference &&
                    selectedObjectId == expectedId && pendingPlayer != player;
            }
            if (!valid) note(ChangedDuringValidation);
        }
        if (!valid) {
            void* latestReference = nullptr;
            void* latestTarget = currentTarget(player,&latestReference);
            void* instance = game ? *reinterpret_cast<void**>(game+0x1D07F88) : nullptr;
            if (query.thread == GetCurrentThreadId() && instance && getPlayer(instance) == player &&
                latestTarget == expectedObject && latestReference == expectedReference)
                originalSetTarget(player,nullptr,1);
            latestTarget = currentTarget(player,&latestReference);
            std::lock_guard<std::recursive_mutex> lock(stateMutex);
            if (selectedObject == expectedObject && selectedReference == expectedReference) {
                if (latestTarget == expectedObject && latestReference == expectedReference)
                    selectedQuery.valid = false;
                else {
                    selectedObject = selectedReference = nullptr;
                    selectedObjectId = -1;
                    selectedQuery = {};
                }
            }
            return;
        }
    }
    if (expectedObject && player == query.player) note(Forwarded);
    originalActivate(player,action,flags);
}

constexpr std::array<std::uintptr_t,4> offsets{0xB0C9C0,0xE9C940,0xB61B60,0xB3A7D0};
constexpr unsigned char prologues[4][16] = {
    {0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x41,0x55,0x48,0x8d},
    {0x44,0x89,0x4c,0x24,0x20,0x48,0x89,0x4c,0x24,0x08,0x55,0x57,0x41,0x56,0x41,0x57},
    {0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x8d,0x6c,0x24},
    {0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xc9}
};

bool removeUnpublishedHooks()
{
    void** originals[] = {reinterpret_cast<void**>(&originalPick),reinterpret_cast<void**>(&originalQuery),
        reinterpret_cast<void**>(&originalSetTarget),reinterpret_cast<void**>(&originalActivate),
        reinterpret_cast<void**>(&originalRaytrace)};
    bool ok = true;
    for (std::size_t i=0; i<created.size(); ++i) {
        if (!created[i]) continue;
        void* target = i < offsets.size() ? static_cast<void*>(game+offsets[i]) : reinterpret_cast<void*>(raytrace);
        if (MH_RemoveHook(target) == MH_OK) {
            created[i] = false;
            *originals[i] = nullptr;
        } else {
            ok = false;
        }
    }
    cleanupFailure = !ok;
    return ok;
}
} // namespace

bool install(unsigned char* module, HMODULE engine)
{
    if (!module || !engine || (game && game != module) || (boundEngine && boundEngine != engine)) return false;
    if (std::any_of(enabled.begin(),enabled.end(),[](bool value) { return value; })) return false;
    for (std::size_t i=0; i<offsets.size(); ++i)
        if (std::memcmp(module+offsets[i],prologues[i],16) != 0) return false;
    getPlayer = reinterpret_cast<GetPlayer>(module+0x3C2D90);
    getWorldPosition = reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    getLevel = reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    getParent = reinterpret_cast<GetParent>(GetProcAddress(engine,"?GetParent@IControlObject@@QEBAPEAV1@XZ"));
    getObjectId = reinterpret_cast<GetObjectId>(GetProcAddress(engine,"?GetID@IGSObject@@QEBAHXZ"));
    getObjectById = reinterpret_cast<GetObjectById>(GetProcAddress(engine,"?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
    getWorldBounds = reinterpret_cast<GetArray>(GetProcAddress(engine,"?GetExtentsInWorld@IControlObject@@QEBAAEBVextents@@XZ"));
    getWorldTransform = reinterpret_cast<GetArray>(GetProcAddress(engine,"?GetWorldXform@IControlObject@@QEBAAEBVmtx34@@XZ"));
    isActive = reinterpret_cast<IsActive>(GetProcAddress(engine,"?IsActiveOnModule@IGSObject@@QEBA_NXZ"));
    nearShape = reinterpret_cast<NearShape>(module+0xE9D760);
    isUsable = reinterpret_cast<IsUsable>(module+0xE9D960);
    getAction = reinterpret_cast<GetAction>(module+0x2450);
    raytrace = reinterpret_cast<Raytrace>(GetProcAddress(engine,"?Raytrace@IGSObject@@QEAAEEPEAUSCollision@@AEBVvec3@@AEAV3@GPEAVIControlObject@@IIH@Z"));
    HMODULE runtime = GetModuleHandleW(L"MSVCR100.dll");
    nativeFree = runtime ? reinterpret_cast<Free>(GetProcAddress(runtime,"free")) : nullptr;
    if (!getWorldPosition || !getLevel || !getParent || !getObjectId || !getObjectById ||
        !getWorldBounds || !getWorldTransform || !isActive || !raytrace || !nativeFree) return false;
    pendingDiagnostics.store(0, std::memory_order_relaxed);
    emittedDiagnostics.store(0, std::memory_order_relaxed);
    game = module;
    boundEngine = engine;
    void* detours[] = {reinterpret_cast<void*>(pickHook),reinterpret_cast<void*>(queryHook),
        reinterpret_cast<void*>(setTargetHook),reinterpret_cast<void*>(activateHook),
        reinterpret_cast<void*>(pickerRayHook)};
    void** originals[] = {reinterpret_cast<void**>(&originalPick),reinterpret_cast<void**>(&originalQuery),
        reinterpret_cast<void**>(&originalSetTarget),reinterpret_cast<void**>(&originalActivate),
        reinterpret_cast<void**>(&originalRaytrace)};
    for (std::size_t i=0; i<created.size(); ++i) {
        if (!created[i]) {
            void* target = i < offsets.size() ? static_cast<void*>(game+offsets[i]) : reinterpret_cast<void*>(raytrace);
            if (MH_CreateHook(target,detours[i],originals[i]) != MH_OK) {
                if (!published) (void)removeUnpublishedHooks();
                return false;
            }
            created[i] = true;
        }
    }
    HMODULE self = nullptr;
    if (!published && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&pickHook), &self)) {
        (void)removeUnpublishedHooks();
        return false;
    }
    published = true;
    for (std::size_t i=0; i<enabled.size(); ++i) {
        void* target = i < offsets.size() ? static_cast<void*>(game+offsets[i]) : reinterpret_cast<void*>(raytrace);
        if (MH_EnableHook(target) != MH_OK) { (void)disable(); return false; }
        enabled[i] = true;
    }
    return true;
}

void setFrame(const Frame& frame)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (frame.player != publishedFrame.player || frame.camera != publishedFrame.camera ||
        frame.eligible != publishedFrame.eligible ||
        frame.position.x != publishedFrame.position.x || frame.position.y != publishedFrame.position.y ||
        frame.position.z != publishedFrame.position.z || frame.forward.x != publishedFrame.forward.x ||
        frame.forward.y != publishedFrame.forward.y || frame.forward.z != publishedFrame.forward.z ||
        frame.verticalFovRadians != publishedFrame.verticalFovRadians || frame.aspect != publishedFrame.aspect)
        ++poseRevision;
    if (!frame.eligible || frame.player != publishedFrame.player || frame.camera != publishedFrame.camera) {
        ++generation;
        // Keep the native weak-handle identity until the stock picker replaces
        // it, so a stale mod-selected object cannot be activated meanwhile.
        selectedQuery.valid = false;
    }
    publishedFrame = frame;
}

void clear()
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    ++generation;
    ++poseRevision;
    publishedFrame = {};
    selectedQuery.valid = false;
}

void drainDiagnostics(void (*log)(const char*, const char*))
{
    const unsigned pending = pendingDiagnostics.exchange(0, std::memory_order_relaxed);
    if (!log || !pending) return;
    const unsigned fresh = pending & ~emittedDiagnostics.fetch_or(pending, std::memory_order_relaxed);
    struct Message { unsigned bit; const char* level; const char* text; };
    static constexpr Message messages[] = {
        {TargetPending,"WARN","Third Person interaction: selection is still being updated."},
        {TargetChanged,"WARN","Third Person interaction: the selected object changed before activation."},
        {Ineligible,"WARN","Third Person interaction: the camera or gameplay state became unavailable."},
        {StaleQuery,"WARN","Third Person interaction: the selection was invalidated before activation."},
        {ChangedLevel,"WARN","Third Person interaction: the selected level changed before activation."},
        {NoLongerReachable,"WARN","Third Person interaction: the selected object failed native reach or usability checks."},
        {NotVisible,"WARN","Third Person interaction: the selected object is no longer visible from the camera."},
        {ChangedDuringValidation,"WARN","Third Person interaction: camera or target changed during validation."},
        {ExpiredSelection,"WARN","Third Person interaction: the nearby-object scan was too old for activation."},
        {PositionChanged,"WARN","Third Person interaction: the target changed shape since the nearby-object scan."},
        {Forwarded,"INFO","Third Person interaction: selected target passed validation and was sent to the game."}
    };
    for (const auto& message : messages)
        if (fresh & message.bit) log(message.level, message.text);
}

bool disable()
{
    clear();
    QuerySnapshot query{};
    void* object = nullptr;
    void* reference = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        // A picker may be between collecting and publishing its candidate.
        // Keep every guard installed until process exit rather than exposing a
        // partially modified in-flight query to unguarded activation.
        if (activePickers || activeActivations || pendingPlayer) return false;
        query = selectedQuery;
        object = selectedObject;
        reference = selectedReference;
    }
    bool canReleaseTarget = true;
    if (object && query.player && game) {
        void* instance = *reinterpret_cast<void**>(game + 0x1D07F88);
        if (instance && getPlayer(instance) == query.player) {
            void* currentReference = nullptr;
            void* current = currentTarget(query.player,&currentReference);
            if (current == object && currentReference == reference) {
                if (query.thread == GetCurrentThreadId()) {
                    originalSetTarget(query.player,nullptr,1);
                } else {
                    // Keep the activation guard reachable on the wrong thread.
                    canReleaseTarget = false;
                }
            }
        }
    }
    bool ok = !cleanupFailure;
    for (std::size_t i=0; i<enabled.size(); ++i) {
        if (!enabled[i]) continue;
        if (i == 3 && !canReleaseTarget) { ok = false; continue; }
        void* target = i < offsets.size() ? static_cast<void*>(game+offsets[i]) : reinterpret_cast<void*>(raytrace);
        if (MH_DisableHook(target) == MH_OK) enabled[i] = false;
        else ok = false;
    }
    return ok;
}

} // namespace thirdperson::interactions
