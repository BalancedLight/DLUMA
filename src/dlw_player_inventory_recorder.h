#ifndef DLUMA_PLAYER_INVENTORY_RECORDER_H
#define DLUMA_PLAYER_INVENTORY_RECORDER_H

#include "dlw_playerdi_teardown.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_PLAYER_INVENTORY_GROUP_COUNT 5U
#define DLUMA_PLAYER_INVENTORY_CHILD_COUNT 10U
#define DLUMA_PLAYER_INVENTORY_ROW_COUNT \
    (DLUMA_PLAYER_INVENTORY_GROUP_COUNT * \
     DLUMA_PLAYER_INVENTORY_CHILD_COUNT)
#define DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY 32U
#define DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY 4U
#define DLUMA_PLAYER_INVENTORY_OWNER_OFFSET 0x0928U
#define DLUMA_PLAYER_INVENTORY_GROUP_POINTERS_OFFSET 0x0008U
#define DLUMA_PLAYER_INVENTORY_CHILD_POINTERS_OFFSET 0x0040U
#define DLUMA_PLAYER_INVENTORY_VECTOR_OFFSET 0x0040U
#define DLUMA_PLAYER_INVENTORY_EXPECTED_VECTOR_TAG 0x33U
#define DLUMA_PLAYER_INVENTORY_DIAGNOSTIC_CAPACITY_CEILING 0x00100000U
#define DLUMA_PLAYER_INVENTORY_EXPECTED_ITEM_VECTOR_ROWS \
    (DLUMA_PLAYER_INVENTORY_GROUP_COUNT * 3U)
#define DLUMA_PLAYER_INVENTORY_EXPECTED_NON_ITEM_ROWS \
    (DLUMA_PLAYER_INVENTORY_GROUP_COUNT * 5U)
#define DLUMA_PLAYER_INVENTORY_EXPECTED_ABSENT_ROWS \
    (DLUMA_PLAYER_INVENTORY_GROUP_COUNT * 2U)

/*
 * Exact RTTI-backed vftable RVAs for the supported DevTools Player image.
 * The full gamedll SHA-256 gate remains authoritative; these values are never
 * accepted for an unvalidated image.
 */
#define DLUMA_PLAYER_INVENTORY_VTABLE_AMMO_RVA 0x01804608U
#define DLUMA_PLAYER_INVENTORY_VTABLE_COLLECTABLE_RVA 0x018046B8U
#define DLUMA_PLAYER_INVENTORY_VTABLE_LOOSE_ITEMS_RVA 0x01804C88U
#define DLUMA_PLAYER_INVENTORY_VTABLE_MAIN_RVA 0x01804D38U
#define DLUMA_PLAYER_INVENTORY_VTABLE_MONEY_RVA 0x01804E08U
#define DLUMA_PLAYER_INVENTORY_VTABLE_HELLRAID_MONEY_RVA 0x01804EC8U
#define DLUMA_PLAYER_INVENTORY_VTABLE_REPAIR_PART_RVA 0x01805038U
#define DLUMA_PLAYER_INVENTORY_VTABLE_SPECIAL_RVA 0x018051F8U

typedef enum DlumaPlayerInventoryChildKind {
    DLUMA_PLAYER_INVENTORY_CHILD_ABSENT = 0,
    DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR,
    DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD,
    DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING
} DlumaPlayerInventoryChildKind;

typedef enum DlumaPlayerInventoryTransitionKind {
    DLUMA_PLAYER_INVENTORY_TRANSITION_NONE = 0,
    DLUMA_PLAYER_INVENTORY_TRANSITION_ANY_HEADER_CHANGE = 1U << 0,
    DLUMA_PLAYER_INVENTORY_TRANSITION_VALID_TO_INVALID = 1U << 1,
    DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_UNDERFLOW = 1U << 2,
    DLUMA_PLAYER_INVENTORY_TRANSITION_COUNT_GREATER_THAN_CAPACITY = 1U << 3,
    DLUMA_PLAYER_INVENTORY_TRANSITION_UNREADABLE = 1U << 4,
    DLUMA_PLAYER_INVENTORY_TRANSITION_HEADER_INSTABILITY = 1U << 5,
    DLUMA_PLAYER_INVENTORY_TRANSITION_CLEAR_REENTRANCY = 1U << 6
} DlumaPlayerInventoryTransitionKind;

