#define _HAS_EXCEPTIONS 0

#include "PlayerGameplayParamsWindowsSnapshot.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct StringHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

struct VectorHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

static_assert(
    offsetof(VectorHeader, data) ==
        kWindowsPlayerDefinitionLevelVectorOffset);
static_assert(
    offsetof(VectorHeader, size) ==
        kWindowsPlayerDefinitionLevelCountOffset);
static_assert(
    offsetof(VectorHeader, capacity) ==
        kWindowsPlayerDefinitionLevelCapacityOffset);
static_assert(
    offsetof(VectorHeader, data) ==
        kWindowsPlayerDefinitionPropertyVectorOffset);
static_assert(
    offsetof(VectorHeader, size) ==
        kWindowsPlayerDefinitionPropertyCountOffset);
static_assert(
    offsetof(VectorHeader, capacity) ==
        kWindowsPlayerDefinitionPropertyCapacityOffset);
static_assert(sizeof(VectorHeader) == 0x10U);

struct SkillHeader {
    std::uintptr_t root;
    std::uint32_t count;
};

struct SkillNode {
    std::uint16_t key;
    unsigned char saved_instance[12];
    std::uintptr_t left;
    std::uintptr_t parent;
    std::uintptr_t right;
};

bool AddAddress(std::uintptr_t base, std::size_t offset,
                std::uintptr_t* output) noexcept
{
    if (!output || !base ||
        base > static_cast<std::uintptr_t>(-1) - offset)
        return false;
    *output = base + offset;
    return true;
}

bool ValidContext(
    const PlayerGameplayParamsWindowsReadContext* context) noexcept
{
    return context && context->gamedll_base && context->read_memory;
}

bool ReadMemory(const PlayerGameplayParamsWindowsReadContext& context,
                std::uintptr_t address, void* output,
                std::size_t size) noexcept
{
    return address && output && size &&
        address <= static_cast<std::uintptr_t>(-1) - (size - 1U) &&
        context.read_memory(context.read_memory_user, address, output, size);
}

bool ValidSpan(std::uintptr_t address, std::size_t size) noexcept
{
    return address && size &&
        address <= static_cast<std::uintptr_t>(-1) - (size - 1U);
}

bool ReadAt(const PlayerGameplayParamsWindowsReadContext& context,
            std::uintptr_t base, std::size_t offset, void* output,
            std::size_t size) noexcept
{
    std::uintptr_t address = 0;
    return AddAddress(base, offset, &address) &&
        ReadMemory(context, address, output, size);
}

template <typename T>
T BodyValue(const unsigned char* body, std::size_t offset) noexcept
{
    T value = {};
    std::memcpy(&value, body + offset, sizeof(value));
    return value;
}

void AddReason(std::uint64_t* mask,
               PlayerGameplayParamsWindowsSnapshotReason reason) noexcept
{
    if (mask) *mask |= static_cast<std::uint64_t>(reason);
}

void HashBytes(std::uint64_t* hash, const void* data,
               std::size_t size) noexcept
{
    const unsigned char* bytes =
        static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        *hash ^= bytes[index];
        *hash *= kFnvPrime;
    }
}

