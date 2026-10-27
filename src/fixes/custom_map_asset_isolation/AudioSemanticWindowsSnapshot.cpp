#define _HAS_EXCEPTIONS 0

#include "AudioSemanticWindowsSnapshot.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint32_t kBankHashMultiplier = 0x29U;
constexpr std::size_t kDefinitionRecordSize = 104U;
constexpr std::size_t kProcedureRecordSize = 40U;

struct StringHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

struct TaggedVectorHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
    std::uint32_t tag;
    std::uint32_t reserved;
};

/* The exact Windows DLE implementation initializes the property-group tree
 * at engine+0xAD5360 as { root, count, padding }.  The DWORD at +0x10 is the
 * tag of the following container, not this tree's capacity. */
struct OrderedTreeHeader {
    std::uintptr_t root;
    std::uint32_t size;
    std::uint32_t reserved;
};

struct PropertyNode {
    std::uintptr_t link;
    std::uintptr_t node;
    std::uintptr_t left;
    std::uintptr_t parent;
    std::uintptr_t right;
    std::uint32_t color;
    unsigned char body[kAudioPropertyNodeReadableSize];
};

struct BankFrame {
    std::uintptr_t link;
    std::uintptr_t right;
    unsigned char body[kAudioBankNodeReadableSize];
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

void AddReason(std::uint64_t* mask,
               AudioSemanticSnapshotReason reason) noexcept
{
    if (mask) *mask |= static_cast<std::uint64_t>(reason);
}

bool ValidContext(const AudioSemanticReadContext* context) noexcept
{
    return context && context->engine_base && context->read_memory &&
        context->exact_engine_image_validated &&
        context->caller_owns_audio_lock;
}

bool ReadMemory(const AudioSemanticReadContext& context,
                std::uintptr_t address, void* output,
                std::size_t size) noexcept
{
    return address && output && size &&
        address <= static_cast<std::uintptr_t>(-1) - (size - 1U) &&
        context.read_memory(
            context.callback_user, address, output, size);
}

bool ReadAt(const AudioSemanticReadContext& context,
            std::uintptr_t base, std::size_t offset,
            void* output, std::size_t size) noexcept
{
    std::uintptr_t address = 0;
    return AddAddress(base, offset, &address) &&
        ReadMemory(context, address, output, size);
}

bool ReadRva(const AudioSemanticReadContext& context,
             std::uintptr_t rva, void* output, std::size_t size,
             std::uint64_t* reasons) noexcept
{
    std::uintptr_t address = 0;
    if (!AddAddress(context.engine_base, rva, &address)) {
        AddReason(reasons, AudioSemanticReasonAddressOverflow);
        return false;
    }
    if (!ReadMemory(context, address, output, size)) {
        AddReason(reasons, AudioSemanticReasonMemoryUnreadable);
        return false;
    }
    return true;
}

template <typename T>
T BodyValue(const unsigned char* body, std::size_t offset) noexcept
{
    T value = {};
    std::memcpy(&value, body + offset, sizeof(value));
    return value;
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

void HashU8(std::uint64_t* hash, std::uint8_t value) noexcept
{
    HashBytes(hash, &value, sizeof(value));
}

void HashU32(std::uint64_t* hash, std::uint32_t value) noexcept
{
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU64(std::uint64_t* hash, std::uint64_t value) noexcept
{
    const unsigned char bytes[8] = {
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

std::uint64_t BeginHash(const char* domain) noexcept
{
    std::uint64_t hash = kFnvOffset;
    while (domain && *domain) {
        HashBytes(&hash, domain, 1U);
        ++domain;
    }
    return hash;
}

std::uint64_t NonzeroHash(std::uint64_t hash) noexcept
{
    return hash ? hash : kFnvOffset;
}

bool AddCount(std::uint32_t value, std::uint32_t* total,
              std::uint64_t* reasons) noexcept
{
    if (!total || value > UINT32_MAX - *total) {
        AddReason(reasons, AudioSemanticReasonCountOutOfRange);
        return false;
    }
    *total += value;
    return true;
}

bool ValidVectorHeader(const TaggedVectorHeader& header,
                       std::size_t maximum,
                       std::uint64_t* reasons) noexcept
{
    if (header.size > header.capacity ||
        header.capacity > maximum ||
        header.tag != kAudioScriptVectorTag ||
        (header.size != 0U && !header.data)) {
        AddReason(reasons, AudioSemanticReasonHeaderInvalid);
        return false;
    }
    return true;
}

bool ReadHeaderString(
    const AudioSemanticReadContext& context,
    const StringHeader& header, bool allow_empty,
    char* output, std::size_t output_capacity,
    std::uint32_t* length, std::uint64_t* reasons) noexcept
{
    if (!output || output_capacity == 0U || !length ||
        header.size > header.capacity ||
        header.capacity > kAudioScriptVectorCapacity ||
        header.size >= output_capacity ||
        (header.size != 0U && !header.data) ||
        (!allow_empty && header.size == 0U)) {
        AddReason(reasons, AudioSemanticReasonStringInvalid);
        return false;
    }
    *length = header.size;
    if (header.size == 0U) {
        output[0] = '\0';
        return true;
    }
    if (!ReadMemory(
            context, header.data, output,
            static_cast<std::size_t>(header.size) + 1U)) {
        AddReason(reasons, AudioSemanticReasonMemoryUnreadable);
        return false;
    }
    for (std::uint32_t index = 0; index < header.size; ++index) {
        if (output[index] == '\0') {
            AddReason(reasons, AudioSemanticReasonStringInvalid);
            return false;
        }
    }
    if (output[header.size] != '\0') {
        AddReason(reasons, AudioSemanticReasonStringInvalid);
        return false;
    }
    return true;
}

bool ReadPointerString(
    const AudioSemanticReadContext& context,
    std::uintptr_t pointer, char* output,
    std::size_t output_capacity, std::uint32_t* length,
    std::uint64_t* reasons) noexcept
{
    if (!output || output_capacity < 2U || !length) {
        AddReason(reasons, AudioSemanticReasonInvalidArgument);
        return false;
    }
    if (!pointer) {
        output[0] = '\0';
        *length = 0;
        return true;
    }
    for (std::size_t index = 0; index < output_capacity; ++index) {
        if (!ReadMemory(
                context, pointer + index, &output[index], 1U)) {
            AddReason(reasons, AudioSemanticReasonMemoryUnreadable);
            return false;
        }
        if (output[index] == '\0') {
            *length = static_cast<std::uint32_t>(index);
            return true;
        }
    }
    AddReason(reasons, AudioSemanticReasonStringInvalid);
    return false;
}

bool HashHeaderString(
    const AudioSemanticReadContext& context,
    const StringHeader& header, bool allow_empty,
    std::uint64_t* hash, std::uint32_t* length,
    std::uint64_t* reasons) noexcept
{
    char text[kAudioSemanticStringCapacity] = {};
    if (!ReadHeaderString(
            context, header, allow_empty, text, sizeof(text),
            length, reasons))
        return false;
    HashU32(hash, *length);
    HashBytes(hash, text, *length);
    return true;
}

bool HashDefinitionVector(
    const AudioSemanticReadContext& context,
    const TaggedVectorHeader& header,
    std::uint64_t* hash, std::uint32_t* definition_count,
    std::uint64_t* reasons) noexcept
{
    if (!ValidVectorHeader(
            header, kAudioScriptVectorCapacity, reasons) ||
        !AddCount(header.size, definition_count, reasons))
        return false;
    HashU32(hash, header.size);
    HashU32(hash, header.tag);
    for (std::uint32_t index = 0; index < header.size; ++index) {
        unsigned char record[kDefinitionRecordSize] = {};
        const std::size_t offset =
            static_cast<std::size_t>(index) * sizeof(record);
        if (offset / sizeof(record) != index ||
            !ReadAt(
                context, header.data, offset, record,
                sizeof(record))) {
            AddReason(reasons, AudioSemanticReasonMemoryUnreadable);
            return false;
        }
        HashU32(hash, index);
        HashU64(hash, BodyValue<std::uint64_t>(record, 0U));
        for (std::uint32_t slot = 0; slot < 6U; ++slot) {
            const std::size_t slot_offset =
                8U + static_cast<std::size_t>(slot) * 16U;
            const std::uintptr_t raw =
                BodyValue<std::uintptr_t>(record, slot_offset);
            const std::uint32_t type =
                BodyValue<std::uint32_t>(record, slot_offset + 8U);
            if (type > 2U) {
                AddReason(
                    reasons, AudioSemanticReasonValueTypeInvalid);
                return false;
            }
            HashU32(hash, slot);
            HashU32(hash, type);
            if (type == 0U) {
                char text[kAudioSemanticStringCapacity] = {};
                std::uint32_t length = 0;
                if (!ReadPointerString(
                        context, raw, text, sizeof(text), &length,
                        reasons))
                    return false;
                HashU32(hash, length);
                HashBytes(hash, text, length);
            } else {
                HashU64(hash, static_cast<std::uint64_t>(raw));
            }
        }
    }
    return true;
}

bool HashStringVector(
    const AudioSemanticReadContext& context,
    const TaggedVectorHeader& header,
    std::uint64_t* hash, std::uint32_t* definition_count,
    std::uint64_t* reasons) noexcept
{
    if (!ValidVectorHeader(
            header, kAudioScriptVectorCapacity, reasons) ||
        !AddCount(header.size, definition_count, reasons))
        return false;
    HashU32(hash, header.size);
    HashU32(hash, header.tag);
    for (std::uint32_t index = 0; index < header.size; ++index) {
        StringHeader item = {};
        const std::size_t offset =
            static_cast<std::size_t>(index) * sizeof(item);
        if (offset / sizeof(item) != index ||
            !ReadAt(
                context, header.data, offset, &item,
                sizeof(item))) {
            AddReason(reasons, AudioSemanticReasonMemoryUnreadable);
            return false;
        }
        std::uint32_t length = 0;
        HashU32(hash, index);
        if (!HashHeaderString(
                context, item, true, hash, &length, reasons))
            return false;
    }
    return true;
}

bool SeenLink(const std::uintptr_t* links, std::uint32_t count,
              std::uintptr_t link) noexcept
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (links[index] == link) return true;
    return false;
}

int CompareText(const char* left, const char* right) noexcept
{
    std::size_t index = 0;
    while (left[index] && right[index] &&
           left[index] == right[index])
        ++index;
    const unsigned char l =
        static_cast<unsigned char>(left[index]);
    const unsigned char r =
        static_cast<unsigned char>(right[index]);
    return l < r ? -1 : l > r ? 1 : 0;
}

bool ReadPropertyNode(
    const AudioSemanticReadContext& context,
    std::uintptr_t link, std::uintptr_t root,
    PropertyNode* output, std::uint64_t* reasons) noexcept
{
    if (!output || link < kAudioPropertyNodeLinkOffset) {
        AddReason(reasons, AudioSemanticReasonTreeNodeInvalid);
        return false;
    }
    std::memset(output, 0, sizeof(*output));
    output->link = link;
    output->node = link - kAudioPropertyNodeLinkOffset;
    if (!ReadMemory(
            context, output->node, output->body,
            sizeof(output->body))) {
        AddReason(reasons, AudioSemanticReasonMemoryUnreadable);
        return false;
    }
    output->left = BodyValue<std::uintptr_t>(
        output->body, kAudioPropertyNodeLeftOffset);
    output->parent = BodyValue<std::uintptr_t>(
        output->body, kAudioPropertyNodeParentOffset);
    output->right = BodyValue<std::uintptr_t>(
        output->body, kAudioPropertyNodeRightOffset);
    output->color = BodyValue<std::uint32_t>(
        output->body, kAudioPropertyNodeColorOffset);
    if (output->color > 1U ||
        (link == root && output->color != 1U)) {
        AddReason(reasons, AudioSemanticReasonTreeNodeInvalid);
        return false;
    }
    if (output->left == link || output->right == link ||
        (output->left && output->left == output->right)) {
        AddReason(reasons, AudioSemanticReasonTreeCycle);
        return false;
    }
    return true;
}

bool dluma_capture_complete_audio_script_property_tree_b0(
    const AudioSemanticReadContext& context,
    const OrderedTreeHeader& tree,
    std::uint32_t allocator_tag,
    std::uint64_t* hash, std::uint32_t* definition_count,
    std::uint32_t* group_count,
    AudioScriptRegistrySemanticSnapshot* diagnostics,
    std::uint64_t* reasons) noexcept
{
    if (!diagnostics) {
        AddReason(reasons, AudioSemanticReasonInvalidArgument);
        return false;
    }
    diagnostics->property_tree_raw_root = tree.root;
    diagnostics->property_tree_raw_count = tree.size;
    diagnostics->property_tree_raw_padding = tree.reserved;
    diagnostics->property_tree_raw_allocator_tag = allocator_tag;
    diagnostics->property_tree_configured_bound =
        static_cast<std::uint32_t>(
            kAudioScriptPropertyTreeCapacity);
    if (tree.size > kAudioScriptPropertyTreeCapacity) {
        diagnostics->property_tree_header_validation_result =
            AudioPropertyTreeHeaderValidationResult::CountOutOfRange;
        AddReason(reasons, AudioSemanticReasonCountOutOfRange);
        return false;
    }
    if ((!tree.root) != (tree.size == 0U)) {
        diagnostics->property_tree_header_validation_result =
            AudioPropertyTreeHeaderValidationResult::RootCountMismatch;
        AddReason(reasons, AudioSemanticReasonHeaderInvalid);
        return false;
    }
    if (tree.reserved != 0U) {
        diagnostics->property_tree_header_validation_result =
            AudioPropertyTreeHeaderValidationResult::PaddingInvalid;
        AddReason(reasons, AudioSemanticReasonHeaderInvalid);
        return false;
    }
    if (allocator_tag != kAudioScriptVectorTag) {
        diagnostics->property_tree_header_validation_result =
            AudioPropertyTreeHeaderValidationResult::AllocatorTagInvalid;
        AddReason(reasons, AudioSemanticReasonHeaderInvalid);
        return false;
    }
    diagnostics->property_tree_header_validation_result =
        AudioPropertyTreeHeaderValidationResult::Valid;
    HashU32(hash, tree.size);
    std::uint32_t output_count = 0;
    std::uintptr_t current = tree.root;
    std::uintptr_t expected_parent = 0;
    const std::uint32_t maximum_steps =
        tree.size <= (UINT32_MAX - 16U) / 8U
            ? tree.size * 8U + 16U
            : UINT32_MAX;
    std::uint32_t structural_steps = 0;
    PropertyNode node = {};
    char previous_name[kAudioSemanticStringCapacity] = {};
    bool have_previous = false;

    while (current) {
        if (++structural_steps > maximum_steps) {
            AddReason(reasons, AudioSemanticReasonTreeCycle);
            return false;
        }
        if (!ReadPropertyNode(
                context, current, tree.root, &node, reasons))
            return false;
        if (node.parent != expected_parent) {
            AddReason(reasons, AudioSemanticReasonTreeNodeInvalid);
            return false;
        }
        if (!node.left) break;
        expected_parent = current;
        current = node.left;
    }

    while (current) {
        if (output_count >= tree.size) {
            AddReason(reasons, AudioSemanticReasonTreeCycle);
            return false;
        }
        const StringHeader name =
            BodyValue<StringHeader>(
                node.body, kAudioPropertyNodeKeyOffset);
        char current_name[kAudioSemanticStringCapacity] = {};
        std::uint32_t name_length = 0;
        if (!ReadHeaderString(
                context, name, false, current_name,
                sizeof(current_name), &name_length, reasons))
            return false;
        if (have_previous &&
            CompareText(previous_name, current_name) >= 0) {
            AddReason(reasons, AudioSemanticReasonTreeOrderInvalid);
            return false;
        }
        std::memcpy(previous_name, current_name, name_length + 1U);
        have_previous = true;
        HashU32(hash, output_count);
        HashU32(hash, name_length);
        HashBytes(hash, current_name, name_length);
        const TaggedVectorHeader values =
            BodyValue<TaggedVectorHeader>(
                node.body, kAudioPropertyNodeValueVectorOffset);
        if (!HashStringVector(
                context, values, hash, definition_count, reasons))
            return false;
        ++output_count;

        if (node.right) {
            expected_parent = current;
            current = node.right;
            for (;;) {
                if (++structural_steps > maximum_steps) {
                    AddReason(reasons, AudioSemanticReasonTreeCycle);
                    return false;
                }
                if (!ReadPropertyNode(
                        context, current, tree.root, &node,
                        reasons))
                    return false;
                if (node.parent != expected_parent) {
                    AddReason(
                        reasons, AudioSemanticReasonTreeNodeInvalid);
                    return false;
                }
                if (!node.left) break;
                expected_parent = current;
                current = node.left;
            }
            continue;
        }

        std::uintptr_t child = current;
        std::uintptr_t parent = node.parent;
        current = 0;
        while (parent) {
            if (++structural_steps > maximum_steps) {
                AddReason(reasons, AudioSemanticReasonTreeCycle);
                return false;
            }
            PropertyNode parent_node = {};
            if (!ReadPropertyNode(
                    context, parent, tree.root, &parent_node,
                    reasons))
                return false;
            if (parent_node.left == child) {
                current = parent;
                node = parent_node;
                break;
            }
            if (parent_node.right != child) {
                AddReason(
                    reasons, AudioSemanticReasonTreeNodeInvalid);
                return false;
            }
            child = parent;
            parent = parent_node.parent;
        }
    }
    if (output_count != tree.size) {
        AddReason(reasons, AudioSemanticReasonTreeCountMismatch);
        return false;
    }
    *group_count = tree.size;
    return true;
}

std::uint32_t ComputeBankNameHash(const char* name,
                                  std::uint32_t length) noexcept
{
    std::uint32_t hash = 0;
    for (std::uint32_t index = 0; index < length; ++index) {
        const std::int32_t byte =
            static_cast<std::int8_t>(name[index]);
        hash = hash * kBankHashMultiplier +
            static_cast<std::uint32_t>(byte);
    }
    return hash;
}

bool NamesEqual(const char* left, const char* right) noexcept
{
    if (!left || !right) return false;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

}  // namespace

bool dluma_compare_fmod_bank_key_in_engine_order(
    std::uint32_t left_bits, std::uint32_t right_bits) noexcept
{
    return static_cast<std::int32_t>(left_bits) <
        static_cast<std::int32_t>(right_bits);
}

bool SnapshotAudioScriptRegistrySemanticUnderLock(
    const AudioSemanticReadContext* context,
    AudioScriptRegistrySemanticSnapshot* output) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!output || !context) return false;
    output->attempted = true;
    output->failure_stage = AudioScriptSemanticStage::ContextValidation;
    if (!ValidContext(context)) {
        AddReason(
            &output->reason_mask,
            context->exact_engine_image_validated
                ? AudioSemanticReasonInvalidArgument
                : AudioSemanticReasonUnsupportedImage);
        return false;
    }
    output->registry_owner =
        context->engine_base + kAudioScriptPathVectorRva;
    std::uint64_t hash =
        BeginHash("audio_script_registry_semantic_v1");

    output->failure_stage = AudioScriptSemanticStage::PathVector;
    TaggedVectorHeader paths = {};
    if (!ReadRva(
            *context, kAudioScriptPathVectorRva, &paths,
            sizeof(paths), &output->reason_mask) ||
        !ValidVectorHeader(
            paths, kAudioScriptVectorCapacity,
            &output->reason_mask))
        return false;
    output->path_count = paths.size;
    HashU32(&hash, paths.size);
    HashU32(&hash, paths.tag);
    for (std::uint32_t index = 0; index < paths.size; ++index) {
        StringHeader path = {};
        const std::size_t offset =
            static_cast<std::size_t>(index) * sizeof(path);
        if (offset / sizeof(path) != index ||
            !ReadAt(
                *context, paths.data, offset, &path,
                sizeof(path))) {
            AddReason(
                &output->reason_mask,
                AudioSemanticReasonMemoryUnreadable);
            return false;
        }
        std::uint32_t length = 0;
        HashU32(&hash, index);
        if (!HashHeaderString(
                *context, path, false, &hash, &length,
                &output->reason_mask))
            return false;
    }

    output->failure_stage = AudioScriptSemanticStage::OnStartProcedure;
    StringHeader on_start = {};
    if (!ReadRva(
            *context, kAudioScriptOnStartRva, &on_start,
            sizeof(on_start), &output->reason_mask))
        return false;
    std::uint32_t on_start_length = 0;
    if (!HashHeaderString(
            *context, on_start, true, &hash, &on_start_length,
            &output->reason_mask))
        return false;
    if (on_start_length &&
        !AddCount(
            1U, &output->definition_count,
            &output->reason_mask))
        return false;

    output->failure_stage =
        AudioScriptSemanticStage::MainDefinitionVector;
    TaggedVectorHeader definitions = {};
    if (!ReadRva(
            *context, kAudioScriptDefinitionsRva, &definitions,
            sizeof(definitions), &output->reason_mask) ||
        !HashDefinitionVector(
            *context, definitions, &hash,
            &output->definition_count, &output->reason_mask))
        return false;

    output->failure_stage = AudioScriptSemanticStage::DefaultProcedure;
    StringHeader default_proc = {};
    if (!ReadRva(
            *context, kAudioScriptDefaultProcRva, &default_proc,
            sizeof(default_proc), &output->reason_mask))
        return false;
    std::uint32_t default_proc_length = 0;
    if (!HashHeaderString(
            *context, default_proc, true, &hash,
            &default_proc_length, &output->reason_mask))
        return false;
    if (default_proc_length &&
        !AddCount(
            1U, &output->definition_count,
            &output->reason_mask))
        return false;

    output->failure_stage =
        AudioScriptSemanticStage::DefaultDefinitionVector;
    TaggedVectorHeader defaults = {};
    if (!ReadRva(
            *context, kAudioScriptDefaultDefinitionsRva,
            &defaults, sizeof(defaults), &output->reason_mask) ||
        !HashDefinitionVector(
            *context, defaults, &hash,
            &output->definition_count, &output->reason_mask))
        return false;

    output->failure_stage = AudioScriptSemanticStage::ProcedureVector;
    TaggedVectorHeader procedures = {};
    if (!ReadRva(
            *context, kAudioScriptProceduresRva, &procedures,
            sizeof(procedures), &output->reason_mask) ||
        !ValidVectorHeader(
            procedures, kAudioScriptVectorCapacity,
            &output->reason_mask))
        return false;
    output->procedure_count = procedures.size;
    HashU32(&hash, procedures.size);
    HashU32(&hash, procedures.tag);
    for (std::uint32_t index = 0;
         index < procedures.size; ++index) {
        unsigned char record[kProcedureRecordSize] = {};
        const std::size_t offset =
            static_cast<std::size_t>(index) * sizeof(record);
        if (offset / sizeof(record) != index ||
            !ReadAt(
                *context, procedures.data, offset, record,
                sizeof(record))) {
            AddReason(
                &output->reason_mask,
                AudioSemanticReasonMemoryUnreadable);
            return false;
        }
        const StringHeader name =
            BodyValue<StringHeader>(record, 0U);
        std::uint32_t name_length = 0;
        HashU32(&hash, index);
        if (!HashHeaderString(
                *context, name, false, &hash, &name_length,
                &output->reason_mask))
            return false;
        const TaggedVectorHeader values =
            BodyValue<TaggedVectorHeader>(record, 0x10U);
        if (!HashDefinitionVector(
                *context, values, &hash,
                &output->definition_count, &output->reason_mask))
            return false;
    }

    output->failure_stage = AudioScriptSemanticStage::PropertyTree;
    OrderedTreeHeader property_tree = {};
    std::uint32_t property_tree_allocator_tag = 0;
    if (!ReadRva(
            *context, kAudioScriptPropertyTreeRva,
            &property_tree, sizeof(property_tree),
            &output->reason_mask) ||
        !ReadRva(
            *context, kAudioScriptPropertyTreeAllocatorTagRva,
            &property_tree_allocator_tag,
            sizeof(property_tree_allocator_tag),
            &output->reason_mask) ||
        !dluma_capture_complete_audio_script_property_tree_b0(
            *context, property_tree, property_tree_allocator_tag,
            &hash,
            &output->definition_count,
            &output->property_group_count,
            output,
            &output->reason_mask))
        return false;

    output->failure_stage =
        AudioScriptSemanticStage::FinalSemanticFingerprint;
    HashU32(&hash, output->path_count);
    HashU32(&hash, output->definition_count);
    HashU32(&hash, output->procedure_count);
    HashU32(&hash, output->property_group_count);
    output->semantic_fingerprint = NonzeroHash(hash);
    output->complete = true;
    output->failure_stage = AudioScriptSemanticStage::Complete;
    return true;
}

bool SnapshotAudioBankTreeSemanticUnderLock(
    const AudioSemanticReadContext* context,
    std::uintptr_t manager,
    const char* requested_bank_name,
    AudioBankTreeSemanticSnapshot* output) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!output || !context || !manager)
        return false;
    output->attempted = true;
    output->failure_stage = AudioBankSemanticStage::ContextValidation;
    if (!ValidContext(context)) {
        AddReason(
            &output->reason_mask,
            context->exact_engine_image_validated
                ? AudioSemanticReasonInvalidArgument
                : AudioSemanticReasonUnsupportedImage);
        return false;
    }
    const bool match_requested =
        requested_bank_name && requested_bank_name[0];
    std::size_t requested_length = 0;
    while (match_requested &&
           requested_bank_name[requested_length] &&
           requested_length < kAudioSemanticStringCapacity)
        ++requested_length;
    if (match_requested &&
        requested_length >= kAudioSemanticStringCapacity) {
        AddReason(
            &output->reason_mask,
            AudioSemanticReasonStringInvalid);
        return false;
    }
    output->manager = manager;
    output->failure_stage = AudioBankSemanticStage::TreeHeader;
    if (!ReadAt(
            *context, manager, kAudioBankTreeRootOffset,
            &output->tree_root, sizeof(output->tree_root)) ||
        !ReadAt(
            *context, manager, kAudioBankTreeCountOffset,
            &output->bank_count, sizeof(output->bank_count))) {
        AddReason(
            &output->reason_mask,
            AudioSemanticReasonMemoryUnreadable);
        return false;
    }
    if (output->bank_count > kAudioBankTreeCapacity) {
        AddReason(
            &output->reason_mask,
            AudioSemanticReasonCountOutOfRange);
        return false;
    }
    if ((!output->tree_root) != (output->bank_count == 0U)) {
        AddReason(
            &output->reason_mask,
            AudioSemanticReasonHeaderInvalid);
        return false;
    }

