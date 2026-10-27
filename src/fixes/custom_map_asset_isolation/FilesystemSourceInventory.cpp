#define _HAS_EXCEPTIONS 0

#include "FilesystemSourceInventory.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::vfs {
namespace {

constexpr char kNativeFallbackLabel[] = "filesystem:native-fallback";

void Fail(FilesystemSourceInventory* output,
          FilesystemSourceInventoryFailure failure) noexcept
{
    if (!output) return;
    output->failure = failure;
    output->complete = false;
}

bool CopyLiteral(char* output, std::size_t capacity,
                 const char* text) noexcept
{
    if (!output || !capacity || !text) return false;
    std::size_t index = 0;
    while (index + 1U < capacity && text[index]) {
        output[index] = text[index];
        ++index;
    }
    output[index] = 0;
    return text[index] == 0;
}

bool BoundedTextEqual(const char* left, const char* right,
                      std::size_t capacity) noexcept
{
    if (!left || !right || !capacity) return false;
    for (std::size_t index = 0; index < capacity; ++index) {
        if (left[index] != right[index]) return false;
        if (!left[index]) return true;
    }
    return false;
}

bool HasBoundedText(const char* text, std::size_t capacity) noexcept
{
    if (!text || !capacity || !text[0]) return false;
    for (std::size_t index = 1; index < capacity; ++index)
        if (!text[index]) return true;
    return false;
}

std::uint32_t BoundedTextLength(
    const char* text, std::size_t capacity) noexcept
{
    if (!text) return 0;
    std::size_t length = 0;
    while (length < capacity && text[length]) ++length;
    return length < capacity
        ? static_cast<std::uint32_t>(length) : 0U;
}

bool LinkWasSeen(const FilesystemSourceInventory& inventory,
                 std::uintptr_t link) noexcept
{
    for (std::uint32_t index = 0;
         index < inventory.source_count; ++index)
        if (inventory.sources[index].link == link) return true;
    return false;
}

}  // namespace

bool CaptureFilesystemSourceInventory(
    const FilesystemSourceRegistryView* registry,
    const FilesystemSourceInventoryReader* reader,
    FilesystemSourceInventory* output) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    if (!registry || !reader || !reader->read_pointer ||
        !reader->copy_path) {
        Fail(output, FilesystemSourceInventoryFailure::InvalidArguments);
        return false;
    }
    output->reported_count = registry->reported_count;
    output->observed_head_link = registry->head_link;
    output->observed_tail_link = registry->tail_link;
    output->observed_fallback_link = registry->fallback_link;
    output->registry_generation = registry->registry_generation;
    output->registry_generation_observed =
        registry->registry_generation_observed;
    output->fallback_state_known = registry->fallback_state_known;
    output->fallback_installed = registry->fallback_installed;
    if (!registry->layout_validated) {
        Fail(output, FilesystemSourceInventoryFailure::LayoutNotValidated);
        return false;
    }
    if (!registry->synchronization_held) {
        Fail(output,
             FilesystemSourceInventoryFailure::SynchronizationNotHeld);
        return false;
    }
    if (!registry->fallback_state_known) {
        Fail(output,
             FilesystemSourceInventoryFailure::FallbackStateUnknown);
        return false;
    }
    if (registry->fallback_installed !=
            (registry->fallback_link != 0)) {
        Fail(output, FilesystemSourceInventoryFailure::FallbackMismatch);
        return false;
    }
    if (!registry->reported_count) {
        if (registry->head_link || registry->tail_link ||
            registry->fallback_installed) {
            Fail(output, FilesystemSourceInventoryFailure::CountMismatch);
            return false;
        }
        output->failure = FilesystemSourceInventoryFailure::None;
        output->complete = true;
        return true;
    }
    if (!registry->head_link || !registry->tail_link) {
        Fail(output, FilesystemSourceInventoryFailure::RegistryUnavailable);
        return false;
    }
    if (registry->reported_count >
        kFilesystemSourceInventoryCapacity) {
        Fail(output, FilesystemSourceInventoryFailure::CapacityExceeded);
        return false;
    }
    if (registry->fallback_installed &&
        registry->tail_link != registry->fallback_link) {
        Fail(output, FilesystemSourceInventoryFailure::FallbackMismatch);
        return false;
    }

    std::uintptr_t current = registry->head_link;
    std::uintptr_t previous = 0;
    bool fallback_seen = false;
    while (current) {
        if (output->source_count >= registry->reported_count) {
            Fail(output, FilesystemSourceInventoryFailure::CountMismatch);
            return false;
        }
        if (LinkWasSeen(*output, current)) {
            Fail(output,
                 FilesystemSourceInventoryFailure::DuplicateOrCyclicLink);
            return false;
        }
        if (current < kFilesystemSourceLinkOffset) {
            Fail(output, FilesystemSourceInventoryFailure::SourceUnreadable);
            return false;
        }

        std::uintptr_t next = 0;
        std::uintptr_t observed_previous = 0;
        std::uintptr_t source = 0;
        if (!reader->read_pointer(reader->user, current, &next) ||
            !reader->read_pointer(
                reader->user, current + sizeof(std::uintptr_t),
                &observed_previous)) {
            Fail(output, FilesystemSourceInventoryFailure::LinkUnreadable);
            return false;
        }
        if (observed_previous != previous) {
            Fail(output,
                 FilesystemSourceInventoryFailure::PreviousLinkMismatch);
            return false;
        }
        if (!reader->read_pointer(
                reader->user,
                current - kFilesystemSourceLinkOffset, &source) ||
            !source) {
            Fail(output, FilesystemSourceInventoryFailure::SourceUnreadable);
            return false;
        }

        FilesystemSourceInventoryRecord* record =
            &output->sources[output->source_count];
        record->link = current;
        record->source = source;
        record->list_ordinal = output->source_count + 1U;
        record->native_fallback =
            registry->fallback_installed &&
            current == registry->fallback_link;
        if (record->native_fallback) {
            if (fallback_seen || current != registry->tail_link || next) {
                Fail(output,
                     FilesystemSourceInventoryFailure::FallbackMismatch);
                return false;
            }
            fallback_seen = true;
            if (!CopyLiteral(record->path, sizeof(record->path),
                             kNativeFallbackLabel)) {
                Fail(output,
                     FilesystemSourceInventoryFailure::PathUnavailable);
                return false;
            }
        } else if (!reader->copy_path(
                       reader->user, source, record->path,
                       sizeof(record->path)) ||
                   !HasBoundedText(record->path,
                                   sizeof(record->path))) {
            Fail(output, FilesystemSourceInventoryFailure::PathUnavailable);
            return false;
        }
        record->raw_path_length =
            BoundedTextLength(record->path, sizeof(record->path));
        if (!record->raw_path_length) {
            Fail(output, FilesystemSourceInventoryFailure::PathUnavailable);
            return false;
        }
        ++output->source_count;

        if (current == registry->tail_link && next) {
            Fail(output, FilesystemSourceInventoryFailure::TailMismatch);
            return false;
        }
        previous = current;
        current = next;
    }

    if (output->source_count != registry->reported_count) {
        Fail(output, FilesystemSourceInventoryFailure::CountMismatch);
        return false;
    }
    if (previous != registry->tail_link) {
        Fail(output, FilesystemSourceInventoryFailure::TailMismatch);
        return false;
    }
    if (registry->fallback_installed != fallback_seen) {
        Fail(output, FilesystemSourceInventoryFailure::FallbackMismatch);
        return false;
    }
    output->failure = FilesystemSourceInventoryFailure::None;
    output->complete = true;
    return true;
}