typedef enum DlumaPlayerInventoryPhase {
    DLUMA_PLAYER_INVENTORY_PHASE_NONE = 0,
    DLUMA_PLAYER_INVENTORY_PHASE_PLAYER_ON_CREATE,
    DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY,
    DLUMA_PLAYER_INVENTORY_PHASE_C08_ENTRY,
    DLUMA_PLAYER_INVENTORY_PHASE_C08_RETURN,
    DLUMA_PLAYER_INVENTORY_PHASE_C10_ENTRY,
    DLUMA_PLAYER_INVENTORY_PHASE_SESSION_ON_DESTROY_ENTRY,
    DLUMA_PLAYER_INVENTORY_PHASE_UNLOAD_LEVEL_ENTRY,
    DLUMA_PLAYER_INVENTORY_PHASE_PRE_PLAYER_TEARDOWN,
    DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_ENTRY,
    DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_ENTRY,
    DLUMA_PLAYER_INVENTORY_PHASE_OBSERVER_EXIT,
    DLUMA_PLAYER_INVENTORY_PHASE_CLEAR_EXIT,
    DLUMA_PLAYER_INVENTORY_PHASE_GET_ALL_ITEMS_ENTRY
} DlumaPlayerInventoryPhase;

typedef enum DlumaPlayerInventoryFailure {
    DLUMA_PLAYER_INVENTORY_FAILURE_NONE = 0,
    DLUMA_PLAYER_INVENTORY_FAILURE_NO_PUBLICATION,
    DLUMA_PLAYER_INVENTORY_FAILURE_PLAYER_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_OWNER_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CONTAINER,
    DLUMA_PLAYER_INVENTORY_FAILURE_CONTAINER_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_NULL_CHILD,
    DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_HEADER_CHANGED,
    DLUMA_PLAYER_INVENTORY_FAILURE_COUNT_GREATER_THAN_CAPACITY,
    DLUMA_PLAYER_INVENTORY_FAILURE_CAPACITY_CEILING,
    DLUMA_PLAYER_INVENTORY_FAILURE_EMPTY_DATA_REPRESENTATION,
    DLUMA_PLAYER_INVENTORY_FAILURE_DATA_REQUIRED,
    DLUMA_PLAYER_INVENTORY_FAILURE_DATA_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_OVERFLOW,
    DLUMA_PLAYER_INVENTORY_FAILURE_ELEMENT_SPAN_UNREADABLE,
    DLUMA_PLAYER_INVENTORY_FAILURE_TAG_MISMATCH,
    DLUMA_PLAYER_INVENTORY_FAILURE_CHILD_PROFILE_UNKNOWN_OR_CONFLICTING
} DlumaPlayerInventoryFailure;

typedef struct DlumaPlayerInventoryPublication {
    uintptr_t player_di;
    uintptr_t inventory_owner;
    uint64_t creation_generation;
    uint32_t content_epoch;
    uint32_t map_generation;
    uint32_t owner_thread;
    uint32_t valid_live;
    uint64_t publication_sequence;
} DlumaPlayerInventoryPublication;

