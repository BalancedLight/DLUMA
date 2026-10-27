#include "../src/core/dlw_editor_model_animation_guard.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        return 0; \
    } \
} while (0)

#define MODEL_ADDRESS ((uintptr_t)0x1000U)
#define BIND_ADDRESS ((uintptr_t)0x2000U)
#define VTABLE_ADDRESS ((uintptr_t)0x3000U)
#define LOOKUP_ADDRESS ((uintptr_t)0x4000U)
#define PLAY_ADDRESS ((uintptr_t)0x5000U)

typedef struct FakeAccessState {
    uint32_t looped_animation_size;
    int model_readable;
    int bind_present;
    int bind_readable;
    int vtable_readable;
    int lookup_executable;
    int play_executable;
    unsigned int get_bind_calls;
    unsigned char model_state[32];
} FakeAccessState;

static int fake_read_u32(
    void* context,
    uintptr_t address,
    uint32_t* output)
{
    FakeAccessState* state = (FakeAccessState*)context;
    if (!state || !state->model_readable || !output ||
        address != MODEL_ADDRESS +
            DLUMA_EDITOR_MODEL_LOOPED_ANIMATION_SIZE_OFFSET)
        return 0;
    *output = state->looped_animation_size;
    return 1;
}

static uintptr_t fake_get_anim_bind(void* context, uintptr_t model)
{
    FakeAccessState* state = (FakeAccessState*)context;
    if (!state || model != MODEL_ADDRESS) return 0;
    ++state->get_bind_calls;
    return state->bind_present ? BIND_ADDRESS : 0;
}

static int fake_read_pointer(
    void* context,
    uintptr_t address,
    uintptr_t* output)
{
    FakeAccessState* state = (FakeAccessState*)context;
    if (!state || !output) return 0;
    if (address == BIND_ADDRESS) {
        if (!state->bind_readable) return 0;
        *output = VTABLE_ADDRESS;
        return 1;
    }
    if (!state->vtable_readable) return 0;
    if (address == VTABLE_ADDRESS +
            DLUMA_EDITOR_ANIM_BIND_LOOKUP_SLOT_OFFSET) {
        *output = LOOKUP_ADDRESS;
        return 1;
    }
    if (address == VTABLE_ADDRESS +
            DLUMA_EDITOR_ANIM_BIND_PLAY_SLOT_OFFSET) {
        *output = PLAY_ADDRESS;
        return 1;
    }
    return 0;
}

static int fake_is_executable(void* context, uintptr_t address)
{
    FakeAccessState* state = (FakeAccessState*)context;
    if (!state) return 0;
    if (address == LOOKUP_ADDRESS) return state->lookup_executable;
    if (address == PLAY_ADDRESS) return state->play_executable;
    return 0;
}

static FakeAccessState valid_state(void)
{
    FakeAccessState state;
    unsigned int index;
    memset(&state, 0, sizeof(state));
    state.looped_animation_size = 1U;
    state.model_readable = 1;
    state.bind_present = 1;
    state.bind_readable = 1;
    state.vtable_readable = 1;
    state.lookup_executable = 1;
    state.play_executable = 1;
    for (index = 0; index < sizeof(state.model_state); ++index)
        state.model_state[index] = (unsigned char)(0xA0U + index);
    return state;
}

static DlumaEditorModelAnimationDecision probe(FakeAccessState* state)
{
    DlumaEditorModelAnimationAccess access;
    access.context = state;
    access.read_u32 = fake_read_u32;
    access.get_anim_bind = fake_get_anim_bind;
    access.read_pointer = fake_read_pointer;
    access.is_executable = fake_is_executable;
    return dluma_editor_model_animation_guard_probe(MODEL_ADDRESS, &access);
}

static unsigned int invoke_original_if_allowed(
    DlumaEditorModelAnimationDecision decision)
{
    unsigned int calls = 0;
    if (dluma_editor_model_animation_guard_calls_original(decision))
        ++calls;
    return calls;
}

static int test_empty_animation_calls_original_once(void)
{
    FakeAccessState state = valid_state();
    DlumaEditorModelAnimationDecision decision;
    state.looped_animation_size = 0U;
    state.bind_present = 0;
    decision = probe(&state);
    CHECK(decision == DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_EMPTY);
    CHECK(invoke_original_if_allowed(decision) == 1U);
    CHECK(state.get_bind_calls == 0U);
    return 1;
}

static int test_valid_bind_calls_original_once(void)
{
    FakeAccessState state = valid_state();
    DlumaEditorModelAnimationDecision decision = probe(&state);
    CHECK(decision ==
          DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_VALID_BIND);
    CHECK(invoke_original_if_allowed(decision) == 1U);
    CHECK(state.get_bind_calls == 1U);
    return 1;
}

static int test_null_bind_is_suppressed(void)
{
    FakeAccessState state = valid_state();
    DlumaEditorModelAnimationDecision decision;
    state.bind_present = 0;
    decision = probe(&state);
    CHECK(decision == DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_BIND_MISSING);
    CHECK(invoke_original_if_allowed(decision) == 0U);
    return 1;
}

static int test_unreadable_bind_and_vtable_are_suppressed(void)
{
    FakeAccessState state = valid_state();
    state.bind_readable = 0;
    CHECK(probe(&state) ==
          DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_BIND_UNREADABLE);
    state = valid_state();
    state.vtable_readable = 0;
    CHECK(probe(&state) ==
          DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_VTABLE_UNREADABLE);
    return 1;
}

