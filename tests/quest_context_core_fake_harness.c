#include "core/dlw_quest_context_core.h"

#include <stdio.h>
#include <string.h>

static int expect(int value, const char* message)
{
    if (value) return 1;
    fprintf(stderr, "FAILED: %s\n", message);
    return 0;
}

static const DlumaQuestContextObjectCore* find_object(
    const DlumaQuestContextCore* context, const char* name)
{
    uint32_t index;
    for (index = 0; index < context->object_count; ++index)
        if (strcmp(context->objects[index].name, name) == 0)
            return &context->objects[index];
    return 0;
}

int main(void)
{
    DlumaQuestContextCore context;
    const char source[] =
        ",interactive-movie _ InteractiveMovieScenario Thr";
    uint64_t before;
    dluma_quest_context_initialize(&context);
    if (!expect(dluma_quest_context_capture_autocomplete(
            &context, source, (uint32_t)strlen(source),
            (uint32_t)strlen(source), "ThrillStart ThrillEnd", ' '),
            "scenario suggestions should be captured")) return 1;
    if (!expect(context.object_count == 2U,
            "two scenario suggestions should be present")) return 1;
    if (!expect(strcmp(context.objects[0].name, "ThrillStart") == 0 &&
            strcmp(context.objects[0].class_name,
                   "InteractiveMovieScenario") == 0,
            "scenario should retain its selected stock class")) return 1;
    if (!expect(context.objects[0].role_count == 2U &&
            strcmp(context.objects[0].roles[0], "scenario") == 0,
            "scenario should carry functional and positional roles")) return 1;
    before = context.generation;
    (void)dluma_quest_context_capture_autocomplete(
        &context, source, (uint32_t)strlen(source),
        (uint32_t)strlen(source), "ThrillStart ThrillEnd", ' ');
    if (!expect(context.generation == before,
            "identical stock suggestions must not advance generation")) return 1;
    {
        const char take_source[] = ",take pickup Door door";
        const DlumaQuestContextObjectCore* object;
        if (!expect(dluma_quest_context_capture_autocomplete(
                &context, take_source,
                (uint32_t)strlen(take_source),
                (uint32_t)strlen(take_source),
                "door_a|door_b", '|'),
                "take object suggestions should honor the stock separator"))
            return 1;
        object = find_object(&context, "door_a");
        if (!expect(object &&
                strcmp(object->class_name, "Door") == 0,
                "take suggestions should retain their selected class"))
            return 1;
    }
    before = context.generation;
    {
        const char unrelated_source[] = ",semaphore phase";
        if (!expect(!dluma_quest_context_capture_autocomplete(
                &context, unrelated_source,
                (uint32_t)strlen(unrelated_source),
                (uint32_t)strlen(unrelated_source),
                "unrelated_a unrelated_b", ' '),
                "unrelated completions should not become map objects"))
            return 1;
        if (!expect(context.generation == before,
                "ignored completions must not advance generation")) return 1;
    }
    if (!expect(dluma_quest_context_add_object(
            &context, "public_door_a", "Door", "object"),
            "a public UI object should be addable")) return 1;
    if (!expect(!dluma_quest_context_add_object(
            &context, "public_door_a", "Door", "object"),
            "duplicate public UI objects should be stable")) return 1;
    puts("quest context core: ok");
    return 0;
}
