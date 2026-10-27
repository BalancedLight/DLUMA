#include "dlw_common.h"
#include "dlw_player_inventory_recorder.h"

typedef struct DlumaPlayerInventoryRecorderState {
    volatile LONG writer_lock;
    volatile LONG publication_seq;
    volatile LONG snapshot_seq;
    volatile LONG clear_depth;
    volatile LONG observer_depth;
    volatile LONG dropped_writes;
    volatile LONG diagnostic_enabled;
    volatile LONG exact_profile_validated;
    uintptr_t gamedll_base;
    size_t gamedll_size;
    uint64_t next_sequence;
    uint64_t publication_counter;
    volatile long long stable_publication_sequence;
    DlumaPlayerInventoryPublication publication;
    DlumaPlayerInventoryTreeSnapshot last_tree;
    DlumaPlayerInventoryTreeSnapshot last_complete_tree;
    DlumaPlayerInventoryTreeSnapshot last_valid_item_vector_tree;
    DlumaPlayerInventoryTreeSnapshot last_valid_tree;
    DlumaPlayerInventoryFirstTransition first_transition;
    DlumaPlayerInventoryFirstTransition first_any_header_change;
    DlumaPlayerInventoryFirstTransition first_invalid_transition;
    DlumaPlayerInventoryFirstTransition first_count_underflow_transition;
    DlumaPlayerInventoryFirstTransition
        first_count_greater_than_capacity;
    DlumaPlayerInventoryFirstTransition first_unreadable_transition;
    DlumaPlayerInventoryFirstTransition first_header_instability;
    DlumaPlayerInventoryFirstTransition
        first_clear_reentrancy_transition;
    DlumaPlayerInventoryInvalidTransitionEvidence
        first_invalid_evidence;
    DlumaPlayerInventoryEvent events[DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY];
    uint32_t event_count;
    uint32_t event_write_index;
    uint32_t event_overwrite_count;
    DlumaPlayerInventoryStateFiveCrashState state_five;
} DlumaPlayerInventoryRecorderState;

static DlumaPlayerInventoryRecorderState g_player_inventory;

typedef struct DlumaPlayerInventoryExpectedChildProfile {
    uint32_t kind;
    uint32_t vtable_rva;
} DlumaPlayerInventoryExpectedChildProfile;

static const DlumaPlayerInventoryExpectedChildProfile
    g_player_inventory_expected_children[
        DLUMA_PLAYER_INVENTORY_CHILD_COUNT] = {
        {DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD,
         DLUMA_PLAYER_INVENTORY_VTABLE_MONEY_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD,
         DLUMA_PLAYER_INVENTORY_VTABLE_AMMO_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD,
         DLUMA_PLAYER_INVENTORY_VTABLE_COLLECTABLE_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR,
         DLUMA_PLAYER_INVENTORY_VTABLE_LOOSE_ITEMS_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR,
         DLUMA_PLAYER_INVENTORY_VTABLE_MAIN_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR,
         DLUMA_PLAYER_INVENTORY_VTABLE_SPECIAL_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_ABSENT, 0},
        {DLUMA_PLAYER_INVENTORY_CHILD_ABSENT, 0},
        {DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD,
         DLUMA_PLAYER_INVENTORY_VTABLE_REPAIR_PART_RVA},
        {DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD,
         DLUMA_PLAYER_INVENTORY_VTABLE_HELLRAID_MONEY_RVA},
    };

static int dluma_player_inventory_lock(void)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 1024U; ++attempt)
        if (dluma_atomic_compare_exchange(
                &g_player_inventory.writer_lock, 1, 0) == 0)
            return 1;
    dluma_atomic_increment(&g_player_inventory.dropped_writes);
    return 0;
}

static void dluma_player_inventory_unlock(void)
{
    dluma_atomic_exchange(&g_player_inventory.writer_lock, 0);
}

static void dluma_player_inventory_begin_write(void)
{
    (void)dluma_atomic_increment(&g_player_inventory.snapshot_seq);
}

static void dluma_player_inventory_end_write(void)
{
    (void)dluma_atomic_increment(&g_player_inventory.snapshot_seq);
}

static int dluma_player_inventory_region_readable(
    const DlumaPlayerDiMemoryRegion* region,
    uintptr_t address,
    size_t size)
{
    uintptr_t end;
    uintptr_t region_end;
    uint32_t protection;
    if (!region || !region->query_succeeded ||
        !region->address_in_region || !size ||
        address > ~(uintptr_t)0 - size ||
        region->base > ~(uintptr_t)0 - region->size)
        return 0;
    end = address + size;
    region_end = region->base + region->size;
    protection = region->protection & 0xFFU;
    return region->state == 0x1000U &&
        (region->protection & 0x100U) == 0U &&
        (protection == 0x02U || protection == 0x04U ||
         protection == 0x08U || protection == 0x20U ||
         protection == 0x40U || protection == 0x80U) &&
        address >= region->base && end <= region_end;
}

static void dluma_player_inventory_query(
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    uintptr_t address,
    DlumaPlayerDiMemoryRegion* output)
{
    uintptr_t end;
    if (!output) return;
    memset(output, 0, sizeof(*output));
    if (!address || !query_memory ||
        !query_memory(callback_user, address, output))
        return;
    output->query_succeeded = 1;
    if (output->base <= address &&
        output->size <= ~(uintptr_t)0 - output->base) {
        end = output->base + output->size;
        output->address_in_region = address < end ? 1U : 0U;
    }
}

static int dluma_player_inventory_read_pointer(
    DlumaPlayerDiReadMemory read_memory,
    void* callback_user,
    uintptr_t address,
    uintptr_t* output)
{
    return output && read_memory &&
        read_memory(
            callback_user, address, output, sizeof(*output));
}

static int dluma_player_inventory_read_header(
    DlumaPlayerDiReadMemory read_memory,
    void* callback_user,
    uintptr_t address,
    DlumaPlayerDiVectorHeader* output)
{
    unsigned char bytes[20];
    if (!output || !read_memory ||
        !read_memory(callback_user, address, bytes, sizeof(bytes)))
        return 0;
    memset(output, 0, sizeof(*output));
    memcpy(&output->data, bytes, 8U);
    memcpy(&output->count, bytes + 8U, 4U);
    memcpy(&output->capacity, bytes + 12U, 4U);
    memcpy(&output->tag_or_field, bytes + 16U, 4U);
    return 1;
}