void HashU16(std::uint64_t* hash, std::uint16_t value) noexcept
{
    unsigned char bytes[2] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU32(std::uint64_t* hash, std::uint32_t value) noexcept
{
    unsigned char bytes[4] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU64(std::uint64_t* hash, std::uint64_t value) noexcept
{
    unsigned char bytes[8] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
        static_cast<unsigned char>(value >> 32),
        static_cast<unsigned char>(value >> 40),
        static_cast<unsigned char>(value >> 48),
        static_cast<unsigned char>(value >> 56),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

std::uint64_t NonzeroHash(std::uint64_t hash) noexcept
{
    return hash ? hash : kFnvOffset;
}

std::uint64_t BeginDomainHash(const char* domain) noexcept
{
    std::uint64_t hash = kFnvOffset;
    if (domain) {
        while (*domain) {
            HashBytes(&hash, domain, 1U);
            ++domain;
        }
    }
    return hash;
}

bool EqualStringHeader(const StringHeader& left,
                       const StringHeader& right) noexcept
{
    return left.data == right.data && left.size == right.size &&
        left.capacity == right.capacity;
}

bool EqualVectorHeader(const VectorHeader& left,
                       const VectorHeader& right) noexcept
{
    return left.data == right.data && left.size == right.size &&
        left.capacity == right.capacity;
}

bool EqualSkillHeader(const SkillHeader& left,
                      const SkillHeader& right) noexcept
{
    return left.root == right.root && left.count == right.count;
}

bool ReadSkillHeader(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t state, SkillHeader* output) noexcept
{
    return output &&
        ReadAt(context, state, kWindowsPlayerStateSkillRootOffset,
               &output->root, sizeof(output->root)) &&
        ReadAt(context, state, kWindowsPlayerStateSkillCountOffset,
               &output->count, sizeof(output->count));
}

bool ReadSkillNode(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t link, SkillNode* output,
    std::uint64_t* reason_mask) noexcept
{
    if (!output || link < 16U) return false;
    unsigned char bytes[40] = {};
    if (!ValidSpan(link - 16U, sizeof(bytes))) {
        AddReason(reason_mask, WindowsSnapshotReasonAddressOverflow);
        return false;
    }
    if (!ReadMemory(context, link - 16U, bytes, sizeof(bytes))) return false;
    std::memcpy(&output->key, bytes, sizeof(output->key));
    std::memcpy(output->saved_instance, bytes + 4U,
                sizeof(output->saved_instance));
    std::memcpy(&output->left, bytes + 16U, sizeof(output->left));
    std::memcpy(&output->parent, bytes + 24U, sizeof(output->parent));
    std::memcpy(&output->right, bytes + 32U, sizeof(output->right));
    return true;
}

bool SeenPointer(const std::uintptr_t* pointers, std::uint32_t count,
                 std::uintptr_t candidate) noexcept
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (pointers[index] == candidate) return true;
    return false;
}

bool CaptureSkillTreeOnce(
    const PlayerGameplayParamsWindowsReadContext& context,
    const SkillHeader& header, std::uint64_t* fingerprint,
    std::uint64_t* reason_mask) noexcept
{
    if (!fingerprint) return false;
    if (header.count > kWindowsSkillNodeCapacity) {
        AddReason(reason_mask, WindowsSnapshotReasonCountOutOfRange);
        return false;
    }
    if ((!header.root) != (header.count == 0U)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderInvalid);
        return false;
    }

    std::uintptr_t stack[kWindowsSkillNodeCapacity] = {};
    std::uintptr_t visited[kWindowsSkillNodeCapacity] = {};
    std::uint32_t depth = 0;
    std::uint32_t visited_count = 0;
    std::uint32_t hashed_count = 0;
    std::uintptr_t current = header.root;
    std::uintptr_t expected_parent = 0;
    std::uint64_t hash = BeginDomainHash("player_skill_tree_v1");
    HashU32(&hash, header.count);

    while (current || depth) {
        while (current) {
            if (SeenPointer(visited, visited_count, current)) {
                AddReason(reason_mask, WindowsSnapshotReasonSkillTreeCycle);
                return false;
            }
            if (visited_count >= header.count) {
                AddReason(reason_mask,
                          WindowsSnapshotReasonSkillTreeCountMismatch);
                return false;
            }
            if (visited_count >= kWindowsSkillNodeCapacity) {
                AddReason(reason_mask, WindowsSnapshotReasonCountOutOfRange);
                return false;
            }
            SkillNode node = {};
            if (!ReadSkillNode(context, current, &node, reason_mask)) {
                AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
                return false;
            }
            if (node.parent != expected_parent ||
                BodyValue<std::uint16_t>(node.saved_instance, 0U) !=
                    node.key) {
                AddReason(reason_mask, WindowsSnapshotReasonSkillNodeInvalid);
                return false;
            }
            visited[visited_count++] = current;
            stack[depth++] = current;
            expected_parent = current;
            current = node.left;
        }

        current = stack[--depth];
        SkillNode node = {};
        if (!ReadSkillNode(context, current, &node, reason_mask)) {
            AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        HashU16(&hash, node.key);
        HashBytes(&hash, node.saved_instance, sizeof(node.saved_instance));
        ++hashed_count;
        expected_parent = current;
        current = node.right;
    }

    if (visited_count != header.count || hashed_count != header.count) {
        AddReason(reason_mask, WindowsSnapshotReasonSkillTreeCountMismatch);
        return false;
    }
    *fingerprint = NonzeroHash(hash);
    return true;
}

bool CaptureSkillTree(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t state, const SkillHeader& body_header,
    PlayerGameplayParamsWindowsFingerprint* output,
    std::uint64_t* reason_mask) noexcept
{
    if (!output) return false;
    SkillHeader before = {};
    SkillHeader middle = {};
    SkillHeader after = {};
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    if (!ReadSkillHeader(context, state, &before)) {
        AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
        return false;
    }
    if (!EqualSkillHeader(before, body_header)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (!CaptureSkillTreeOnce(context, before, &first, reason_mask))
        return false;
    if (!ReadSkillHeader(context, state, &middle) ||
        !EqualSkillHeader(before, middle)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (!CaptureSkillTreeOnce(context, middle, &second, reason_mask))
        return false;
    if (!ReadSkillHeader(context, state, &after) ||
        !EqualSkillHeader(middle, after)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (first != second) {
        AddReason(reason_mask, WindowsSnapshotReasonSnapshotChanged);
        return false;
    }
    output->fingerprint = first;
    output->count = before.count;
    output->auxiliary_count = 0;
    output->valid = true;
    return true;
}

bool CaptureBuffVectorOnce(
    const PlayerGameplayParamsWindowsReadContext& context,
    const VectorHeader& header, bool timed, std::uint64_t* fingerprint,
    std::uint32_t* definition_pointer_count,
    std::uint64_t* reason_mask) noexcept
{
    if (!fingerprint || !definition_pointer_count) return false;
    if (header.size > header.capacity ||
        header.size > kWindowsBuffCapacity ||
        header.capacity > kWindowsBuffCapacity) {
        AddReason(reason_mask, WindowsSnapshotReasonCountOutOfRange);
        return false;
    }
    if (header.capacity && !header.data) {
        AddReason(reason_mask, WindowsSnapshotReasonPointerInvalid);
        return false;
    }
    const std::size_t stride = timed ? 0x18U : 0x04U;
    std::uint64_t hash = BeginDomainHash(
        timed ? "player_timed_buffs_v1" : "player_regular_buffs_v1");
    HashU32(&hash, header.size);
    std::uint32_t pointer_count = 0;
    for (std::uint32_t index = 0; index < header.size; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * stride;
        std::uintptr_t address = 0;
        if (!AddAddress(header.data, offset, &address)) {
            AddReason(reason_mask, WindowsSnapshotReasonAddressOverflow);
            return false;
        }
        unsigned char element[0x18] = {};
        if (!ValidSpan(address, stride)) {
            AddReason(reason_mask, WindowsSnapshotReasonAddressOverflow);
            return false;
        }
        if (!ReadMemory(context, address, element, stride)) {
            AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        const std::uint16_t id = BodyValue<std::uint16_t>(element, 0U);
        const std::uint16_t stack = BodyValue<std::uint16_t>(element, 2U);
        HashU16(&hash, id);
        HashU16(&hash, stack);
        if (timed) {
            const std::uintptr_t definition =
                BodyValue<std::uintptr_t>(element, 8U);
            const std::uint32_t duration_bits =
                BodyValue<std::uint32_t>(element, 0x10U);
            if (definition) ++pointer_count;
            HashU32(&hash, duration_bits);
        }
    }
    *fingerprint = NonzeroHash(hash);
    *definition_pointer_count = pointer_count;
    return true;
}

bool CaptureBuffVector(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t state, std::size_t state_offset,
    const VectorHeader& body_header, bool timed,
    PlayerGameplayParamsWindowsFingerprint* output,
    std::uint64_t* reason_mask) noexcept
{
    if (!output) return false;
    VectorHeader before = {};
    VectorHeader middle = {};
    VectorHeader after = {};
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint32_t first_pointers = 0;
    std::uint32_t second_pointers = 0;
    if (!ReadAt(context, state, state_offset, &before, sizeof(before))) {
        AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
        return false;
    }
    if (!EqualVectorHeader(before, body_header)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (!CaptureBuffVectorOnce(context, before, timed, &first,
                               &first_pointers, reason_mask))
        return false;
    if (!ReadAt(context, state, state_offset, &middle, sizeof(middle)) ||
        !EqualVectorHeader(before, middle)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (!CaptureBuffVectorOnce(context, middle, timed, &second,
                               &second_pointers, reason_mask))
        return false;
    if (!ReadAt(context, state, state_offset, &after, sizeof(after)) ||
        !EqualVectorHeader(middle, after)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (first != second || first_pointers != second_pointers) {
        AddReason(reason_mask, WindowsSnapshotReasonSnapshotChanged);
        return false;
    }
    output->fingerprint = first;
    output->count = before.size;
    output->auxiliary_count = first_pointers;
    output->valid = true;
    return true;
}

bool CaptureSelector(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t state, std::size_t state_offset,
    const StringHeader& body_header, const char* domain,
    PlayerGameplayParamsWindowsSelectorSnapshot* output,
    std::uint64_t* reason_mask) noexcept
{
    if (!output) return false;
    StringHeader before = {};
    StringHeader middle = {};
    StringHeader after = {};
    if (!ReadAt(context, state, state_offset, &before, sizeof(before))) {
        AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
        return false;
    }
    if (!EqualStringHeader(before, body_header) ||
        before.size > before.capacity ||
        before.size > kWindowsSelectorByteCapacity ||
        (before.capacity && !before.data)) {
        AddReason(reason_mask, WindowsSnapshotReasonSelectorInvalid);
        return false;
    }

    unsigned char bytes[kWindowsSelectorByteCapacity + 1U] = {};
    std::uint64_t first = BeginDomainHash(domain);
    std::uint64_t second = BeginDomainHash(domain);
    HashU32(&first, before.size);
    HashU32(&second, before.size);
    if (before.size) {
        const std::size_t read_size =
            static_cast<std::size_t>(before.size) + 1U;
        if (!ValidSpan(before.data, read_size)) {
            AddReason(reason_mask, WindowsSnapshotReasonAddressOverflow);
            return false;
        }
        if (!ReadMemory(context, before.data, bytes, read_size)) {
            AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        if (bytes[before.size] != 0) {
            AddReason(reason_mask, WindowsSnapshotReasonSelectorInvalid);
            return false;
        }
        for (std::uint32_t index = 0; index < before.size; ++index) {
            if (!bytes[index]) {
                AddReason(reason_mask, WindowsSnapshotReasonSelectorInvalid);
                return false;
            }
        }
        HashBytes(&first, bytes, before.size);
    }
    if (!ReadAt(context, state, state_offset, &middle, sizeof(middle)) ||
        !EqualStringHeader(before, middle)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (middle.size) {
        const std::size_t read_size =
            static_cast<std::size_t>(middle.size) + 1U;
        std::memset(bytes, 0, sizeof(bytes));
        if (!ValidSpan(middle.data, read_size)) {
            AddReason(reason_mask, WindowsSnapshotReasonAddressOverflow);
            return false;
        }
        if (!ReadMemory(context, middle.data, bytes, read_size)) {
            AddReason(reason_mask, WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        if (bytes[middle.size] != 0) {
            AddReason(reason_mask, WindowsSnapshotReasonSelectorInvalid);
            return false;
        }
        for (std::uint32_t index = 0; index < middle.size; ++index) {
            if (!bytes[index]) {
                AddReason(reason_mask, WindowsSnapshotReasonSelectorInvalid);
                return false;
            }
        }
        HashBytes(&second, bytes, middle.size);
    }
    if (!ReadAt(context, state, state_offset, &after, sizeof(after)) ||
        !EqualStringHeader(middle, after)) {
        AddReason(reason_mask, WindowsSnapshotReasonHeaderChanged);
        return false;
    }
    if (first != second) {
        AddReason(reason_mask, WindowsSnapshotReasonSnapshotChanged);
        return false;
    }
    output->fingerprint = NonzeroHash(first);
    output->length = before.size;
    output->valid = true;
    return true;
}

bool ValidVectorHeader(const VectorHeader& header,
                       std::uint32_t capacity) noexcept
{
    return header.size <= header.capacity && header.size <= capacity &&
        header.capacity <= capacity && (!header.capacity || header.data);
}

PlayerDefinitionVectorHeaderSnapshot DefinitionHeader(
    const VectorHeader& header) noexcept
{
    PlayerDefinitionVectorHeaderSnapshot result = {};
    result.data = header.data;
    result.size = header.size;
    result.capacity = header.capacity;
    return result;
}

bool CheckedSpanSize(std::uint32_t count, std::size_t stride,
                     std::size_t* output) noexcept
{
    if (!output || !stride ||
        (count && stride > static_cast<std::size_t>(-1) / count))
        return false;
    *output = static_cast<std::size_t>(count) * stride;
    return true;
}

bool ProbeReadableSpan(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t address, std::size_t size) noexcept
{
    if (!size) return true;
    if (!ValidSpan(address, size)) return false;
    unsigned char scratch[256] = {};
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t remaining = size - offset;
        const std::size_t chunk =
            remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        std::uintptr_t cursor = 0;
        if (!AddAddress(address, offset, &cursor) ||
            !ReadMemory(context, cursor, scratch, chunk))
            return false;
        offset += chunk;
    }
    return true;
}

bool ProbeReadableElements(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t address, std::uint32_t count, std::size_t stride,
    std::uint32_t* failed_index) noexcept
{
    if (failed_index) *failed_index = UINT32_MAX;
    std::size_t span_size = 0;
    if (!CheckedSpanSize(count, stride, &span_size) ||
        (span_size && !ValidSpan(address, span_size)))
        return false;
    unsigned char scratch[kWindowsPlayerDefinitionLevelStride] = {};
    if (stride > sizeof(scratch)) return false;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uintptr_t cursor = 0;
        if (!AddAddress(
                address, static_cast<std::size_t>(index) * stride,
                &cursor) ||
            !ReadMemory(context, cursor, scratch, stride)) {
            if (failed_index) *failed_index = index;
            return false;
        }
    }
    return true;
}

void InitializeDefinitionTraversal(
    std::uintptr_t preset, std::uint32_t row_index,
    PlayerDefinitionSemanticSnapshot* output) noexcept
{
    if (!output) return;
    std::memset(output, 0, sizeof(*output));
    output->evidence.row_index = row_index;
    output->evidence.preset = preset;
    output->evidence.configured_level_stride =
        kWindowsPlayerDefinitionLevelStride;
    output->evidence.configured_property_stride =
        kWindowsPlayerDefinitionPropertyStride;
    output->evidence.failed_level_index = UINT32_MAX;
    output->evidence.failed_property_index = UINT32_MAX;
}

bool FailDefinitionTraversal(
    PlayerDefinitionSemanticSnapshot* output,
    PlayerDefinitionTraversalFailure failure) noexcept
{
    if (!output) return false;
    if (output->evidence.first_failure ==
        PlayerDefinitionTraversalFailure::None)
        output->evidence.first_failure = failure;
    output->complete = false;
    return false;
}

bool CaptureWindowsPlayerDefinitionSemanticsOnce(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t preset, std::uint32_t row_index,
    PlayerDefinitionSemanticSnapshot* output) noexcept
{
    InitializeDefinitionTraversal(preset, row_index, output);
    if (!preset || !output)
        return FailDefinitionTraversal(
            output, PlayerDefinitionTraversalFailure::InvalidArgument);

    VectorHeader levels_before = {};
    VectorHeader levels_after = {};
    if (!ReadMemory(context, preset, &levels_before,
                    sizeof(levels_before)))
        return FailDefinitionTraversal(
            output,
            PlayerDefinitionTraversalFailure::PresetHeaderUnreadable);
    output->evidence.level_header_first =
        DefinitionHeader(levels_before);
    output->evidence.level_data = levels_before.data;
    output->evidence.level_count = levels_before.size;
    output->evidence.level_capacity = levels_before.capacity;
    if (levels_before.size > levels_before.capacity ||
        (levels_before.capacity && !levels_before.data))
        return FailDefinitionTraversal(
            output, PlayerDefinitionTraversalFailure::PresetHeaderInvalid);
    if (levels_before.size > kWindowsDefinitionLevelCapacity ||
        levels_before.capacity > kWindowsDefinitionLevelCapacity)
        return FailDefinitionTraversal(
            output,
            PlayerDefinitionTraversalFailure::LevelCountOutOfRange);

    if (!ProbeReadableElements(
            context, levels_before.data, levels_before.size,
            kWindowsPlayerDefinitionLevelStride,
            &output->evidence.failed_level_index))
        return FailDefinitionTraversal(
            output, PlayerDefinitionTraversalFailure::LevelSpanUnreadable);
    output->evidence.failed_level_index = UINT32_MAX;

    std::uint64_t hash =
        BeginDomainHash("windows_player_definition_0x28_v1");
    HashU32(&hash, row_index);
    HashU64(&hash, static_cast<std::uint64_t>(levels_before.data));
    HashU32(&hash, levels_before.size);
    HashU32(&hash, levels_before.capacity);
    std::uint32_t property_total = 0;
    for (std::uint32_t level_index = 0;
         level_index < levels_before.size; ++level_index) {
        output->evidence.failed_level_index = level_index;
        const std::size_t level_offset =
            static_cast<std::size_t>(level_index) *
            kWindowsPlayerDefinitionLevelStride;
        std::uintptr_t level = 0;
        VectorHeader properties_before = {};
        VectorHeader properties_after = {};
        if (!AddAddress(levels_before.data, level_offset, &level) ||
            !ReadAt(
                context, level,
                kWindowsPlayerDefinitionPropertyVectorOffset,
                &properties_before, sizeof(properties_before)))
            return FailDefinitionTraversal(
                output,
                PlayerDefinitionTraversalFailure::
                    LevelPropertyHeaderUnreadable);
        output->evidence.property_header_first =
            DefinitionHeader(properties_before);
        output->evidence.property_data = properties_before.data;
        output->evidence.property_count = properties_before.size;
        output->evidence.property_capacity = properties_before.capacity;
        output->evidence.failed_property_index =
            properties_before.size || properties_before.capacity
                ? 0U : UINT32_MAX;
        if (properties_before.size > properties_before.capacity ||
            (properties_before.capacity && !properties_before.data))
            return FailDefinitionTraversal(
                output,
                PlayerDefinitionTraversalFailure::
                    LevelPropertyHeaderInvalid);
        if (properties_before.size >
                kWindowsPlayerDefinitionPropertyCapacity ||
            properties_before.capacity >
                kWindowsPlayerDefinitionPropertyCapacity ||
            property_total >
                kWindowsPlayerDefinitionTotalPropertyCapacity -
                    properties_before.size)
            return FailDefinitionTraversal(
                output,
                PlayerDefinitionTraversalFailure::
                    PropertyCountOutOfRange);

        if (!ProbeReadableElements(
                context, properties_before.data,
                properties_before.size,
                kWindowsPlayerDefinitionPropertyStride,
                &output->evidence.failed_property_index))
            return FailDefinitionTraversal(
                output,
                PlayerDefinitionTraversalFailure::
                    PropertySpanUnreadable);
        output->evidence.failed_property_index = UINT32_MAX;

        const std::uint32_t logical_level = level_index + 1U;
        HashU32(&hash, logical_level);
        HashU32(&hash, levels_before.size);
        HashU64(
            &hash, static_cast<std::uint64_t>(properties_before.data));
        HashU32(&hash, properties_before.size);
        HashU32(&hash, properties_before.capacity);
        for (std::uint32_t property_index = 0;
             property_index < properties_before.size;
             ++property_index) {
            output->evidence.failed_property_index = property_index;
            const std::size_t property_offset =
                static_cast<std::size_t>(property_index) *
                kWindowsPlayerDefinitionPropertyStride;
            std::uintptr_t property = 0;
            unsigned char bytes[
                kWindowsPlayerDefinitionPropertyStride] = {};
            if (!AddAddress(
                    properties_before.data, property_offset, &property) ||
                !ReadMemory(
                    context, property, bytes, sizeof(bytes)))
                return FailDefinitionTraversal(
                    output,
                    PlayerDefinitionTraversalFailure::
                        PropertySpanUnreadable);

            const std::uint32_t property_id =
                BodyValue<std::uint32_t>(
                    bytes,
                    kWindowsPlayerDefinitionPropertyIdOffset);
            const std::uint32_t scalar_bits =
                BodyValue<std::uint32_t>(
                    bytes,
                    kWindowsPlayerDefinitionPropertyScalarBitsOffset);
            const std::uint8_t bool_value =
                BodyValue<std::uint8_t>(
                    bytes,
                    kWindowsPlayerDefinitionPropertyBoolOffset);
            const StringHeader string_value =
                BodyValue<StringHeader>(
                    bytes,
                    kWindowsPlayerDefinitionPropertyStringOffset);
            output->evidence.failed_property_id = property_id;
            if (string_value.size > string_value.capacity ||
                string_value.capacity >
                    kWindowsPlayerDefinitionStringByteCapacity ||
                (string_value.capacity && !string_value.data) ||
                (string_value.size && !string_value.data))
                return FailDefinitionTraversal(
                    output,
                    PlayerDefinitionTraversalFailure::
                        StringValueInvalid);

            HashU32(&hash, property_index);
            HashU32(&hash, property_id);
            HashU32(&hash, scalar_bits);
            HashBytes(&hash, &bool_value, sizeof(bool_value));
            HashU64(
                &hash, static_cast<std::uint64_t>(string_value.data));
            HashU32(&hash, string_value.size);
            HashU32(&hash, string_value.capacity);
            if (string_value.size) {
                if (!ProbeReadableSpan(
                        context, string_value.data, string_value.size))
                    return FailDefinitionTraversal(
                        output,
                        PlayerDefinitionTraversalFailure::
                            StringValueInvalid);
                unsigned char string_bytes[256] = {};
                std::size_t string_offset = 0;
                while (string_offset < string_value.size) {
                    const std::size_t remaining =
                        string_value.size - string_offset;
                    const std::size_t chunk =
                        remaining < sizeof(string_bytes)
                            ? remaining : sizeof(string_bytes);
                    std::uintptr_t cursor = 0;
                    if (!AddAddress(
                            string_value.data, string_offset, &cursor) ||
                        !ReadMemory(
                            context, cursor, string_bytes, chunk))
                        return FailDefinitionTraversal(
                            output,
                            PlayerDefinitionTraversalFailure::
                                StringValueInvalid);
                    HashBytes(&hash, string_bytes, chunk);
                    string_offset += chunk;
                }
            }
            const int fov_index =
                property_id == 779U ? 0 :
                property_id == 780U ? 1 :
                property_id == 817U ? 2 :
                property_id == 818U ? 3 : -1;
            if (fov_index >= 0) {
                output->fov_value_bits[fov_index] = scalar_bits;
                output->fov_present_mask |=
                    static_cast<std::uint8_t>(1U << fov_index);
            }
            ++property_total;
        }

        if (!ReadAt(
                context, level,
                kWindowsPlayerDefinitionPropertyVectorOffset,
                &properties_after, sizeof(properties_after)))
            return FailDefinitionTraversal(
                output,
                PlayerDefinitionTraversalFailure::
                    LevelPropertyHeaderUnreadable);
        output->evidence.property_header_second =
            DefinitionHeader(properties_after);
        if (!EqualVectorHeader(
                properties_before, properties_after))
            return FailDefinitionTraversal(
                output,
                PlayerDefinitionTraversalFailure::
                    PropertyHeaderChanged);
        output->evidence.failed_property_index = UINT32_MAX;
        output->evidence.failed_property_id = 0;
    }

    if (!ReadMemory(context, preset, &levels_after,
                    sizeof(levels_after)))
        return FailDefinitionTraversal(
            output,
            PlayerDefinitionTraversalFailure::PresetHeaderUnreadable);
    output->evidence.level_header_second =
        DefinitionHeader(levels_after);
    if (!EqualVectorHeader(levels_before, levels_after))
        return FailDefinitionTraversal(
            output,
            PlayerDefinitionTraversalFailure::LevelHeaderChanged);

    output->semantic_fingerprint = NonzeroHash(hash);
    output->level_count = levels_before.size;
    output->property_count = property_total;
    output->evidence.failed_level_index = UINT32_MAX;
    output->complete = true;
    return true;
}

void RecordRowFailure(
    PlayerGameplayParamsWindowsStateSnapshot* output,
    std::size_t index,
    PlayerGameplayParamsWindowsSnapshotReason reason) noexcept
{
    if (!output || index >= kWindowsPlayerLogicalTypeCount) return;
    PlayerGameplayParamsWindowsRowSnapshot& row = output->rows[index];
    row.failure_reason |= static_cast<std::uint64_t>(reason);
    AddReason(&output->reason_mask, reason);
    if (output->first_failed_row_index == UINT32_MAX) {
        output->first_failed_row_index =
            static_cast<std::uint32_t>(index);
        output->first_failed_row_reason = row.failure_reason;
    }
}

void RecordDefinitionTraversalFailure(
    PlayerGameplayParamsWindowsStateSnapshot* output,
    std::size_t index,
    PlayerDefinitionTraversalFailure failure) noexcept
{
    if (!output || index >= kWindowsPlayerLogicalTypeCount) return;
    PlayerGameplayParamsWindowsRowSnapshot& row = output->rows[index];
    switch (failure) {
    case PlayerDefinitionTraversalFailure::PresetHeaderUnreadable:
        row.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::
                MemoryUnreadable;
        row.header_validation_result =
            PlayerGameplayParamsWindowsRowHeaderValidationResult::
                NotAttempted;
        RecordRowFailure(
            output, index, WindowsSnapshotReasonMemoryUnreadable);
        break;
    case PlayerDefinitionTraversalFailure::PresetHeaderInvalid:
        row.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::Complete;
        if (row.definition_level_count >
            row.definition_level_capacity)
            row.header_validation_result =
                PlayerGameplayParamsWindowsRowHeaderValidationResult::
                    SizeExceedsCapacity;
        else
            row.header_validation_result =
                PlayerGameplayParamsWindowsRowHeaderValidationResult::
                    DataPointerUnavailable;
        RecordRowFailure(
            output, index, WindowsSnapshotReasonHeaderInvalid);
        break;
    case PlayerDefinitionTraversalFailure::LevelCountOutOfRange:
        row.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::Complete;
        row.header_validation_result =
            PlayerGameplayParamsWindowsRowHeaderValidationResult::
                CountOutOfRange;
        RecordRowFailure(
            output, index, WindowsSnapshotReasonCountOutOfRange);
        break;
    case PlayerDefinitionTraversalFailure::LevelHeaderChanged:
    case PlayerDefinitionTraversalFailure::PropertyHeaderChanged:
    case PlayerDefinitionTraversalFailure::SemanticFingerprintChanged:
        row.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::
                ChangedBetweenReads;
        row.header_validation_result =
            PlayerGameplayParamsWindowsRowHeaderValidationResult::Valid;
        RecordRowFailure(
            output, index, WindowsSnapshotReasonSnapshotChanged);
        break;
    case PlayerDefinitionTraversalFailure::InvalidArgument:
        row.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::NotAttempted;
        row.header_validation_result =
            PlayerGameplayParamsWindowsRowHeaderValidationResult::
                NotAttempted;
        RecordRowFailure(
            output, index, WindowsSnapshotReasonInvalidArgument);
        break;
    default:
        row.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::Complete;
        row.header_validation_result =
            PlayerGameplayParamsWindowsRowHeaderValidationResult::Valid;
        break;
    }
    RecordRowFailure(
        output, index,
        WindowsSnapshotReasonDefinitionTraversalIncomplete);
}

bool CaptureRows(
    const PlayerGameplayParamsWindowsReadContext& context,
    std::uintptr_t state, std::uintptr_t row_table,
    PlayerGameplayParamsWindowsStateSnapshot* output) noexcept
{
    if (!output) return false;
    if (!row_table) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonRowTableUnavailable);
        return false;
    }
    constexpr std::size_t kRowBytes =
        kWindowsPlayerLogicalTypeCount * kWindowsPlayerRowStride;
    unsigned char first[kRowBytes] = {};
    unsigned char second[kRowBytes] = {};
    if (!ValidSpan(row_table, sizeof(first))) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonAddressOverflow);
        return false;
    }
    if (!ReadMemory(context, row_table, first, sizeof(first))) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonMemoryUnreadable);
        return false;
    }

    bool definition_headers_complete = true;
    bool definition_semantics_complete = true;
    VectorHeader definition_headers[kWindowsPlayerLogicalTypeCount] = {};
    bool definition_header_valid[kWindowsPlayerLogicalTypeCount] = {};
    for (std::size_t index = 0;
         index < kWindowsPlayerLogicalTypeCount; ++index) {
        const unsigned char* row =
            first + index * kWindowsPlayerRowStride;
        PlayerGameplayParamsWindowsRowSnapshot& captured =
            output->rows[index];
        captured.preset = BodyValue<std::uintptr_t>(
            row, kWindowsPlayerRowPresetOffset);
        captured.definition_vector = captured.preset;
        captured.earned_experience =
            BodyValue<std::uint64_t>(
                row, kWindowsPlayerRowEarnedExperienceOffset);
        captured.unknown_100 =
            BodyValue<std::uint32_t>(
                row, kWindowsPlayerRowUnknown100Offset);
        captured.current_level =
            BodyValue<std::uint16_t>(
                row, kWindowsPlayerRowCurrentLevelOffset);
        captured.maximum_level =
            BodyValue<std::uint16_t>(
                row, kWindowsPlayerRowMaximumLevelOffset);
        captured.trailing_qword =
            BodyValue<std::uint64_t>(
                row, kWindowsPlayerRowTrailingQwordOffset);
        captured.row_readable = true;

        if (!captured.preset) {
            captured.semantic_kind =
                PlayerGameplayParamsWindowsRowSemanticKind::
                    AbsentPresetRow;
            captured.header_read_result =
                PlayerGameplayParamsWindowsRowHeaderReadResult::
                    AbsentPresetNotApplicable;
            captured.header_validation_result =
                PlayerGameplayParamsWindowsRowHeaderValidationResult::
                    AbsentPresetAccepted;
            captured.definition_vector_header_readable = true;
            captured.semantic_complete = true;
            captured.preset_absent = true;
            definition_header_valid[index] = true;
            continue;
        }

        captured.semantic_kind =
            PlayerGameplayParamsWindowsRowSemanticKind::PresetRow;
        PlayerDefinitionSemanticSnapshot definition = {};
        const bool definition_complete =
            CaptureWindowsPlayerDefinitionSemantics(
                &context, captured.preset,
                static_cast<std::uint32_t>(index), &definition);
        captured.definition_traversal = definition.evidence;
        captured.definition_header_data =
            definition.evidence.level_data;
        captured.definition_level_count =
            definition.evidence.level_count;
        captured.definition_level_capacity =
            definition.evidence.level_capacity;
        captured.definition_property_count =
            definition.property_count;
        captured.definition_fingerprint =
            definition.semantic_fingerprint;
        const VectorHeader vector_before = {
            definition.evidence.level_header_first.data,
            definition.evidence.level_header_first.size,
            definition.evidence.level_header_first.capacity};
        const bool header_valid =
            definition.evidence.first_failure !=
                PlayerDefinitionTraversalFailure::
                    PresetHeaderUnreadable &&
            definition.evidence.first_failure !=
                PlayerDefinitionTraversalFailure::
                    PresetHeaderInvalid &&
            definition.evidence.first_failure !=
                PlayerDefinitionTraversalFailure::
                    LevelCountOutOfRange &&
            definition.evidence.first_failure !=
                PlayerDefinitionTraversalFailure::
                    LevelHeaderChanged &&
            definition.evidence.first_failure !=
                PlayerDefinitionTraversalFailure::
                    InvalidArgument &&
            ValidVectorHeader(
                vector_before, kWindowsDefinitionLevelCapacity);
        captured.definition_vector_header_readable = header_valid;
        captured.semantic_complete = definition_complete;
        if (!definition_complete) {
            RecordDefinitionTraversalFailure(
                output, index, definition.evidence.first_failure);
            if (!header_valid) definition_headers_complete = false;
            definition_semantics_complete = false;
            continue;
        }
        captured.header_read_result =
            PlayerGameplayParamsWindowsRowHeaderReadResult::Complete;
        captured.header_validation_result =
            PlayerGameplayParamsWindowsRowHeaderValidationResult::Valid;
        definition_headers[index] = vector_before;
        definition_header_valid[index] = true;
    }

    std::uintptr_t observed_table = 0;
    if (!ReadMemory(context, row_table, second, sizeof(second)) ||
        std::memcmp(first, second, sizeof(first)) != 0 ||
        !ReadAt(context, state, kWindowsPlayerStateRowTableOffset,
                &observed_table, sizeof(observed_table)) ||
        observed_table != row_table) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonSnapshotChanged);
        return false;
    }
    for (std::size_t index = 0;
         index < kWindowsPlayerLogicalTypeCount; ++index) {
        if (!definition_header_valid[index] ||
            output->rows[index].preset_absent)
            continue;
        VectorHeader observed = {};
        if (!ReadMemory(context, output->rows[index].preset,
                        &observed, sizeof(observed)) ||
            !EqualVectorHeader(definition_headers[index], observed)) {
            output->rows[index].definition_vector_header_readable = false;
            output->rows[index].semantic_complete = false;
            output->rows[index].header_read_result =
                PlayerGameplayParamsWindowsRowHeaderReadResult::
                    ChangedBetweenReads;
            output->rows[index].header_validation_result =
                PlayerGameplayParamsWindowsRowHeaderValidationResult::
                    NotAttempted;
            RecordRowFailure(
                output, index, WindowsSnapshotReasonHeaderChanged);
            RecordRowFailure(
                output, index,
                WindowsSnapshotReasonDefinitionTraversalIncomplete);
            definition_headers_complete = false;
            definition_semantics_complete = false;
        }
    }
    output->complete_fields |= WindowsFieldPlayerRows;
    if (definition_headers_complete)
        output->complete_fields |= WindowsFieldDefinitionVectorHeaders;
    else
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonDefinitionVectorHeaderUnavailable);
    if (definition_semantics_complete)
        output->complete_fields |= WindowsFieldDefinitionSemantics;
    else
        AddReason(
            &output->reason_mask,
            WindowsSnapshotReasonDefinitionTraversalIncomplete);
    return true;
}

std::uint64_t AppearanceFingerprint(const unsigned char* body) noexcept
{
    std::uint64_t hash = BeginDomainHash("player_appearance_values_v1");
    HashU32(&hash,
            static_cast<std::uint32_t>(kWindowsPlayerStateAppearanceSize));
    HashBytes(&hash, body + kWindowsPlayerStateAppearanceOffset,
              kWindowsPlayerStateAppearanceSize);
    return NonzeroHash(hash);
}

bool StableBodyFields(const unsigned char* before,
                      const unsigned char* after) noexcept
{
    constexpr std::size_t kPointerOffsets[] = {
        0x00U, 0x08U, 0x10U, 0x30U,
        kWindowsPlayerStateRowTableOffset,
        kWindowsPlayerStatePlayerVariablesOffset,
        kWindowsPlayerStateSkillRootOffset,
    };
    for (std::size_t index = 0;
         index < sizeof(kPointerOffsets) / sizeof(kPointerOffsets[0]);
         ++index) {
        const std::size_t offset = kPointerOffsets[index];
        if (std::memcmp(before + offset, after + offset,
                        sizeof(std::uintptr_t)) != 0)
            return false;
    }
    if (std::memcmp(before + kWindowsPlayerStateSkillCountOffset,
                    after + kWindowsPlayerStateSkillCountOffset,
                    sizeof(std::uint32_t)) != 0 ||
        std::memcmp(before + kWindowsPlayerStateProgressionSelectorOffset,
                    after + kWindowsPlayerStateProgressionSelectorOffset,
                    sizeof(StringHeader)) != 0 ||
        std::memcmp(before + kWindowsPlayerStateClassSelectorOffset,
                    after + kWindowsPlayerStateClassSelectorOffset,
                    sizeof(StringHeader)) != 0 ||
        std::memcmp(before + kWindowsPlayerStateRegularBuffOffset,
                    after + kWindowsPlayerStateRegularBuffOffset,
                    sizeof(VectorHeader)) != 0 ||
        std::memcmp(before + kWindowsPlayerStateTimedBuffOffset,
                    after + kWindowsPlayerStateTimedBuffOffset,
                    sizeof(VectorHeader)) != 0 ||
        AppearanceFingerprint(before) != AppearanceFingerprint(after))
        return false;
    return true;
}

bool ValidatePlayerStateIdentity(
    const PlayerGameplayParamsWindowsReadContext& context,
    const unsigned char* body) noexcept
{
    struct ExpectedVtable {
        std::size_t offset;
        std::uintptr_t rva;
    };
    constexpr ExpectedVtable expected[] = {
        {0x00U, kWindowsPlayerStatePrimaryVtableRva},
        {0x08U, kWindowsPlayerStateVtable8Rva},
        {0x10U, kWindowsPlayerStateVtable16Rva},
        {0x30U, kWindowsPlayerStateVtable48Rva},
    };
    for (std::size_t index = 0;
         index < sizeof(expected) / sizeof(expected[0]); ++index) {
        std::uintptr_t address = 0;
        if (!AddAddress(context.gamedll_base, expected[index].rva,
                        &address) ||
            BodyValue<std::uintptr_t>(body, expected[index].offset) !=
                address)
            return false;
    }
    return true;
}

}  // namespace

bool CaptureWindowsPlayerDefinitionSemantics(
    const PlayerGameplayParamsWindowsReadContext* context,
    std::uintptr_t preset, std::uint32_t row_index,
    PlayerDefinitionSemanticSnapshot* output) noexcept
{
    if (!output) return false;
    InitializeDefinitionTraversal(preset, row_index, output);
    if (!ValidContext(context) || !context->exact_gamedll_image_validated ||
        !preset)
        return FailDefinitionTraversal(
            output,
            PlayerDefinitionTraversalFailure::InvalidArgument);

    PlayerDefinitionSemanticSnapshot first = {};
    PlayerDefinitionSemanticSnapshot second = {};
    if (!CaptureWindowsPlayerDefinitionSemanticsOnce(
            *context, preset, row_index, &first)) {
        *output = first;
        return false;
    }
    if (!CaptureWindowsPlayerDefinitionSemanticsOnce(
            *context, preset, row_index, &second)) {
        *output = second;
        return false;
    }
    if (first.semantic_fingerprint != second.semantic_fingerprint ||
        first.level_count != second.level_count ||
        first.property_count != second.property_count ||
        first.fov_present_mask != second.fov_present_mask ||
        std::memcmp(
            first.fov_value_bits, second.fov_value_bits,
            sizeof(first.fov_value_bits)) != 0) {
        *output = second;
        output->evidence.first_failure =
            PlayerDefinitionTraversalFailure::
                SemanticFingerprintChanged;
        output->complete = false;
        return false;
    }
    *output = second;
    return true;
}

bool SnapshotPlayerGameplayParamsWindowsEnvironment(
    const PlayerGameplayParamsWindowsReadContext* context,
    PlayerGameplayParamsWindowsEnvironmentSnapshot* output,
    PlayerGameplayParamsWindowsEnvironmentReadMode mode) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    if (!ValidContext(context)) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonInvalidArgument);
        return false;
    }
    if (!context->exact_gamedll_image_validated) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonUnsupportedImage);
        return false;
    }
    output->complete_fields |= WindowsFieldExactImage;

    struct GlobalRead {
        std::uintptr_t rva;
        std::uintptr_t* value;
        std::uint64_t field;
    };
    GlobalRead globals[] = {
        {kWindowsGameplayParamsContainerGlobalRva,
         &output->gameplay_params_container,
         WindowsFieldGameplayParamsContainer},
        {kWindowsGameDiGlobalRva, &output->game_di, WindowsFieldGameDi},
        {kWindowsLocalPlayerStateGlobalRva,
         &output->local_process_global_state,
         WindowsFieldLocalPlayerStateGlobal},
        {kWindowsCharacterPlayerStateGlobalRva,
         &output->character_manager_global_state,
         WindowsFieldCharacterPlayerStateGlobal},
    };
    for (std::size_t index = 0;
         index < sizeof(globals) / sizeof(globals[0]); ++index) {
        std::uintptr_t address = 0;
        if (!AddAddress(context->gamedll_base, globals[index].rva,
                        &address) ||
            !ReadMemory(*context, address, globals[index].value,
                        sizeof(*globals[index].value))) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        output->complete_fields |= globals[index].field;
    }

    if (!output->gameplay_params_container) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonGameplayParamsContainerUnavailable);
    } else {
        std::uintptr_t container_storage = 0;
        if (!ReadMemory(*context, output->gameplay_params_container,
                        &container_storage, sizeof(container_storage))) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonMemoryUnreadable);
            output->complete_fields &=
                ~static_cast<std::uint64_t>(
                    WindowsFieldGameplayParamsContainer);
        }
    }

    if (!output->game_di) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonGameDiUnavailable);
    } else {
        std::uintptr_t observed_vtable = 0;
        std::uintptr_t expected_vtable = 0;
        if (!AddAddress(context->gamedll_base, kWindowsGameDiVtableRva,
                        &expected_vtable) ||
            !ReadMemory(*context, output->game_di, &observed_vtable,
                        sizeof(observed_vtable))) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonMemoryUnreadable);
            output->complete_fields &=
                ~static_cast<std::uint64_t>(WindowsFieldGameDi);
        } else if (observed_vtable != expected_vtable) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonGameDiIdentityMismatch);
            output->complete_fields &=
                ~static_cast<std::uint64_t>(WindowsFieldGameDi);
        } else if (!ReadAt(*context, output->game_di,
                           kWindowsGameDiCurrentSessionOffset,
                           &output->current_session,
                           sizeof(output->current_session))) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonMemoryUnreadable);
        } else {
            output->complete_fields |= WindowsFieldCurrentSession;
            if (output->current_session &&
                mode ==
                    PlayerGameplayParamsWindowsEnvironmentReadMode::
                        SessionAware) {
                if (!ReadAt(*context, output->current_session,
                            kWindowsSessionActiveLevelOffset,
                            &output->active_level,
                            sizeof(output->active_level))) {
                    AddReason(&output->reason_mask,
                              WindowsSnapshotReasonMemoryUnreadable);
                } else {
                    output->complete_fields |= WindowsFieldActiveLevel;
                }
            } else if (!output->current_session) {
                /* An absent current SessionDI has no owned active-level
                 * field to dereference; its active-level result is exactly
                 * absent, not a synthesized PlayerDI observation. */
                output->active_level = 0;
                output->complete_fields |= WindowsFieldActiveLevel;
            } else {
                /* SessionDI::OnDestroy has returned, but GameDI may not yet
                 * have cleared its current-session slot. Never dereference
                 * that outgoing object from the post-destroy semantic read.
                 * The stable nonzero slot is already an exact blocker. */
                output->active_level = 0;
            }
        }
    }

    for (std::size_t index = 0;
         index < sizeof(globals) / sizeof(globals[0]); ++index) {
        std::uintptr_t address = 0;
        std::uintptr_t observed = 0;
        if (!AddAddress(context->gamedll_base, globals[index].rva,
                        &address) ||
            !ReadMemory(*context, address, &observed, sizeof(observed))) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        if (observed != *globals[index].value) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonSnapshotChanged);
            return false;
        }
    }
    if ((output->complete_fields & WindowsFieldCurrentSession) != 0) {
        std::uintptr_t observed_current_session = 0;
        if (!ReadAt(*context, output->game_di,
                    kWindowsGameDiCurrentSessionOffset,
                    &observed_current_session,
                    sizeof(observed_current_session))) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonMemoryUnreadable);
            return false;
        }
        if (observed_current_session != output->current_session) {
            AddReason(&output->reason_mask,
                      WindowsSnapshotReasonSnapshotChanged);
            return false;
        }
        if (output->current_session &&
            mode ==
                PlayerGameplayParamsWindowsEnvironmentReadMode::
                    SessionAware &&
            (output->complete_fields & WindowsFieldActiveLevel) != 0) {
            std::uintptr_t observed_active_level = 0;
            if (!ReadAt(*context, output->current_session,
                        kWindowsSessionActiveLevelOffset,
                        &observed_active_level,
                        sizeof(observed_active_level))) {
                AddReason(&output->reason_mask,
                          WindowsSnapshotReasonMemoryUnreadable);
                return false;
            }
            if (observed_active_level != output->active_level) {
                AddReason(&output->reason_mask,
                          WindowsSnapshotReasonSnapshotChanged);
                return false;
            }
        }
    }

    if (!output->local_process_global_state)
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonLocalPlayerStateUnavailable);
    if (!output->character_manager_global_state)
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonCharacterPlayerStateUnavailable);
    if (output->current_session)
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonActiveSessionPresent);
    if (output->active_level)
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonActiveLevelPresent);

    const std::uint64_t required_fields =
        mode ==
                PlayerGameplayParamsWindowsEnvironmentReadMode::
                    PostDestroyProcessGlobals
            ? kWindowsEnvironmentRequiredFields &
                  ~static_cast<std::uint64_t>(WindowsFieldActiveLevel)
            : kWindowsEnvironmentRequiredFields;
    output->observation_complete =
        (output->complete_fields & required_fields) == required_fields;
    output->no_session_environment_ready = output->observation_complete &&
        output->gameplay_params_container && output->game_di &&
        output->local_process_global_state &&
        output->character_manager_global_state &&
        !output->current_session && !output->active_level;
    return output->observation_complete;
}

