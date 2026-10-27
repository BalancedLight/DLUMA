#include "fixes/custom_map_asset_isolation/AudioSemanticDelta.h"
#include "fixes/custom_map_asset_isolation/AudioSemanticWindowsSnapshot.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace semantic =
    dluma::fixes::custom_map_asset_isolation::audio_semantic;
namespace snapshot =
    dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot;

namespace {

constexpr std::uintptr_t kBase = 0x0000000180000000ULL;
constexpr std::size_t kMemorySize = 0x00D00000U;
constexpr std::size_t kAllocationStart = 0x00B00000U;
constexpr std::uint32_t kStockPropertyGroupCount = 1382U;
unsigned char g_memory[kMemorySize];
std::size_t g_next_allocation = kAllocationStart;
std::uintptr_t g_property_nodes[kStockPropertyGroupCount];
int g_failures;
int g_checks;

#define CHECK(expression)                                                   \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(expression)) {                                                \
            ++g_failures;                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #expression);      \
        }                                                                   \
    } while (0)

struct StringHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

StringHeader g_property_names[kStockPropertyGroupCount];

struct TaggedVectorHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
    std::uint32_t tag;
    std::uint32_t reserved;
};

struct OrderedTreeHeader {
    std::uintptr_t root;
    std::uint32_t size;
    std::uint32_t reserved;
};

bool FakeRead(void*, std::uintptr_t address, void* output,
              std::size_t size) noexcept
{
    if (!output || address < kBase) return false;
    const std::uintptr_t offset = address - kBase;
    if (offset > kMemorySize ||
        size > kMemorySize - static_cast<std::size_t>(offset))
        return false;
    std::memcpy(output, g_memory + offset, size);
    return true;
}

bool WriteAbsolute(std::uintptr_t address, const void* value,
                   std::size_t size) noexcept
{
    if (!value || address < kBase) return false;
    const std::uintptr_t offset = address - kBase;
    if (offset > kMemorySize ||
        size > kMemorySize - static_cast<std::size_t>(offset))
        return false;
    std::memcpy(g_memory + offset, value, size);
    return true;
}

template <typename T>
bool WriteRva(std::uintptr_t rva, const T& value) noexcept
{
    return WriteAbsolute(kBase + rva, &value, sizeof(value));
}

template <typename T>
T Load(const unsigned char* memory, std::size_t offset) noexcept
{
    T value = {};
    if (!memory || offset > kMemorySize ||
        sizeof(value) > kMemorySize - offset)
        return value;
    std::memcpy(&value, memory + offset, sizeof(value));
    return value;
}

std::uintptr_t Allocate(std::size_t size) noexcept
{
    const std::size_t aligned = (g_next_allocation + 15U) & ~15U;
    if (aligned > kMemorySize || size > kMemorySize - aligned)
        return 0;
    g_next_allocation = aligned + size;
    return kBase + aligned;
}

StringHeader AllocateString(const char* text) noexcept
{
    StringHeader result = {};
    if (!text) return result;
    std::size_t length = 0;
    while (text[length]) ++length;
    result.data = Allocate(length + 1U);
    result.size = static_cast<std::uint32_t>(length);
    result.capacity = result.size;
    if (!result.data ||
        !WriteAbsolute(result.data, text, length + 1U))
        std::memset(&result, 0, sizeof(result));
    return result;
}

void InitializeEmptyScriptRegistry() noexcept
{
    const TaggedVectorHeader empty = {
        0, 0, 0, snapshot::kAudioScriptVectorTag, 0};
    const StringHeader empty_string = {};
    CHECK(WriteRva(snapshot::kAudioScriptPathVectorRva, empty));
    CHECK(WriteRva(snapshot::kAudioScriptOnStartRva, empty_string));
    CHECK(WriteRva(snapshot::kAudioScriptDefinitionsRva, empty));
    CHECK(WriteRva(snapshot::kAudioScriptDefaultProcRva, empty_string));
    CHECK(WriteRva(
        snapshot::kAudioScriptDefaultDefinitionsRva, empty));
    CHECK(WriteRva(snapshot::kAudioScriptProceduresRva, empty));
    const OrderedTreeHeader empty_tree = {};
    CHECK(WriteRva(
        snapshot::kAudioScriptPropertyTreeRva, empty_tree));
    const std::uint32_t allocator_tag =
        snapshot::kAudioScriptVectorTag;
    CHECK(WriteRva(
        snapshot::kAudioScriptPropertyTreeAllocatorTagRva,
        allocator_tag));
}