typedef struct DlumaPlayerInventoryRow {
    uint32_t group_index;
    uint32_t child_index;
    uintptr_t container;
    uintptr_t child_inventory;
    uintptr_t child_vtable;
    uint32_t profile_vtable_rva;
    uint32_t child_kind;
    uint32_t profile_validated;
    uint32_t item_vector_contract_applicable;
    uint32_t item_vector_valid;
    uintptr_t raw_header_address;
    DlumaPlayerDiVectorHeader raw_header;
    uint32_t raw_header_readable;
    uint32_t raw_header_stable;
    uintptr_t vector_header_address;
    DlumaPlayerDiVectorHeader header;
    DlumaPlayerDiMemoryRegion object_region;
    DlumaPlayerDiMemoryRegion header_region;
    DlumaPlayerDiMemoryRegion data_region;
    DlumaPlayerDiMemoryRegion end_element_region;
    uint32_t header_readable;
    uint32_t header_stable;
    uint32_t invariant_valid;
    uint32_t exact_failure;
} DlumaPlayerInventoryRow;

typedef struct DlumaPlayerInventoryTreeSnapshot {
    uint64_t capture_sequence;
    uint32_t phase;
    uint32_t thread_id;
    uint32_t content_epoch;
    uint64_t destroy_generation;
    uint64_t route_token;
    uintptr_t session;
    uintptr_t level;
    DlumaPlayerInventoryPublication publication;
    uintptr_t player_vtable;
    uintptr_t owner_vtable;
    uintptr_t containers[DLUMA_PLAYER_INVENTORY_GROUP_COUNT];
    DlumaPlayerInventoryRow rows[DLUMA_PLAYER_INVENTORY_ROW_COUNT];
    uint32_t valid_row_count;
    uint32_t invalid_row_count;
    uint32_t first_invalid_row;
    uint32_t complete;
    uint32_t item_vector_row_count;
    uint32_t valid_item_vector_row_count;
    uint32_t invalid_item_vector_row_count;
    uint32_t non_item_row_count;
    uint32_t absent_row_count;
    uint32_t unknown_row_count;
    uint32_t tree_profile_complete;
    uint32_t item_vector_tree_valid;
} DlumaPlayerInventoryTreeSnapshot;

typedef struct DlumaPlayerInventoryEvent {
    uint64_t sequence;
    uint32_t phase;
    uint32_t thread_id;
    uint32_t clear_depth;
    uint32_t observer_depth;
    uint32_t reason;
    uint32_t group_index;
    uint32_t child_index;
    uint32_t outer_loop_index;
    uintptr_t inventory;
    uintptr_t item;
    uintptr_t vector_header_address;
    DlumaPlayerDiVectorHeader previous;
    DlumaPlayerDiVectorHeader current;
    uint32_t item_vector_contract_applicable;
    uint32_t previous_valid;
    uint32_t previous_failure;
    uint32_t current_valid;
    uint32_t current_failure;
    uint32_t transition_kind_mask;
    uint32_t transition_latched;
} DlumaPlayerInventoryEvent;

typedef struct DlumaPlayerInventoryFirstTransition {
    uint32_t present;
    uint32_t reserved;
    DlumaPlayerInventoryEvent event;
} DlumaPlayerInventoryFirstTransition;

typedef struct DlumaPlayerInventoryInvalidTransitionEvidence {
    uint32_t present;
    uint32_t preceding_count;
    uint32_t following_count;
    uint32_t following_complete;
    DlumaPlayerInventoryEvent invalidating_event;
    DlumaPlayerInventoryEvent
        preceding[DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY];
    DlumaPlayerInventoryEvent
        following[DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY];
} DlumaPlayerInventoryInvalidTransitionEvidence;

typedef struct DlumaPlayerInventoryStateFiveCrashState {
    uint64_t publication_sequence;
    uint64_t c08_token;
    uint64_t c08_return_count;
    uint64_t c10_entry_count;
    uint32_t c08_thread_id;
    uint32_t c10_thread_id;
    uint32_t c10_scope_result;
    uint32_t c10_scope_active;
    uint32_t c10_scope_armed;
    uint32_t armed_epoch;
    uintptr_t outgoing_session;
    uint32_t session_on_destroy_entry_count;
    uint32_t session_binding_matched;
    uintptr_t session_on_destroy_session;
    uint32_t unload_level_active_depth;
    uint32_t unload_level_thread_id;
    uintptr_t unload_level_session;
    uint32_t periodic_report_finalized;
    uint32_t periodic_report_stale;
} DlumaPlayerInventoryStateFiveCrashState;

