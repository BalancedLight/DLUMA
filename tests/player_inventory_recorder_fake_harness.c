#include "dlw_player_inventory_recorder.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #condition, __FILE__, __LINE__); \
            return 0; \
        } \
    } while (0)

#define ARENA_SIZE 0x10000U
#define FAKE_GAMEDLL_BASE ((uintptr_t)0x0000000180000000ULL)
#define FAKE_GAMEDLL_SIZE ((size_t)0x02000000U)
#define TEST_ITEM_CHILD 3U

typedef enum FakeClearMutation {
    FAKE_CLEAR_NONE = 0,
    FAKE_CLEAR_TO_ZERO,
    FAKE_CLEAR_TO_UNDERFLOW,
    FAKE_CLEAR_TO_OVER_CAPACITY,
    FAKE_CLEAR_TO_UNREADABLE,
    FAKE_CLEAR_TO_UNSTABLE
} FakeClearMutation;

typedef struct FakeMemory {
    unsigned char arena[ARENA_SIZE];
    uintptr_t base;
    uintptr_t player;
    uintptr_t owner;
    uintptr_t containers[DLUMA_PLAYER_INVENTORY_GROUP_COUNT];
    uintptr_t children[DLUMA_PLAYER_INVENTORY_ROW_COUNT];
    uintptr_t data[DLUMA_PLAYER_INVENTORY_ROW_COUNT];
    uintptr_t reject_address;
    uintptr_t unstable_header;
    unsigned int unstable_reads;
    uintptr_t short_region_base;
    size_t short_region_size;
    unsigned int read_calls;
} FakeMemory;

static FakeMemory* g_wrapper_memory;
static uintptr_t g_wrapper_inventory;
static unsigned int g_clear_calls;
static unsigned int g_observer_calls;
static unsigned int g_get_all_calls;
static int g_nested_clear;
static FakeClearMutation g_clear_mutation;

static uint32_t profile_vtable_rva(uint32_t child)
{
    switch (child) {
    case 0U: return DLUMA_PLAYER_INVENTORY_VTABLE_MONEY_RVA;
    case 1U: return DLUMA_PLAYER_INVENTORY_VTABLE_AMMO_RVA;
    case 2U: return DLUMA_PLAYER_INVENTORY_VTABLE_COLLECTABLE_RVA;
    case 3U: return DLUMA_PLAYER_INVENTORY_VTABLE_LOOSE_ITEMS_RVA;
    case 4U: return DLUMA_PLAYER_INVENTORY_VTABLE_MAIN_RVA;
    case 5U: return DLUMA_PLAYER_INVENTORY_VTABLE_SPECIAL_RVA;
    case 8U: return DLUMA_PLAYER_INVENTORY_VTABLE_REPAIR_PART_RVA;
    case 9U: return DLUMA_PLAYER_INVENTORY_VTABLE_HELLRAID_MONEY_RVA;
    default: return 0;
    }
}

static uint32_t expected_child_kind(uint32_t child)
{
    if (child >= 3U && child <= 5U)
        return DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR;
    if (child == 6U || child == 7U)
        return DLUMA_PLAYER_INVENTORY_CHILD_ABSENT;
    return DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD;
}

static void configure_recorder(void)
{
    dluma_player_inventory_recorder_configure(
        1, 1, FAKE_GAMEDLL_BASE, FAKE_GAMEDLL_SIZE);
}

static void put_pointer(FakeMemory* memory, uintptr_t address, uintptr_t value)
{
    memcpy(
        memory->arena + (address - memory->base),
        &value, sizeof(value));
}

static void put_header(
    FakeMemory* memory, uintptr_t inventory, uintptr_t data,
    uint32_t count, uint32_t capacity, uint32_t tag)
{
    unsigned char* header =
        memory->arena + inventory - memory->base +
        DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET;
    memcpy(header, &data, 8U);
    memcpy(header + 8U, &count, 4U);
    memcpy(header + 12U, &capacity, 4U);
    memcpy(header + 16U, &tag, 4U);
}