bool SnapshotPlayerGameplayParamsWindowsState(
    const PlayerGameplayParamsWindowsReadContext* context,
    std::uintptr_t player_state, std::uint64_t state_generation,
    PlayerGameplayParamsWindowsStateSnapshot* output) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    output->state = player_state;
    output->state_generation = state_generation;
    output->first_failed_row_index = UINT32_MAX;
    output->unresolved_fields = kWindowsPlayerStateUnresolvedFields;
    AddReason(&output->reason_mask,
              WindowsSnapshotReasonInventoryOwnerUnresolved);
    AddReason(&output->reason_mask,
              WindowsSnapshotReasonEquipmentOwnerUnresolved);
    AddReason(&output->reason_mask,
              WindowsSnapshotReasonQuestOwnerUnresolved);
    AddReason(&output->reason_mask,
              WindowsSnapshotReasonProviderIdentityUnresolved);

    if (!ValidContext(context) || !player_state || !state_generation) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonInvalidArgument);
        return false;
    }
    if (!context->exact_gamedll_image_validated) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonUnsupportedImage);
        return false;
    }
    output->complete_fields |= WindowsFieldExactImage;

    unsigned char body[kWindowsPlayerStateSize] = {};
    unsigned char body_after[kWindowsPlayerStateSize] = {};
    if (!ValidSpan(player_state, sizeof(body))) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonAddressOverflow);
        return false;
    }
    if (!ReadMemory(*context, player_state, body, sizeof(body))) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonMemoryUnreadable);
        return false;
    }
    if (!ValidatePlayerStateIdentity(*context, body)) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonPlayerStateIdentityMismatch);
        return false;
    }
    output->complete_fields |= WindowsFieldPlayerStateIdentity;

    const std::uintptr_t row_table =
        BodyValue<std::uintptr_t>(body,
                                  kWindowsPlayerStateRowTableOffset);
    CaptureRows(*context, player_state, row_table, output);

    const StringHeader progression = BodyValue<StringHeader>(
        body, kWindowsPlayerStateProgressionSelectorOffset);
    if (CaptureSelector(*context, player_state,
                        kWindowsPlayerStateProgressionSelectorOffset,
                        progression, "player_progression_selector_v1",
                        &output->progression_selector,
                        &output->reason_mask))
        output->complete_fields |= WindowsFieldProgressionSelector;

    const StringHeader player_class = BodyValue<StringHeader>(
        body, kWindowsPlayerStateClassSelectorOffset);
    if (CaptureSelector(*context, player_state,
                        kWindowsPlayerStateClassSelectorOffset,
                        player_class, "player_class_selector_v1",
                        &output->class_selector,
                        &output->reason_mask))
        output->complete_fields |= WindowsFieldClassSelector;

    output->player_variables = BodyValue<std::uintptr_t>(
        body, kWindowsPlayerStatePlayerVariablesOffset);
    std::uintptr_t observed_player_variables = 0;
    unsigned char player_variables_byte = 0;
    if (output->player_variables &&
        ReadAt(*context, player_state,
               kWindowsPlayerStatePlayerVariablesOffset,
               &observed_player_variables,
               sizeof(observed_player_variables)) &&
        observed_player_variables == output->player_variables &&
        ReadMemory(*context, output->player_variables,
                   &player_variables_byte, sizeof(player_variables_byte))) {
        output->complete_fields |= WindowsFieldPlayerVariables;
    } else {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonPlayerVariablesUnavailable);
    }

    SkillHeader skills = {};
    skills.root = BodyValue<std::uintptr_t>(
        body, kWindowsPlayerStateSkillRootOffset);
    skills.count = BodyValue<std::uint32_t>(
        body, kWindowsPlayerStateSkillCountOffset);
    if (CaptureSkillTree(*context, player_state, skills,
                         &output->skills, &output->reason_mask))
        output->complete_fields |= WindowsFieldSkills;

    const VectorHeader regular = BodyValue<VectorHeader>(
        body, kWindowsPlayerStateRegularBuffOffset);
    if (CaptureBuffVector(*context, player_state,
                          kWindowsPlayerStateRegularBuffOffset,
                          regular, false, &output->regular_buffs,
                          &output->reason_mask))
        output->complete_fields |= WindowsFieldRegularBuffs;

    const VectorHeader timed = BodyValue<VectorHeader>(
        body, kWindowsPlayerStateTimedBuffOffset);
    if (CaptureBuffVector(*context, player_state,
                          kWindowsPlayerStateTimedBuffOffset,
                          timed, true, &output->timed_buffs,
                          &output->reason_mask))
        output->complete_fields |= WindowsFieldTimedBuffs;

    output->appearance_values_fingerprint = AppearanceFingerprint(body);
    if (!ReadMemory(*context, player_state, body_after,
                    sizeof(body_after))) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonMemoryUnreadable);
    } else if (!ValidatePlayerStateIdentity(*context, body_after) ||
               !StableBodyFields(body, body_after)) {
        AddReason(&output->reason_mask,
                  WindowsSnapshotReasonSnapshotChanged);
        output->complete_fields &=
            ~(static_cast<std::uint64_t>(WindowsFieldPlayerStateIdentity));
    } else {
        output->complete_fields |= WindowsFieldPlayerStateBody;
        output->complete_fields |= WindowsFieldAppearanceValues;
    }

    output->player_semantic_snapshot_complete =
        (output->complete_fields & kWindowsPlayerStateRequiredProvenFields) ==
            kWindowsPlayerStateRequiredProvenFields;
    output->proven_fields_complete =
        output->player_semantic_snapshot_complete;
    /* Deliberately false until exact live inventory, equipment, quest, and
     * provider owners exist.  A zero-valued fake fingerprint is never used. */
    output->player_restart_snapshot_complete =
        output->player_semantic_snapshot_complete &&
        output->unresolved_fields == 0;
    output->restart_snapshot_complete =
        output->player_restart_snapshot_complete;
    return output->player_semantic_snapshot_complete;
}