static int dluma_player_inventory_profile_enabled(void)
{
    return dluma_atomic_compare_exchange(
               &g_player_inventory.exact_profile_validated, 0, 0) != 0 &&
        g_player_inventory.gamedll_base != 0 &&
        g_player_inventory.gamedll_size != 0 &&
        g_player_inventory.gamedll_base <=
            ~(uintptr_t)0 - g_player_inventory.gamedll_size;
}

static int dluma_player_inventory_vtable_rva(
    uintptr_t vtable, uint32_t* output)
{
    uintptr_t image_end;
    uintptr_t delta;
    if (output) *output = 0;
    if (!output || !vtable ||
        !dluma_player_inventory_profile_enabled())
        return 0;
    image_end =
        g_player_inventory.gamedll_base +
        g_player_inventory.gamedll_size;
    if (vtable < g_player_inventory.gamedll_base ||
        vtable >= image_end)
        return 0;
    delta = vtable - g_player_inventory.gamedll_base;
    if (delta > UINT32_MAX) return 0;
    *output = (uint32_t)delta;
    return 1;
}

static uint32_t dluma_player_inventory_kind_for_vtable_rva(
    uint32_t vtable_rva)
{
    uint32_t child;
    uint32_t kind = DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING;
    for (child = 0; child < DLUMA_PLAYER_INVENTORY_CHILD_COUNT;
         ++child) {
        const DlumaPlayerInventoryExpectedChildProfile* profile =
            &g_player_inventory_expected_children[child];
        if (!profile->vtable_rva ||
            profile->vtable_rva != vtable_rva)
            continue;
        if (kind !=
                DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING &&
            kind != profile->kind)
            return DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING;
        kind = profile->kind;
    }
    return kind;
}

static int dluma_player_inventory_capture_raw_header(
    uintptr_t inventory,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerInventoryRow* output)
{
    DlumaPlayerDiVectorHeader second;
    if (!output || !inventory ||
        inventory >
            ~(uintptr_t)0 - DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET)
        return 0;
    output->raw_header_address =
        inventory + DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET;
    dluma_player_inventory_query(
        query_memory, callback_user, output->raw_header_address,
        &output->header_region);
    if (!dluma_player_inventory_read_header(
            read_memory, callback_user, output->raw_header_address,
            &output->raw_header))
        return 0;
    output->raw_header_readable = 1;
    memset(&second, 0, sizeof(second));
    if (!dluma_player_inventory_read_header(
            read_memory, callback_user, output->raw_header_address,
            &second))
        return 0;
    output->raw_header_stable =
        memcmp(&output->raw_header, &second, sizeof(second)) == 0;
    return 1;
}

static uint32_t dluma_player_inventory_validate_row(
    DlumaPlayerInventoryRow* row)
{
    uint64_t span;
    uintptr_t end_address;
    if (!row->header_readable)
        return DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_UNREADABLE;
    if (!row->header_stable)
        return DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_CHANGED;
    if (row->header.count > row->header.capacity)
        return DLUMA_PLAYER_INVENTORY_FAILURE_COUNT_GREATER_THAN_CAPACITY;
    if (row->header.capacity >
            DLUMA_PLAYER_INVENTORY_DIAGNOSTIC_CAPACITY_CEILING)
        return DLUMA_PLAYER_INVENTORY_FAILURE_CAPACITY_CEILING;
    if (row->header.tag_or_field !=
            DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG)
        return DLUMA_PLAYER_INVENTORY_FAILURE_TAG_MISMATCH;
    if (!row->header.capacity && row->header.data)
        return DLUMA_PLAYER_INVENTORY_FAILURE_EMPTY_DATA_REPRESENTATION;
    if (row->header.capacity && !row->header.data)
        return DLUMA_PLAYER_INVENTORY_FAILURE_DATA_REQUIRED;
    if (!row->header.capacity) return DLUMA_PLAYER_INVENTORY_FAILURE_NONE;
    if (!dluma_player_inventory_region_readable(
            &row->data_region, row->header.data, 1U))
        return DLUMA_PLAYER_INVENTORY_FAILURE_DATA_UNREADABLE;
    if (!row->header.count) return DLUMA_PLAYER_INVENTORY_FAILURE_NONE;
    span = (uint64_t)row->header.count * sizeof(uintptr_t);
    if (span > (uint64_t)SIZE_MAX ||
        row->header.data >
            ~(uintptr_t)0 - (uintptr_t)(span - 1U))
        return DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_OVERFLOW;
    end_address =
        row->header.data + (uintptr_t)(span - 1U);
    if (!row->end_element_region.query_succeeded ||
        !dluma_player_inventory_region_readable(
            &row->end_element_region, end_address, 1U) ||
        !dluma_player_inventory_region_readable(
            &row->data_region, row->header.data, (size_t)span))
        return DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_UNREADABLE;
    return DLUMA_PLAYER_INVENTORY_FAILURE_NONE;
}