static uint32_t get_count(FakeMemory* memory, uintptr_t inventory)
{
    uint32_t count = 0;
    memcpy(
        &count,
        memory->arena + inventory - memory->base +
            DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET + 8U,
        sizeof(count));
    return count;
}

static void put_count(
    FakeMemory* memory, uintptr_t inventory, uint32_t count)
{
    memcpy(
        memory->arena + inventory - memory->base +
            DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET + 8U,
        &count, sizeof(count));
}

static int fake_read(
    void* user, uintptr_t address, void* output, size_t size)
{
    FakeMemory* memory = (FakeMemory*)user;
    uintptr_t end;
    ++memory->read_calls;
    if (!output || !size || address == memory->reject_address ||
        address < memory->base ||
        address > ~(uintptr_t)0 - size)
        return 0;
    end = address + size;
    if (end > memory->base + ARENA_SIZE) return 0;
    memcpy(output, memory->arena + address - memory->base, size);
    if (address == memory->unstable_header && size == 20U) {
        ++memory->unstable_reads;
        if (memory->unstable_reads == 2U) {
            uint32_t changed = 7U;
            memcpy((unsigned char*)output + 8U, &changed, 4U);
        }
    }
    return 1;
}

static int fake_query(
    void* user, uintptr_t address, DlumaPlayerDiMemoryRegion* output)
{
    FakeMemory* memory = (FakeMemory*)user;
    memset(output, 0, sizeof(*output));
    output->query_succeeded = 1;
    if (memory->short_region_base &&
        address >= memory->short_region_base &&
        address < memory->short_region_base +
                    memory->short_region_size) {
        output->base = memory->short_region_base;
        output->size = memory->short_region_size;
        output->state = 0x1000U;
        output->protection = 0x04U;
        output->address_in_region = 1;
        return 1;
    }
    if (address >= memory->base &&
        address < memory->base + ARENA_SIZE) {
        output->base = memory->base;
        output->size = ARENA_SIZE;
        output->state = 0x1000U;
        output->protection = 0x04U;
        output->address_in_region = 1;
        return 1;
    }
    output->base = address & ~(uintptr_t)0xFFFU;
    output->size = 0x1000U;
    output->state = 0x10000U;
    output->protection = 0x01U;
    return 1;
}

static void initialize_tree(FakeMemory* memory)
{
    uint32_t group;
    uint32_t child;
    memset(memory, 0, sizeof(*memory));
    memory->base = (uintptr_t)memory->arena;
    memory->player = memory->base + 0x100U;
    memory->owner =
        memory->player + DLUMA_PLAYER_INVENTORY_OWNER_OFFSET;
    put_pointer(memory, memory->player, memory->base + 0xF000U);
    put_pointer(memory, memory->owner, memory->base + 0xF100U);
    for (group = 0; group < DLUMA_PLAYER_INVENTORY_GROUP_COUNT; ++group) {
        memory->containers[group] =
            memory->base + 0x1800U + group * 0x100U;
        put_pointer(
            memory,
            memory->owner +
                DLUMA_PLAYER_INVENTORY_GROUP_POINTERS_OFFSET +
                group * sizeof(uintptr_t),
            memory->containers[group]);
        put_pointer(
            memory, memory->containers[group],
            memory->base + 0xF200U + group * 8U);
        for (child = 0; child < DLUMA_PLAYER_INVENTORY_CHILD_COUNT;
             ++child) {
            const uint32_t index =
                group * DLUMA_PLAYER_INVENTORY_CHILD_COUNT + child;
            const uintptr_t child_slot =
                memory->containers[group] +
                DLUMA_PLAYER_INVENTORY_CHILD_POINTERS_OFFSET +
                child * sizeof(uintptr_t);
            if (child == 6U || child == 7U) {
                put_pointer(memory, child_slot, 0);
                continue;
            }
            memory->children[index] =
                memory->base + 0x3000U + index * 0x80U;
            memory->data[index] =
                memory->base + 0x6000U + index * 0x80U;
            put_pointer(memory, child_slot, memory->children[index]);
            put_pointer(
                memory, memory->children[index],
                FAKE_GAMEDLL_BASE + profile_vtable_rva(child));
            if (child >= 3U && child <= 5U) {
                put_header(
                    memory, memory->children[index],
                    memory->data[index], 2U, 4U,
                    DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG);
            } else if (child == 1U) {
                put_header(
                    memory, memory->children[index],
                    memory->data[index], 1U, 1U, 0x3BU);
            } else if (child == 2U) {
                /* This is deliberately nonsensical as an item vector.
                 * The exact non-item profile must keep it diagnostic-only. */
                put_header(
                    memory, memory->children[index],
                    memory->data[index], 0x4000U, 0x20U, 0x3BU);
            } else {
                put_header(
                    memory, memory->children[index], 0, 0, 0, 0);
            }
        }
    }
}

