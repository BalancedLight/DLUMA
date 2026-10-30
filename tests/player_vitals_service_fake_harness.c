#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_player_service.c"
#include "../src/core/dlw_capability_registry.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int binding_ready = 1, sample_ready = 1, sample_calls;
static DlumaPlayerVitalsSnapshot sample = {75.0f, 140.0f, 1U};
static int replacement_state;
static void* current_player_state = &sample;
static DlumaModOwner* revoke_during_read;
static const DlumaPlayerVariableDefinition variable_definition = {
    "camera_default_fov", 779U, 20.0f, 170.0f
};
static float variable_base = 80.0f;
static int variable_write_fails;

int dluma_player_actor_binding_vitals_ready(void) { return binding_ready; }
int dluma_player_actor_binding_read_vitals(DlumaPlayerVitalsSnapshot* out)
{
    ++sample_calls;
    if (revoke_during_read) dluma_player_service_revoke_owner(revoke_during_read->owner_id);
    if (!sample_ready) return 0;
    *out = sample;
    return 1;
}
int dluma_player_variables_binding_configured(void) { return 1; }
int dluma_player_variables_binding_ready(void) { return 1; }
int dluma_player_variables_binding_local_state(void** out) { *out = current_player_state; return 1; }
uint32_t dluma_player_variables_binding_definition_count(void) { return 1; }
const DlumaPlayerVariableDefinition* dluma_player_variables_binding_definition_at(uint32_t i) { return i == 0 ? &variable_definition : 0; }
const DlumaPlayerVariableDefinition* dluma_player_variables_binding_definition_id(uint32_t i) { return i == variable_definition.engine_id ? &variable_definition : 0; }
const DlumaPlayerVariableDefinition* dluma_player_variables_binding_definition_named(const char* n) { return strcmp(n, variable_definition.name) == 0 ? &variable_definition : 0; }
int dluma_player_variables_binding_read(const DlumaPlayerVariableDefinition* d, DlumaPlayerVariableSnapshot* s)
{
    memset(s, 0, sizeof(*s)); s->definition = d;
    s->player_state = current_player_state; s->base = variable_base; s->value = variable_base;
    return 1;
}

