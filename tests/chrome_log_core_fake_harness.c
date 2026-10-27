#include "dlw_chrome_log_core.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

typedef struct FakeChromeLog {
    volatile LONG calls;
    unsigned int level_id;
    unsigned int level_result;
    const char* category;
    const char* source;
    int line;
    int filter_action;
    int line_mode;
    char format[32];
    char message[512];
} FakeChromeLog;

static FakeChromeLog g_fake_log;
static const unsigned char g_log_prefix[] = {
    0x4C, 0x8B, 0xDC, 0x48, 0x83, 0xEC, 0x48
};
static const unsigned char g_level_prefix[] = {
    0x81, 0xF9, 0x52, 0x52, 0x52, 0x45, 0x74, 0x39,
    0x81, 0xF9
};

static unsigned int fake_level_from_id(unsigned int id)
{
    g_fake_log.level_id = id;
    return g_fake_log.level_result;
}

static void __cdecl fake_log(
    unsigned int level, const char* category, const char* source, int line,
    int filter_action, int line_mode, const char* format, ...)
{
    va_list args;
    g_fake_log.calls = InterlockedIncrement(&g_fake_log.calls);
    g_fake_log.level_result = level;
    g_fake_log.category = category;
    g_fake_log.source = source;
    g_fake_log.line = line;
    g_fake_log.filter_action = filter_action;
    g_fake_log.line_mode = line_mode;
    strncpy_s(g_fake_log.format, sizeof(g_fake_log.format),
              format ? format : "", _TRUNCATE);
    va_start(args, format);
    vsnprintf_s(g_fake_log.message, sizeof(g_fake_log.message),
                _TRUNCATE, format ? format : "", args);
    va_end(args);
}

static DlumaChromeLogCoreInput fake_input(uint32_t timestamp,
                                        uint32_t image_size,
                                        int path_verified)
{
    DlumaChromeLogCoreInput input;
    memset(&input, 0, sizeof(input));
    input.path_verified = path_verified;
    input.timestamp = timestamp;
    input.image_size = image_size;
    input.log_prefix = g_log_prefix;
    input.log_prefix_size = sizeof(g_log_prefix);
    input.level_prefix = g_level_prefix;
    input.level_prefix_size = sizeof(g_level_prefix);
    input.log = fake_log;
    input.level_from_id = fake_level_from_id;
    return input;
}

static void fake_log_reset(void)
{
    memset(&g_fake_log, 0, sizeof(g_fake_log));
    g_fake_log.level_result = 7;
}

static int case_exact_native_warning(void)
{
    DlumaChromeLogCoreInput input;
    DlumaChromeLogCoreResult result;
    fake_log_reset();
    input = fake_input(DLUMA_CHROME_SHIPPING_TIMESTAMP,
                       DLUMA_CHROME_SHIPPING_IMAGE_SIZE, 1);
    CHECK(dluma_chrome_log_core_emit(&input, &result));
    CHECK(result.status == DLUMA_CHROME_LOG_CORE_EMITTED);
    CHECK(g_fake_log.calls == 1);
    CHECK(g_fake_log.level_id == DLUMA_CHROME_WARN_ID);
    CHECK(g_fake_log.level_result == 7);
    CHECK(strcmp(g_fake_log.category, "DLUMA") == 0);
    CHECK(g_fake_log.source == 0);
    CHECK(g_fake_log.line == 0);
    CHECK(g_fake_log.filter_action == 2);
    CHECK(g_fake_log.line_mode == 1);
    CHECK(strcmp(g_fake_log.format, "%s\n") == 0);
    CHECK(strcmp(g_fake_log.message, DLUMA_CHROME_LOG_MESSAGE "\n") == 0);
    return 1;
}

static int case_devtools_profile(void)
{
    DlumaChromeLogCoreInput input;
    DlumaChromeLogCoreResult result;
    fake_log_reset();
    input = fake_input(DLUMA_CHROME_DEVTOOLS_TIMESTAMP,
                       DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE, 1);
    CHECK(dluma_chrome_log_core_emit(&input, &result));
    CHECK(g_fake_log.calls == 1);
    return 1;
}