static DlumaPlayerInventoryPublication publication_for(
    const FakeMemory* memory)
{
    DlumaPlayerInventoryPublication publication;
    memset(&publication, 0, sizeof(publication));
    publication.player_di = memory->player;
    publication.inventory_owner = memory->owner;
    publication.creation_generation = 9U;
    publication.content_epoch = 3U;
    publication.map_generation = 5U;
    publication.owner_thread = 77U;
    publication.publication_sequence = 1U;
    publication.valid_live = 1U;
    return publication;
}

static int capture_tree(
    FakeMemory* memory, DlumaPlayerInventoryTreeSnapshot* output)
{
    DlumaPlayerInventoryPublication publication =
        publication_for(memory);
    return dluma_player_inventory_capture_tree(
        &publication, DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY,
        77U, 3U, 4U, 5U, 6U, 7U, fake_read, fake_query,
        memory, output);
}

static int test_exact_five_by_ten_profile(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryTreeSnapshot tree;
    unsigned char before[ARENA_SIZE];
    uint32_t group;
    uint32_t child;
    dluma_player_inventory_recorder_reset();
    configure_recorder();
    initialize_tree(&memory);
    memcpy(before, memory.arena, sizeof(before));
    CHECK(capture_tree(&memory, &tree));
    CHECK(tree.complete);
    CHECK(tree.tree_profile_complete);
    CHECK(tree.item_vector_tree_valid);
    CHECK(tree.item_vector_row_count == 15U);
    CHECK(tree.valid_item_vector_row_count == 15U);
    CHECK(tree.invalid_item_vector_row_count == 0U);
    CHECK(tree.non_item_row_count == 25U);
    CHECK(tree.absent_row_count == 10U);
    CHECK(tree.unknown_row_count == 0U);
    CHECK(tree.valid_row_count == 50U);
    CHECK(tree.invalid_row_count == 0U);
    for (group = 0; group < DLUMA_PLAYER_INVENTORY_GROUP_COUNT; ++group) {
        for (child = 0; child < DLUMA_PLAYER_INVENTORY_CHILD_COUNT;
             ++child) {
            const uint32_t index =
                group * DLUMA_PLAYER_INVENTORY_CHILD_COUNT + child;
            const DlumaPlayerInventoryRow* row = &tree.rows[index];
            CHECK(row->group_index == group);
            CHECK(row->child_index == child);
            CHECK(row->child_kind == expected_child_kind(child));
            CHECK(row->profile_validated);
            CHECK(
                (row->item_vector_contract_applicable != 0U) ==
                (child >= 3U && child <= 5U));
            if (child >= 3U && child <= 5U) {
                CHECK(row->profile_vtable_rva ==
                      profile_vtable_rva(child));
                CHECK(row->item_vector_valid);
                CHECK(row->header.tag_or_field == 0x33U);
            } else if (child == 6U || child == 7U) {
                CHECK(!row->child_inventory);
                CHECK(row->exact_failure ==
                      DLUMA_PLAYER_INVENTORY_FAILURE_NONE);
            } else {
                CHECK(row->raw_header_readable);
                CHECK(!row->item_vector_valid);
                CHECK(row->exact_failure ==
                      DLUMA_PLAYER_INVENTORY_FAILURE_NONE);
            }
        }
    }
    CHECK(tree.rows[1].raw_header.tag_or_field == 0x3BU);
    CHECK(tree.rows[2].raw_header.count == 0x4000U);
    CHECK(memcmp(before, memory.arena, sizeof(before)) == 0);
    return 1;
}