static int test_player_handle_retirement(void)
{
    DlumaModOwner owner = {0};
    DlumaObjectResult local = {0};
    DlumaPlayerVariableSetRequest set = {0};
    DlumaPlayerVariableRequest reset = {0};
    DlumaBoolResult result = {0};
    DlumaObjectHandle old;
    char name[32], class_name[32];
    owner.owner_id = 31; owner.map_generation = 4;
    local.name.data = name; local.name.capacity = sizeof(name);
    local.class_name.data = class_name; local.class_name.capacity = sizeof(class_name);
    variable_base = 80;
    CHECK(dluma_player_service_invoke(&owner, 3001, 0, &local) == DLUMA_STATUS_OK);
    old = local.handle;
    set.player = old; set.name.data = variable_definition.name;
    set.name.size = strlen(variable_definition.name); set.value = 100;
    reset.player = old; reset.name = set.name;
    CHECK(dluma_player_service_invoke(&owner, 3010, &set, &result) == DLUMA_STATUS_OK);
    current_player_state = &replacement_state;
    variable_base = 77; /* New player must not receive the old baseline. */
    CHECK(dluma_player_service_invoke(&owner, 3011, &reset, &result) == DLUMA_STATUS_STALE_HANDLE);
    CHECK(dluma_player_service_invoke(&owner, 3001, 0, &local) == DLUMA_STATUS_OK);
    CHECK(local.handle.handle_id != old.handle_id && variable_base == 77);
    CHECK(!dluma_player_service_is_overridden_locked(owner.owner_id, 779U));
    set.player = local.handle; set.value = 90;
    CHECK(dluma_player_service_invoke(&owner, 3010, &set, &result) == DLUMA_STATUS_OK);
    ++owner.map_generation;
    owner.active_handles = 1; /* Another service already owns a new-generation handle. */
    variable_write_fails = 1;
    CHECK(dluma_player_service_invoke(&owner, 3001, 0, &local) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(variable_base == 90 && dluma_player_service_is_overridden_locked(owner.owner_id, 779U));
    variable_write_fails = 0;
    CHECK(dluma_player_service_invoke(&owner, 3001, 0, &local) == DLUMA_STATUS_OK);
    CHECK(variable_base == 77 && local.handle.generation == owner.map_generation);
    CHECK(owner.active_handles == 2);
    dluma_player_service_revoke_owner(owner.owner_id);
    current_player_state = &sample;
    return 0;
}
int dluma_player_variables_binding_write_base(const DlumaPlayerVariableSnapshot* s, float b)
{
    (void)s;
    if (variable_write_fails) return 0;
    variable_base = b; return 1;
}

static int test_variable_reset_transaction(void)
{
    DlumaModOwner owner = {0}, other = {0};
    DlumaObjectResult local = {0};
    DlumaPlayerVariableSetRequest set = {0};
    DlumaPlayerVariableRequest reset = {0};
    DlumaBoolResult result = {0};
    char name[32], class_name[32];
    owner.owner_id = 21; owner.map_generation = 1;
    other.owner_id = 22; other.map_generation = 1;
    local.name.data = name; local.name.capacity = sizeof(name);
    local.class_name.data = class_name; local.class_name.capacity = sizeof(class_name);
    CHECK(dluma_player_service_invoke(&owner, 3001, 0, &local) == DLUMA_STATUS_OK);
    set.player = local.handle; set.name.data = variable_definition.name;
    set.name.size = strlen(variable_definition.name); set.value = 100;
    reset.player = local.handle; reset.name = set.name;
    CHECK(dluma_player_service_invoke(&owner, 3010, &set, &result) == DLUMA_STATUS_OK);
    variable_write_fails = 1; result.value = 0;
    CHECK(dluma_player_service_invoke(&owner, 3011, &reset, &result) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(!result.value && variable_base == 100);
    CHECK(dluma_player_service_is_overridden_locked(owner.owner_id, 779U));
    variable_write_fails = 0;
    CHECK(dluma_player_service_invoke(&owner, 3011, &reset, &result) == DLUMA_STATUS_OK);
    CHECK(variable_base == 80);
    CHECK(!dluma_player_service_is_overridden_locked(owner.owner_id, 779U));
    CHECK(dluma_player_service_invoke(&owner, 3010, &set, &result) == DLUMA_STATUS_OK);
    CHECK(dluma_player_service_invoke(&other, 3001, 0, &local) == DLUMA_STATUS_OK);
    set.player = local.handle; set.value = 120;
    CHECK(dluma_player_service_invoke(&other, 3010, &set, &result) == DLUMA_STATUS_OK);
    variable_write_fails = 1;
    CHECK(dluma_player_service_invoke(&owner, 3011, &reset, &result) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(variable_base == 120);
    CHECK(dluma_player_service_is_overridden_locked(owner.owner_id, 779U));
    variable_write_fails = 0;
    CHECK(dluma_player_service_invoke(&owner, 3011, &reset, &result) == DLUMA_STATUS_OK);
    CHECK(variable_base == 120);
    reset.player = local.handle;
    CHECK(dluma_player_service_invoke(&other, 3011, &reset, &result) == DLUMA_STATUS_OK);
    CHECK(variable_base == 80);
    /* Final owner revocation must discard records even if engine teardown
     * makes the best-effort restore unavailable. */
    CHECK(dluma_player_service_invoke(&other, 3010, &set, &result) == DLUMA_STATUS_OK);
    variable_write_fails = 1;
    dluma_player_service_revoke_owner(other.owner_id);
    CHECK(!dluma_player_service_is_overridden_locked(other.owner_id, 779U));
    variable_write_fails = 0;
    dluma_player_service_revoke_owner(owner.owner_id);
    return 0;
}

int main(void)
{
    DlumaModOwner owner = {0}, other = {0};
    DlumaObjectResult local = {0};
    DlumaObjectRequest request = {0};
    DlumaFloatResult number = {0};
    DlumaBoolResult boolean = {0};
    DlumaCapabilityRegistry capabilities;
    char name[32], class_name[32];
    owner.owner_id = 11; owner.map_generation = 1;
    other.owner_id = 12; other.map_generation = 1;
    local.name.data = name; local.name.capacity = sizeof(name);
    local.class_name.data = class_name; local.class_name.capacity = sizeof(class_name);
    CHECK(dluma_player_service_invoke(&owner, 3001, 0, &local) == DLUMA_STATUS_OK);
    request.handle = local.handle;
    CHECK(dluma_player_service_invoke(&owner, 3005, &request, &number) == DLUMA_STATUS_OK);
    CHECK(number.value == 75.0);
    CHECK(dluma_player_service_invoke(&owner, 3006, &request, &number) == DLUMA_STATUS_OK);
    CHECK(number.value == 140.0);
    sample.health = 0.0f; sample.alive = 0;
    CHECK(dluma_player_service_invoke(&owner, 3005, &request, &number) == DLUMA_STATUS_OK);
    CHECK(number.value == 0.0);
    CHECK(dluma_player_service_invoke(&owner, 3007, &request, &boolean) == DLUMA_STATUS_OK && !boolean.value);
    sample.health = 10.0f; /* Knockdown may still count as acting dead. */
    CHECK(dluma_player_service_invoke(&owner, 3007, &request, &boolean) == DLUMA_STATUS_OK && !boolean.value);
    sample_calls = 0;
    CHECK(dluma_player_service_invoke(&other, 3005, &request, &number) == DLUMA_STATUS_FOREIGN_HANDLE);
    ++owner.map_generation;
    CHECK(dluma_player_service_invoke(&owner, 3005, &request, &number) == DLUMA_STATUS_STALE_HANDLE);
    --owner.map_generation;
    ++request.handle.generation;
    CHECK(dluma_player_service_invoke(&owner, 3005, &request, &number) == DLUMA_STATUS_STALE_HANDLE);
    request.handle = local.handle;
    CHECK(sample_calls == 0);
    sample_ready = 0; number.value = -123.0;
    CHECK(dluma_player_service_invoke(&owner, 3005, &request, &number) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(number.value == -123.0);
    sample_ready = 1; revoke_during_read = &owner;
    CHECK(dluma_player_service_invoke(&owner, 3005, &request, &number) == DLUMA_STATUS_STALE_HANDLE);
    CHECK(number.value == -123.0);
    revoke_during_read = 0;
    dluma_capability_registry_initialize(&capabilities);
    dluma_player_service_prepare_capabilities(&capabilities);
    CHECK(dluma_capability_registry_ready(&capabilities, DLUMA_CAPABILITY_PLAYER_VITALS));
    CHECK(!dluma_capability_registry_ready(&capabilities, DLUMA_CAPABILITY_PLAYER_TRANSFORM));
    binding_ready = 0;
    dluma_player_service_prepare_capabilities(&capabilities);
    CHECK(!dluma_capability_registry_ready(&capabilities, DLUMA_CAPABILITY_PLAYER_VITALS));
    CHECK(test_variable_reset_transaction() == 0);
    CHECK(test_player_handle_retirement() == 0);
    puts("player vitals service: ok");
    return 0;
}