std::uint32_t BankHash(const char* text) noexcept
{
    std::uint32_t hash = 0;
    for (std::size_t index = 0; text[index]; ++index) {
        const std::int32_t byte =
            static_cast<std::int8_t>(text[index]);
        hash = hash * 0x29U + static_cast<std::uint32_t>(byte);
    }
    return hash;
}

bool HasReason(
    std::uint64_t mask,
    snapshot::AudioSemanticSnapshotReason reason) noexcept
{
    return (mask & static_cast<std::uint64_t>(reason)) != 0U;
}

bool WirePropertySubtree(
    std::uint32_t begin, std::uint32_t end,
    std::uintptr_t parent, std::uintptr_t* link_out) noexcept
{
    if (!link_out || end > kStockPropertyGroupCount || begin > end)
        return false;
    if (begin == end) {
        *link_out = 0;
        return true;
    }
    const std::uint32_t middle = begin + (end - begin) / 2U;
    const std::uintptr_t node = g_property_nodes[middle];
    const std::uintptr_t link =
        node + snapshot::kAudioPropertyNodeLinkOffset;
    std::uintptr_t left = 0;
    std::uintptr_t right = 0;
    if (!node ||
        !WirePropertySubtree(begin, middle, link, &left) ||
        !WirePropertySubtree(middle + 1U, end, link, &right))
        return false;
    const TaggedVectorHeader empty_values = {
        0, 0, 0, snapshot::kAudioScriptVectorTag, 0};
    const std::uint32_t black = 1U;
    if (!WriteAbsolute(
            node + snapshot::kAudioPropertyNodeKeyOffset,
            &g_property_names[middle],
            sizeof(g_property_names[middle])) ||
        !WriteAbsolute(
            node + snapshot::kAudioPropertyNodeValueVectorOffset,
            &empty_values, sizeof(empty_values)) ||
        !WriteAbsolute(
            node + snapshot::kAudioPropertyNodeLeftOffset,
            &left, sizeof(left)) ||
        !WriteAbsolute(
            node + snapshot::kAudioPropertyNodeParentOffset,
            &parent, sizeof(parent)) ||
        !WriteAbsolute(
            node + snapshot::kAudioPropertyNodeRightOffset,
            &right, sizeof(right)) ||
        !WriteAbsolute(
            node + snapshot::kAudioPropertyNodeColorOffset,
            &black, sizeof(black)))
        return false;
    *link_out = link;
    return true;
}

bool InitializePropertyTree(std::uint32_t count) noexcept
{
    if (!count || count > kStockPropertyGroupCount) return false;
    for (std::uint32_t index = 0; index < count; ++index) {
        g_property_nodes[index] =
            Allocate(snapshot::kAudioPropertyNodeReadableSize);
        char name[32] = {};
        const int length = std::snprintf(
            name, sizeof(name), "group_%04u", index);
        if (!g_property_nodes[index] || length <= 0 ||
            static_cast<std::size_t>(length) >= sizeof(name))
            return false;
        g_property_names[index] = AllocateString(name);
        if (!g_property_names[index].data) return false;
    }
    std::uintptr_t root = 0;
    if (!WirePropertySubtree(0U, count, 0U, &root))
        return false;
    const OrderedTreeHeader header = {root, count, 0U};
    const std::uint32_t allocator_tag =
        snapshot::kAudioScriptVectorTag;
    return WriteRva(
               snapshot::kAudioScriptPropertyTreeRva, header) &&
        WriteRva(
               snapshot::kAudioScriptPropertyTreeAllocatorTagRva,
               allocator_tag);
}