bool ExactFilesystemSourceInventoryIdentityMatches(
    const FilesystemSourceInventory* baseline,
    const FilesystemSourceInventory* current) noexcept
{
    if (!baseline || !current || !baseline->complete ||
        !current->complete || !baseline->fallback_state_known ||
        !current->fallback_state_known ||
        baseline->reported_count != current->reported_count ||
        baseline->source_count != current->source_count ||
        baseline->observed_head_link != current->observed_head_link ||
        baseline->observed_tail_link != current->observed_tail_link ||
        baseline->observed_fallback_link !=
            current->observed_fallback_link ||
        baseline->fallback_installed != current->fallback_installed)
        return false;
    for (std::uint32_t index = 0;
         index < baseline->source_count; ++index) {
        const FilesystemSourceInventoryRecord& before =
            baseline->sources[index];
        const FilesystemSourceInventoryRecord& after =
            current->sources[index];
        if (!before.source || before.source != after.source ||
            before.link != after.link ||
            before.list_ordinal != after.list_ordinal ||
            before.raw_path_length != after.raw_path_length ||
            before.native_fallback != after.native_fallback ||
            !BoundedTextEqual(before.path, after.path,
                              sizeof(before.path)))
            return false;
    }
    return true;
}
const char* FilesystemSourceInventoryFailureName(
    FilesystemSourceInventoryFailure failure) noexcept
{
    switch (failure) {
    case FilesystemSourceInventoryFailure::None: return "none";
    case FilesystemSourceInventoryFailure::InvalidArguments:
        return "invalid_arguments";
    case FilesystemSourceInventoryFailure::SynchronizationNotHeld:
        return "synchronization_not_held";
    case FilesystemSourceInventoryFailure::LayoutNotValidated:
        return "layout_not_validated";
    case FilesystemSourceInventoryFailure::RegistryUnavailable:
        return "registry_unavailable";
    case FilesystemSourceInventoryFailure::CapacityExceeded:
        return "capacity_exceeded";
    case FilesystemSourceInventoryFailure::LinkUnreadable:
        return "link_unreadable";
    case FilesystemSourceInventoryFailure::SourceUnreadable:
        return "source_unreadable";
    case FilesystemSourceInventoryFailure::PreviousLinkMismatch:
        return "previous_link_mismatch";
    case FilesystemSourceInventoryFailure::DuplicateOrCyclicLink:
        return "duplicate_or_cyclic_link";
    case FilesystemSourceInventoryFailure::TailMismatch:
        return "tail_mismatch";
    case FilesystemSourceInventoryFailure::FallbackStateUnknown:
        return "fallback_state_unknown";
    case FilesystemSourceInventoryFailure::FallbackMismatch:
        return "fallback_mismatch";
    case FilesystemSourceInventoryFailure::CountMismatch:
        return "count_mismatch";
    case FilesystemSourceInventoryFailure::PathUnavailable:
        return "path_unavailable";
    case FilesystemSourceInventoryFailure::ClassificationUnavailable:
        return "classification_unavailable";
    case FilesystemSourceInventoryFailure::RegistryChangedDuringCapture:
        return "registry_changed_during_capture";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::vfs