static int test_unknown_is_incomplete_not_corrupt_vector(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryTreeSnapshot tree;
    const uint32_t index = 2U * DLUMA_PLAYER_INVENTORY_CHILD_COUNT + 9U;
    dluma_player_inventory_recorder_reset();
    configure_recorder();
    initialize_tree(&memory);
    put_pointer(
        &memory, memory.children[index],
        FAKE_GAMEDLL_BASE + 0x1000U);
    CHECK(capture_tree(&memory, &tree));
    CHECK(!tree.tree_profile_complete);
    CHECK(!tree.item_vector_tree_valid);
    CHECK(tree.item_vector_row_count == 15U);
    CHECK(tree.valid_item_vector_row_count == 15U);
    CHECK(tree.invalid_item_vector_row_count == 0U);
    CHECK(tree.non_item_row_count == 24U);
    CHECK(tree.absent_row_count == 10U);
    CHECK(tree.unknown_row_count == 1U);
    CHECK(tree.rows[index].child_kind ==
          DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING);
    CHECK(!tree.rows[index].item_vector_contract_applicable);
    CHECK(tree.rows[index].raw_header_readable);
    CHECK(tree.rows[index].exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_PROFILE_UNKNOWN_OR_CONFLICTING);
    return 1;
}

static int test_tree_failures_are_precise(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryTreeSnapshot tree;
    DlumaPlayerInventoryPublication publication;
    dluma_player_inventory_recorder_reset();
    configure_recorder();
    initialize_tree(&memory);
    publication = publication_for(&memory);
    memory.reject_address = memory.player;
    CHECK(dluma_player_inventory_capture_tree(
        &publication, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        fake_read, fake_query, &memory, &tree));
    CHECK(tree.rows[0].exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_PLAYER_UNREADABLE);
    CHECK(tree.unknown_row_count == 50U);

    initialize_tree(&memory);
    memory.reject_address = memory.owner;
    CHECK(capture_tree(&memory, &tree));
    CHECK(tree.rows[0].exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_OWNER_UNREADABLE);

    initialize_tree(&memory);
    put_pointer(
        &memory,
        memory.owner + DLUMA_PLAYER_INVENTORY_GROUP_POINTERS_OFFSET,
        0);
    CHECK(capture_tree(&memory, &tree));
    CHECK(tree.rows[0].exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CONTAINER);
    CHECK(tree.unknown_row_count == 10U);

    initialize_tree(&memory);
    put_pointer(
        &memory,
        memory.containers[0] +
            DLUMA_PLAYER_INVENTORY_CHILD_POINTERS_OFFSET,
        0);
    CHECK(capture_tree(&memory, &tree));
    CHECK(tree.rows[0].child_kind ==
          DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING);
    CHECK(tree.rows[0].exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CHILD);
    CHECK(tree.invalid_item_vector_row_count == 0U);
    return 1;
}