bool WriteBankNode(
    std::uintptr_t node, const char* name,
    std::uint32_t key, std::uint32_t record_hash,
    std::uintptr_t native_file, std::uintptr_t left,
    std::uintptr_t parent, std::uintptr_t right,
    std::uint32_t color) noexcept
{
    const StringHeader header = AllocateString(name);
    const std::uint8_t localised = 0U;
    return node && header.data &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeHashOffset,
            &key, sizeof(key)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeNameOffset,
            &header, sizeof(header)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeNativeFileOffset,
            &native_file, sizeof(native_file)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeRecordHashOffset,
            &record_hash, sizeof(record_hash)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeLocalisedOffset,
            &localised, sizeof(localised)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeLeftOffset,
            &left, sizeof(left)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeParentOffset,
            &parent, sizeof(parent)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeRightOffset,
            &right, sizeof(right)) &&
        WriteAbsolute(
            node + snapshot::kAudioBankNodeColorOffset,
            &color, sizeof(color));
}

snapshot::AudioSemanticReadContext ReadContext() noexcept
{
    snapshot::AudioSemanticReadContext context = {};
    context.engine_base = kBase;
    context.read_memory = &FakeRead;
    context.exact_engine_image_validated = true;
    context.caller_owns_audio_lock = true;
    return context;
}

snapshot::AudioScriptRegistrySemanticSnapshot ScriptState(
    std::uint64_t fingerprint) noexcept
{
    snapshot::AudioScriptRegistrySemanticSnapshot state = {};
    state.registry_owner = 0x11110000U;
    state.semantic_fingerprint = fingerprint;
    state.definition_count = 10;
    state.path_count = 2;
    state.procedure_count = 1;
    state.property_group_count = 1;
    state.complete = true;
    return state;
}

snapshot::AudioBankTreeSemanticSnapshot BankState(
    std::uint64_t semantic_fingerprint,
    std::uint64_t identity_fingerprint) noexcept
{
    snapshot::AudioBankTreeSemanticSnapshot state = {};
    state.manager = 0x22220000U;
    state.semantic_fingerprint = semantic_fingerprint;
    state.identity_fingerprint = identity_fingerprint;
    state.complete = true;
    return state;
}

semantic::AudioSemanticScriptObservation ScriptObservation(
    std::uint32_t epoch, std::uint64_t before,
    std::uint64_t after) noexcept
{
    semantic::AudioSemanticScriptObservation observation = {};
    observation.epoch = epoch;
    observation.source = 0x33330000U;
    observation.before = ScriptState(before);
    observation.after = ScriptState(after);
    observation.exact_source = true;
    observation.logical_path_complete = true;
    observation.parse_result_observed = true;
    observation.parse_succeeded = true;
    std::strcpy(
        observation.logical_path, "audiodata_workshop.scr");
    return observation;
}

semantic::AudioSemanticBankObservation FailedBankObservation(
    std::uint32_t epoch, std::uint64_t before,
    std::uint64_t after) noexcept
{
    semantic::AudioSemanticBankObservation observation = {};
    observation.epoch = epoch;
    observation.source = 0x33330000U;
    observation.before = BankState(before, 0xABCDU);
    observation.after = BankState(after, 0xABCDU);
    observation.result = -1;
    observation.exact_source = true;
    observation.bank_name_complete = true;
    observation.result_observed = true;
    observation.reference_count_delta_observed = true;
    observation.reference_count_delta = 0;
    observation.failed_lookup_no_retained_state_proven = true;
    std::strcpy(observation.bank_name, "workshop_audio");
    return observation;
}

