#include <stdio.h>
#include <stdlib.h>
#include "../src/dlw_editor_source_control_host.c"
#include "../src/core/dlw_json_reader.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    DlumaEditorGitParsedRequest *out = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*out));
    const char *first = "{\"projectRoot\":\"C:\\\\repo\",\"projectGeneration\":1,\"operation\":\"stage\",\"destination\":null,\"armId\":null}";
    const char *retry = "{\"projectRoot\":\"C:\\\\repo\",\"projectGeneration\":1,\"operation\":\"stage\",\"armId\":\"0123456789abcdef0123456789abcdef\"}";
    const char *cancel = "{\"projectRoot\":\"C:\\\\repo\",\"projectGeneration\":1,\"armId\":\"0123456789abcdef0123456789abcdef\"}";
    const char *bad = "{\"projectRoot\":\"C:\\\\repo\",\"projectGeneration\":1,\"armId\":\"bad\"}";
    CHECK(out);
    CHECK(dluma_editor_source_control_parse_request("gitProjectAcquire", first, strlen(first), out) && !out->has_arm);
    CHECK(dluma_editor_source_control_parse_request("gitProjectAcquire", retry, strlen(retry), out) && out->has_arm);
    CHECK(dluma_editor_source_control_parse_request("gitProjectCancel", cancel, strlen(cancel), out) && out->has_arm && out->kind == DLUMA_GIT_PROJECT_CANCEL);
    CHECK(!dluma_editor_source_control_parse_request("gitProjectCancel", bad, strlen(bad), out));
    CHECK(!dluma_editor_source_control_parse_request("gitProjectCancel", first, strlen(first), out));
    HeapFree(GetProcessHeap(), 0, out);
    puts("Editor Git arming protocol contract passed.");
    return 0;
}
