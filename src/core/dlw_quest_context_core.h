#ifndef DLUMA_QUEST_CONTEXT_CORE_H
#define DLUMA_QUEST_CONTEXT_CORE_H

#include <stdint.h>

#define DLUMA_QUEST_CONTEXT_MAX_OBJECTS 256U
#define DLUMA_QUEST_CONTEXT_MAX_ROLES 4U
#define DLUMA_QUEST_CONTEXT_NAME_CAPACITY 256U
#define DLUMA_QUEST_CONTEXT_CLASS_CAPACITY 128U
#define DLUMA_QUEST_CONTEXT_ROLE_CAPACITY 96U

typedef struct DlumaQuestContextObjectCore {
    char name[DLUMA_QUEST_CONTEXT_NAME_CAPACITY];
    char class_name[DLUMA_QUEST_CONTEXT_CLASS_CAPACITY];
    char roles[DLUMA_QUEST_CONTEXT_MAX_ROLES]
              [DLUMA_QUEST_CONTEXT_ROLE_CAPACITY];
    uint32_t role_count;
} DlumaQuestContextObjectCore;

typedef struct DlumaQuestContextCore {
    uint64_t generation;
    DlumaQuestContextObjectCore objects[DLUMA_QUEST_CONTEXT_MAX_OBJECTS];
    uint32_t object_count;
} DlumaQuestContextCore;

void dluma_quest_context_initialize(DlumaQuestContextCore* context);

int dluma_quest_context_add_object(
    DlumaQuestContextCore* context,
    const char* name,
    const char* class_name,
    const char* role);

int dluma_quest_context_capture_autocomplete(
    DlumaQuestContextCore* context,
    const char* source,
    uint32_t source_size,
    uint32_t caret_offset,
    const char* candidates,
    char separator);

#endif