void TestWindowsSemanticSnapshots() noexcept
{
    std::memset(g_memory, 0, sizeof(g_memory));
    g_next_allocation = kAllocationStart;
    InitializeEmptyScriptRegistry();
    const snapshot::AudioSemanticReadContext context = ReadContext();
    snapshot::AudioScriptRegistrySemanticSnapshot empty = {};
    CHECK(snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &empty));
    CHECK(empty.attempted);
    CHECK(!empty.skipped);
    CHECK(empty.complete);
    CHECK(empty.failure_stage ==
          snapshot::AudioScriptSemanticStage::Complete);
    CHECK(empty.definition_count == 0U);
    CHECK(empty.path_count == 0U);
    CHECK(empty.property_tree_raw_root == 0U);
    CHECK(empty.property_tree_raw_count == 0U);
    CHECK(empty.property_tree_raw_padding == 0U);
    CHECK(empty.property_tree_raw_allocator_tag ==
          snapshot::kAudioScriptVectorTag);
    CHECK(empty.property_tree_configured_bound ==
          snapshot::kAudioScriptPropertyTreeCapacity);
    CHECK(empty.property_tree_header_validation_result ==
          snapshot::AudioPropertyTreeHeaderValidationResult::Valid);
    CHECK(empty.semantic_fingerprint != 0U);

    const StringHeader on_start = AllocateString("boot");
    CHECK(on_start.data != 0U);
    CHECK(WriteRva(snapshot::kAudioScriptOnStartRva, on_start));
    snapshot::AudioScriptRegistrySemanticSnapshot changed = {};
    CHECK(snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &changed));
    CHECK(changed.complete);
    CHECK(changed.definition_count == 1U);
    CHECK(changed.semantic_fingerprint !=
          empty.semantic_fingerprint);

    const OrderedTreeHeader invalid_property_tree = {0, 1U, 0};
    CHECK(WriteRva(
        snapshot::kAudioScriptPropertyTreeRva,
        invalid_property_tree));
    snapshot::AudioScriptRegistrySemanticSnapshot property_failure = {};
    CHECK(!snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &property_failure));
    CHECK(property_failure.attempted);
    CHECK(!property_failure.skipped);
    CHECK(!property_failure.complete);
    CHECK(property_failure.failure_stage ==
          snapshot::AudioScriptSemanticStage::PropertyTree);
    CHECK(property_failure.reason_mask != 0U);
    CHECK(property_failure.definition_count == 1U);
    CHECK(property_failure.path_count == 0U);
    CHECK(property_failure.procedure_count == 0U);
    CHECK(property_failure.semantic_fingerprint == 0U);

    const std::uintptr_t manager = Allocate(16U);
    CHECK(manager != 0U);
    snapshot::AudioBankTreeSemanticSnapshot empty_banks = {};
    CHECK(snapshot::SnapshotAudioBankTreeSemanticUnderLock(
        &context, manager, "workshop_audio", &empty_banks));
    CHECK(empty_banks.attempted);
    CHECK(!empty_banks.skipped);
    CHECK(empty_banks.complete);
    CHECK(empty_banks.failure_stage ==
          snapshot::AudioBankSemanticStage::Complete);
    CHECK(empty_banks.bank_count == 0U);
    CHECK(empty_banks.matching_node_count == 0U);

    const std::uintptr_t node =
        Allocate(snapshot::kAudioBankNodeReadableSize);
    const StringHeader bank_name = AllocateString("workshop_audio");
    CHECK(node != 0U);
    CHECK(bank_name.data != 0U);
    const std::uint32_t tree_key = BankHash("workshop_audio");
    const std::uint32_t record_hash = 0x12345678U;
    const std::uintptr_t native_file = 0x0000000180ABC000ULL;
    const std::uint32_t color = 1U;
    CHECK(WriteAbsolute(
        node + snapshot::kAudioBankNodeHashOffset,
        &tree_key, sizeof(tree_key)));
    CHECK(WriteAbsolute(
        node + snapshot::kAudioBankNodeNameOffset,
        &bank_name, sizeof(bank_name)));
    CHECK(WriteAbsolute(
        node + snapshot::kAudioBankNodeNativeFileOffset,
        &native_file, sizeof(native_file)));
    CHECK(WriteAbsolute(
        node + snapshot::kAudioBankNodeRecordHashOffset,
        &record_hash, sizeof(record_hash)));
    CHECK(WriteAbsolute(
        node + snapshot::kAudioBankNodeColorOffset,
        &color, sizeof(color)));
    const std::uintptr_t root =
        node + snapshot::kAudioBankNodeLinkOffset;
    const std::uint32_t count = 1U;
    CHECK(WriteAbsolute(
        manager + snapshot::kAudioBankTreeRootOffset,
        &root, sizeof(root)));
    CHECK(WriteAbsolute(
        manager + snapshot::kAudioBankTreeCountOffset,
        &count, sizeof(count)));
    snapshot::AudioBankTreeSemanticSnapshot one_bank = {};
    CHECK(snapshot::SnapshotAudioBankTreeSemanticUnderLock(
        &context, manager, "workshop_audio", &one_bank));
    CHECK(one_bank.complete);
    CHECK(one_bank.bank_count == 1U);
    CHECK(one_bank.matching_node_count == 1U);
    CHECK(one_bank.matching_node == node);
    CHECK(one_bank.matching_native_file == native_file);
    CHECK(one_bank.diagnostic_node_count == 1U);
    CHECK(one_bank.node_diagnostics[0].raw_key_bits == tree_key);
    CHECK(one_bank.node_diagnostics[0].recomputed_hash_bits == tree_key);
    CHECK(std::strcmp(
              one_bank.node_diagnostics[0].name,
              "workshop_audio") == 0);
    CHECK(one_bank.semantic_fingerprint !=
          empty_banks.semantic_fingerprint);
}