static int dluma_player_inventory_capture_profiled_child(
    uintptr_t inventory,
    uint32_t expected_child_index,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerInventoryRow* output)
{
    uint64_t span;
    uintptr_t end_address;
    uint32_t detected_kind;
    const DlumaPlayerInventoryExpectedChildProfile* expected = 0;
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    output->child_kind =
        DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING;
    output->child_inventory = inventory;
    if (expected_child_index < DLUMA_PLAYER_INVENTORY_CHILD_COUNT)
        expected =
            &g_player_inventory_expected_children[expected_child_index];
    if (!inventory) {
        if (expected &&
            expected->kind == DLUMA_PLAYER_INVENTORY_CHILD_ABSENT &&
            dluma_player_inventory_profile_enabled()) {
            output->child_kind =
                DLUMA_PLAYER_INVENTORY_CHILD_ABSENT;
            output->profile_validated = 1;
            output->exact_failure =
                DLUMA_PLAYER_INVENTORY_FAILURE_NONE;
            return 1;
        }
        output->exact_failure =
            DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CHILD;
        return 1;
    }
    dluma_player_inventory_query(
        query_memory, callback_user, inventory,
        &output->object_region);
    if (!dluma_player_inventory_read_pointer(
            read_memory, callback_user, inventory,
            &output->child_vtable)) {
        output->exact_failure =
            DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_UNREADABLE;
        return 1;
    }

    if (!dluma_player_inventory_vtable_rva(
            output->child_vtable, &output->profile_vtable_rva)) {
        (void)dluma_player_inventory_capture_raw_header(
            inventory, read_memory, query_memory, callback_user,
            output);
        output->exact_failure =
            DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_PROFILE_UNKNOWN_OR_CONFLICTING;
        return 1;
    }
    detected_kind = dluma_player_inventory_kind_for_vtable_rva(
        output->profile_vtable_rva);
    if (expected) {
        if (expected->kind == DLUMA_PLAYER_INVENTORY_CHILD_ABSENT ||
            expected->vtable_rva != output->profile_vtable_rva ||
            expected->kind != detected_kind) {
            (void)dluma_player_inventory_capture_raw_header(
                inventory, read_memory, query_memory, callback_user,
                output);
            output->exact_failure =
                DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_PROFILE_UNKNOWN_OR_CONFLICTING;
            return 1;
        }
    } else if (
        detected_kind ==
        DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING) {
        (void)dluma_player_inventory_capture_raw_header(
            inventory, read_memory, query_memory, callback_user,
            output);
        output->exact_failure =
            DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_PROFILE_UNKNOWN_OR_CONFLICTING;
        return 1;
    }
    output->child_kind = detected_kind;
    output->profile_validated = 1;
    if (detected_kind ==
        DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR) {
        output->item_vector_contract_applicable = 1;
        output->vector_header_address =
            inventory + DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET;
    }
    if (!dluma_player_inventory_capture_raw_header(
            inventory, read_memory, query_memory, callback_user,
            output)) {
        if (detected_kind ==
            DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR)
            output->exact_failure =
                DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_UNREADABLE;
        return 1;
    }
    if (detected_kind !=
        DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR) {
        output->exact_failure =
            DLUMA_PLAYER_INVENTORY_FAILURE_NONE;
        return 1;
    }
    output->vector_header_address = output->raw_header_address;
    output->header = output->raw_header;
    output->header_readable = output->raw_header_readable;
    output->header_stable = output->raw_header_stable;
    if (output->header.data)
        dluma_player_inventory_query(
            query_memory, callback_user, output->header.data,
            &output->data_region);
    if (output->header.count && output->header.data) {
        span = (uint64_t)output->header.count * sizeof(uintptr_t);
        if (span <= (uint64_t)SIZE_MAX && span &&
            output->header.data <=
                ~(uintptr_t)0 - (uintptr_t)(span - 1U)) {
            end_address =
                output->header.data + (uintptr_t)(span - 1U);
            dluma_player_inventory_query(
                query_memory, callback_user, end_address,
                &output->end_element_region);
        }
    }
    output->exact_failure =
        dluma_player_inventory_validate_row(output);
    output->item_vector_valid =
        output->exact_failure == DLUMA_PLAYER_INVENTORY_FAILURE_NONE;
    output->invariant_valid = output->item_vector_valid;
    return 1;
}

int dluma_player_inventory_capture_vector(
    uintptr_t inventory,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerInventoryRow* output)
{
    return dluma_player_inventory_capture_profiled_child(
        inventory, UINT32_MAX, read_memory, query_memory,
        callback_user, output);
}

void dluma_player_inventory_recorder_reset(void)
{
    memset(&g_player_inventory, 0, sizeof(g_player_inventory));
}

void dluma_player_inventory_recorder_configure(
    int diagnostic_enabled, int exact_profile_validated,
    uintptr_t gamedll_base, size_t gamedll_size)
{
    g_player_inventory.gamedll_base =
        exact_profile_validated ? gamedll_base : 0;
    g_player_inventory.gamedll_size =
        exact_profile_validated ? gamedll_size : 0;
    dluma_atomic_exchange(
        &g_player_inventory.diagnostic_enabled,
        diagnostic_enabled ? 1 : 0);
    dluma_atomic_exchange(
        &g_player_inventory.exact_profile_validated,
        exact_profile_validated ? 1 : 0);
}

void dluma_player_inventory_publish_player(
    void* player_di,
    uint64_t creation_generation,
    uint32_t content_epoch,
    uint32_t map_generation,
    uint32_t owner_thread)
{
    if (!dluma_player_inventory_lock()) return;
    dluma_player_inventory_begin_write();
    ++g_player_inventory.publication_counter;
    memset(
        &g_player_inventory.publication, 0,
        sizeof(g_player_inventory.publication));
    g_player_inventory.publication.player_di =
        (uintptr_t)player_di;
    if (player_di &&
        (uintptr_t)player_di <=
            ~(uintptr_t)0 - DLUMA_PLAYER_INVENTORY_OWNER_OFFSET)
        g_player_inventory.publication.inventory_owner =
            (uintptr_t)player_di +
            DLUMA_PLAYER_INVENTORY_OWNER_OFFSET;
    g_player_inventory.publication.creation_generation =
        creation_generation;
    g_player_inventory.publication.content_epoch = content_epoch;
    g_player_inventory.publication.map_generation = map_generation;
    g_player_inventory.publication.owner_thread = owner_thread;
    g_player_inventory.publication.valid_live =
        player_di ? 1U : 0U;
    g_player_inventory.publication.publication_sequence =
        g_player_inventory.publication_counter;
    _InterlockedExchange64(
        &g_player_inventory.stable_publication_sequence, 0);
    dluma_player_inventory_end_write();
    dluma_player_inventory_unlock();
}

void dluma_player_inventory_publication_snapshot(
    DlumaPlayerInventoryPublication* output)
{
    LONG before;
    LONG after;
    unsigned int attempt;
    if (!output) return;
    memset(output, 0, sizeof(*output));
    for (attempt = 0; attempt < 8U; ++attempt) {
        before = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before & 1) continue;
        memcpy(
            output, &g_player_inventory.publication,
            sizeof(*output));
        after = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before == after && !(after & 1)) return;
    }
    memset(output, 0, sizeof(*output));
}

