#include "dlw_developer_mode_core.h"

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

typedef struct FakeDeveloperMemory {
    unsigned char references[3][16];
    size_t reference_sizes[3];
    unsigned char flags[3];
    int read_calls;
    int write_calls;
    int fail_read_call;
    int fail_write_call;
} FakeDeveloperMemory;

static const unsigned char kCheatsReference[] = {
    0xC6, 0x05, 0x8D, 0xB1, 0xC3, 0x00, 0x01,
    0xE9, 0xB5, 0x05, 0x00, 0x00
};
static const unsigned char kNoLogosReference[] = {
    0xC6, 0x05, 0x42, 0xAF, 0xC3, 0x00, 0x01,
    0xE9, 0x69, 0x03, 0x00, 0x00
};
static const unsigned char kSkipWaitReference[] = {
    0xC6, 0x05, 0xF7, 0xD7, 0x93, 0x01, 0x01,
    0xB0, 0x01, 0xE9, 0x20, 0x23, 0x00, 0x00
};

static int fake_reference_index(uint32_t rva)
{
    if (rva == DLUMA_DEVELOPER_MODE_CHEATS_REFERENCE_RVA) return 0;
    if (rva == DLUMA_DEVELOPER_MODE_NO_LOGOS_REFERENCE_RVA) return 1;
    if (rva == DLUMA_DEVELOPER_MODE_SKIP_WAIT_REFERENCE_RVA) return 2;
    return -1;
}

static int fake_flag_index(uint32_t rva)
{
    if (rva == DLUMA_DEVELOPER_MODE_CHEATS_FLAG_RVA) return 0;
    if (rva == DLUMA_DEVELOPER_MODE_NO_LOGOS_FLAG_RVA) return 1;
    if (rva == DLUMA_DEVELOPER_MODE_SKIP_WAIT_FLAG_RVA) return 2;
    return -1;
}

static int fake_read(
    void* user, uint32_t rva, void* out, size_t size)
{
    FakeDeveloperMemory* memory = (FakeDeveloperMemory*)user;
    int index;
    ++memory->read_calls;
    if (memory->fail_read_call == memory->read_calls)
        return 0;
    index = fake_reference_index(rva);
    if (index >= 0 && size == memory->reference_sizes[index])
    {
        memcpy(out, memory->references[index], size);
        return 1;
    }
    index = fake_flag_index(rva);
    if (index >= 0 && size == 1)
    {
        *(unsigned char*)out = memory->flags[index];
        return 1;
    }
    return 0;
}

static int fake_write(
    void* user, uint32_t rva, const void* bytes, size_t size)
{
    FakeDeveloperMemory* memory = (FakeDeveloperMemory*)user;
    int index;
    ++memory->write_calls;
    if (memory->fail_write_call == memory->write_calls)
        return 0;
    index = fake_flag_index(rva);
    if (index < 0 || size != 1 || *(const unsigned char*)bytes > 1)
        return 0;
    memory->flags[index] = *(const unsigned char*)bytes;
    return 1;
}

static void fake_init(FakeDeveloperMemory* memory)
{
    memset(memory, 0, sizeof(*memory));
    memcpy(memory->references[0], kCheatsReference, sizeof(kCheatsReference));
    memcpy(memory->references[1], kNoLogosReference, sizeof(kNoLogosReference));
    memcpy(memory->references[2], kSkipWaitReference, sizeof(kSkipWaitReference));
    memory->reference_sizes[0] = sizeof(kCheatsReference);
    memory->reference_sizes[1] = sizeof(kNoLogosReference);
    memory->reference_sizes[2] = sizeof(kSkipWaitReference);
}

static DlumaDeveloperModeCoreInput fake_input(FakeDeveloperMemory* memory)
{
    DlumaDeveloperModeCoreInput input;
    input.enabled = 1;
    input.timestamp = DLUMA_DEVELOPER_MODE_GAMEDLL_TIMESTAMP;
    input.image_size = DLUMA_DEVELOPER_MODE_GAMEDLL_IMAGE_SIZE;
    input.read = fake_read;
    input.write = fake_write;
    input.user = memory;
    return input;
}