    output->failure_stage = AudioBankSemanticStage::TreeTraversal;
    BankFrame stack[kAudioBankTreeCapacity] = {};
    std::uintptr_t visited[kAudioBankTreeCapacity] = {};
    std::uint32_t depth = 0;
    std::uint32_t visited_count = 0;
    std::uint32_t output_count = 0;
    std::uint32_t previous_key_bits = 0;
    bool have_previous_key = false;
    std::uintptr_t current = output->tree_root;
    std::uintptr_t expected_parent = 0;
    std::uint64_t semantic =
        BeginHash("audio_bank_tree_semantic_v1");
    std::uint64_t identities =
        BeginHash("audio_bank_tree_identity_v1");
    HashU32(&semantic, output->bank_count);
    HashU32(&identities, output->bank_count);

    while (current || depth) {
        while (current) {
            if (current < kAudioBankNodeLinkOffset ||
                SeenLink(visited, visited_count, current)) {
                AddReason(
                    &output->reason_mask,
                    AudioSemanticReasonTreeCycle);
                return false;
            }
            if (visited_count >= output->bank_count ||
                depth >= kAudioBankTreeCapacity) {
                AddReason(
                    &output->reason_mask,
                    AudioSemanticReasonTreeCountMismatch);
                return false;
            }
            BankFrame frame = {};
            frame.link = current;
            const std::uintptr_t node =
                current - kAudioBankNodeLinkOffset;
            if (!ReadMemory(
                    *context, node, frame.body,
                    sizeof(frame.body))) {
                AddReason(
                    &output->reason_mask,
                    AudioSemanticReasonMemoryUnreadable);
                return false;
            }
            const std::uintptr_t parent =
                BodyValue<std::uintptr_t>(
                    frame.body, kAudioBankNodeParentOffset);
            if (parent != expected_parent) {
                AddReason(
                    &output->reason_mask,
                    AudioSemanticReasonTreeNodeInvalid);
                return false;
            }
            frame.right =
                BodyValue<std::uintptr_t>(
                    frame.body, kAudioBankNodeRightOffset);
            visited[visited_count++] = current;
            stack[depth++] = frame;
            expected_parent = current;
            current = BodyValue<std::uintptr_t>(
                frame.body, kAudioBankNodeLeftOffset);
        }

        const BankFrame frame = stack[--depth];
        const std::uintptr_t node =
            frame.link - kAudioBankNodeLinkOffset;
        const StringHeader name =
            BodyValue<StringHeader>(
                frame.body, kAudioBankNodeNameOffset);
        char bank_name[kAudioSemanticStringCapacity] = {};
        std::uint32_t name_length = 0;
        if (!ReadHeaderString(
                *context, name, false, bank_name,
                sizeof(bank_name), &name_length,
                &output->reason_mask))
            return false;
        const std::uint32_t tree_key =
            BodyValue<std::uint32_t>(
                frame.body, kAudioBankNodeHashOffset);
        const std::uint32_t record_hash =
            BodyValue<std::uint32_t>(
                frame.body, kAudioBankNodeRecordHashOffset);
        const std::uintptr_t native_file =
            BodyValue<std::uintptr_t>(
                frame.body, kAudioBankNodeNativeFileOffset);
        const std::uint8_t localised =
            BodyValue<std::uint8_t>(
                frame.body, kAudioBankNodeLocalisedOffset);
        const std::uint32_t color =
            BodyValue<std::uint32_t>(
                frame.body, kAudioBankNodeColorOffset);
        if (!native_file || localised > 1U || color > 1U ||
            (frame.link == output->tree_root && color != 1U)) {
            AddReason(
                &output->reason_mask,
                AudioSemanticReasonTreeNodeInvalid);
            return false;
        }
        const std::uint32_t recomputed_hash =
            ComputeBankNameHash(bank_name, name_length);
        if (output->diagnostic_node_count <
            kAudioBankNodeDiagnosticCapacity) {
            AudioBankNodeSemanticDiagnostic& diagnostic =
                output->node_diagnostics[
                    output->diagnostic_node_count++];
            diagnostic.in_order_ordinal = output_count;
            diagnostic.node = node;
            diagnostic.link = frame.link;
            diagnostic.left = BodyValue<std::uintptr_t>(
                frame.body, kAudioBankNodeLeftOffset);
            diagnostic.parent = BodyValue<std::uintptr_t>(
                frame.body, kAudioBankNodeParentOffset);
            diagnostic.right = BodyValue<std::uintptr_t>(
                frame.body, kAudioBankNodeRightOffset);
            diagnostic.native_file = native_file;
            diagnostic.color = color;
            diagnostic.raw_key_bits = tree_key;
            diagnostic.signed_key_value =
                static_cast<std::int32_t>(tree_key);
            diagnostic.recomputed_hash_bits = recomputed_hash;
            diagnostic.record_hash = record_hash;
            diagnostic.name_length = name_length;
            diagnostic.localised = localised;
            const std::size_t copy_length =
                name_length <
                        kAudioBankNodeDiagnosticNameCapacity - 1U
                    ? name_length
                    : kAudioBankNodeDiagnosticNameCapacity - 1U;
            std::memcpy(
                diagnostic.name, bank_name, copy_length);
            diagnostic.name[copy_length] = 0;
        } else {
            output->node_diagnostics_truncated = true;
        }
        if (recomputed_hash != tree_key) {
            AddReason(
                &output->reason_mask,
                AudioSemanticReasonBankHashMismatch);
            return false;
        }
        if (have_previous_key &&
            !dluma_compare_fmod_bank_key_in_engine_order(
                previous_key_bits, tree_key)) {
            AddReason(
                &output->reason_mask,
                AudioSemanticReasonTreeOrderInvalid);
            return false;
        }
        have_previous_key = true;
        previous_key_bits = tree_key;
        HashU32(&semantic, output_count);
        HashU32(&semantic, tree_key);
        HashU32(&semantic, record_hash);
        HashU32(&semantic, name_length);
        HashBytes(&semantic, bank_name, name_length);
        HashU8(&semantic, localised);
        HashU32(&semantic, color);
        HashU32(&identities, output_count);
        HashU64(&identities, node);
        HashU64(&identities, native_file);
        if (match_requested &&
            NamesEqual(bank_name, requested_bank_name)) {
            ++output->matching_node_count;
            output->matching_node = node;
            output->matching_native_file = native_file;
        }
        ++output_count;
        expected_parent = frame.link;
        current = frame.right;
    }
    if (visited_count != output->bank_count ||
        output_count != output->bank_count ||
        output->matching_node_count > 1U) {
        AddReason(
            &output->reason_mask,
            AudioSemanticReasonTreeCountMismatch);
        return false;
    }
    output->failure_stage =
        AudioBankSemanticStage::FinalSemanticFingerprint;
    output->semantic_fingerprint = NonzeroHash(semantic);
    output->identity_fingerprint = NonzeroHash(identities);
    output->complete = true;
    output->failure_stage = AudioBankSemanticStage::Complete;
    return true;
}