static void dluma_player_inventory_mark_tree_unavailable(
    DlumaPlayerInventoryTreeSnapshot* output,
    uint32_t failure)
{
    if (!output) return;
    output->unknown_row_count = DLUMA_PLAYER_INVENTORY_ROW_COUNT;
    output->invalid_row_count = DLUMA_PLAYER_INVENTORY_ROW_COUNT;
    output->first_invalid_row = 0;
    output->rows[0].child_kind =
        DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING;
    output->rows[0].exact_failure = failure;
}

static void dluma_player_inventory_summarize_row(
    DlumaPlayerInventoryTreeSnapshot* output,
    uint32_t row_index)
{
    DlumaPlayerInventoryRow* row;
    if (!output || row_index >= DLUMA_PLAYER_INVENTORY_ROW_COUNT)
        return;
    row = &output->rows[row_index];
    switch ((DlumaPlayerInventoryChildKind)row->child_kind) {
    case DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR:
        ++output->item_vector_row_count;
        if (row->item_vector_contract_applicable &&
            row->item_vector_valid) {
            ++output->valid_item_vector_row_count;
            ++output->valid_row_count;
        } else {
            ++output->invalid_item_vector_row_count;
            ++output->invalid_row_count;
            if (output->first_invalid_row == UINT32_MAX)
                output->first_invalid_row = row_index;
        }
        break;
    case DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD:
        ++output->non_item_row_count;
        ++output->valid_row_count;
        break;
    case DLUMA_PLAYER_INVENTORY_CHILD_ABSENT:
        ++output->absent_row_count;
        ++output->valid_row_count;
        break;
    case DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING:
    default:
        ++output->unknown_row_count;
        ++output->invalid_row_count;
        if (output->first_invalid_row == UINT32_MAX)
            output->first_invalid_row = row_index;
        break;
    }
}

int dluma_player_inventory_capture_tree(
    const DlumaPlayerInventoryPublication* publication,
    uint32_t phase,
    uint32_t thread_id,
    uint32_t content_epoch,
    uint64_t destroy_generation,
    uint64_t route_token,
    uintptr_t session,
    uintptr_t level,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerInventoryTreeSnapshot* output)
{
    uint32_t group;
    uint32_t child;
    uint32_t row_index;
    uintptr_t player;
    uintptr_t owner;
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    output->first_invalid_row = UINT32_MAX;
    output->phase = phase;
    output->thread_id = thread_id;
    output->content_epoch = content_epoch;
    output->destroy_generation = destroy_generation;
    output->route_token = route_token;
    output->session = session;
    output->level = level;
    if (!publication || !publication->valid_live) {
        dluma_player_inventory_mark_tree_unavailable(
            output, DLUMA_PLAYER_INVENTORY_FAILURE_NO_PUBLICATION);
        return 1;
    }
    memcpy(
        &output->publication, publication,
        sizeof(output->publication));
    player = publication->player_di;
    owner = publication->inventory_owner;
    if (!dluma_player_inventory_read_pointer(
            read_memory, callback_user, player,
            &output->player_vtable)) {
        dluma_player_inventory_mark_tree_unavailable(
            output,
            DLUMA_PLAYER_INVENTORY_FAILURE_PLAYER_UNREADABLE);
        return 1;
    }
    if (!dluma_player_inventory_read_pointer(
            read_memory, callback_user, owner,
            &output->owner_vtable)) {
        dluma_player_inventory_mark_tree_unavailable(
            output,
            DLUMA_PLAYER_INVENTORY_FAILURE_OWNER_UNREADABLE);
        return 1;
    }
    for (group = 0; group < DLUMA_PLAYER_INVENTORY_GROUP_COUNT;
         ++group) {
        uintptr_t container = 0;
        uintptr_t container_vtable = 0;
        uintptr_t group_address =
            owner + DLUMA_PLAYER_INVENTORY_GROUP_POINTERS_OFFSET +
            (uintptr_t)group * sizeof(uintptr_t);
        int container_readable =
            dluma_player_inventory_read_pointer(
                read_memory, callback_user, group_address,
                &container);
        output->containers[group] = container;
        if (container)
            container_readable =
                container_readable &&
                dluma_player_inventory_read_pointer(
                    read_memory, callback_user, container,
                    &container_vtable);
        for (child = 0; child < DLUMA_PLAYER_INVENTORY_CHILD_COUNT;
             ++child) {
            DlumaPlayerInventoryRow* row;
            uintptr_t child_inventory = 0;
            row_index =
                group * DLUMA_PLAYER_INVENTORY_CHILD_COUNT + child;
            row = &output->rows[row_index];
            row->group_index = group;
            row->child_index = child;
            row->container = container;
            row->child_kind =
                DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING;
            if (!container) {
                row->exact_failure =
                    DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CONTAINER;
            } else if (!container_readable) {
                row->exact_failure =
                    DLUMA_PLAYER_INVENTORY_FAILURE_CONTAINER_UNREADABLE;
            } else if (!dluma_player_inventory_read_pointer(
                           read_memory, callback_user,
                           container +
                               DLUMA_PLAYER_INVENTORY_CHILD_POINTERS_OFFSET +
                               (uintptr_t)child * sizeof(uintptr_t),
                           &child_inventory)) {
                row->exact_failure =
                    DLUMA_PLAYER_INVENTORY_FAILURE_CONTAINER_UNREADABLE;
            } else {
                DlumaPlayerInventoryRow captured;
                (void)dluma_player_inventory_capture_profiled_child(
                    child_inventory, child, read_memory,
                    query_memory, callback_user, &captured);
                captured.group_index = group;
                captured.child_index = child;
                captured.container = container;
                memcpy(row, &captured, sizeof(*row));
            }
            dluma_player_inventory_summarize_row(output, row_index);
        }
    }
    output->tree_profile_complete =
        output->item_vector_row_count ==
            DLUMA_PLAYER_INVENTORY_EXPECTED_ITEM_VECTOR_ROWS &&
        output->non_item_row_count ==
            DLUMA_PLAYER_INVENTORY_EXPECTED_NON_ITEM_ROWS &&
        output->absent_row_count ==
            DLUMA_PLAYER_INVENTORY_EXPECTED_ABSENT_ROWS &&
        output->unknown_row_count == 0U;
    output->item_vector_tree_valid =
        output->tree_profile_complete &&
        output->valid_item_vector_row_count ==
            DLUMA_PLAYER_INVENTORY_EXPECTED_ITEM_VECTOR_ROWS &&
        output->invalid_item_vector_row_count == 0U;
    output->complete = output->tree_profile_complete;
    return 1;
}