bool dluma_capture_stable_main_menu_player_semantic_snapshot(
    const PlayerGameplayParamsWindowsReadContext* context,
    std::uintptr_t player_state, std::uint64_t state_generation,
    PlayerGameplayParamsWindowsStateSnapshot* first,
    PlayerGameplayParamsWindowsStateSnapshot* second) noexcept
{
    if (!first || !second || first == second) return false;
    if (!SnapshotPlayerGameplayParamsWindowsState(
            context, player_state, state_generation, first) ||
        !SnapshotPlayerGameplayParamsWindowsState(
            context, player_state, state_generation, second))
        return false;
    if (!first->player_semantic_snapshot_complete ||
        !second->player_semantic_snapshot_complete ||
        std::memcmp(first, second, sizeof(*first)) != 0) {
        AddReason(
            &second->reason_mask,
            WindowsSnapshotReasonSnapshotChanged);
        second->player_semantic_snapshot_complete = false;
        second->player_restart_snapshot_complete = false;
        second->proven_fields_complete = false;
        second->restart_snapshot_complete = false;
        return false;
    }
    return true;
}

const char* PlayerGameplayParamsWindowsRowSemanticKindName(
    PlayerGameplayParamsWindowsRowSemanticKind kind) noexcept
{
    switch (kind) {
    case PlayerGameplayParamsWindowsRowSemanticKind::Unavailable:
        return "unavailable";
    case PlayerGameplayParamsWindowsRowSemanticKind::AbsentPresetRow:
        return "absent_preset_row";
    case PlayerGameplayParamsWindowsRowSemanticKind::PresetRow:
        return "preset_row";
    default:
        return "unknown";
    }
}

