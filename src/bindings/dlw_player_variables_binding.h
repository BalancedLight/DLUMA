#ifndef DLUMA_PLAYER_VARIABLES_BINDING_H
#define DLUMA_PLAYER_VARIABLES_BINDING_H

#include <stdint.h>

typedef struct DlumaPlayerVariableDefinition {
    const char* name;
    uint32_t engine_id;
    float minimum;
    float maximum;
} DlumaPlayerVariableDefinition;

typedef struct DlumaPlayerVariableSnapshot {
    void* player_state;
    void* variables;
    void* variable_record;
    const DlumaPlayerVariableDefinition* definition;
    float value;
    float base;
    float add;
    float multiplier;
} DlumaPlayerVariableSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

/* Exact-build binding only. No engine pointer from this interface may cross
 * a language-host boundary. The game service immediately copies every value
 * into the generated DLUMA API records. */
int dluma_player_variables_binding_configured(void);
int dluma_player_variables_binding_ready(void);
int dluma_player_variables_binding_local_state(void** out_player_state);
uint32_t dluma_player_variables_binding_definition_count(void);
const DlumaPlayerVariableDefinition*
dluma_player_variables_binding_definition_at(uint32_t index);
const DlumaPlayerVariableDefinition*
dluma_player_variables_binding_definition_named(const char* exact_name);
const DlumaPlayerVariableDefinition*
dluma_player_variables_binding_definition_id(uint32_t engine_id);
int dluma_player_variables_binding_read(
    const DlumaPlayerVariableDefinition* definition,
    DlumaPlayerVariableSnapshot* snapshot);
int dluma_player_variables_binding_write_base(
    const DlumaPlayerVariableSnapshot* snapshot,
    float base);

#ifdef __cplusplus
}
#endif

#endif