static int test_item_vector_invariants(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryRow row;
    uintptr_t inventory;
    const uint32_t index = TEST_ITEM_CHILD;
    dluma_player_inventory_recorder_reset();
    configure_recorder();
    initialize_tree(&memory);
    inventory = memory.children[index];

    put_header(
        &memory, inventory, 0, 0, 0,
        DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG);
    CHECK(dluma_player_inventory_capture_vector(
        inventory, fake_read, fake_query, &memory, &row));
    CHECK(row.item_vector_contract_applicable);
    CHECK(row.item_vector_valid);

    put_header(
        &memory, inventory, memory.data[index], 5U, 4U,
        DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG);
    CHECK(dluma_player_inventory_capture_vector(
        inventory, fake_read, fake_query, &memory, &row));
    CHECK(row.exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_COUNT_GREATER_THAN_CAPACITY);

    put_header(
        &memory, inventory, memory.data[index], 4U, 4U,
        DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG);
    memory.short_region_base = memory.data[index];
    memory.short_region_size = 8U;
    CHECK(dluma_player_inventory_capture_vector(
        inventory, fake_read, fake_query, &memory, &row));
    CHECK(row.exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_UNREADABLE);

    memory.short_region_base = 0;
    memory.short_region_size = 0;
    memory.unstable_header =
        inventory + DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET;
    memory.unstable_reads = 0;
    CHECK(dluma_player_inventory_capture_vector(
        inventory, fake_read, fake_query, &memory, &row));
    CHECK(row.exact_failure ==
          DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_CHANGED);
    return 1;
}

static int prepare_runtime(
    FakeMemory* memory, uint32_t count, uint32_t capacity)
{
    const uint32_t index = TEST_ITEM_CHILD;
    initialize_tree(memory);
    put_header(
        memory, memory->children[index],
        capacity ? memory->data[index] : 0,
        count, capacity,
        DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG);
    dluma_player_inventory_recorder_reset();
    configure_recorder();
    dluma_player_inventory_publish_player(
        (void*)memory->player, 1U, 2U, 3U, 77U);
    return dluma_player_inventory_capture_and_publish_tree(
        DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY,
        77U, 2U, 3U, 4U, 0, 0,
        fake_read, fake_query, memory);
}

static int test_last_complete_and_valid_item_tree(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryRuntimeSnapshot runtime;
    uint64_t valid_sequence;
    uint32_t group = UINT32_MAX;
    uint32_t child = UINT32_MAX;
    CHECK(prepare_runtime(&memory, 2U, 4U));
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.last_complete_tree.capture_sequence != 0U);
    CHECK(runtime.last_valid_item_vector_tree.capture_sequence != 0U);
    CHECK(runtime.last_valid_tree.capture_sequence ==
          runtime.last_valid_item_vector_tree.capture_sequence);
    valid_sequence =
        runtime.last_valid_item_vector_tree.capture_sequence;
    CHECK(dluma_player_inventory_runtime_find_header(
        memory.children[TEST_ITEM_CHILD] +
            DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET,
        &group, &child));
    CHECK(group == 0U && child == TEST_ITEM_CHILD);
    CHECK(!dluma_player_inventory_runtime_find_header(
        memory.children[1] + DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET,
        &group, &child));

    put_count(&memory, memory.children[TEST_ITEM_CHILD], 5U);
    CHECK(dluma_player_inventory_capture_and_publish_tree(
        DLUMA_PLAYER_INVENTORY_PHASE_UNLOAD_LEVEL_ENTRY,
        77U, 2U, 3U, 4U, 0, 0,
        fake_read, fake_query, &memory));
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.last_tree.invalid_item_vector_row_count == 1U);
    CHECK(runtime.last_tree.tree_profile_complete);
    CHECK(!runtime.last_tree.item_vector_tree_valid);
    CHECK(runtime.last_complete_tree.capture_sequence ==
          runtime.last_tree.capture_sequence);
    CHECK(runtime.last_valid_item_vector_tree.capture_sequence ==
          valid_sequence);
    return 1;
}