int dluma_player_inventory_find_header(
    const DlumaPlayerInventoryTreeSnapshot* snapshot,
    uintptr_t vector_header_address,
    uint32_t* group_index,
    uint32_t* child_index)
{
    uint32_t index;
    if (!snapshot || !vector_header_address) return 0;
    for (index = 0; index < DLUMA_PLAYER_INVENTORY_ROW_COUNT;
         ++index) {
        const DlumaPlayerInventoryRow* row = &snapshot->rows[index];
        if (row->vector_header_address != vector_header_address)
            continue;
        if (group_index) *group_index = row->group_index;
        if (child_index) *child_index = row->child_index;
        return 1;
    }
    return 0;
}

int dluma_player_inventory_capture_and_publish_tree(
    uint32_t phase,
    uint32_t thread_id,
    uint32_t content_epoch,
    uint64_t destroy_generation,
    uint64_t route_token,
    uintptr_t session,
    uintptr_t level,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user)
{
    DlumaPlayerInventoryPublication publication;
    DlumaPlayerInventoryTreeSnapshot snapshot;
    dluma_player_inventory_publication_snapshot(&publication);
    if (phase == DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY &&
        publication.publication_sequence &&
        publication.publication_sequence ==
            (uint64_t)_InterlockedCompareExchange64(
                &g_player_inventory.stable_publication_sequence,
                0, 0))
        return 1;
    if (!dluma_player_inventory_capture_tree(
            &publication, phase, thread_id, content_epoch,
            destroy_generation, route_token, session, level,
            read_memory, query_memory, callback_user, &snapshot))
        return 0;
    if (!dluma_player_inventory_lock()) return 0;
    dluma_player_inventory_begin_write();
    snapshot.capture_sequence =
        ++g_player_inventory.next_sequence;
    memcpy(
        &g_player_inventory.last_tree, &snapshot,
        sizeof(snapshot));
    if (snapshot.tree_profile_complete)
        memcpy(
            &g_player_inventory.last_complete_tree, &snapshot,
            sizeof(snapshot));
    if (snapshot.item_vector_tree_valid) {
        memcpy(
            &g_player_inventory.last_valid_item_vector_tree,
            &snapshot, sizeof(snapshot));
        memcpy(
            &g_player_inventory.last_valid_tree, &snapshot,
            sizeof(snapshot));
    }
    if (phase == DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY)
        _InterlockedExchange64(
            &g_player_inventory.stable_publication_sequence,
            (long long)publication.publication_sequence);
    dluma_player_inventory_end_write();
    dluma_player_inventory_unlock();
    return 1;
}

static int dluma_player_inventory_headers_differ(
    const DlumaPlayerDiVectorHeader* left,
    const DlumaPlayerDiVectorHeader* right)
{
    return left && right &&
        (left->data != right->data ||
         left->count != right->count ||
         left->capacity != right->capacity ||
         left->tag_or_field != right->tag_or_field);
}

static int dluma_player_inventory_failure_is_unreadable(
    uint32_t failure)
{
    switch ((DlumaPlayerInventoryFailure)failure) {
    case DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_UNREADABLE:
    case DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_UNREADABLE:
    case DLUMA_PLAYER_INVENTORY_FAILURE_DATA_UNREADABLE:
    case DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_UNREADABLE:
        return 1;
    default:
        return 0;
    }
}

static void dluma_player_inventory_latch_transition(
    DlumaPlayerInventoryFirstTransition* latch,
    const DlumaPlayerInventoryEvent* event)
{
    if (!latch || !event || latch->present) return;
    latch->present = 1;
    latch->event = *event;
}

static void dluma_player_inventory_begin_invalid_evidence(
    const DlumaPlayerInventoryEvent* event)
{
    DlumaPlayerInventoryInvalidTransitionEvidence* evidence =
        &g_player_inventory.first_invalid_evidence;
    uint32_t count;
    uint32_t start;
    uint32_t index;
    if (!event || evidence->present) return;
    memset(evidence, 0, sizeof(*evidence));
    evidence->present = 1;
    evidence->invalidating_event = *event;
    count = g_player_inventory.event_count;
    if (count > DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY)
        count = DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY;
    evidence->preceding_count = count;
    start =
        (g_player_inventory.event_write_index +
         DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY - count) %
        DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY;
    for (index = 0; index < count; ++index)
        evidence->preceding[index] =
            g_player_inventory.events[
                (start + index) %
                DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY];
}

static void dluma_player_inventory_append_invalid_following(
    const DlumaPlayerInventoryEvent* event)
{
    DlumaPlayerInventoryInvalidTransitionEvidence* evidence =
        &g_player_inventory.first_invalid_evidence;
    if (!event || !evidence->present ||
        event->sequence <= evidence->invalidating_event.sequence ||
        evidence->following_count >=
            DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY)
        return;
    evidence->following[evidence->following_count++] = *event;
    if (evidence->following_count ==
        DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY)
        evidence->following_complete = 1;
}