const char* PlayerGameplayParamsWindowsRowHeaderReadResultName(
    PlayerGameplayParamsWindowsRowHeaderReadResult result) noexcept
{
    switch (result) {
    case PlayerGameplayParamsWindowsRowHeaderReadResult::NotAttempted:
        return "not_attempted";
    case PlayerGameplayParamsWindowsRowHeaderReadResult::
             AbsentPresetNotApplicable:
        return "absent_preset_not_applicable";
    case PlayerGameplayParamsWindowsRowHeaderReadResult::Complete:
        return "complete";
    case PlayerGameplayParamsWindowsRowHeaderReadResult::AddressOverflow:
        return "address_overflow";
    case PlayerGameplayParamsWindowsRowHeaderReadResult::MemoryUnreadable:
        return "memory_unreadable";
    case PlayerGameplayParamsWindowsRowHeaderReadResult::
             ChangedBetweenReads:
        return "changed_between_reads";
    default:
        return "unknown";
    }
}

const char* PlayerGameplayParamsWindowsRowHeaderValidationResultName(
    PlayerGameplayParamsWindowsRowHeaderValidationResult result) noexcept
{
    switch (result) {
    case PlayerGameplayParamsWindowsRowHeaderValidationResult::NotAttempted:
        return "not_attempted";
    case PlayerGameplayParamsWindowsRowHeaderValidationResult::
             AbsentPresetAccepted:
        return "absent_preset_accepted";
    case PlayerGameplayParamsWindowsRowHeaderValidationResult::Valid:
        return "valid";
    case PlayerGameplayParamsWindowsRowHeaderValidationResult::
             SizeExceedsCapacity:
        return "size_exceeds_capacity";
    case PlayerGameplayParamsWindowsRowHeaderValidationResult::
             CountOutOfRange:
        return "count_out_of_range";
    case PlayerGameplayParamsWindowsRowHeaderValidationResult::
             DataPointerUnavailable:
        return "data_pointer_unavailable";
    default:
        return "unknown";
    }
}

