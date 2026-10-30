#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#pragma warning(disable: 4717)

typedef struct CrashWorkerFixture {
    HANDLE work_event;
    HANDLE stock_event;
    HANDLE complete_event;
    volatile LONG claimed;
    volatile LONG stock_calls;
    DWORD exception_code;
    DWORD fault_thread_id;
    DWORD reporter_thread_id;
    DWORD callback_wait;
    DWORD callback_error;
    int stock_result;
    char report[1024];
} CrashWorkerFixture;

static CrashWorkerFixture g_fixture;

static int expect(int condition, const char* name)
{
    if (!condition) {
        fprintf(stderr, "crash worker assertion failed: %s\n", name);
        return 0;
    }
    return 1;
}

static int fake_stock_callback(void)
{
    InterlockedIncrement(&g_fixture.stock_calls);
    return 2;
}

static DWORD WINAPI reporter_thread(void* unused)
{
    size_t used;
    (void)unused;
    if (WaitForSingleObject(g_fixture.work_event, 10000U) != WAIT_OBJECT_0)
        return 1U;
    g_fixture.reporter_thread_id = GetCurrentThreadId();
    sprintf_s(
        g_fixture.report, sizeof(g_fixture.report),
        "report_begin=DLUMA_CRASH_REPORT_V1\n"
        "report_mode=reserved_worker\n"
        "exception_name=stack_overflow\n"
        "exception_code=0x%08lX\n"
        "fault_thread_id=%lu\n"
        "reporter_thread_id=%lu\n",
        g_fixture.exception_code,
        g_fixture.fault_thread_id,
        g_fixture.reporter_thread_id);
    if (WaitForSingleObject(g_fixture.stock_event, 10000U) != WAIT_OBJECT_0)
        return 2U;
    used = strlen(g_fixture.report);
    sprintf_s(
        g_fixture.report + used, sizeof(g_fixture.report) - used,
        "stock_dump_result=%d\n"
        "report_end=DLUMA_CRASH_REPORT_V1\n",
        g_fixture.stock_result);
    if (!SetEvent(g_fixture.complete_event))
        return 3U;
    return 0U;
}

