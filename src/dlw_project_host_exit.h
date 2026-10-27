#ifndef DLUMA_PROJECT_HOST_EXIT_H
#define DLUMA_PROJECT_HOST_EXIT_H
#include "dlw_project_exit_authority.h"

/* This proof covers callbacks and resources owned through the supported SDK
 * lifecycle. It never claims that arbitrary mod-created threads/hooks stopped,
 * or that a module/assembly can be unloaded. Those stay process-lifetime. */
typedef struct DlumaProjectHostExitDrain {
    unsigned int expected_generation;
    unsigned int owner_thread_confirmed;
    unsigned int accepted;
    unsigned int complete;
    unsigned int project_owners;
    unsigned int global_owners_preserved;
    unsigned int active_callbacks;
    unsigned int remaining_owners;
    unsigned int unsupported_owners;
    unsigned int sdk_contract_only;
    char blocker[128];
    char first_owner[128];
} DlumaProjectHostExitDrain;

#endif