static int test_invalid_virtual_slots_are_suppressed(void)
{
    FakeAccessState state = valid_state();
    state.lookup_executable = 0;
    CHECK(probe(&state) ==
          DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_LOOKUP_NOT_EXECUTABLE);
    state = valid_state();
    state.play_executable = 0;
    CHECK(probe(&state) ==
          DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_PLAY_NOT_EXECUTABLE);
    return 1;
}

static int test_repeated_suppression_warns_once(void)
{
    uint64_t count;
    unsigned int warnings = 0;
    for (count = 1U; count <= 8U; ++count)
        if (dluma_editor_model_animation_guard_first_warning(count))
            ++warnings;
    CHECK(warnings == 1U);
    return 1;
}

static int test_probe_never_changes_model_state(void)
{
    FakeAccessState state = valid_state();
    unsigned char before[sizeof(state.model_state)];
    memcpy(before, state.model_state, sizeof(before));
    state.bind_present = 0;
    CHECK(!dluma_editor_model_animation_guard_calls_original(probe(&state)));
    CHECK(memcmp(before, state.model_state, sizeof(before)) == 0);
    return 1;
}

static int test_exact_identity_and_one_byte_refusal(void)
{
    static const char expected_hash[] =
        "8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54";
    char actual_hash[65];
    DlumaEditorModelAnimationIdentity expected = {
        0x623397AAU, 0x02082000U, expected_hash};
    DlumaEditorModelAnimationIdentity actual;
    strcpy_s(actual_hash, sizeof(actual_hash), expected_hash);
    actual.timestamp = expected.timestamp;
    actual.size_of_image = expected.size_of_image;
    actual.sha256 = actual_hash;
    CHECK(dluma_editor_model_animation_identity_matches(&actual, &expected));
    actual_hash[63] = actual_hash[63] == '4' ? '5' : '4';
    CHECK(!dluma_editor_model_animation_identity_matches(&actual, &expected));
    strcpy_s(actual_hash, sizeof(actual_hash), expected_hash);
    ++actual.size_of_image;
    CHECK(!dluma_editor_model_animation_identity_matches(&actual, &expected));
    actual.size_of_image = expected.size_of_image;
    ++actual.timestamp;
    CHECK(!dluma_editor_model_animation_identity_matches(&actual, &expected));
    return 1;
}

static int test_absolute_jump_bytes(void)
{
    static const uint8_t expected[DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE] = {
        0xFFU, 0x25U, 0U, 0U, 0U, 0U,
        0x88U, 0x77U, 0x66U, 0x55U,
        0x44U, 0x33U, 0x22U, 0x11U};
    uint8_t actual[DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE];
    dluma_editor_model_animation_build_absolute_jump(
        actual, (uintptr_t)0x1122334455667788ULL);
    CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
    return 1;
}

static int test_clean_generations_and_retention(void)
{
    DlumaEditorModelAnimationLifecycle lifecycle;
    memset(&lifecycle, 0, sizeof(lifecycle));
    CHECK(dluma_editor_model_animation_lifecycle_begin(&lifecycle, 1U));
    dluma_editor_model_animation_lifecycle_mark_installed(&lifecycle);
    CHECK(lifecycle.installed);
    CHECK(!dluma_editor_model_animation_lifecycle_begin(&lifecycle, 2U));
    dluma_editor_model_animation_lifecycle_mark_restored(&lifecycle);
    CHECK(!lifecycle.installed && lifecycle.last_generation == 1U);
    CHECK(dluma_editor_model_animation_lifecycle_begin(&lifecycle, 2U));
    dluma_editor_model_animation_lifecycle_mark_installed(&lifecycle);
    dluma_editor_model_animation_lifecycle_mark_retained(&lifecycle);
    CHECK(lifecycle.retained);
    CHECK(!dluma_editor_model_animation_lifecycle_begin(&lifecycle, 3U));
    return 1;
}

int main(void)
{
    int passed = 0;
    if (!test_empty_animation_calls_original_once()) return 1;
    puts("[PASS] empty looped animation calls original once");
    ++passed;
    if (!test_valid_bind_calls_original_once()) return 1;
    puts("[PASS] valid bind calls original once");
    ++passed;
    if (!test_null_bind_is_suppressed()) return 1;
    puts("[PASS] null bind is suppressed");
    ++passed;
    if (!test_unreadable_bind_and_vtable_are_suppressed()) return 1;
    puts("[PASS] unreadable bind and vtable are suppressed");
    ++passed;
    if (!test_invalid_virtual_slots_are_suppressed()) return 1;
    puts("[PASS] invalid virtual slots are suppressed");
    ++passed;
    if (!test_repeated_suppression_warns_once()) return 1;
    puts("[PASS] repeated suppression warns once");
    ++passed;
    if (!test_probe_never_changes_model_state()) return 1;
    puts("[PASS] suppressed model state stays unchanged");
    ++passed;
    if (!test_exact_identity_and_one_byte_refusal()) return 1;
    puts("[PASS] exact identity accepts and one-byte mismatch refuses");
    ++passed;
    if (!test_absolute_jump_bytes()) return 1;
    puts("[PASS] register-preserving absolute jump bytes");
    ++passed;
    if (!test_clean_generations_and_retention()) return 1;
    puts("[PASS] clean generations repeat and retained state rejects reinit");
    ++passed;
    printf("Editor animation guard fake cases passed: %d\n", passed);
    return 0;
}
