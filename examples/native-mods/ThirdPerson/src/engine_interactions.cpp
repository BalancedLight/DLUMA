#include "engine_interactions.h"
#include "interaction_selection.h"

#include <MinHook.h>
#include <intrin.h>

#include <algorithm>
#include <array>
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
    bool valid = false;
};
struct PickerScope {
    PickerScope* previous = nullptr;
    Frame frame{};
    NativeVector* candidates = nullptr;
    QuerySnapshot query{};
    std::uint64_t poseRevision = 0;
};

std::recursive_mutex stateMutex;
Frame publishedFrame{};
std::uint64_t generation = 1;
std::uint64_t poseRevision = 1;
DWORD frameThread = 0;
QuerySnapshot selectedQuery{};
void* selectedObject = nullptr;
void* selectedReference = nullptr;
std::int32_t selectedObjectId = -1;
void* pendingPlayer = nullptr;
unsigned activePickers = 0;
unsigned char* game = nullptr;
GetPlayer getPlayer = nullptr;
GetVector getWorldPosition = nullptr;
GetLevel getLevel = nullptr;
GetParent getParent = nullptr;
GetObjectId getObjectId = nullptr;
GetObjectById getObjectById = nullptr;
Raytrace raytrace = nullptr;
Free nativeFree = nullptr;
Pick originalPick = nullptr;
Query originalQuery = nullptr;
SetTarget originalSetTarget = nullptr;
Activate originalActivate = nullptr;
thread_local PickerScope* pickerScope = nullptr;
std::array<bool, 4> created{};
std::array<bool, 4> enabled{};
bool published = false;
bool cleanupFailure = false;

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

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
bool finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
Vec3 unit(Vec3 v)
{
    const float length = std::sqrt(dot(v,v));
    return std::isfinite(length) && length > 0.00001f ? Vec3{v.x/length,v.y/length,v.z/length} : Vec3{};
}

Frame snapshot(std::uint64_t* epoch = nullptr, std::uint64_t* revision = nullptr, DWORD* thread = nullptr)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (epoch) *epoch = generation;
    if (revision) *revision = poseRevision;
    if (thread) *thread = frameThread;
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
    return false;
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
    std::uint64_t revision = 0;
    DWORD publisherThread = 0;
    Frame frame = snapshot(&epoch,&revision,&publisherThread);
    if (!output || publisherThread != GetCurrentThreadId() || !eligible(frame,player)) {
        originalPick(player,output);
        return;
    }
    PickerScope scope{};
    scope.frame = frame;
    scope.candidates = output;
    scope.poseRevision = revision;
    scope.query.player = player;
    scope.query.generation = epoch;
    scope.query.thread = GetCurrentThreadId();
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (generation != epoch || poseRevision != revision || frameThread != GetCurrentThreadId() ||
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
        currentScope = scope->query.generation == generation && scope->poseRevision == poseRevision &&
            frameThread == GetCurrentThreadId() && publishedFrame.eligible &&
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
    scope->query.valid = true;
    return found;
}

void __fastcall setTargetHook(void* player, void* nativeTarget, unsigned char mode)
{
    const void* caller = _ReturnAddress();
    PickerScope* scope = pickerScope;
    if (scope && game && caller == game + 0xB0DA0F && player == scope->query.player && scope->query.valid) {
        std::uint64_t epoch = 0;
        std::uint64_t revision = 0;
        DWORD publisherThread = 0;
        const Frame latest = snapshot(&epoch,&revision,&publisherThread);
        if (epoch != scope->query.generation || revision != scope->poseRevision ||
            publisherThread != GetCurrentThreadId() || latest.camera != scope->frame.camera || !eligible(latest,player))
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
    std::size_t used = 0;
    std::uint64_t currentId = 0;
    for (std::uint32_t i=0; usableList && i<scope->candidates->size; ++i) {
        void* object = scope->candidates->data[i];
        if (!object) continue;
        void* scriptObject = static_cast<unsigned char*>(object)+40;
        const std::int32_t objectId = getObjectId(scriptObject);
        if (objectId < 0 || getObjectById(scope->query.level,objectId) != scriptObject) continue;
        Vec3 point{}; float angle = 0.0f;
        if (!candidatePoint(object,point) || !cameraVisible(scope->frame,object,point,angle)) continue;
        const Vec3 delta = subtract(point,*rootPoint);
        auto& candidate = candidates[used];
        objects[used++] = object;
        candidate.id = static_cast<std::uint64_t>(static_cast<std::uint32_t>(objectId))+1;
        if (object == oldTarget) currentId = candidate.id;
        candidate.priority = object == nativeTarget ? 1 : 0;
        candidate.distanceSquared = dot(delta,delta);
        candidate.viewAngle = angle;
        candidate.reachable = candidate.unobstructed = candidate.usable = candidate.visible = true;
    }
    const auto chosenId = selectInteraction(candidates.data(),used,currentId);
    void* chosen = nullptr;
    for (std::size_t i=0; i<used; ++i)
        if (candidates[i].id == chosenId) { chosen = objects[i]; break; }
    // Reserve this selection until ownership metadata is published; an action
    // arriving concurrently must never observe a newly selected, untracked target.
    if (!eligible(snapshot(),player)) return;
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (scope->query.generation != generation || scope->poseRevision != poseRevision ||
            frameThread != GetCurrentThreadId() || !publishedFrame.eligible ||
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
        selectedQuery = scope->query;
        if (scope->query.generation != generation || scope->poseRevision != poseRevision ||
            frameThread != GetCurrentThreadId() || !publishedFrame.eligible ||
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
    QuerySnapshot query{};
    void* expectedObject = nullptr;
    void* expectedReference = nullptr;
    std::int32_t expectedId = -1;
    Frame frame{};
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (pendingPlayer == player) return;
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
        DWORD publisherThread = 0;
        frame = snapshot(&epoch,&revision,&publisherThread);
        const bool eligibleNow = eligible(frame,player);
        void* level = eligibleNow ? getLevel(static_cast<unsigned char*>(player)+40) : nullptr;
        if (eligibleNow && query.valid && epoch == query.generation && query.thread == GetCurrentThreadId() &&
            publisherThread == GetCurrentThreadId() &&
            level && level == query.level) {
            NativeVector fresh{};
            QueryContext context = query.context;
            Vec3 sentinel{};
            std::memcpy(&sentinel,game+0x1D08330,sizeof(sentinel));
            if (finite(sentinel)) (void)originalQuery(level,&context,&fresh,query.flags,&sentinel);
            if (expectedId >= 0 && listContains(fresh,current) &&
                getObjectById(level,expectedId) == static_cast<unsigned char*>(current)+40) {
                Vec3 point{}; float angle = 0.0f;
                valid = candidatePoint(current,point) && cameraVisible(frame,current,point,angle);
            }
            if (fresh.data) nativeFree(fresh.data);
        }
        if (valid) {
            Frame latest{};
            {
                std::lock_guard<std::recursive_mutex> lock(stateMutex);
                latest = publishedFrame;
                valid = generation == epoch && poseRevision == revision && frameThread == GetCurrentThreadId() &&
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
                // This locked recheck commits the action; later revocation is
                // treated like an already-started native action.
                valid = generation == epoch && poseRevision == revision && frameThread == GetCurrentThreadId() &&
                    selectedQuery.valid && selectedQuery.generation == epoch &&
                    selectedObject == expectedObject && selectedReference == expectedReference &&
                    selectedObjectId == expectedId && pendingPlayer != player;
            }
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
        reinterpret_cast<void**>(&originalSetTarget),reinterpret_cast<void**>(&originalActivate)};
    bool ok = true;
    for (std::size_t i=0; i<offsets.size(); ++i) {
        if (!created[i]) continue;
        if (MH_RemoveHook(game+offsets[i]) == MH_OK) {
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
    if (!module || !engine || (game && game != module)) return false;
    for (std::size_t i=0; i<offsets.size(); ++i)
        if (enabled[i] || std::memcmp(module+offsets[i],prologues[i],16) != 0) return false;
    getPlayer = reinterpret_cast<GetPlayer>(module+0x3C2D90);
    getWorldPosition = reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    getLevel = reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    getParent = reinterpret_cast<GetParent>(GetProcAddress(engine,"?GetParent@IControlObject@@QEBAPEAV1@XZ"));
    getObjectId = reinterpret_cast<GetObjectId>(GetProcAddress(engine,"?GetID@IGSObject@@QEBAHXZ"));
    getObjectById = reinterpret_cast<GetObjectById>(GetProcAddress(engine,"?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
    raytrace = reinterpret_cast<Raytrace>(GetProcAddress(engine,"?Raytrace@IGSObject@@QEAAEEPEAUSCollision@@AEBVvec3@@AEAV3@GPEAVIControlObject@@IIH@Z"));
    HMODULE runtime = GetModuleHandleW(L"MSVCR100.dll");
    nativeFree = runtime ? reinterpret_cast<Free>(GetProcAddress(runtime,"free")) : nullptr;
    if (!getWorldPosition || !getLevel || !getParent || !getObjectId || !getObjectById || !raytrace || !nativeFree) return false;
    game = module;
    void* detours[] = {reinterpret_cast<void*>(pickHook),reinterpret_cast<void*>(queryHook),
        reinterpret_cast<void*>(setTargetHook),reinterpret_cast<void*>(activateHook)};
    void** originals[] = {reinterpret_cast<void**>(&originalPick),reinterpret_cast<void**>(&originalQuery),
        reinterpret_cast<void**>(&originalSetTarget),reinterpret_cast<void**>(&originalActivate)};
    for (std::size_t i=0; i<offsets.size(); ++i) {
        if (!created[i]) {
            if (MH_CreateHook(game+offsets[i],detours[i],originals[i]) != MH_OK) {
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
    for (std::size_t i=0; i<offsets.size(); ++i) {
        if (MH_EnableHook(game+offsets[i]) != MH_OK) { (void)disable(); return false; }
        enabled[i] = true;
    }
    return true;
}

void setFrame(const Frame& frame)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    const DWORD thread = frame.eligible ? GetCurrentThreadId() : 0;
    if (frame.player != publishedFrame.player || frame.camera != publishedFrame.camera ||
        frame.eligible != publishedFrame.eligible || thread != frameThread ||
        frame.position.x != publishedFrame.position.x || frame.position.y != publishedFrame.position.y ||
        frame.position.z != publishedFrame.position.z || frame.forward.x != publishedFrame.forward.x ||
        frame.forward.y != publishedFrame.forward.y || frame.forward.z != publishedFrame.forward.z ||
        frame.verticalFovRadians != publishedFrame.verticalFovRadians || frame.aspect != publishedFrame.aspect)
        ++poseRevision;
    frameThread = thread;
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
    frameThread = 0;
    publishedFrame = {};
    selectedQuery.valid = false;
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
        if (activePickers || pendingPlayer) return false;
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
    for (std::size_t i=0; i<offsets.size(); ++i) {
        if (!enabled[i]) continue;
        if (i == 3 && !canReleaseTarget) { ok = false; continue; }
        if (MH_DisableHook(game+offsets[i]) == MH_OK) enabled[i] = false;
        else ok = false;
    }
    return ok;
}

} // namespace thirdperson::interactions