void TestCompleteStockPropertyTreeAndCorruption() noexcept
{
    std::memset(g_memory, 0, sizeof(g_memory));
    g_next_allocation = kAllocationStart;
    InitializeEmptyScriptRegistry();
    CHECK(InitializePropertyTree(kStockPropertyGroupCount));
    const snapshot::AudioSemanticReadContext context = ReadContext();
    snapshot::AudioScriptRegistrySemanticSnapshot first = {};
    snapshot::AudioScriptRegistrySemanticSnapshot second = {};
    CHECK(snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &first));
    CHECK(snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &second));
    CHECK(first.complete);
    CHECK(second.complete);
    CHECK(first.property_group_count == kStockPropertyGroupCount);
    CHECK(first.property_tree_raw_count == kStockPropertyGroupCount);
    CHECK(first.property_tree_raw_root != 0U);
    CHECK(first.property_tree_raw_padding == 0U);
    CHECK(first.property_tree_raw_allocator_tag ==
          snapshot::kAudioScriptVectorTag);
    CHECK(first.property_tree_configured_bound == 4096U);
    CHECK(first.property_tree_header_validation_result ==
          snapshot::AudioPropertyTreeHeaderValidationResult::Valid);
    CHECK(first.semantic_fingerprint != 0U);
    CHECK(first.semantic_fingerprint == second.semantic_fingerprint);

    OrderedTreeHeader above_bound = {
        first.property_tree_raw_root,
        static_cast<std::uint32_t>(
            snapshot::kAudioScriptPropertyTreeCapacity + 1U),
        0U};
    CHECK(WriteRva(
        snapshot::kAudioScriptPropertyTreeRva, above_bound));
    snapshot::AudioScriptRegistrySemanticSnapshot failed = {};
    CHECK(!snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &failed));
    CHECK(failed.property_tree_header_validation_result ==
          snapshot::AudioPropertyTreeHeaderValidationResult::
              CountOutOfRange);
    CHECK(HasReason(
        failed.reason_mask,
        snapshot::AudioSemanticReasonCountOutOfRange));

    CHECK(InitializePropertyTree(kStockPropertyGroupCount));
    const std::uintptr_t root =
        Load<std::uintptr_t>(
            g_memory,
            snapshot::kAudioScriptPropertyTreeRva);
    const std::uintptr_t root_node =
        root - snapshot::kAudioPropertyNodeLinkOffset;
    const std::uintptr_t root_left =
        Load<std::uintptr_t>(
            g_memory,
            static_cast<std::size_t>(
                root_node - kBase +
                snapshot::kAudioPropertyNodeLeftOffset));
    const std::uintptr_t no_parent = 0;
    CHECK(root_left != 0U);
    CHECK(WriteAbsolute(
        root_left - snapshot::kAudioPropertyNodeLinkOffset +
            snapshot::kAudioPropertyNodeParentOffset,
        &no_parent, sizeof(no_parent)));
    CHECK(!snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &failed));
    CHECK(HasReason(
        failed.reason_mask,
        snapshot::AudioSemanticReasonTreeNodeInvalid));

    CHECK(InitializePropertyTree(kStockPropertyGroupCount));
    const std::uintptr_t color_root =
        Load<std::uintptr_t>(
            g_memory,
            snapshot::kAudioScriptPropertyTreeRva);
    const std::uint32_t red = 0U;
    CHECK(WriteAbsolute(
        color_root - snapshot::kAudioPropertyNodeLinkOffset +
            snapshot::kAudioPropertyNodeColorOffset,
        &red, sizeof(red)));
    CHECK(!snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &failed));
    CHECK(HasReason(
        failed.reason_mask,
        snapshot::AudioSemanticReasonTreeNodeInvalid));

    CHECK(InitializePropertyTree(kStockPropertyGroupCount));
    const std::uintptr_t cycle_root =
        Load<std::uintptr_t>(
            g_memory,
            snapshot::kAudioScriptPropertyTreeRva);
    CHECK(WriteAbsolute(
        cycle_root - snapshot::kAudioPropertyNodeLinkOffset +
            snapshot::kAudioPropertyNodeLeftOffset,
        &cycle_root, sizeof(cycle_root)));
    CHECK(!snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &failed));
    CHECK(HasReason(
        failed.reason_mask,
        snapshot::AudioSemanticReasonTreeCycle));

    CHECK(InitializePropertyTree(kStockPropertyGroupCount));
    OrderedTreeHeader count_mismatch = {
        Load<std::uintptr_t>(
            g_memory,
            snapshot::kAudioScriptPropertyTreeRva),
        kStockPropertyGroupCount + 1U, 0U};
    CHECK(WriteRva(
        snapshot::kAudioScriptPropertyTreeRva, count_mismatch));
    CHECK(!snapshot::SnapshotAudioScriptRegistrySemanticUnderLock(
        &context, &failed));
    CHECK(HasReason(
        failed.reason_mask,
        snapshot::AudioSemanticReasonTreeCountMismatch));
}