typedef struct DlumaPlayerInventoryRuntimeSnapshot {
    uint32_t diagnostic_enabled;
    uint32_t exact_profile_validated;
    uint32_t dropped_writes;
    uint32_t event_count;
    uint32_t event_write_index;
    uint32_t event_overwrite_count;
    uint32_t clear_depth;
    uint32_t observer_depth;
    DlumaPlayerInventoryPublication publication;
    DlumaPlayerInventoryTreeSnapshot last_tree;
    DlumaPlayerInventoryTreeSnapshot last_complete_tree;
    DlumaPlayerInventoryTreeSnapshot last_valid_item_vector_tree;
    /* Compatibility alias for last_valid_item_vector_tree. */
    DlumaPlayerInventoryTreeSnapshot last_valid_tree;
    /* Compatibility alias for first_any_header_change. */
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
    DlumaPlayerInventoryStateFiveCrashState state_five;
} DlumaPlayerInventoryRuntimeSnapshot;

typedef uintptr_t (DLUMA_PLAYERDI_FASTCALL *DlumaPlayerInventoryClearOriginal)(
    void* inventory, unsigned int reason);
typedef void (DLUMA_PLAYERDI_FASTCALL *DlumaPlayerInventoryObserverOriginal)(
    void* observer, void* inventory, void* item, int reason);
typedef uintptr_t (DLUMA_PLAYERDI_FASTCALL *DlumaPlayerInventoryGetAllItemsOriginal)(
    void* inventory, void* output);

void dluma_player_inventory_recorder_reset(void);
void dluma_player_inventory_recorder_configure(
    int diagnostic_enabled, int exact_profile_validated,
    uintptr_t gamedll_base, size_t gamedll_size);

void dluma_player_inventory_publish_player(
    void* player_di,
    uint64_t creation_generation,
    uint32_t content_epoch,
    uint32_t map_generation,
    uint32_t owner_thread);

void dluma_player_inventory_publication_snapshot(
    DlumaPlayerInventoryPublication* output);

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
    DlumaPlayerInventoryTreeSnapshot* output);

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
    void* callback_user);

int dluma_player_inventory_capture_vector(
    uintptr_t inventory,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerInventoryRow* output);

int dluma_player_inventory_find_header(
    const DlumaPlayerInventoryTreeSnapshot* snapshot,
    uintptr_t vector_header_address,
    uint32_t* group_index,
    uint32_t* child_index);

uintptr_t dluma_player_inventory_observe_clear_call(
    DlumaPlayerInventoryClearOriginal original,
    void* inventory,
    unsigned int reason,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user);

void dluma_player_inventory_observe_removed_call(
    DlumaPlayerInventoryObserverOriginal original,
    void* observer,
    void* inventory,
    void* item,
    int reason,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user);

uintptr_t dluma_player_inventory_observe_get_all_items_call(
    DlumaPlayerInventoryGetAllItemsOriginal original,
    void* inventory,
    void* output,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user);

void dluma_player_inventory_state_five_publish(
    const DlumaPlayerInventoryStateFiveCrashState* state);
void dluma_player_inventory_state_five_snapshot(
    DlumaPlayerInventoryStateFiveCrashState* output);
void dluma_player_inventory_periodic_report_finalized(int finalized);

int dluma_player_inventory_runtime_snapshot(
    DlumaPlayerInventoryRuntimeSnapshot* output);
int dluma_player_inventory_runtime_find_header(
    uintptr_t vector_header_address,
    uint32_t* group_index,
    uint32_t* child_index);

const char* dluma_player_inventory_phase_name(uint32_t phase);
const char* dluma_player_inventory_failure_name(uint32_t failure);
const char* dluma_player_inventory_child_kind_name(uint32_t kind);

#ifdef __cplusplus
}
#endif

#endif
