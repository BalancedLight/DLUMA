#ifndef DLUMA_FIXES_CUSTOM_CONTENT_RESET_REGISTRY_H
#define DLUMA_FIXES_CUSTOM_CONTENT_RESET_REGISTRY_H

#include "CustomContentLedger.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::content {

constexpr std::size_t kMaximumResetAdapters = 16;

enum class ContentResetResult : std::uint8_t {
    NotApplicable,
    Restored,
    Partial,
    Unsupported,
    Failed,
};

enum class ContentResetSemantics : std::uint8_t {
    Replace,
    Merge,
    OneShot,
    Snapshot,
};

enum class ContentResetPhase : std::uint8_t {
    BaselineCapture,
    Prepare,
    Reset,
    Verify,
};

struct ContentResetContext {
    std::uint32_t epoch;
    const CustomFileLoadRecord* records;
    std::uint32_t record_count;
};

using ContentResetSupportsFn = bool (*)(
    void* user, const CustomFileLoadRecord* record) noexcept;
using ContentResetPhaseFn = ContentResetResult (*)(
    void* user, const ContentResetContext* context) noexcept;

struct CustomContentResetAdapter {
    std::uint32_t id;
    char name[kAdapterNameCapacity];
    ContentResetSemantics semantics;

    bool enabled;
    bool destructive_owner_validated;
    bool authoritative_clear_available;
    bool authoritative_reinitialize_available;
    bool logical_snapshot_available;

    void* user;
    ContentResetSupportsFn supports;
    ContentResetPhaseFn capture_baseline;
    ContentResetPhaseFn prepare_for_reset;
    ContentResetPhaseFn reset_to_baseline;
    ContentResetPhaseFn verify_baseline;
};

struct ContentResetAdapterState {
    std::uint32_t id;
    char name[kAdapterNameCapacity];
    ContentResetSemantics semantics;
    ContentResetResult capture_result;
    ContentResetResult prepare_result;
    ContentResetResult reset_result;
    ContentResetResult verify_result;
    std::uint32_t matched_records;
    bool enabled;
    bool definition_valid;
    bool baseline_captured;
};

struct CustomContentResetRegistry {
    volatile LONG lock;
    CustomContentResetAdapter adapters[kMaximumResetAdapters];
    ContentResetAdapterState states[kMaximumResetAdapters];
    std::uint32_t count;
    bool sealed;
};

void Initialize(CustomContentResetRegistry* registry) noexcept;

bool ValidateAdapterDefinition(
    const CustomContentResetAdapter* adapter) noexcept;
bool RegisterAdapter(CustomContentResetRegistry* registry,
                     const CustomContentResetAdapter* adapter) noexcept;
void SealRegistry(CustomContentResetRegistry* registry) noexcept;

ContentResetResult CaptureBaselines(
    CustomContentResetRegistry* registry) noexcept;
ContentResetResult PrepareForReset(
    CustomContentResetRegistry* registry,
    const ContentResetContext* context) noexcept;
ContentResetResult ResetToBaseline(
    CustomContentResetRegistry* registry,
    const ContentResetContext* context) noexcept;
ContentResetResult VerifyBaseline(
    CustomContentResetRegistry* registry,
    const ContentResetContext* context) noexcept;

bool FindSupportingAdapter(
    CustomContentResetRegistry* registry,
    const CustomFileLoadRecord* record,
    std::uint32_t* adapter_id) noexcept;
std::size_t SnapshotAdapterStates(
    CustomContentResetRegistry* registry,
    ContentResetAdapterState* output,
    std::size_t capacity) noexcept;

const char* ResetResultName(ContentResetResult result) noexcept;
const char* ResetSemanticsName(ContentResetSemantics semantics) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::content

#endif