const char* AudioScriptSemanticStageName(
    AudioScriptSemanticStage stage) noexcept
{
    switch (stage) {
    case AudioScriptSemanticStage::NotAttempted:
        return "not_attempted";
    case AudioScriptSemanticStage::ContextValidation:
        return "context_validation";
    case AudioScriptSemanticStage::PathVector:
        return "path_vector";
    case AudioScriptSemanticStage::OnStartProcedure:
        return "on_start_procedure";
    case AudioScriptSemanticStage::MainDefinitionVector:
        return "main_definition_vector";
    case AudioScriptSemanticStage::DefaultProcedure:
        return "default_procedure";
    case AudioScriptSemanticStage::DefaultDefinitionVector:
        return "default_definition_vector";
    case AudioScriptSemanticStage::ProcedureVector:
        return "procedure_vector";
    case AudioScriptSemanticStage::PropertyTree:
        return "property_tree";
    case AudioScriptSemanticStage::FinalSemanticFingerprint:
        return "final_semantic_fingerprint";
    case AudioScriptSemanticStage::Complete:
        return "complete";
    default:
        return "unknown";
    }
}

const char* AudioBankSemanticStageName(
    AudioBankSemanticStage stage) noexcept
{
    switch (stage) {
    case AudioBankSemanticStage::NotAttempted:
        return "not_attempted";
    case AudioBankSemanticStage::ContextValidation:
        return "context_validation";
    case AudioBankSemanticStage::TreeHeader:
        return "tree_header";
    case AudioBankSemanticStage::TreeTraversal:
        return "tree_traversal";
    case AudioBankSemanticStage::FinalSemanticFingerprint:
        return "final_semantic_fingerprint";
    case AudioBankSemanticStage::Complete:
        return "complete";
    default:
        return "unknown";
    }
}

const char* AudioPropertyTreeHeaderValidationResultName(
    AudioPropertyTreeHeaderValidationResult result) noexcept
{
    switch (result) {
    case AudioPropertyTreeHeaderValidationResult::NotAttempted:
        return "not_attempted";
    case AudioPropertyTreeHeaderValidationResult::Valid:
        return "valid";
    case AudioPropertyTreeHeaderValidationResult::RootCountMismatch:
        return "root_count_mismatch";
    case AudioPropertyTreeHeaderValidationResult::CountOutOfRange:
        return "count_out_of_range";
    case AudioPropertyTreeHeaderValidationResult::PaddingInvalid:
        return "padding_invalid";
    case AudioPropertyTreeHeaderValidationResult::AllocatorTagInvalid:
        return "allocator_tag_invalid";
    default:
        return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot
