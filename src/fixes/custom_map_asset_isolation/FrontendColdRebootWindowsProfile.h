#ifndef DLUMA_FIXES_FRONTEND_COLD_REBOOT_WINDOWS_PROFILE_H
#define DLUMA_FIXES_FRONTEND_COLD_REBOOT_WINDOWS_PROFILE_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::windows {

constexpr std::uint32_t kSupportedTimestamp = 0x623397AAU;
constexpr std::uint32_t kSupportedSizeOfImage = 0x02082000U;
constexpr char kSupportedSha256[] =
    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";

constexpr std::uintptr_t kGameDiVtableRva = 0x0157D5F8U;
constexpr std::uintptr_t kDestroySessionRva = 0x003C29E0U;
constexpr std::uintptr_t kDestroyActiveLevelStateRva = 0x003D0760U;
constexpr std::uintptr_t kPleaseWaitFinishedRva = 0x003D0B20U;
constexpr std::uintptr_t kDestroyingSessionCallbackRva = 0x003D0B40U;
constexpr std::uintptr_t kAdvanceDestroyStateRva = 0x003D0B80U;
constexpr std::uintptr_t kShowMenuMainRva = 0x011BD2A0U;

constexpr std::uintptr_t kDestroySessionSlot = 0x0898U;
constexpr std::uintptr_t kDestroyActiveLevelStateSlot = 0x0BF0U;
constexpr std::uintptr_t kAdvanceDestroyStateSlot = 0x0BF8U;
constexpr std::uintptr_t kPleaseWaitFinishedSlot = 0x0C08U;
constexpr std::uintptr_t kDestroyingSessionCallbackSlot = 0x0C10U;

constexpr std::uintptr_t kDestroyStateGuardOffset = 0x08F8U;
constexpr std::uintptr_t kDestroyStateOffset = 0x08FCU;
constexpr std::uintptr_t kDestroyReasonOffset = 0x0900U;
constexpr std::uintptr_t kDestroyUiDataOffset = 0x0908U;
constexpr std::uintptr_t kCurrentSessionOffset = 0x0508U;

constexpr std::uintptr_t kStateFiveBodyRva = 0x003D0906U;
constexpr std::uintptr_t kStateFiveHideAllCallRva = 0x003D0910U;
constexpr std::uintptr_t kStateFiveHideAllReturnRva = 0x003D0916U;
constexpr std::uintptr_t kStateFiveHideAllIatRva = 0x013CDE50U;
constexpr std::uintptr_t kStateFiveScheduleDeactivateCallRva = 0x003D0925U;
constexpr std::uintptr_t kStateFiveScheduleDeactivateReturnRva = 0x003D092AU;
constexpr std::uintptr_t kScheduleDeactivateRva = 0x011E1A30U;
constexpr std::uintptr_t kStateFivePredicateCallRva = 0x003D092DU;
constexpr std::uintptr_t kStateFivePredicateReturnRva = 0x003D0932U;
constexpr std::uintptr_t kStateFivePredicateRva = 0x003DDF40U;
constexpr std::uintptr_t kStateFiveWaitCallRva = 0x003D095DU;
constexpr std::uintptr_t kStateFiveWaitReturnRva = 0x003D0963U;
constexpr std::uintptr_t kStateFiveWaitIatRva = 0x013CDE58U;
constexpr std::uintptr_t kStateFiveCallbackCallRva = 0x003D0969U;
constexpr std::uintptr_t kStateFiveCallbackReturnRva = 0x003D0970U;
constexpr std::uintptr_t kDestroySessionCallRva = 0x003D0B55U;
constexpr std::uintptr_t kDestroySessionReturnRva = 0x003D0B5BU;
constexpr std::uintptr_t kDestroyGuardWriteRva = 0x003D0B61U;
constexpr std::uintptr_t kAdvanceCallRva = 0x003D0B6BU;
constexpr std::uintptr_t kAdvanceReturnRva = 0x003D0B71U;
constexpr std::uintptr_t kShowMenuMainCallRva = 0x003D09F6U;
constexpr std::uintptr_t kShowMenuMainReturnRva = 0x003D09FBU;

