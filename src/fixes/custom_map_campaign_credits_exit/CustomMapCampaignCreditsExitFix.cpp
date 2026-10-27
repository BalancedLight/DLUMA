#define _HAS_EXCEPTIONS 0

#include "CustomMapCampaignCreditsExitFix.h"

extern "C" {
#include "../../dlw_common.h"

__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
__declspec(dllimport) BOOL WINAPI IsProcessorFeaturePresent(DWORD);
}

#include <array>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange128)
#endif

#ifndef PF_COMPARE_EXCHANGE128
#define PF_COMPARE_EXCHANGE128 14
#endif

/* The assembly stubs only read these C-linkage values.  They never call into
 * C++ from a game fault path. */
extern "C" {
volatile LONG g_dluma_campaign_credits_exit_guard_active = 0;
volatile LONG g_dluma_campaign_credits_exit_guard_thread_id = 0;
volatile LONG g_dluma_campaign_credits_exit_guard_suppressed_count = 0;
volatile LONG g_dluma_campaign_credits_exit_guard_suppressed_1072e90 = 0;
volatile LONG g_dluma_campaign_credits_exit_guard_suppressed_10739b0 = 0;
volatile LONG g_dluma_campaign_credits_exit_guard_suppressed_1074c13 = 0;
volatile LONG g_dluma_campaign_credits_exit_guard_suppressed_1078a40 = 0;
std::uintptr_t g_dluma_campaign_credits_exit_guard_resume_1072e90 = 0;
std::uintptr_t g_dluma_campaign_credits_exit_guard_resume_10739b0 = 0;
std::uintptr_t g_dluma_campaign_credits_exit_guard_resume_1074c13 = 0;
std::uintptr_t g_dluma_campaign_credits_exit_guard_resume_1078a40 = 0;
void dluma_campaign_credits_exit_tail_guard_1072e90_stub(void);
void dluma_campaign_credits_exit_tail_guard_10739b0_stub(void);
void dluma_campaign_credits_exit_tail_guard_1078a40_stub(void);
void dluma_campaign_credits_exit_unaligned_resume_stub(void);
}