static uintptr_t DLUMA_PLAYERDI_FASTCALL fake_clear(
    void* inventory, unsigned int reason)
{
    ++g_clear_calls;
    if (g_nested_clear && g_clear_calls == 1U)
        (void)dluma_player_inventory_observe_clear_call(
            fake_clear, inventory, reason, fake_read, fake_query,
            g_wrapper_memory);
    switch (g_clear_mutation) {
    case FAKE_CLEAR_TO_ZERO:
        put_count(g_wrapper_memory, (uintptr_t)inventory, 0U);
        break;
    case FAKE_CLEAR_TO_UNDERFLOW:
        put_count(g_wrapper_memory, (uintptr_t)inventory, UINT32_MAX);
        break;
    case FAKE_CLEAR_TO_OVER_CAPACITY:
        put_count(g_wrapper_memory, (uintptr_t)inventory, 17U);
        break;
    case FAKE_CLEAR_TO_UNREADABLE:
        g_wrapper_memory->reject_address =
            (uintptr_t)inventory +
            DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET;
        break;
    case FAKE_CLEAR_TO_UNSTABLE:
        g_wrapper_memory->unstable_header =
            (uintptr_t)inventory +
            DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET;
        g_wrapper_memory->unstable_reads = 0;
        break;
    case FAKE_CLEAR_NONE:
    default:
        break;
    }
    return (uintptr_t)reason + 0x100U;
}

static void DLUMA_PLAYERDI_FASTCALL fake_observer(
    void* observer, void* inventory, void* item, int reason)
{
    (void)observer;
    (void)inventory;
    (void)item;
    (void)reason;
    ++g_observer_calls;
}

static uintptr_t DLUMA_PLAYERDI_FASTCALL fake_get_all(
    void* inventory, void* output)
{
    (void)inventory;
    ++g_get_all_calls;
    return (uintptr_t)output;
}

static void reset_wrapper_state(FakeMemory* memory)
{
    g_wrapper_memory = memory;
    g_wrapper_inventory = memory->children[TEST_ITEM_CHILD];
    g_clear_calls = 0;
    g_observer_calls = 0;
    g_get_all_calls = 0;
    g_nested_clear = 0;
    g_clear_mutation = FAKE_CLEAR_NONE;
}

static int test_benign_growth_and_clean_clear(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryRuntimeSnapshot runtime;
    DlumaPlayerInventoryTreeSnapshot tree;
    uintptr_t result;
    CHECK(prepare_runtime(&memory, 0U, 0U));
    reset_wrapper_state(&memory);
    put_header(
        &memory, g_wrapper_inventory, memory.data[TEST_ITEM_CHILD],
        1U, 16U, DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG);
    CHECK(dluma_player_inventory_observe_get_all_items_call(
        fake_get_all, (void*)g_wrapper_inventory, (void*)0xABC0U,
        fake_read, fake_query, &memory) == 0xABC0U);
    CHECK(g_get_all_calls == 1U);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.first_any_header_change.present);
    CHECK(!runtime.first_invalid_transition.present);
    CHECK(runtime.first_any_header_change.event.previous.count == 0U);
    CHECK(runtime.first_any_header_change.event.current.count == 1U);

    CHECK(prepare_runtime(&memory, 2U, 16U));
    reset_wrapper_state(&memory);
    g_clear_mutation = FAKE_CLEAR_TO_ZERO;
    result = dluma_player_inventory_observe_clear_call(
        fake_clear, (void*)g_wrapper_inventory, 12U,
        fake_read, fake_query, &memory);
    CHECK(result == 0x10CU);
    CHECK(g_clear_calls == 1U);
    CHECK(get_count(&memory, g_wrapper_inventory) == 0U);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.first_any_header_change.present);
    CHECK(!runtime.first_invalid_transition.present);
    CHECK(!runtime.first_count_underflow_transition.present);
    CHECK(capture_tree(&memory, &tree));
    CHECK(tree.item_vector_tree_valid);
    CHECK(tree.invalid_item_vector_row_count == 0U);
    return 1;
}