constexpr std::uintptr_t kDestroySessionOwnerCallRva = 0x003C2A24U;
constexpr std::uintptr_t kDestroySessionOwnerReturnRva = 0x003C2A2AU;
constexpr std::uintptr_t kDestroySessionOwnerSlot = 0x07C8U;
constexpr std::uintptr_t kDestroySessionCurrentSessionClearRva = 0x003C2A73U;

constexpr std::uintptr_t kDestroySessionFalseReturnRva = 0x003C29FAU;
constexpr std::uintptr_t kDestroySessionTrueReturnRva = 0x003C2A86U;
constexpr std::uintptr_t kDestroySessionNextFunctionRva = 0x003C2A90U;
constexpr std::uintptr_t kDestroyActiveLevelStateReturnRva = 0x003D0ACCU;
constexpr std::uintptr_t kDestroyActiveLevelStateJumpTableEndRva = 0x003D0B18U;
constexpr std::uintptr_t kPleaseWaitFinishedReturnRva = 0x003D0B3AU;
constexpr std::uintptr_t kDestroyingSessionCallbackReturnRva = 0x003D0B76U;
constexpr std::uintptr_t kAdvanceDestroyStateEndRva = 0x003D0B9EU;
constexpr std::uintptr_t kAdvanceDestroyStateNextFunctionRva = 0x003D0BA0U;
constexpr std::uintptr_t kShowMenuMainFunctionReturnRva = 0x011BD6C6U;
constexpr std::uintptr_t kShowMenuMainNextFunctionRva = 0x011BD6F0U;

static_assert(kStateFiveHideAllCallRva + 6U ==
              kStateFiveHideAllReturnRva);
static_assert(kStateFiveScheduleDeactivateCallRva + 5U ==
              kStateFiveScheduleDeactivateReturnRva);
static_assert(kStateFivePredicateCallRva + 5U ==
              kStateFivePredicateReturnRva);
static_assert(kStateFiveWaitCallRva + 6U == kStateFiveWaitReturnRva);
static_assert(kStateFiveCallbackCallRva + 7U ==
              kStateFiveCallbackReturnRva);
static_assert(kDestroySessionCallRva + 6U == kDestroySessionReturnRva);
static_assert(kAdvanceCallRva + 6U == kAdvanceReturnRva);
static_assert(kDestroySessionOwnerCallRva + 6U ==
              kDestroySessionOwnerReturnRva);
static_assert(kShowMenuMainCallRva + 5U == kShowMenuMainReturnRva);

enum ProfileValidation : std::uint32_t {
    ProfileIdentity = 1U << 0,
    ProfileFunctionPrefixes = 1U << 1,
    ProfileVtableTargets = 1U << 2,
    ProfileStateFiveContext = 1U << 3,
    ProfileDestroySessionContext = 1U << 4,
    ProfileStateAdvanceContext = 1U << 5,
    ProfileShowMenuMainContext = 1U << 6,
    ProfileDestroySessionOwnerContext = 1U << 7,
    ProfileFunctionExtents = 1U << 8,
    ProfileComplete = (1U << 9) - 1U,
};

using ReadProfileMemoryFn = bool (*)(void* user, std::uintptr_t address,
                                     void* output,
                                     std::size_t size) noexcept;

struct ProfileValidationInput {
    std::uintptr_t gamedll_base;
    std::uint32_t timestamp;
    std::uint32_t size_of_image;
    bool full_sha256_valid;
    ReadProfileMemoryFn read;
    void* user;
};

struct ProfileValidationResult {
    std::uint32_t validated_mask;
    std::uint32_t required_mask;
    bool complete;
};

ProfileValidationResult ValidateWindowsProfile(
    const ProfileValidationInput* input) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::windows

#endif