void TestSignedFmodBankOrdering() noexcept
{
    std::memset(g_memory, 0, sizeof(g_memory));
    g_next_allocation = kAllocationStart;
    char negative_name[32] = {};
    char positive_name[32] = {};
    std::uint32_t negative_bits = 0U;
    std::uint32_t positive_bits = 0U;
    for (std::uint32_t index = 0;
         index < 100000U &&
         (!negative_name[0] || !positive_name[0]); ++index) {
        char candidate[32] = {};
        const int length = std::snprintf(
            candidate, sizeof(candidate), "bank_%u", index);
        if (length <= 0 ||
            static_cast<std::size_t>(length) >= sizeof(candidate))
            continue;
        const std::uint32_t bits = BankHash(candidate);
        if (static_cast<std::int32_t>(bits) < 0 &&
            !negative_name[0]) {
            std::strcpy(negative_name, candidate);
            negative_bits = bits;
        } else if (static_cast<std::int32_t>(bits) >= 0 &&
                   !positive_name[0]) {
            std::strcpy(positive_name, candidate);
            positive_bits = bits;
        }
    }
    CHECK(negative_name[0] != 0);
    CHECK(positive_name[0] != 0);
    CHECK(static_cast<std::int32_t>(negative_bits) < 0);
    CHECK(static_cast<std::int32_t>(positive_bits) >= 0);
    CHECK(!(negative_bits < positive_bits));
    CHECK(snapshot::dluma_compare_fmod_bank_key_in_engine_order(
        negative_bits, positive_bits));
    CHECK(!snapshot::dluma_compare_fmod_bank_key_in_engine_order(
        positive_bits, negative_bits));

    const std::uintptr_t manager = Allocate(16U);
    const std::uintptr_t negative_node =
        Allocate(snapshot::kAudioBankNodeReadableSize);
    const std::uintptr_t positive_node =
        Allocate(snapshot::kAudioBankNodeReadableSize);
    const std::uintptr_t negative_link =
        negative_node + snapshot::kAudioBankNodeLinkOffset;
    const std::uintptr_t positive_link =
        positive_node + snapshot::kAudioBankNodeLinkOffset;
    CHECK(WriteBankNode(
        negative_node, negative_name, negative_bits,
        0x10101010U, 0x0000000180A00000ULL,
        0U, positive_link, 0U, 0U));
    CHECK(WriteBankNode(
        positive_node, positive_name, positive_bits,
        0x20202020U, 0x0000000180B00000ULL,
        negative_link, 0U, 0U, 1U));
    const std::uint32_t count = 2U;
    CHECK(WriteAbsolute(
        manager + snapshot::kAudioBankTreeRootOffset,
        &positive_link, sizeof(positive_link)));
    CHECK(WriteAbsolute(
        manager + snapshot::kAudioBankTreeCountOffset,
        &count, sizeof(count)));
    const snapshot::AudioSemanticReadContext context = ReadContext();
    snapshot::AudioBankTreeSemanticSnapshot first = {};
    snapshot::AudioBankTreeSemanticSnapshot second = {};
    CHECK(snapshot::SnapshotAudioBankTreeSemanticUnderLock(
        &context, manager, negative_name, &first));
    CHECK(snapshot::SnapshotAudioBankTreeSemanticUnderLock(
        &context, manager, negative_name, &second));
    CHECK(first.complete);
    CHECK(second.complete);
    CHECK(first.bank_count == 2U);
    CHECK(first.matching_node_count == 1U);
    CHECK(first.matching_node == negative_node);
    CHECK(first.diagnostic_node_count == 2U);
    CHECK(first.node_diagnostics[0].raw_key_bits == negative_bits);
    CHECK(first.node_diagnostics[0].signed_key_value < 0);
    CHECK(first.node_diagnostics[1].raw_key_bits == positive_bits);
    CHECK(first.node_diagnostics[1].signed_key_value >= 0);
    CHECK(first.semantic_fingerprint == second.semantic_fingerprint);
    CHECK(first.identity_fingerprint == second.identity_fingerprint);
}