static int capture_stack_overflow(EXCEPTION_POINTERS* exception_pointers)
{
    if (!exception_pointers || !exception_pointers->ExceptionRecord ||
        exception_pointers->ExceptionRecord->ExceptionCode !=
            EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&g_fixture.claimed, 1, 0) == 0) {
        g_fixture.exception_code =
            exception_pointers->ExceptionRecord->ExceptionCode;
        g_fixture.fault_thread_id = GetCurrentThreadId();
        if (!SetEvent(g_fixture.work_event)) {
            g_fixture.callback_error = GetLastError();
            return EXCEPTION_EXECUTE_HANDLER;
        }
        g_fixture.stock_result = fake_stock_callback();
        if (!SetEvent(g_fixture.stock_event)) {
            g_fixture.callback_error = GetLastError();
            return EXCEPTION_EXECUTE_HANDLER;
        }
        g_fixture.callback_wait =
            WaitForSingleObject(g_fixture.complete_event, 10000U);
        if (g_fixture.callback_wait == WAIT_FAILED)
            g_fixture.callback_error = GetLastError();
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

__declspec(noinline) static void exhaust_stack(unsigned int depth)
{
    volatile unsigned char padding[2048] = {0};
    padding[depth & (sizeof(padding) - 1U)] = (unsigned char)depth;
    exhaust_stack(depth + 1U);
    if (padding[0] == 0xFFU)
        InterlockedIncrement(&g_fixture.claimed);
}

static DWORD WINAPI fault_thread(void* unused)
{
    ULONG exception_stack_bytes = 16U * 1024U;
    (void)unused;
    if (!SetThreadStackGuarantee(&exception_stack_bytes)) {
        fprintf(stderr, "crash worker stack guarantee failed: error=%lu\n",
                GetLastError());
        return 1U;
    }
    __try {
        exhaust_stack(1U);
    }
    __except (capture_stack_overflow(GetExceptionInformation())) {
        /* Restore the guard page only after SEH has unwound the stack. */
        if (!_resetstkoflw())
            return 2U;
    }
    return 0U;
}

int main(void)
{
    HANDLE reporter;
    HANDLE fault;
    DWORD fault_wait;
    DWORD reporter_wait;
    DWORD fault_exit = STILL_ACTIVE;
    DWORD reporter_exit = STILL_ACTIVE;
    int ok = 1;
    memset(&g_fixture, 0, sizeof(g_fixture));
    g_fixture.callback_wait = WAIT_FAILED;
    g_fixture.work_event = CreateEventW(0, FALSE, FALSE, 0);
    g_fixture.stock_event = CreateEventW(0, TRUE, FALSE, 0);
    g_fixture.complete_event = CreateEventW(0, TRUE, FALSE, 0);
    ok &= expect(
        g_fixture.work_event && g_fixture.stock_event &&
            g_fixture.complete_event,
        "events created");
    if (!ok) return 1;
    reporter = CreateThread(
        0, 1024U * 1024U, reporter_thread, 0, 0, 0);
    fault = CreateThread(
        0, 128U * 1024U, fault_thread, 0,
        STACK_SIZE_PARAM_IS_A_RESERVATION, 0);
    ok &= expect(reporter != 0 && fault != 0, "threads created");
    if (!ok) return 1;
    fault_wait = WaitForSingleObject(fault, 15000U);
    reporter_wait = WaitForSingleObject(reporter, 15000U);
    ok &= expect(fault_wait == WAIT_OBJECT_0, "fault thread completed");
    ok &= expect(reporter_wait == WAIT_OBJECT_0, "reporter completed");
    if (!ok) {
        fprintf(stderr, "crash worker waits: fault=%lu reporter=%lu\n",
                fault_wait, reporter_wait);
        return 1;
    }
    ok &= expect(GetExitCodeThread(fault, &fault_exit), "fault exit queried");
    ok &= expect(
        GetExitCodeThread(reporter, &reporter_exit), "reporter exit queried");
    ok &= expect(fault_exit == 0U, "fault thread succeeded");
    ok &= expect(reporter_exit == 0U, "reporter succeeded");
    ok &= expect(
        g_fixture.callback_wait == WAIT_OBJECT_0 &&
            g_fixture.callback_error == ERROR_SUCCESS,
        "fault callback received the completed report");
    ok &= expect(
        g_fixture.exception_code == EXCEPTION_STACK_OVERFLOW,
        "stack overflow captured");
    ok &= expect(g_fixture.stock_calls == 1, "stock callback called once");
    ok &= expect(
        g_fixture.fault_thread_id != 0 &&
            g_fixture.reporter_thread_id != 0 &&
            g_fixture.fault_thread_id != g_fixture.reporter_thread_id,
        "fault and reporter threads differ");
    ok &= expect(
        strstr(g_fixture.report, "report_begin=DLUMA_CRASH_REPORT_V1") &&
            strstr(g_fixture.report, "exception_name=stack_overflow") &&
            strstr(g_fixture.report, "stock_dump_result=2") &&
            strstr(g_fixture.report, "report_end=DLUMA_CRASH_REPORT_V1"),
        "complete report");
    CloseHandle(fault);
    CloseHandle(reporter);
    CloseHandle(g_fixture.complete_event);
    CloseHandle(g_fixture.stock_event);
    CloseHandle(g_fixture.work_event);
    if (!ok) {
        fprintf(stderr,
                "crash worker incident: exception=0x%08lX "
                "callback_wait=%lu callback_error=%lu "
                "fault_exit=%lu reporter_exit=%lu\n",
                g_fixture.exception_code, g_fixture.callback_wait,
                g_fixture.callback_error, fault_exit, reporter_exit);
        return 1;
    }
    puts("crash worker constrained-stack harness passed");
    return 0;
}