namespace dluma::fixes::custom_map_campaign_credits_exit {
namespace {

constexpr std::uint32_t kSupportedGamedllTimestamp = 0x623397AAU;
constexpr std::uint32_t kSupportedGamedllImageSize = 0x02082000U;
constexpr int kDestroyActiveLevelCampaignFinished = 4;
constexpr std::size_t kAtomicPatchSize = 16;
constexpr std::size_t kScopeTextCapacity = 512;
constexpr std::uintptr_t kPrimaryFaultRva = 0x01072E90U;
constexpr std::uintptr_t kUnalignedFaultRva = 0x01074C13U;
constexpr std::uintptr_t kAnimBindImportSlotRva = 0x013CCC20U;
constexpr DWORD kExceptionAccessViolation = 0xC0000005UL;
constexpr LONG kExceptionContinueExecution = -1;
constexpr LONG kExceptionContinueSearch = 0;

/* dluma_win32 deliberately exposes only a void ContextRecord pointer.  This is
 * the fixed x64 CONTEXT prefix through RIP, enough to continue one verified
 * access violation without importing the broad Windows SDK surface. */
struct X64ContextPrefix {
    std::uint64_t p1_home, p2_home, p3_home, p4_home, p5_home, p6_home;
    DWORD context_flags, mx_csr;
    WORD seg_cs, seg_ds, seg_es, seg_fs, seg_gs, seg_ss;
    DWORD eflags;
    std::uint64_t dr0, dr1, dr2, dr3, dr6, dr7;
    std::uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    std::uint64_t r8, r9, r10, r11, r12, r13, r14, r15, rip;
};
typedef LONG (CALLBACK *VectoredHandler)(EXCEPTION_POINTERS*);
extern "C" __declspec(dllimport) PVOID WINAPI AddVectoredExceptionHandler(
    ULONG first, VectoredHandler handler);
extern "C" __declspec(dllimport) ULONG WINAPI RemoveVectoredExceptionHandler(
    PVOID handle);

constexpr std::uint8_t kTailBytes[] = {
    0x4C,0x8B,0x00,0x48,0x8B,0xC8,0x0F,0x29,0x44,0x24,0x20,
    0x41,0xFF,0x50,0x38,0xC7,0x44,0x24,0x20,0xFF,0xFF,0xFF,0xFF};
constexpr std::uint8_t kBefore1072e90[] = {
    0x48,0x8D,0x54,0x24,0x20,0x0F,0x10,0x83,0x40,0x01,0x00,0x00};
constexpr std::uint8_t kAfter1072e90[] = {
    0x48,0x8B,0x44,0x24,0x20,0x48,0x89,0x83,0x40,0x01,0x00,0x00,
    0x48,0xC7,0x83,0x48};
constexpr std::uint8_t kBefore10739b0[] = {
    0x48,0x8D,0x54,0x24,0x20,0x0F,0x10,0x83,0x50,0x01,0x00,0x00};
constexpr std::uint8_t kAfter10739b0[] = {
    0x48,0x8B,0x44,0x24,0x20,0x33,0xC9,0xC7,0x44,0x24,0x20,0xFF,
    0xFF,0xFF,0xFF,0x48};
constexpr std::uint8_t kBefore1078a40[] = {
    0x48,0x8D,0x54,0x24,0x20,0x0F,0x10,0x83,0x08,0x01,0x00,0x00};
constexpr std::uint8_t kAfter1078a40[] = {
    0x48,0x8B,0x44,0x24,0x20,0x48,0x89,0x83,0x08,0x01,0x00,0x00,
    0x48,0xC7,0x83,0x10};
/* The unaligned fault is handled without modifying code: these bytes prove
 * the imported GetAnimBind call, setup, exact fault tail, and cleanup. */
constexpr std::uint8_t kUnalignedBefore[] = {
    0xFF,0x15,0x19,0x80,0x35,0x00,0x48,0x8D,0x54,0x24,0x20,
    0x0F,0x10,0x83,0xD8,0x01,0x00,0x00};
constexpr std::uint8_t kUnalignedAfter[] = {
    0x48,0x8B,0x44,0x24,0x20,0x48,0x89,0x83,0xD8,0x01,0x00,0x00,
    0x48,0xC7,0x83,0xE0};

struct AtomicTailSite {
    const char* label;
    std::uintptr_t patch_rva;
    std::uintptr_t resume_rva;
    const std::uint8_t* before;
    const std::uint8_t* after;
    void (*stub)(void);
    bool owned;
};

std::array<AtomicTailSite, 3> g_sites = {{{
    "1072e90", 0x01072E90U, 0x01072EA7U, kBefore1072e90, kAfter1072e90,
    &dluma_campaign_credits_exit_tail_guard_1072e90_stub, false}, {
    "10739b0", 0x010739B0U, 0x010739C7U, kBefore10739b0, kAfter10739b0,
    &dluma_campaign_credits_exit_tail_guard_10739b0_stub, false}, {
    "1078a40", 0x01078A40U, 0x01078A57U, kBefore1078a40, kAfter1078a40,
    &dluma_campaign_credits_exit_tail_guard_1078a40_stub, false}}};

bool g_initialized;
bool g_enabled;
bool g_patch_owned;
std::uintptr_t g_gamedll_base;
PVOID g_unaligned_handler;
volatile LONG g_pending_scope = 0;
char g_pending_scope_map[kScopeTextCapacity] = {};
char g_pending_scope_project[kScopeTextCapacity] = {};
FixStatus g_status = {kFixId, kFixDisplayName, FixState::UnsupportedBuild, 0,
                      kPrimaryFaultRva, false};

FixStatus MakeStatus(FixState state, std::uintptr_t address = 0,
                     bool owned = false) noexcept {
    g_status = {kFixId, kFixDisplayName, state, address, kPrimaryFaultRva, owned};
    return g_status;
}

bool ReadExact(std::uintptr_t address, const std::uint8_t* expected,
               std::size_t size) noexcept {
    return address && expected && size &&
           dluma_is_readable(reinterpret_cast<const void*>(address), size) &&
           std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
}

bool SupportedIdentity(const RuntimeContext& context) noexcept {
    return context.gamedll_module && context.gamedll_base &&
           context.gamedll_timestamp == kSupportedGamedllTimestamp &&
           context.gamedll_size_of_image == kSupportedGamedllImageSize;
}

std::array<std::uint8_t, kAtomicPatchSize> OriginalPatchBytes() noexcept {
    std::array<std::uint8_t, kAtomicPatchSize> result = {};
    std::memcpy(result.data(), kTailBytes, result.size());
    return result;
}

std::array<std::uint8_t, kAtomicPatchSize> BuildAbsoluteTailJump(
    void (*stub)(void)) noexcept {
    std::array<std::uint8_t, kAtomicPatchSize> route = {};
    route[0] = 0xFF; route[1] = 0x25;
    const std::uintptr_t destination = reinterpret_cast<std::uintptr_t>(stub);
    std::memcpy(route.data() + 6, &destination, sizeof(destination));
    route[14] = 0x90; route[15] = 0x90;
    return route;
}

bool AtomicReplace16(std::uintptr_t address, const std::uint8_t* expected,
                     const std::uint8_t* replacement) noexcept {
    if (!address || !expected || !replacement ||
        (address & (kAtomicPatchSize - 1)) != 0) return false;
    alignas(16) LONGLONG comparand[2] = {};
    alignas(16) LONGLONG desired[2] = {};
    std::memcpy(comparand, expected, kAtomicPatchSize);
    std::memcpy(desired, replacement, kAtomicPatchSize);
    DWORD old_protection = 0, ignored_protection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address), kAtomicPatchSize,
                        PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    const bool exchanged = _InterlockedCompareExchange128(
        reinterpret_cast<volatile LONGLONG*>(address), desired[1], desired[0],
        comparand) != 0;
    const bool flushed = !exchanged || FlushInstructionCache(
        GetCurrentProcess(), reinterpret_cast<const void*>(address),
        kAtomicPatchSize) != FALSE;
    const bool restored = VirtualProtect(reinterpret_cast<void*>(address),
        kAtomicPatchSize, old_protection, &ignored_protection) != FALSE;
    return exchanged && flushed && restored;
}

void PinLoaderForProcessExit() noexcept {
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&PinLoaderForProcessExit), &pinned))
        dluma_atomic_exchange(&g_dluma.retained_for_process_exit, 1);
}

