#include "../src/core/dlw_quest_phase.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        return 0; \
    } \
} while (0)

static DlumaQuestPhaseDispatcher g_dispatcher;
static DlumaQuestPhaseDispatcher g_peer_two;
static char g_order[128];
static unsigned int g_order_size;
static uint64_t g_self_token;
static uint64_t g_new_token;
static unsigned int g_nested_once;

static DlumaQuestPhaseEvent make_event(const char* contract, const char* arguments)
{
    DlumaQuestPhaseEvent value;
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_NATIVE;
    value.completion = DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET;
    strcpy_s(value.quest_id, sizeof(value.quest_id), "quest.test");
    strcpy_s(value.phase_id, sizeof(value.phase_id), "phase.test");
    strcpy_s(value.contract_id, sizeof(value.contract_id), contract);
    strcpy_s(value.provider_mod, sizeof(value.provider_mod), "provider.mod");
    strcpy_s(value.handler_id, sizeof(value.handler_id), "handler");
    strcpy_s(value.binding_hash, sizeof(value.binding_hash),
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    value.arguments_json = arguments;
    value.arguments_size = (unsigned int)strlen(arguments);
    return value;
}

static void append(char value)
{
    if (g_order_size + 1U < sizeof(g_order)) {
        g_order[g_order_size++] = value;
        g_order[g_order_size] = 0;
    }
}

static void callback_new(void* user, const DlumaQuestPhaseEvent* event_value)
{
    (void)user;
    if (event_value->local_peer != 1U) append('!');
    append('C');
}

static void callback_primary(void* user, const DlumaQuestPhaseEvent* event_value)
{
    DlumaQuestPhaseDispatcher* dispatcher = (DlumaQuestPhaseDispatcher*)user;
    append('P');
    if (!g_nested_once) {
        DlumaQuestPhaseEvent nested = *event_value;
        ++g_nested_once;
        (void)dluma_quest_phase_enqueue(dispatcher, &nested);
    }
}

static void callback_self_replace(void* user, const DlumaQuestPhaseEvent* event_value)
{
    DlumaQuestPhaseDispatcher* dispatcher = (DlumaQuestPhaseDispatcher*)user;
    (void)event_value;
    append('A');
    (void)dluma_quest_phase_unregister(dispatcher, g_self_token);
    (void)dluma_quest_phase_register_observer(
        dispatcher, "observer.c", "provider.mod:handler", 0,
        callback_new, dispatcher, &g_new_token);
}

static void callback_last(void* user, const DlumaQuestPhaseEvent* event_value)
{
    (void)user;
    (void)event_value;
    append('B');
}

static void callback_missing(void* user, const DlumaQuestPhaseEvent* event_value)
{
    (void)user;
    (void)event_value;
    append('M');
}

static void callback_quiesce(void* user, const DlumaQuestPhaseEvent* event_value)
{
    DlumaQuestPhaseDispatcher* dispatcher = (DlumaQuestPhaseDispatcher*)user;
    (void)event_value;
    append('Q');
    if (dluma_quest_phase_dispatcher_quiesce(dispatcher)) append('!');
}

static int test_json_contract(void)
{
    static const char source[] =
        " { \"text\" : \"x\\u002Fy\", \"nul\":\"\\u0000\","
        "\"items\" : [1, true, null] } ";
    static const char duplicate[] = "{\"a\":1,\"\\u0061\":2}";
    static const char array_root[] = "[]";
    static const unsigned char invalid_utf8[] = {'{','"','x','"',':','"',0xC0,0xAF,'"','}'};
    char output[8193];
    unsigned int output_size = 0;
    DlumaQuestPhaseDiagnostic diagnostic;
    CHECK(dluma_quest_phase_json_canonicalize_object(
        source, (unsigned int)strlen(source), output, sizeof(output),
        &output_size, &diagnostic));
    CHECK(strcmp(output,
        "{\"text\":\"x/y\",\"nul\":\"\\u0000\",\"items\":[1,true,null]}") == 0);
    CHECK(output_size == strlen(output));
    CHECK(!dluma_quest_phase_json_canonicalize_object(
        duplicate, (unsigned int)strlen(duplicate), output, sizeof(output),
        &output_size, &diagnostic));
    CHECK(strcmp(diagnostic.code, "json_duplicate_key") == 0);
    CHECK(!dluma_quest_phase_json_canonicalize_object(
        array_root, (unsigned int)strlen(array_root), output, sizeof(output),
        &output_size, &diagnostic));
    CHECK(strcmp(diagnostic.code, "json_root") == 0);
    CHECK(!dluma_quest_phase_json_canonicalize_object(
        invalid_utf8, sizeof(invalid_utf8), output, sizeof(output),
        &output_size, &diagnostic));
    CHECK(strcmp(diagnostic.code, "json_utf8") == 0);
    return 1;
}

static int test_directives_and_edf(void)
{
    static const char builtin[] =
        ",call-lua <<phase one>> -mod=<<owner.mod>> -handler=<<start>> "
        "-arguments=<<{\"key\":\"value\"}>> -wait=0";
    static const char alias_line[] =
        ",fade-test <<phase two>> -arguments=<<{\"seconds\":2.5}>> -wait=2.5";
    static const char raw_terminator[] =
        ",call-native <<bad>> -mod=<<owner.mod>> -handler=<<start>> "
        "-arguments=<<{\"bad\":\">>\"}>> -wait=0";
    static const char* stock[] = {"wait", "Play-Anim", "collision"};
    DlumaQuestPhaseAlias aliases[3];
    unsigned char accepted[3];
    DlumaQuestPhaseDirective directive;
    DlumaQuestPhaseDiagnostic diagnostic;
    char edf[4096];
    char compiler_line[1024];
    unsigned int edf_size = 0;
    unsigned int compiler_line_size = 0;
    memset(aliases, 0, sizeof(aliases));
    strcpy_s(aliases[0].command, sizeof(aliases[0].command), "fade-test");
    strcpy_s(aliases[0].provider_mod, sizeof(aliases[0].provider_mod), "owner.mod");
    strcpy_s(aliases[0].handler_id, sizeof(aliases[0].handler_id), "fade");
    aliases[0].runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_NATIVE;
    strcpy_s(aliases[1].command, sizeof(aliases[1].command), "COLLISION");
    strcpy_s(aliases[1].provider_mod, sizeof(aliases[1].provider_mod), "owner.mod");
    strcpy_s(aliases[1].handler_id, sizeof(aliases[1].handler_id), "other");
    aliases[1].runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_NATIVE;
    strcpy_s(aliases[2].command, sizeof(aliases[2].command), "call-lua");
    strcpy_s(aliases[2].provider_mod, sizeof(aliases[2].provider_mod), "owner.mod");
    strcpy_s(aliases[2].handler_id, sizeof(aliases[2].handler_id), "other");
    aliases[2].runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_LUA;
    CHECK(dluma_quest_phase_aliases_validate(
        aliases, 3U, stock, 3U, accepted, &diagnostic));
    CHECK(accepted[0] == 1U && accepted[1] == 0U && accepted[2] == 0U);
    memset(&directive, 0, sizeof(directive));
    directive.struct_size = sizeof(directive);
    CHECK(dluma_quest_phase_parse_directive(
        builtin, (unsigned int)strlen(builtin), 19U, aliases, 1U,
        &directive, &diagnostic));
    CHECK(directive.runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA);
    CHECK(strcmp(directive.contract_id, "owner.mod:start") == 0);
    CHECK(directive.source_line == 19U);
    CHECK(directive.completion == DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET);
    CHECK(dluma_quest_phase_build_compiler_line(
        "call-lua", &directive, "0.0", compiler_line,
        sizeof(compiler_line), &compiler_line_size));
    CHECK(strcmp(
        compiler_line,
        ",call-lua <<phase one>> <<owner.mod>> <<start>> <<{}>> 0.0") == 0);
    CHECK(compiler_line_size == strlen(compiler_line));
    memset(&directive, 0, sizeof(directive));
    directive.struct_size = sizeof(directive);
    CHECK(dluma_quest_phase_parse_directive(
        alias_line, (unsigned int)strlen(alias_line), 20U, aliases, 1U,
        &directive, &diagnostic));
    CHECK(directive.runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_NATIVE);
    CHECK(strcmp(directive.contract_id, "owner.mod:fade") == 0);
    CHECK(directive.wait_seconds == 2.5);
    CHECK(directive.completion == DLUMA_QUEST_PHASE_COMPLETION_STOCK_TIMED_WAIT);
    CHECK(dluma_quest_phase_build_compiler_line(
        "fade-test", &directive, "2.5", compiler_line,
        sizeof(compiler_line), &compiler_line_size));
    CHECK(strcmp(
        compiler_line,
        ",fade-test <<phase two>> <<{}>> 2.5") == 0);
    CHECK(!dluma_quest_phase_build_compiler_line(
        "call-lua", &directive, "2.5", compiler_line,
        sizeof(compiler_line), &compiler_line_size));
    memset(&directive, 0, sizeof(directive));
    directive.struct_size = sizeof(directive);
    CHECK(!dluma_quest_phase_parse_directive(
        raw_terminator, (unsigned int)strlen(raw_terminator), 21U, aliases, 1U,
        &directive, &diagnostic));
    CHECK(dluma_quest_phase_build_edf(
        aliases, accepted, 3U, edf, sizeof(edf), &edf_size));
    CHECK(edf[0] == '#');
    CHECK(strstr(edf, "; Generated by DL Universal Loader") == NULL);
    CHECK(strstr(edf, ",call-lua name mod handler arguments wait =") != NULL);
    CHECK(strstr(edf, ",call-native name mod handler arguments wait =") != NULL);
    CHECK(strstr(edf, ",fade-test name arguments wait =") != NULL);
    CHECK(strstr(edf, ",COLLISION") == NULL);
    CHECK(strstr(edf, "-time4p=<(,wait)>") != NULL);
    CHECK(edf_size == strlen(edf));
    return 1;
}

static int test_dispatch_order_mutation_and_nested(void)
{
    DlumaQuestPhaseEvent event_value;
    uint64_t provider_token;
    uint64_t last_token;
    uint64_t missing_token;
    uint64_t missing_late_token;
    memset(g_order, 0, sizeof(g_order));
    g_order_size = 0;
    g_nested_once = 0;
    dluma_quest_phase_dispatcher_initialize(&g_dispatcher, 7U);
    CHECK(dluma_quest_phase_register_provider(
        &g_dispatcher, "provider.mod", "handler", 0,
        callback_primary, &g_dispatcher, &provider_token));
    CHECK(dluma_quest_phase_register_observer(
        &g_dispatcher, "observer.a", "provider.mod:handler", -10,
        callback_self_replace, &g_dispatcher, &g_self_token));
    CHECK(dluma_quest_phase_register_observer(
        &g_dispatcher, "observer.b", "provider.mod:handler", 10,
        callback_last, &g_dispatcher, &last_token));
    CHECK(dluma_quest_phase_register_observer(
        &g_dispatcher, "observer.z", "missing.mod:handler", 10,
        callback_last, &g_dispatcher, &missing_late_token));
    CHECK(dluma_quest_phase_register_observer(
        &g_dispatcher, "observer.a", "missing.mod:handler", -10,
        callback_missing, &g_dispatcher, &missing_token));
    event_value = make_event("provider.mod:handler", "{\"x\":1}");
    CHECK(dluma_quest_phase_enqueue(&g_dispatcher, &event_value));
    CHECK(dluma_quest_phase_pump(&g_dispatcher, 8U) == 2U);
    CHECK(strcmp(g_order, "PABPCB") == 0);
    event_value = make_event("missing.mod:handler", "{}");
    CHECK(dluma_quest_phase_enqueue(&g_dispatcher, &event_value));
    CHECK(dluma_quest_phase_pump(&g_dispatcher, 1U) == 1U);
    CHECK(strcmp(g_order, "PABPCBMB") == 0);
    CHECK(dluma_quest_phase_unregister(&g_dispatcher, provider_token));
    CHECK(!dluma_quest_phase_unregister(&g_dispatcher, provider_token));
    CHECK(dluma_quest_phase_dispatcher_quiesce(&g_dispatcher));
    return 1;
}

static int test_queue_boundaries_and_peer_independence(void)
{
    DlumaQuestPhaseEvent event_value;
    unsigned int index;
    uint64_t token_one;
    uint64_t token_two;
    static char maximum[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES];
    memset(maximum, 'x', sizeof(maximum));
    maximum[17] = 0;
    dluma_quest_phase_dispatcher_initialize(&g_dispatcher, 1U);
    event_value = make_event("provider.mod:handler", "{}");
    for (index = 0; index < DLUMA_QUEST_PHASE_QUEUE_CAPACITY; ++index)
        CHECK(dluma_quest_phase_enqueue(&g_dispatcher, &event_value));
    CHECK(!dluma_quest_phase_enqueue(&g_dispatcher, &event_value));
    CHECK(g_dispatcher.overflow_count == 1U);
    CHECK(dluma_quest_phase_pump(&g_dispatcher, DLUMA_QUEST_PHASE_QUEUE_CAPACITY) ==
          DLUMA_QUEST_PHASE_QUEUE_CAPACITY);
    CHECK(dluma_quest_phase_dispatcher_quiesce(&g_dispatcher));

    dluma_quest_phase_dispatcher_initialize(&g_dispatcher, 3U);
    dluma_quest_phase_dispatcher_initialize(&g_peer_two, 9U);
    CHECK(dluma_quest_phase_register_provider(
        &g_dispatcher, "provider.mod", "handler", 0,
        callback_last, &g_dispatcher, &token_one));
    CHECK(dluma_quest_phase_register_provider(
        &g_peer_two, "provider.mod", "handler", 0,
        callback_last, &g_peer_two, &token_two));
    event_value.arguments_json = maximum;
    event_value.arguments_size = sizeof(maximum);
    CHECK(dluma_quest_phase_enqueue(&g_dispatcher, &event_value));
    CHECK(dluma_quest_phase_enqueue(&g_peer_two, &event_value));
    CHECK(g_dispatcher.queue[0].event_value.sequence == 1U);
    CHECK(g_peer_two.queue[0].event_value.sequence == 1U);
    CHECK(g_dispatcher.queue[0].arguments[17] == 0);
    CHECK(g_dispatcher.queue[0].arguments[18] == 'x');
    CHECK(dluma_quest_phase_dispatcher_quiesce(&g_dispatcher));
    CHECK(dluma_quest_phase_dispatcher_quiesce(&g_peer_two));
    return 1;
}

static int test_shutdown_from_callback(void)
{
    DlumaQuestPhaseEvent event_value;
    uint64_t provider;
    uint64_t observer;
    g_order_size = 0;
    g_order[0] = 0;
    dluma_quest_phase_dispatcher_initialize(&g_dispatcher, 1U);
    CHECK(dluma_quest_phase_register_provider(
        &g_dispatcher, "provider.mod", "handler", 0,
        callback_quiesce, &g_dispatcher, &provider));
    CHECK(dluma_quest_phase_register_observer(
        &g_dispatcher, "observer.z", "provider.mod:handler", 0,
        callback_last, &g_dispatcher, &observer));
    event_value = make_event("provider.mod:handler", "{}");
    CHECK(dluma_quest_phase_enqueue(&g_dispatcher, &event_value));
    CHECK(dluma_quest_phase_pump(&g_dispatcher, 1U) == 1U);
    CHECK(strcmp(g_order, "Q") == 0);
    CHECK(dluma_quest_phase_dispatcher_quiesce(&g_dispatcher));
    return 1;
}

int main(void)
{
    int passed = 0;
    if (!test_json_contract()) return 1;
    puts("[PASS] strict bounded quest argument JSON");
    ++passed;
    if (!test_directives_and_edf()) return 1;
    puts("[PASS] custom directives and collision-safe EDF");
    ++passed;
    if (!test_dispatch_order_mutation_and_nested()) return 1;
    puts("[PASS] primary and observer dispatch mutation");
    ++passed;
    if (!test_queue_boundaries_and_peer_independence()) return 1;
    puts("[PASS] bounded independent peer queues");
    ++passed;
    if (!test_shutdown_from_callback()) return 1;
    puts("[PASS] shutdown from dispatch callback");
    ++passed;
    printf("Quest phase fake cases passed: %d\n", passed);
    return 0;
}