static void dluma_player_inventory_record_event(
    uint32_t phase,
    uint32_t thread_id,
    uint32_t clear_depth,
    uint32_t observer_depth,
    uintptr_t inventory,
    uintptr_t item,
    uint32_t reason,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user)
{
    DlumaPlayerInventoryEvent event;
    DlumaPlayerInventoryRow row;
    DlumaPlayerInventoryTreeSnapshot tree;
    uint32_t index;
    int have_previous = 0;
    memset(&event, 0, sizeof(event));
    memset(&row, 0, sizeof(row));
    memset(&tree, 0, sizeof(tree));
    (void)dluma_player_inventory_capture_vector(
        inventory, read_memory, query_memory, callback_user, &row);
    event.phase = phase;
    event.thread_id = thread_id;
    event.clear_depth = clear_depth;
    event.observer_depth = observer_depth;
    event.reason = reason;
    event.group_index = UINT32_MAX;
    event.child_index = UINT32_MAX;
    event.outer_loop_index = UINT32_MAX;
    event.inventory = inventory;
    event.item = item;
    event.vector_header_address = row.vector_header_address;
    event.current = row.header;
    event.item_vector_contract_applicable =
        row.item_vector_contract_applicable;
    event.current_valid = row.item_vector_valid;
    event.current_failure = row.exact_failure;

    if (!dluma_player_inventory_lock()) return;
    dluma_player_inventory_begin_write();
    tree = g_player_inventory.last_tree;
    if (phase == DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_ENTRY) {
        uint32_t prior_observer_entries = 0;
        for (index = 0; index < g_player_inventory.event_count;
             ++index) {
            uint32_t ring_index =
                (g_player_inventory.event_write_index +
                 DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY - 1U -
                 index) % DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY;
            const DlumaPlayerInventoryEvent* prior =
                &g_player_inventory.events[ring_index];
            if (prior->thread_id != event.thread_id ||
                prior->inventory != event.inventory ||
                prior->clear_depth != event.clear_depth)
                continue;
            if (prior->phase ==
                    DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_ENTRY) {
                event.outer_loop_index =
                    prior_observer_entries;
                break;
            }
            if (prior->phase ==
                    DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_ENTRY)
                ++prior_observer_entries;
        }
    } else if (
        phase == DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_EXIT ||
        phase == DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_EXIT ||
        phase ==
            DLUMA_PLAYER_INVENTORY_PHASE_GET_ALL_ITEMS_ENTRY) {
        for (index = 0; index < g_player_inventory.event_count;
             ++index) {
            uint32_t ring_index =
                (g_player_inventory.event_write_index +
                 DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY - 1U -
                 index) % DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY;
            const DlumaPlayerInventoryEvent* prior =
                &g_player_inventory.events[ring_index];
            if (prior->thread_id != event.thread_id ||
                prior->clear_depth != event.clear_depth ||
                prior->outer_loop_index == UINT32_MAX)
                continue;
            if (phase ==
                    DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_EXIT &&
                (prior->phase !=
                     DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_ENTRY ||
                 prior->inventory != event.inventory ||
                 prior->observer_depth != event.observer_depth))
                continue;
            event.outer_loop_index =
                prior->outer_loop_index;
            break;
        }
    }
    for (index = 0; index < g_player_inventory.event_count;
         ++index) {
        uint32_t ring_index =
            (g_player_inventory.event_write_index +
             DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY - 1U - index) %
            DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY;
        const DlumaPlayerInventoryEvent* previous =
            &g_player_inventory.events[ring_index];
        if (previous->vector_header_address ==
                event.vector_header_address) {
            event.previous = previous->current;
            event.previous_valid = previous->current_valid;
            event.previous_failure = previous->current_failure;
            have_previous = 1;
            break;
        }
    }
    if (!have_previous) {
        for (index = 0; index < DLUMA_PLAYER_INVENTORY_ROW_COUNT;
             ++index) {
            const DlumaPlayerInventoryRow* previous =
                &tree.rows[index];
            if (previous->vector_header_address !=
                    event.vector_header_address)
                continue;
            event.previous = previous->header;
            event.previous_valid = previous->item_vector_valid;
            event.previous_failure = previous->exact_failure;
            have_previous = 1;
            break;
        }
    }
    (void)dluma_player_inventory_find_header(
        &tree, event.vector_header_address,
        &event.group_index, &event.child_index);
    event.sequence = ++g_player_inventory.next_sequence;
    if (event.item_vector_contract_applicable && have_previous) {
        if (dluma_player_inventory_headers_differ(
                &event.previous, &event.current))
            event.transition_kind_mask |=
                DLUMA_PLAYER_INVENTORY_TRANSITION_ANY_HEADER_CHANGE;
        if (event.previous_valid && !event.current_valid)
            event.transition_kind_mask |=
                DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID;
        if (event.previous.count == 0U &&
            event.current.count == UINT32_MAX)
            event.transition_kind_mask |=
                DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_UNDERFLOW;
        if (event.previous.count <= event.previous.capacity &&
            event.current.count > event.current.capacity)
            event.transition_kind_mask |=
                DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_GREATER_THAN_CAPACITY;
        if (event.previous_valid &&
            dluma_player_inventory_failure_is_unreadable(
                event.current_failure))
            event.transition_kind_mask |=
                DLUMA_PLAYER_INVENTORY_TRANSITION_UNREADABLE;
        if (event.previous_valid &&
            event.current_failure ==
                DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_CHANGED)
            event.transition_kind_mask |=
                DLUMA_PLAYER_INVENTORY_TRANSITION_HEADER_INSTABILITY;
    }
    if (event.item_vector_contract_applicable &&
        event.clear_depth > 1U)
        event.transition_kind_mask |=
            DLUMA_PLAYER_INVENTORY_TRANSITION_CLEAR_REENTRANCY;
    event.transition_latched =
        event.transition_kind_mask !=
            DLUMA_PLAYER_INVENTORY_TRANSITION_NONE;

    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_ANY_HEADER_CHANGE) {
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_any_header_change, &event);
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_transition, &event);
    }
    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID) {
        const int first_invalid =
            !g_player_inventory.first_invalid_transition.present;
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_invalid_transition, &event);
        if (first_invalid)
            dluma_player_inventory_begin_invalid_evidence(&event);
    }
    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_UNDERFLOW)
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_count_underflow_transition,
            &event);
    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_GREATER_THAN_CAPACITY)
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_count_greater_than_capacity,
            &event);
    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_UNREADABLE)
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_unreadable_transition, &event);
    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_HEADER_INSTABILITY)
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_header_instability, &event);
    if (event.transition_kind_mask &
        DLUMA_PLAYER_INVENTORY_TRANSITION_CLEAR_REENTRANCY)
        dluma_player_inventory_latch_transition(
            &g_player_inventory.first_clear_reentrancy_transition,
            &event);
    dluma_player_inventory_append_invalid_following(&event);
    g_player_inventory.events[
        g_player_inventory.event_write_index] = event;
    g_player_inventory.event_write_index =
        (g_player_inventory.event_write_index + 1U) %
        DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY;
    if (g_player_inventory.event_count <
            DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY)
        ++g_player_inventory.event_count;
    else
        ++g_player_inventory.event_overwrite_count;
    dluma_player_inventory_end_write();
    dluma_player_inventory_unlock();
}