bool ValidateAtomicSite(const RuntimeContext& context,
                        const AtomicTailSite& site) noexcept {
    if (site.patch_rva < 12 ||
        site.patch_rva + sizeof(kTailBytes) + 16 > context.gamedll_size_of_image)
        return false;
    const std::uintptr_t target = context.gamedll_base + site.patch_rva;
    return (target & (kAtomicPatchSize - 1)) == 0 &&
           ReadExact(target - 12, site.before, 12) &&
           ReadExact(target, kTailBytes, sizeof(kTailBytes)) &&
           ReadExact(target + sizeof(kTailBytes), site.after, 16);
}

bool ValidateUnalignedSite(const RuntimeContext& context) noexcept {
    const std::uintptr_t fault = context.gamedll_base + kUnalignedFaultRva;
    const std::uintptr_t import_slot = context.gamedll_base + kAnimBindImportSlotRva;
    if (kUnalignedFaultRva < sizeof(kUnalignedBefore) ||
        kUnalignedFaultRva + sizeof(kTailBytes) + sizeof(kUnalignedAfter) >
            context.gamedll_size_of_image ||
        !ReadExact(fault - sizeof(kUnalignedBefore), kUnalignedBefore,
                   sizeof(kUnalignedBefore)) ||
        !ReadExact(fault, kTailBytes, sizeof(kTailBytes)) ||
        !ReadExact(fault + sizeof(kTailBytes), kUnalignedAfter,
                   sizeof(kUnalignedAfter)) ||
        !dluma_is_readable(reinterpret_cast<void*>(import_slot),
                         sizeof(std::uintptr_t))) return false;
    const std::uintptr_t target = *reinterpret_cast<const std::uintptr_t*>(import_slot);
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!target || !VirtualQuery(reinterpret_cast<const void*>(target), &mbi,
                                 sizeof(mbi))) return false;
    const DWORD protect = mbi.Protect & 0xFFU;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

bool RestoreOwnedRoutes() noexcept {
    const auto original = OriginalPatchBytes();
    bool all_restored = true;
    for (auto it = g_sites.rbegin(); it != g_sites.rend(); ++it) {
        if (!it->owned) continue;
        const auto route = BuildAbsoluteTailJump(it->stub);
        const std::uintptr_t target = g_gamedll_base + it->patch_rva;
        (void)AtomicReplace16(target, route.data(), original.data());
        if (!ReadExact(target, original.data(), original.size())) {
            all_restored = false;
            continue;
        }
        it->owned = false;
    }
    return all_restored;
}