const char* PlayerDefinitionTraversalFailureName(
    PlayerDefinitionTraversalFailure failure) noexcept
{
    switch (failure) {
    case PlayerDefinitionTraversalFailure::None:
        return "none";
    case PlayerDefinitionTraversalFailure::InvalidArgument:
        return "invalid_argument";
    case PlayerDefinitionTraversalFailure::PresetHeaderUnreadable:
        return "preset_header_unreadable";
    case PlayerDefinitionTraversalFailure::PresetHeaderInvalid:
        return "preset_header_invalid";
    case PlayerDefinitionTraversalFailure::LevelCountOutOfRange:
        return "level_count_out_of_range";
    case PlayerDefinitionTraversalFailure::LevelSpanUnreadable:
        return "level_span_unreadable";
    case PlayerDefinitionTraversalFailure::LevelHeaderChanged:
        return "level_header_changed";
    case PlayerDefinitionTraversalFailure::LevelPropertyHeaderUnreadable:
        return "level_property_header_unreadable";
    case PlayerDefinitionTraversalFailure::LevelPropertyHeaderInvalid:
        return "level_property_header_invalid";
    case PlayerDefinitionTraversalFailure::PropertyCountOutOfRange:
        return "property_count_out_of_range";
    case PlayerDefinitionTraversalFailure::PropertySpanUnreadable:
        return "property_span_unreadable";
    case PlayerDefinitionTraversalFailure::PropertyValueInvalid:
        return "property_value_invalid";
    case PlayerDefinitionTraversalFailure::StringValueInvalid:
        return "string_value_invalid";
    case PlayerDefinitionTraversalFailure::PropertyHeaderChanged:
        return "property_header_changed";
    case PlayerDefinitionTraversalFailure::SemanticFingerprintChanged:
        return "semantic_fingerprint_changed";
    default:
        return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart
