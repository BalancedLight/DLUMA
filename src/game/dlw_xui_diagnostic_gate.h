#ifndef DLUMA_XUI_DIAGNOSTIC_GATE_H
#define DLUMA_XUI_DIAGNOSTIC_GATE_H
#include <stdint.h>

/* Copied facts only. The host derives these from its exact profile, project
 * activation, and currently admitted game-frame callback. */
typedef struct DlumaXuiDiagnosticFacts {
    uint32_t compiled, developer, opted_in, exact_profile;
    uint32_t project_identity_matches, map_name_matches;
    uint32_t active_frame, exact_caller;
    uint32_t captured_generation, current_generation;
} DlumaXuiDiagnosticFacts;
#define DLUMA_XUI_DIAGNOSTIC_ATTEMPT_LIMIT 8U
#define DLUMA_XUI_DIAGNOSTIC_NODE_LIMIT 32768U
typedef struct DlumaXuiDiagnosticBudget {
    uint64_t sequence;
    uint32_t attempts, nodes;
} DlumaXuiDiagnosticBudget;
static int dluma_xui_diagnostic_budget_claim(DlumaXuiDiagnosticBudget* budget,uint64_t sequence)
{
    if(!budget || !sequence) return 0;
    if(budget->sequence!=sequence) { budget->sequence=sequence; budget->attempts=0; budget->nodes=0; }
    if(budget->attempts>=DLUMA_XUI_DIAGNOSTIC_ATTEMPT_LIMIT) return 0;
    ++budget->attempts; return 1;
}
static int dluma_xui_diagnostic_budget_visit(DlumaXuiDiagnosticBudget* budget)
{
    if(!budget || !budget->sequence || budget->nodes>=DLUMA_XUI_DIAGNOSTIC_NODE_LIMIT) return 0;
    ++budget->nodes; return 1;
}
static int dluma_xui_diagnostic_ready(const DlumaXuiDiagnosticFacts* f)
{
    return f && f->compiled && f->developer && f->opted_in && f->exact_profile &&
        f->project_identity_matches && f->map_name_matches && f->active_frame && f->exact_caller &&
        f->captured_generation && f->captured_generation == f->current_generation;
}
static int dluma_xui_diagnostic_owner_allowed(const DlumaXuiDiagnosticFacts* f,
    uint32_t scope, uint32_t owner_generation)
{
    return dluma_xui_diagnostic_ready(f) && (scope == 2U || scope == 3U) &&
        owner_generation == f->captured_generation;
}
#endif