bool CopyScopeText(char* destination, const char* source) noexcept {
    if (!destination || !source || !source[0]) return false;
    std::size_t index = 0;
    for (; index + 1 < kScopeTextCapacity && source[index]; ++index)
        destination[index] = source[index];
    destination[index] = 0;
    return source[index] == 0;
}

bool EqualScopeText(const char* left, const char* right) noexcept {
    if (!left || !right) return left == right;
    for (std::size_t index = 0; index < kScopeTextCapacity; ++index) {
        if (left[index] != right[index]) return false;
        if (!left[index]) return true;
    }
    return false;
}

void ClearPendingScope() noexcept {
    dluma_atomic_exchange(&g_pending_scope, -1);
    g_pending_scope_map[0] = 0;
    g_pending_scope_project[0] = 0;
    dluma_atomic_exchange(&g_pending_scope, 0);
}

LONG CALLBACK GuardUnalignedFault(EXCEPTION_POINTERS* exception) noexcept {
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord ||
        exception->ExceptionRecord->ExceptionCode != kExceptionAccessViolation ||
        reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) !=
            g_gamedll_base + kUnalignedFaultRva ||
        dluma_atomic_compare_exchange(&g_dluma_campaign_credits_exit_guard_active, 0, 0) != 1 ||
        dluma_atomic_compare_exchange(&g_dluma_campaign_credits_exit_guard_thread_id, 0, 0) !=
            static_cast<LONG>(GetCurrentThreadId()))
        return kExceptionContinueSearch;
    X64ContextPrefix* const context =
        reinterpret_cast<X64ContextPrefix*>(exception->ContextRecord);
    if (context->rax != 0)
        return kExceptionContinueSearch;
    dluma_atomic_increment(&g_dluma_campaign_credits_exit_guard_suppressed_count);
    dluma_atomic_increment(&g_dluma_campaign_credits_exit_guard_suppressed_1074c13);
    context->rip = reinterpret_cast<std::uintptr_t>(
        &dluma_campaign_credits_exit_unaligned_resume_stub);
    return kExceptionContinueExecution;
}

void LogSuppressionCounts() noexcept {
    dluma_log_u64("FIX", "custom_campaign_credits_exit_guard_suppressed_count",
        static_cast<ULONGLONG>(dluma_atomic_compare_exchange(
            &g_dluma_campaign_credits_exit_guard_suppressed_count, 0, 0)));
    dluma_log_u64("FIX", "custom_campaign_credits_exit_guard_suppressed_1072e90",
        static_cast<ULONGLONG>(dluma_atomic_compare_exchange(
            &g_dluma_campaign_credits_exit_guard_suppressed_1072e90, 0, 0)));
    dluma_log_u64("FIX", "custom_campaign_credits_exit_guard_suppressed_10739b0",
        static_cast<ULONGLONG>(dluma_atomic_compare_exchange(
            &g_dluma_campaign_credits_exit_guard_suppressed_10739b0, 0, 0)));
    dluma_log_u64("FIX", "custom_campaign_credits_exit_guard_suppressed_1074c13",
        static_cast<ULONGLONG>(dluma_atomic_compare_exchange(
            &g_dluma_campaign_credits_exit_guard_suppressed_1074c13, 0, 0)));
    dluma_log_u64("FIX", "custom_campaign_credits_exit_guard_suppressed_1078a40",
        static_cast<ULONGLONG>(dluma_atomic_compare_exchange(
            &g_dluma_campaign_credits_exit_guard_suppressed_1078a40, 0, 0)));
}

}  // namespace