static int case_disabled(void)
{
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    memset(&input, 0, sizeof(input));
    input.enabled = 0;
    CHECK(dluma_developer_mode_core_apply(&input, &result));
    CHECK(result.status == DLUMA_DEVELOPER_MODE_CORE_DISABLED);
    CHECK(result.changed_count == 0);
    return 1;
}

static int case_supported_apply_and_idempotence(void)
{
    FakeDeveloperMemory memory;
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    fake_init(&memory);
    input = fake_input(&memory);
    CHECK(dluma_developer_mode_core_apply(&input, &result));
    CHECK(result.status == DLUMA_DEVELOPER_MODE_CORE_APPLIED);
    CHECK(result.changed_count == 3);
    CHECK(memory.flags[0] == 1 && memory.flags[1] == 1 && memory.flags[2] == 1);
    memory.read_calls = 0;
    memory.write_calls = 0;
    CHECK(dluma_developer_mode_core_apply(&input, &result));
    CHECK(result.changed_count == 0);
    CHECK(memory.write_calls == 0);
    return 1;
}

static int case_unsupported_identity(void)
{
    FakeDeveloperMemory memory;
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    fake_init(&memory);
    input = fake_input(&memory);
    input.timestamp ^= 1U;
    CHECK(!dluma_developer_mode_core_apply(&input, &result));
    CHECK(result.status == DLUMA_DEVELOPER_MODE_CORE_UNSUPPORTED);
    CHECK(memory.read_calls == 0 && memory.write_calls == 0);
    return 1;
}

static int case_reference_and_flag_validation(void)
{
    FakeDeveloperMemory memory;
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    fake_init(&memory);
    input = fake_input(&memory);
    memory.references[1][3] ^= 1U;
    CHECK(!dluma_developer_mode_core_apply(&input, &result));
    CHECK(strcmp(result.failure_stage, "instruction_reference_mismatch") == 0);
    CHECK(memory.write_calls == 0);

    fake_init(&memory);
    input = fake_input(&memory);
    memory.flags[2] = 2;
    CHECK(!dluma_developer_mode_core_apply(&input, &result));
    CHECK(strcmp(result.failure_stage, "flag_value_invalid") == 0);
    CHECK(memory.write_calls == 0);
    return 1;
}

static int case_write_failure_rolls_back(void)
{
    FakeDeveloperMemory memory;
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    fake_init(&memory);
    input = fake_input(&memory);
    memory.fail_write_call = 2;
    CHECK(!dluma_developer_mode_core_apply(&input, &result));
    CHECK(result.status == DLUMA_DEVELOPER_MODE_CORE_WRITE_FAILED);
    CHECK(strcmp(result.failure_stage, "write_or_readback_failed") == 0);
    CHECK(memory.flags[0] == 0 && memory.flags[1] == 0 && memory.flags[2] == 0);
    return 1;
}

static int case_readback_failure_rolls_back(void)
{
    FakeDeveloperMemory memory;
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    fake_init(&memory);
    input = fake_input(&memory);
    /* Six preflight reads, then the first commit readback. */
    memory.fail_read_call = 7;
    CHECK(!dluma_developer_mode_core_apply(&input, &result));
    CHECK(result.status == DLUMA_DEVELOPER_MODE_CORE_WRITE_FAILED);
    CHECK(strcmp(result.failure_stage, "write_or_readback_failed") == 0);
    CHECK(memory.flags[0] == 0 && memory.flags[1] == 0 && memory.flags[2] == 0);
    return 1;
}

int main(void)
{
    CHECK(case_disabled());
    CHECK(case_supported_apply_and_idempotence());
    CHECK(case_unsupported_identity());
    CHECK(case_reference_and_flag_validation());
    CHECK(case_write_failure_rolls_back());
    CHECK(case_readback_failure_rolls_back());
    puts("developer_mode_core_harness: PASS");
    return 0;
}