static int run_invalid_transition_case(
    FakeClearMutation mutation,
    uint32_t initial_count,
    uint32_t initial_capacity,
    uint32_t required_mask,
    DlumaPlayerInventoryRuntimeSnapshot* output)
{
    FakeMemory memory;
    CHECK(output);
    CHECK(prepare_runtime(&memory, initial_count, initial_capacity));
    reset_wrapper_state(&memory);
    g_clear_mutation = mutation;
    (void)dluma_player_inventory_observe_clear_call(
        fake_clear, (void*)g_wrapper_inventory, 12U,
        fake_read, fake_query, &memory);
    CHECK(g_clear_calls == 1U);
    CHECK(dluma_player_inventory_runtime_snapshot(output));
    CHECK(output->first_invalid_transition.present);
    CHECK((output->first_invalid_transition.event.transition_kind_mask &
           required_mask) == required_mask);
    CHECK(output->first_invalid_transition.event.group_index == 0U);
    CHECK(output->first_invalid_transition.event.child_index ==
          TEST_ITEM_CHILD);
    return 1;
}

static int test_distinct_invalid_latches(void)
{
    DlumaPlayerInventoryRuntimeSnapshot runtime;
    CHECK(run_invalid_transition_case(
        FAKE_CLEAR_TO_OVER_CAPACITY, 2U, 16U,
        DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID |
            DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_GREATER_THAN_CAPACITY,
        &runtime));
    CHECK(runtime.first_count_greater_than_capacity.present);

    CHECK(run_invalid_transition_case(
        FAKE_CLEAR_TO_UNDERFLOW, 0U, 16U,
        DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID |
            DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_UNDERFLOW,
        &runtime));
    CHECK(runtime.first_count_underflow_transition.present);
    CHECK(runtime.first_invalid_transition.event.current.count ==
          UINT32_MAX);

    CHECK(run_invalid_transition_case(
        FAKE_CLEAR_TO_UNREADABLE, 2U, 16U,
        DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID |
            DLUMA_PLAYER_INVENTORY_TRANSITION_UNREADABLE,
        &runtime));
    CHECK(runtime.first_unreadable_transition.present);

    CHECK(run_invalid_transition_case(
        FAKE_CLEAR_TO_UNSTABLE, 2U, 16U,
        DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID |
            DLUMA_PLAYER_INVENTORY_TRANSITION_HEADER_INSTABILITY,
        &runtime));
    CHECK(runtime.first_header_instability.present);
    return 1;
}

static int test_reentrancy_and_original_call_contract(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryRuntimeSnapshot runtime;
    unsigned char before[ARENA_SIZE];
    CHECK(prepare_runtime(&memory, 2U, 16U));
    reset_wrapper_state(&memory);
    memcpy(before, memory.arena, sizeof(before));
    CHECK(dluma_player_inventory_observe_clear_call(
        fake_clear, (void*)g_wrapper_inventory, 9U,
        fake_read, fake_query, &memory) == 0x109U);
    CHECK(g_clear_calls == 1U);
    CHECK(memcmp(before, memory.arena, sizeof(before)) == 0);

    dluma_player_inventory_observe_removed_call(
        fake_observer, (void*)0x1234U, (void*)g_wrapper_inventory,
        (void*)0x5678U, 4, fake_read, fake_query, &memory);
    CHECK(g_observer_calls == 1U);
    CHECK(dluma_player_inventory_observe_get_all_items_call(
        fake_get_all, (void*)g_wrapper_inventory, (void*)0xABCDU,
        fake_read, fake_query, &memory) == 0xABCDU);
    CHECK(g_get_all_calls == 1U);

    CHECK(prepare_runtime(&memory, 2U, 16U));
    reset_wrapper_state(&memory);
    g_nested_clear = 1;
    (void)dluma_player_inventory_observe_clear_call(
        fake_clear, (void*)g_wrapper_inventory, 12U,
        fake_read, fake_query, &memory);
    CHECK(g_clear_calls == 2U);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.clear_depth == 0U);
    CHECK(runtime.first_clear_reentrancy_transition.present);
    CHECK(runtime.first_clear_reentrancy_transition.event.clear_depth ==
          2U);
    return 1;
}

static int test_immutable_context_survives_ring_overwrite(void)
{
    FakeMemory memory;
    DlumaPlayerInventoryRuntimeSnapshot runtime;
    uint64_t invalid_sequence;
    uint64_t preceding_first_sequence;
    uint32_t index;
    CHECK(prepare_runtime(&memory, 2U, 16U));
    reset_wrapper_state(&memory);
    for (index = 0;
         index < DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY;
         ++index)
        (void)dluma_player_inventory_observe_get_all_items_call(
            fake_get_all, (void*)g_wrapper_inventory,
            (void*)(uintptr_t)(0x1000U + index),
            fake_read, fake_query, &memory);
    g_clear_mutation = FAKE_CLEAR_TO_OVER_CAPACITY;
    (void)dluma_player_inventory_observe_clear_call(
        fake_clear, (void*)g_wrapper_inventory, 12U,
        fake_read, fake_query, &memory);
    g_clear_mutation = FAKE_CLEAR_NONE;
    for (index = 0;
         index < DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY;
         ++index)
        (void)dluma_player_inventory_observe_get_all_items_call(
            fake_get_all, (void*)g_wrapper_inventory,
            (void*)(uintptr_t)(0x2000U + index),
            fake_read, fake_query, &memory);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.first_invalid_evidence.present);
    CHECK(runtime.first_invalid_evidence.preceding_count ==
          DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY);
    CHECK(runtime.first_invalid_evidence.following_count ==
          DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY);
    CHECK(runtime.first_invalid_evidence.following_complete);
    invalid_sequence =
        runtime.first_invalid_evidence.invalidating_event.sequence;
    preceding_first_sequence =
        runtime.first_invalid_evidence.preceding[0].sequence;

    for (index = 0;
         index < DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY + 40U;
         ++index)
        (void)dluma_player_inventory_observe_get_all_items_call(
            fake_get_all, (void*)g_wrapper_inventory,
            (void*)(uintptr_t)(0x3000U + index),
            fake_read, fake_query, &memory);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.event_count ==
          DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY);
    CHECK(runtime.event_overwrite_count >= 40U);
    CHECK(runtime.first_invalid_evidence.invalidating_event.sequence ==
          invalid_sequence);
    CHECK(runtime.first_invalid_evidence.preceding[0].sequence ==
          preceding_first_sequence);
    return 1;
}

static int test_crash_state_and_stale_report(void)
{
    DlumaPlayerInventoryStateFiveCrashState state;
    DlumaPlayerInventoryRuntimeSnapshot runtime;
    dluma_player_inventory_recorder_reset();
    memset(&state, 0, sizeof(state));
    state.c08_token = 44U;
    state.c08_return_count = 1U;
    state.c10_entry_count = 1U;
    state.c10_scope_active = 1U;
    state.c10_scope_armed = 1U;
    state.armed_epoch = 7U;
    state.session_on_destroy_entry_count = 1U;
    state.unload_level_active_depth = 1U;
    dluma_player_inventory_state_five_publish(&state);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.state_five.c08_token == 44U);
    CHECK(runtime.state_five.c10_entry_count == 1U);
    CHECK(runtime.state_five.unload_level_active_depth == 1U);
    CHECK(runtime.state_five.periodic_report_stale == 1U);
    dluma_player_inventory_periodic_report_finalized(1);
    CHECK(dluma_player_inventory_runtime_snapshot(&runtime));
    CHECK(runtime.state_five.periodic_report_finalized == 1U);
    CHECK(runtime.state_five.periodic_report_stale == 0U);
    return 1;
}

int main(void)
{
    CHECK(test_exact_five_by_ten_profile());
    CHECK(test_unknown_is_incomplete_not_corrupt_vector());
    CHECK(test_tree_failures_are_precise());
    CHECK(test_item_vector_invariants());
    CHECK(test_last_complete_and_valid_item_tree());
    CHECK(test_benign_growth_and_clean_clear());
    CHECK(test_distinct_invalid_latches());
    CHECK(test_reentrancy_and_original_call_contract());
    CHECK(test_immutable_context_survives_ring_overwrite());
    CHECK(test_crash_state_and_stale_report());
    puts("player inventory recorder fake harness: PASS");
    return 0;
}