bool BeginVerifiedCustomCampaignFinishedTeardown(int destroy_mode,
    bool project_complete, const char* map, const char* project) noexcept {
    if (!g_initialized || !g_enabled || destroy_mode != kDestroyActiveLevelCampaignFinished ||
        !project_complete || !map || !map[0] || !project || !project[0]) return false;
    if (project[0] == '<' || dluma_atomic_compare_exchange(
            &g_dluma_campaign_credits_exit_guard_active, 0, 0) != 0) {
        dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_not_armed", "active-session-scope-present");
        return false;
    }
    LONG pending = dluma_atomic_compare_exchange(&g_pending_scope, -1, 0);
    bool superseded = false;
    if (pending == 1) { pending = dluma_atomic_compare_exchange(&g_pending_scope, -1, 1); superseded = pending == 1; }
    if (pending != 0 && !superseded) {
        dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_not_armed", "pending-scope-being-consumed");
        return false;
    }
    const DWORD thread_id = GetCurrentThreadId();
    if (!CopyScopeText(g_pending_scope_map, map) || !CopyScopeText(g_pending_scope_project, project)) {
        ClearPendingScope();
        dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_not_armed", "incomplete-campaign-scope-label");
        return false;
    }
    dluma_atomic_exchange(&g_pending_scope, 1);
    if (superseded) dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_pending", "superseded-unconsumed-prior-scope");
    dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_pending_map", map);
    dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_pending_project", project);
    dluma_log_u64("FIX", "custom_campaign_credits_exit_guard_thread", static_cast<ULONGLONG>(thread_id));
    return true;
}

bool EnterVerifiedCustomCampaignFinishedSessionDestroy(bool project_complete,
    const char* map, const char* project) noexcept {
    if (!g_initialized || !g_enabled ||
        dluma_atomic_compare_exchange(&g_pending_scope, -1, 1) != 1) return false;
    const DWORD thread_id = GetCurrentThreadId();
    if (!project_complete || !map || !project || !map[0] || !project[0] ||
        !EqualScopeText(map, g_pending_scope_map) || !EqualScopeText(project, g_pending_scope_project)) {
        ClearPendingScope();
        dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_rejected", "session-destroy-ownership-mismatch");
        return false;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_campaign_credits_exit_guard_active, -1, 0) != 0) {
        ClearPendingScope();
        dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_rejected", "active-scope-already-present");
        return false;
    }
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_thread_id, static_cast<LONG>(thread_id));
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_active, 1);
    ClearPendingScope();
    dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_armed_map", map);
    dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_armed_project", project);
    return true;
}

void EndVerifiedCustomCampaignFinishedSessionDestroy(bool armed) noexcept {
    if (!armed) return;
    const DWORD thread_id = GetCurrentThreadId();
    const LONG owner = dluma_atomic_compare_exchange(&g_dluma_campaign_credits_exit_guard_thread_id, 0, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_active, -1);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_thread_id, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_active, 0);
    if (owner != static_cast<LONG>(thread_id)) {
        dluma_log_pair("WARN", "custom_campaign_credits_exit_guard_disarm", "owner-thread-mismatch");
        return;
    }
    LogSuppressionCounts();
}

FixStatus Initialize(const RuntimeContext& context, bool enabled,
                     bool ownership_tracking_enabled) {
    g_enabled = false; g_initialized = false; g_patch_owned = false;
    g_gamedll_base = 0; g_unaligned_handler = nullptr;
    for (auto& site : g_sites) site.owned = false;
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_active, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_thread_id, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_suppressed_count, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_suppressed_1072e90, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_suppressed_10739b0, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_suppressed_1074c13, 0);
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_suppressed_1078a40, 0);
    ClearPendingScope();
    g_dluma_campaign_credits_exit_guard_resume_1072e90 = 0;
    g_dluma_campaign_credits_exit_guard_resume_10739b0 = 0;
    g_dluma_campaign_credits_exit_guard_resume_1074c13 = 0;
    g_dluma_campaign_credits_exit_guard_resume_1078a40 = 0;
    if (!enabled) return MakeStatus(FixState::Disabled);
    if (!ownership_tracking_enabled) {
        dluma_log_pair("FIX", "custom_campaign_credits_exit_guard", "disabled; CustomMapAssetIsolation ownership evidence is off");
        return MakeStatus(FixState::Disabled);
    }
    if (!SupportedIdentity(context) || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128))
        return MakeStatus(FixState::UnsupportedBuild);
    for (const auto& site : g_sites) {
        if (!ValidateAtomicSite(context, site)) {
            dluma_log_pair("WARN", "custom_campaign_credits_exit_guard", "atomic cleanup-tail bytes or context did not match");
            return MakeStatus(FixState::OriginalBytesMismatch, context.gamedll_base + site.patch_rva);
        }
    }
    if (!ValidateUnalignedSite(context)) {
        dluma_log_pair("WARN", "custom_campaign_credits_exit_guard", "unaligned cleanup-tail bytes, context, or import slot did not match");
        return MakeStatus(FixState::OriginalBytesMismatch, context.gamedll_base + kUnalignedFaultRva);
    }
    g_gamedll_base = context.gamedll_base;
    g_dluma_campaign_credits_exit_guard_resume_1072e90 = context.gamedll_base + 0x01072EA7U;
    g_dluma_campaign_credits_exit_guard_resume_10739b0 = context.gamedll_base + 0x010739C7U;
    g_dluma_campaign_credits_exit_guard_resume_1074c13 = context.gamedll_base + 0x01074C2AU;
    g_dluma_campaign_credits_exit_guard_resume_1078a40 = context.gamedll_base + 0x01078A57U;
    const auto original = OriginalPatchBytes();
    for (auto& site : g_sites) {
        const std::uintptr_t target = g_gamedll_base + site.patch_rva;
        const auto route = BuildAbsoluteTailJump(site.stub);
        if (ReadExact(target, route.data(), route.size()) ||
            !ReadExact(target, original.data(), original.size()) ||
            !AtomicReplace16(target, original.data(), route.data()) ||
            !ReadExact(target, route.data(), route.size())) {
            if (ReadExact(target, route.data(), route.size())) site.owned = true;
            if (!RestoreOwnedRoutes()) PinLoaderForProcessExit();
            g_gamedll_base = 0;
            return MakeStatus(FixState::ApplyFailed, target, false);
        }
        site.owned = true;
    }
    g_unaligned_handler = AddVectoredExceptionHandler(1, &GuardUnalignedFault);
    if (!g_unaligned_handler) {
        if (!RestoreOwnedRoutes()) PinLoaderForProcessExit();
        g_gamedll_base = 0;
        return MakeStatus(FixState::ApplyFailed, context.gamedll_base + kUnalignedFaultRva, false);
    }
    g_patch_owned = true; g_enabled = true; g_initialized = true;
    dluma_log_pair("FIX", "custom_campaign_credits_exit_guard", "validated DevTools animation-cleanup family installed");
    return MakeStatus(FixState::Applied, context.gamedll_base + kPrimaryFaultRva, true);
}

FixStatus Shutdown() {
    g_enabled = false; g_initialized = false;
    ClearPendingScope();
    if (!g_patch_owned) return g_status;
    if (dluma_atomic_compare_exchange(&g_dluma_campaign_credits_exit_guard_active, -1, 0) != 0) {
        PinLoaderForProcessExit();
        dluma_log_pair("WARN", "custom_campaign_credits_exit_guard_restore", "active scope retained for process lifetime");
        return MakeStatus(FixState::ApplyFailed, g_gamedll_base + kPrimaryFaultRva, true);
    }
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_thread_id, 0);
    if (g_unaligned_handler && !RemoveVectoredExceptionHandler(g_unaligned_handler)) {
        PinLoaderForProcessExit();
        return MakeStatus(FixState::ApplyFailed, g_gamedll_base + kUnalignedFaultRva, true);
    }
    g_unaligned_handler = nullptr;
    if (!RestoreOwnedRoutes()) {
        PinLoaderForProcessExit();
        dluma_log_pair("WARN", "custom_campaign_credits_exit_guard_restore", "cleanup-family bytes changed or could not be restored; loader retained");
        return MakeStatus(FixState::ApplyFailed, g_gamedll_base + kPrimaryFaultRva, true);
    }
    g_patch_owned = false; g_gamedll_base = 0;
    dluma_atomic_exchange(&g_dluma_campaign_credits_exit_guard_active, 0);
    g_dluma_campaign_credits_exit_guard_resume_1072e90 = 0;
    g_dluma_campaign_credits_exit_guard_resume_10739b0 = 0;
    g_dluma_campaign_credits_exit_guard_resume_1074c13 = 0;
    g_dluma_campaign_credits_exit_guard_resume_1078a40 = 0;
    dluma_log_pair("FIX", "custom_campaign_credits_exit_guard_restore", "restored loader-owned cleanup-family routes");
    return MakeStatus(FixState::Restored, 0, false);
}

FixStatus CurrentStatus() { return g_status; }

}  // namespace dluma::fixes::custom_map_campaign_credits_exit