uintptr_t dluma_player_inventory_observe_clear_call(
    DlumaPlayerInventoryClearOriginal original,
    void* inventory,
    unsigned int reason,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user)
{
    uintptr_t result;
    LONG depth;
    if (!original) return 0;
    depth = dluma_atomic_increment(&g_player_inventory.clear_depth);
    dluma_player_inventory_record_event(
        DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_ENTRY,
        GetCurrentThreadId(), (uint32_t)depth,
        (uint32_t)dluma_atomic_compare_exchange(
            &g_player_inventory.observer_depth, 0, 0),
        (uintptr_t)inventory, 0, reason, read_memory, query_memory,
        callback_user);
    result = original(inventory, reason);
    dluma_player_inventory_record_event(
        DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_EXIT,
        GetCurrentThreadId(), (uint32_t)depth,
        (uint32_t)dluma_atomic_compare_exchange(
            &g_player_inventory.observer_depth, 0, 0),
        (uintptr_t)inventory, 0, reason, read_memory, query_memory,
        callback_user);
    (void)dluma_atomic_decrement(&g_player_inventory.clear_depth);
    return result;
}

void dluma_player_inventory_observe_removed_call(
    DlumaPlayerInventoryObserverOriginal original,
    void* observer,
    void* inventory,
    void* item,
    int reason,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user)
{
    LONG depth;
    if (!original) return;
    depth = dluma_atomic_increment(&g_player_inventory.observer_depth);
    dluma_player_inventory_record_event(
        DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_ENTRY,
        GetCurrentThreadId(),
        (uint32_t)dluma_atomic_compare_exchange(
            &g_player_inventory.clear_depth, 0, 0),
        (uint32_t)depth, (uintptr_t)inventory, (uintptr_t)item,
        (uint32_t)reason, read_memory, query_memory, callback_user);
    original(observer, inventory, item, reason);
    dluma_player_inventory_record_event(
        DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_EXIT,
        GetCurrentThreadId(),
        (uint32_t)dluma_atomic_compare_exchange(
            &g_player_inventory.clear_depth, 0, 0),
        (uint32_t)depth, (uintptr_t)inventory, (uintptr_t)item,
        (uint32_t)reason, read_memory, query_memory, callback_user);
    (void)dluma_atomic_decrement(&g_player_inventory.observer_depth);
}

uintptr_t dluma_player_inventory_observe_get_all_items_call(
    DlumaPlayerInventoryGetAllItemsOriginal original,
    void* inventory,
    void* output,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user)
{
    if (!original) return 0;
    dluma_player_inventory_record_event(
        DLUMA_PLAYER_INVENTORY_PHASE_GET_ALL_ITEMS_ENTRY,
        GetCurrentThreadId(),
        (uint32_t)dluma_atomic_compare_exchange(
            &g_player_inventory.clear_depth, 0, 0),
        (uint32_t)dluma_atomic_compare_exchange(
            &g_player_inventory.observer_depth, 0, 0),
        (uintptr_t)inventory, (uintptr_t)output, 0,
        read_memory, query_memory, callback_user);
    return original(inventory, output);
}

void dluma_player_inventory_state_five_publish(
    const DlumaPlayerInventoryStateFiveCrashState* state)
{
    if (!state || !dluma_player_inventory_lock()) return;
    dluma_player_inventory_begin_write();
    g_player_inventory.state_five = *state;
    g_player_inventory.state_five.publication_sequence =
        ++g_player_inventory.next_sequence;
    g_player_inventory.state_five.periodic_report_stale =
        g_player_inventory.state_five.periodic_report_finalized
            ? 0U : 1U;
    dluma_player_inventory_end_write();
    dluma_player_inventory_unlock();
}

void dluma_player_inventory_state_five_snapshot(
    DlumaPlayerInventoryStateFiveCrashState* output)
{
    LONG before;
    LONG after;
    unsigned int attempt;
    if (!output) return;
    memset(output, 0, sizeof(*output));
    for (attempt = 0; attempt < 8U; ++attempt) {
        before = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before & 1) continue;
        *output = g_player_inventory.state_five;
        after = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before == after && !(after & 1)) return;
    }
    memset(output, 0, sizeof(*output));
}

void dluma_player_inventory_periodic_report_finalized(int finalized)
{
    if (!dluma_player_inventory_lock()) return;
    dluma_player_inventory_begin_write();
    g_player_inventory.state_five.periodic_report_finalized =
        finalized ? 1U : 0U;
    g_player_inventory.state_five.periodic_report_stale =
        finalized ? 0U : 1U;
    dluma_player_inventory_end_write();
    dluma_player_inventory_unlock();
}

