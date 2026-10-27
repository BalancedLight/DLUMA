#define _HAS_EXCEPTIONS 0

#include "CancelledPresentationCleanupVerifier.h"

#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cancelled_cleanup {
namespace {

bool AsciiEqualsInsensitive(const char* left, const char* right) noexcept
{
    if (!left || !right) return false;
    for (std::size_t index = 0;
         index < vfs::kFilesystemSourcePathCapacity; ++index) {
        unsigned char left_char =
            static_cast<unsigned char>(left[index]);
        unsigned char right_char =
            static_cast<unsigned char>(right[index]);
        if (left_char >= 'A' && left_char <= 'Z')
            left_char = static_cast<unsigned char>(
                left_char - 'A' + 'a');
        if (right_char >= 'A' && right_char <= 'Z')
            right_char = static_cast<unsigned char>(
                right_char - 'A' + 'a');
        if (left_char != right_char) return false;
        if (!left_char) return true;
    }
    return false;
}

bool InventoryRecordMatches(
    const vfs::FilesystemSourceInventoryRecord& left,
    const vfs::FilesystemSourceInventoryRecord& right) noexcept
{
    if (!left.source || left.source != right.source ||
        left.link != right.link ||
        left.native_fallback != right.native_fallback)
        return false;
    if (left.native_fallback)
        return AsciiEqualsInsensitive(left.path, right.path);
    char normalized_left[content::kSourcePathCapacity] = {};
    char normalized_right[content::kSourcePathCapacity] = {};
    return content::NormalizePhysicalPath(
               left.path, normalized_left,
               sizeof(normalized_left)) &&
        content::NormalizePhysicalPath(
            right.path, normalized_right,
            sizeof(normalized_right)) &&
        content::PathsEqual(normalized_left, normalized_right);
}

bool SourceIsRemoved(
    std::uintptr_t source,
    const content::SourceResetSnapshot* removed,
    std::size_t removed_count) noexcept
{
    for (std::size_t index = 0; index < removed_count; ++index)
        if (reinterpret_cast<std::uintptr_t>(removed[index].source) ==
            source)
            return true;
    return false;
}

bool RemovedSetIsExact(
    const content::SourceResetSnapshot* removed,
    std::size_t removed_count) noexcept
{
    if (removed_count && !removed) return false;
    for (std::size_t index = 0; index < removed_count; ++index) {
        if (!removed[index].source) return false;
        for (std::size_t prior = 0; prior < index; ++prior)
            if (removed[prior].source == removed[index].source)
                return false;
    }
    return true;
}

}  // namespace

bool VerifyFilesystemInventoryExactMinusSources(
    const vfs::FilesystemSourceInventory* before,
    const vfs::FilesystemSourceInventory* after,
    const content::SourceResetSnapshot* removed,
    std::size_t removed_count) noexcept
{
    if (!before || !after ||
        !RemovedSetIsExact(removed, removed_count) ||
        !before->complete || !after->complete ||
        before->fallback_state_known != after->fallback_state_known ||
        before->fallback_installed != after->fallback_installed ||
        before->source_count < removed_count ||
        after->source_count != before->source_count - removed_count)
        return false;
    for (std::uint32_t before_index = 0;
         before_index < before->source_count; ++before_index) {
        const vfs::FilesystemSourceInventoryRecord& expected =
            before->sources[before_index];
        const bool should_be_removed = SourceIsRemoved(
            expected.source, removed, removed_count);
        std::uint32_t matches = 0;
        for (std::uint32_t after_index = 0;
             after_index < after->source_count; ++after_index)
            if (InventoryRecordMatches(
                    expected, after->sources[after_index]))
                ++matches;
        if ((should_be_removed && matches != 0U) ||
            (!should_be_removed && matches != 1U))
            return false;
    }
    for (std::uint32_t after_index = 0;
         after_index < after->source_count; ++after_index) {
        std::uint32_t matches = 0;
        for (std::uint32_t before_index = 0;
             before_index < before->source_count; ++before_index)
            if (!SourceIsRemoved(
                    before->sources[before_index].source,
                    removed, removed_count) &&
                InventoryRecordMatches(
                    before->sources[before_index],
                    after->sources[after_index]))
                ++matches;
        if (matches != 1U) return false;
    }
    return true;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cancelled_cleanup