void TestDeltaClassification() noexcept
{
    semantic::AudioSemanticDeltaLedger ledger = {};
    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(1U, 0x100U, 0x100U)));
    CHECK(semantic::RecordAudioSemanticBankObservation(
        &ledger, FailedBankObservation(1U, 0x200U, 0x200U)));
    semantic::AudioSemanticEpochAssessment assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 1U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Untouched);
    CHECK(assessment.script_observation_count == 1U);
    CHECK(assessment.bank_observation_count == 1U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(2U, 0x100U, 0x101U)));
    CHECK(semantic::RecordAudioSemanticBankObservation(
        &ledger, FailedBankObservation(2U, 0x200U, 0x200U)));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 2U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Touched);
    CHECK((assessment.reason_mask &
           semantic::AudioSemanticDeltaReasonScriptRegistryDelta) != 0U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(3U, 0x100U, 0x100U)));
    CHECK(semantic::RecordAudioSemanticBankObservation(
        &ledger, FailedBankObservation(3U, 0x200U, 0x201U)));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 3U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Touched);
    CHECK((assessment.reason_mask &
           semantic::AudioSemanticDeltaReasonBankTreeDelta) != 0U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    semantic::AudioSemanticBankObservation successful =
        FailedBankObservation(4U, 0x200U, 0x200U);
    successful.result = 1;
    successful.reference_count_delta_observed = false;
    successful.failed_lookup_no_retained_state_proven = false;
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(4U, 0x100U, 0x100U)));
    CHECK(semantic::RecordAudioSemanticBankObservation(
        &ledger, successful));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 4U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Touched);
    CHECK((assessment.reason_mask &
           semantic::
               AudioSemanticDeltaReasonBankLoadRetainedStatePossible) !=
          0U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    semantic::AudioSemanticBankObservation exact_cache_hit =
        FailedBankObservation(41U, 0x200U, 0x200U);
    exact_cache_hit.result = 1;
    exact_cache_hit.reference_count_delta_observed = true;
    exact_cache_hit.reference_count_delta = 0;
    exact_cache_hit.failed_lookup_no_retained_state_proven = false;
    CHECK(semantic::RecordAudioSemanticBankObservation(
        &ledger, exact_cache_hit));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 41U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Untouched);
    CHECK(!ledger.evidence.bank_reference_evidence_incomplete);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    semantic::AudioSemanticBankObservation unproven =
        FailedBankObservation(5U, 0x200U, 0x200U);
    unproven.reference_count_delta_observed = false;
    unproven.failed_lookup_no_retained_state_proven = false;
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(5U, 0x100U, 0x100U)));
    CHECK(semantic::RecordAudioSemanticBankObservation(
        &ledger, unproven));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 5U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           semantic::
               AudioSemanticDeltaReasonFailedBankNoInsertionUnproven) !=
          0U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 6U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Untouched);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 7U, false);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           semantic::AudioSemanticDeltaReasonHookPlanIncomplete) != 0U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(8U, 0x100U, 0x100U)));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 8U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Untouched);
    CHECK(assessment.reason_mask ==
          semantic::AudioSemanticDeltaReasonNone);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    semantic::AudioSemanticScriptObservation unattributed_unchanged =
        ScriptObservation(81U, 0x100U, 0x100U);
    unattributed_unchanged.source = 0;
    unattributed_unchanged.exact_source = false;
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, unattributed_unchanged));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 81U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Untouched);
    CHECK(!ledger.evidence.full_path_attribution_required);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    semantic::AudioSemanticScriptObservation unattributed_delta =
        ScriptObservation(82U, 0x100U, 0x101U);
    unattributed_delta.source = 0;
    unattributed_delta.exact_source = false;
    CHECK(semantic::RecordAudioSemanticScriptObservation(
        &ledger, unattributed_delta));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 82U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Touched);
    CHECK((assessment.reason_mask &
           semantic::
               AudioSemanticDeltaReasonScriptEvidenceIncomplete) != 0U);
    CHECK(ledger.evidence.full_path_attribution_required);
    CHECK(ledger.evidence.full_path_attribution_incomplete);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    CHECK(semantic::RecordAudioSemanticSpeechObservation(
        &ledger, 83U, true));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 83U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Untouched);
    CHECK(assessment.speech_observation_count == 1U);

    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    CHECK(semantic::RecordAudioSemanticSpeechObservation(
        &ledger, 84U, false));
    assessment =
        semantic::AssessAudioSemanticEpoch(&ledger, 84U, true);
    CHECK(assessment.disposition ==
          semantic::AudioSemanticEpochDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           semantic::
               AudioSemanticDeltaReasonSpeechEvidenceIncomplete) != 0U);

    CHECK(!semantic::RecordAudioSemanticScriptObservation(
        &ledger, ScriptObservation(85U, 0x100U, 0x100U)));
    CHECK(ledger.evidence.epoch == 84U);
    semantic::InitializeAudioSemanticDeltaLedger(&ledger);
    for (std::uint32_t epoch = 1; epoch <= 20; ++epoch) {
        CHECK(semantic::RecordAudioSemanticScriptObservation(
            &ledger, ScriptObservation(epoch, 0x100U, 0x100U)));
        CHECK(semantic::AssessAudioSemanticEpoch(&ledger, epoch, true).disposition ==
            semantic::AudioSemanticEpochDisposition::Untouched);
        dluma::fixes::custom_map_asset_isolation::content::VerifiedEpochRetirement receipt = {};
        receipt.epoch = epoch; receipt.verified_transaction_token = epoch;
        receipt.observer_quiescence_token = epoch + 100U;
        receipt.observer_admission_closed = true;
        CHECK(!semantic::RetireAudioSemanticEpoch(&ledger, receipt));
        receipt.transaction_verified = true;
        receipt.pending_observer_callbacks = 1;
        CHECK(!semantic::RetireAudioSemanticEpoch(&ledger, receipt));
        receipt.pending_observer_callbacks = 0;
        CHECK(semantic::RetireAudioSemanticEpoch(&ledger, receipt));
        CHECK(!semantic::RetireAudioSemanticEpoch(&ledger, receipt));
    }
    CHECK(ledger.retired_epoch == 20U && ledger.retired_observation_count == 20U);
    CHECK(!semantic::RecordAudioSemanticScriptObservation(&ledger, ScriptObservation(20U, 0x100U, 0x100U)));
}

}  // namespace

int main()
{
    TestWindowsSemanticSnapshots();
    TestCompleteStockPropertyTreeAndCorruption();
    TestSignedFmodBankOrdering();
    TestDeltaClassification();
    if (g_failures) {
        std::printf(
            "audio semantic delta fake harness: %d/%d failed\n",
            g_failures, g_checks);
        return 1;
    }
    std::printf(
        "audio semantic delta fake harness: %d checks passed\n",
        g_checks);
    return 0;
}