int dluma_player_inventory_runtime_snapshot(
    DlumaPlayerInventoryRuntimeSnapshot* output)
{
    LONG before;
    LONG after;
    unsigned int attempt;
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    for (attempt = 0; attempt < 8U; ++attempt) {
        before = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before & 1) continue;
        output->diagnostic_enabled =
            (uint32_t)dluma_atomic_compare_exchange(
                &g_player_inventory.diagnostic_enabled, 0, 0);
        output->exact_profile_validated =
            (uint32_t)dluma_atomic_compare_exchange(
                &g_player_inventory.exact_profile_validated, 0, 0);
        output->dropped_writes =
            (uint32_t)dluma_atomic_compare_exchange(
                &g_player_inventory.dropped_writes, 0, 0);
        output->clear_depth =
            (uint32_t)dluma_atomic_compare_exchange(
                &g_player_inventory.clear_depth, 0, 0);
        output->observer_depth =
            (uint32_t)dluma_atomic_compare_exchange(
                &g_player_inventory.observer_depth, 0, 0);
        output->publication = g_player_inventory.publication;
        output->last_tree = g_player_inventory.last_tree;
        output->last_complete_tree =
            g_player_inventory.last_complete_tree;
        output->last_valid_item_vector_tree =
            g_player_inventory.last_valid_item_vector_tree;
        output->last_valid_tree = g_player_inventory.last_valid_tree;
        output->first_transition =
            g_player_inventory.first_transition;
        output->first_any_header_change =
            g_player_inventory.first_any_header_change;
        output->first_invalid_transition =
            g_player_inventory.first_invalid_transition;
        output->first_count_underflow_transition =
            g_player_inventory.first_count_underflow_transition;
        output->first_count_greater_than_capacity =
            g_player_inventory.first_count_greater_than_capacity;
        output->first_unreadable_transition =
            g_player_inventory.first_unreadable_transition;
        output->first_header_instability =
            g_player_inventory.first_header_instability;
        output->first_clear_reentrancy_transition =
            g_player_inventory.first_clear_reentrancy_transition;
        output->first_invalid_evidence =
            g_player_inventory.first_invalid_evidence;
        memcpy(
            output->events, g_player_inventory.events,
            sizeof(output->events));
        output->event_count = g_player_inventory.event_count;
        output->event_write_index =
            g_player_inventory.event_write_index;
        output->event_overwrite_count =
            g_player_inventory.event_overwrite_count;
        output->state_five = g_player_inventory.state_five;
        after = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before == after && !(after & 1)) return 1;
    }
    memset(output, 0, sizeof(*output));
    return 0;
}

int dluma_player_inventory_runtime_find_header(
    uintptr_t vector_header_address,
    uint32_t* group_index,
    uint32_t* child_index)
{
    LONG before;
    LONG after;
    unsigned int attempt;
    int found;
    for (attempt = 0; attempt < 8U; ++attempt) {
        before = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before & 1) continue;
        found = dluma_player_inventory_find_header(
            &g_player_inventory.last_tree,
            vector_header_address, group_index, child_index);
        if (!found)
            found = dluma_player_inventory_find_header(
                &g_player_inventory.last_valid_tree,
                vector_header_address, group_index, child_index);
        after = dluma_atomic_compare_exchange(
            &g_player_inventory.snapshot_seq, 0, 0);
        if (before == after && !(after & 1)) return found;
    }
    return 0;
}

const char* dluma_player_inventory_phase_name(uint32_t phase)
{
    switch ((DlumaPlayerInventoryPhase)phase) {
    case DLUMA_PLAYER_INVENTORY_PHASE_PLAYER_ON_CREATE:
        return "player_on_create";
    case DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY:
        return "stable_gameplay";
    case DLUMA_PLAYER_INVENTORY_PHASE_C08_ENTRY: return "c08_entry";
    case DLUMA_PLAYER_INVENTORY_PHASE_C08_RETURN: return "c08_return";
    case DLUMA_PLAYER_INVENTORY_PHASE_C10_ENTRY: return "c10_entry";
    case DLUMA_PLAYER_INVENTORY_PHASE_SESSION_ON_DESTROY_ENTRY:
        return "session_on_destroy_entry";
    case DLUMA_PLAYER_INVENTORY_PHASE_UNLOAD_LEVEL_ENTRY:
        return "unload_level_entry";
    case DLUMA_PLAYER_INVENTORY_PHASE_PRE_PLAYER_TEARDOWN:
        return "pre_player_teardown";
    case DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_ENTRY: return "clear_entry";
    case DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_ENTRY:
        return "observer_entry";
    case DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_EXIT:
        return "observer_exit";
    case DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_EXIT: return "clear_exit";
    case DLUMA_PLAYER_INVENTORY_PHASE_GET_ALL_ITEMS_ENTRY:
        return "get_all_items_entry";
    default: return "none";
    }
}

const char* dluma_player_inventory_failure_name(uint32_t failure)
{
    switch ((DlumaPlayerInventoryFailure)failure) {
    case DLUMA_PLAYER_INVENTORY_FAILURE_NONE: return "none";
    case DLUMA_PLAYER_INVENTORY_FAILURE_NO_PUBLICATION:
        return "no_publication";
    case DLUMA_PLAYER_INVENTORY_FAILURE_PLAYER_UNREADABLE:
        return "player_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_OWNER_UNREADABLE:
        return "owner_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CONTAINER:
        return "null_container";
    case DLUMA_PLAYER_INVENTORY_FAILURE_CONTAINER_UNREADABLE:
        return "container_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CHILD: return "null_child";
    case DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_UNREADABLE:
        return "child_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_UNREADABLE:
        return "header_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_CHANGED:
        return "header_changed";
    case DLUMA_PLAYER_INVENTORY_FAILURE_COUNT_GREATER_THAN_CAPACITY:
        return "count_greater_than_capacity";
    case DLUMA_PLAYER_INVENTORY_FAILURE_CAPACITY_CEILING:
        return "capacity_ceiling";
    case DLUMA_PLAYER_INVENTORY_FAILURE_EMPTY_DATA_REPRESENTATION:
        return "empty_data_representation";
    case DLUMA_PLAYER_INVENTORY_FAILURE_DATA_REQUIRED:
        return "data_required";
    case DLUMA_PLAYER_INVENTORY_FAILURE_DATA_UNREADABLE:
        return "data_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_OVERFLOW:
        return "element_span_overflow";
    case DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_UNREADABLE:
        return "element_span_unreadable";
    case DLUMA_PLAYER_INVENTORY_FAILURE_TAG_MISMATCH:
        return "tag_mismatch";
    case DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_PROFILE_UNKNOWN_OR_CONFLICTING:
        return "child_profile_unknown_or_conflicting";
    default: return "unknown";
    }
}

const char* dluma_player_inventory_child_kind_name(uint32_t kind)
{
    switch ((DlumaPlayerInventoryChildKind)kind) {
    case DLUMA_PLAYER_INVENTORY_CHILD_ABSENT:
        return "Absent";
    case DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR:
        return "ItemVector";
    case DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD:
        return "NonItemInventoryChild";
    case DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING:
        return "UnknownOrConflicting";
    default:
        return "UnknownOrConflicting";
    }
}