static int case_validation_failures(void)
{
    DlumaChromeLogCoreInput input;
    DlumaChromeLogCoreResult result;
    fake_log_reset();
    input = fake_input(0x12345678UL, DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE, 1);
    CHECK(!dluma_chrome_log_core_emit(&input, &result));
    CHECK(result.status == DLUMA_CHROME_LOG_CORE_UNSUPPORTED_PROFILE);
    CHECK(g_fake_log.calls == 0);

    input = fake_input(DLUMA_CHROME_DEVTOOLS_TIMESTAMP,
                       DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE, 0);
    CHECK(!dluma_chrome_log_core_emit(&input, &result));
    CHECK(result.status == DLUMA_CHROME_LOG_CORE_INVALID_INPUT);
    CHECK(g_fake_log.calls == 0);

    input = fake_input(DLUMA_CHROME_DEVTOOLS_TIMESTAMP,
                       DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE, 1);
    input.log_prefix = 0;
    CHECK(!dluma_chrome_log_core_emit(&input, &result));
    CHECK(result.status == DLUMA_CHROME_LOG_CORE_INVALID_INPUT);

    input = fake_input(DLUMA_CHROME_DEVTOOLS_TIMESTAMP,
                       DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE, 1);
    {
        unsigned char bad_level_prefix[sizeof(g_level_prefix)];
        memcpy(bad_level_prefix, g_level_prefix, sizeof(bad_level_prefix));
        bad_level_prefix[0] ^= 1;
        input.level_prefix = bad_level_prefix;
        CHECK(!dluma_chrome_log_core_emit(&input, &result));
        CHECK(result.status == DLUMA_CHROME_LOG_CORE_PREFIX_MISMATCH);
    }
    CHECK(g_fake_log.calls == 0);
    return 1;
}

typedef struct FakeNoticeGate {
    volatile LONG state;
    DlumaChromeLogCoreInput input;
} FakeNoticeGate;

static int fake_notice_once(FakeNoticeGate* gate)
{
    DlumaChromeLogCoreResult result;
    if (InterlockedCompareExchange(&gate->state, 1, 0) != 0)
        return 0;
    if (dluma_chrome_log_core_emit(&gate->input, &result)) {
        InterlockedExchange(&gate->state, 2);
        return 1;
    }
    InterlockedExchange(&gate->state, 3);
    return 0;
}

static DWORD WINAPI fake_notice_thread(LPVOID parameter)
{
    fake_notice_once((FakeNoticeGate*)parameter);
    return 0;
}

static int case_one_shot_reentry_and_concurrency(void)
{
    HANDLE threads[8];
    unsigned int index;
    FakeNoticeGate gate;
    memset(&gate, 0, sizeof(gate));
    fake_log_reset();
    gate.input = fake_input(DLUMA_CHROME_SHIPPING_TIMESTAMP,
                            DLUMA_CHROME_SHIPPING_IMAGE_SIZE, 1);
    for (index = 0; index < 8; ++index) {
        threads[index] = CreateThread(0, 0, fake_notice_thread, &gate, 0, 0);
        CHECK(threads[index] != 0);
    }
    CHECK(WaitForMultipleObjects(8, threads, TRUE, 5000) == WAIT_OBJECT_0);
    for (index = 0; index < 8; ++index) CloseHandle(threads[index]);
    CHECK(gate.state == 2);
    CHECK(g_fake_log.calls == 1);
    CHECK(fake_notice_once(&gate) == 0);
    CHECK(g_fake_log.calls == 1);
    return 1;
}

int main(void)
{
    CHECK(case_exact_native_warning());
    CHECK(case_devtools_profile());
    CHECK(case_validation_failures());
    CHECK(case_one_shot_reentry_and_concurrency());
    puts("chrome_log_core_fake_harness: PASS");
    return 0;
}
