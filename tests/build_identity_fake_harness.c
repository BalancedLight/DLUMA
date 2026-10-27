#include "dlw_build_identity.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,      \
                    #expression);                                             \
            return 0;                                                         \
        }                                                                     \
    } while (0)

static void write_u16(BYTE* output, unsigned int offset, WORD value)
{
    output[offset] = (BYTE)(value & 0xFFU);
    output[offset + 1U] = (BYTE)((value >> 8) & 0xFFU);
}

static void write_u32(BYTE* output, unsigned int offset, DWORD value)
{
    output[offset] = (BYTE)(value & 0xFFUL);
    output[offset + 1U] = (BYTE)((value >> 8) & 0xFFUL);
    output[offset + 2U] = (BYTE)((value >> 16) & 0xFFUL);
    output[offset + 3U] = (BYTE)((value >> 24) & 0xFFUL);
}

static void make_valid_pe(BYTE image[512])
{
    const unsigned int nt = 0x80U;
    memset(image, 0, 512U);
    write_u16(image, 0, 0x5A4DU);
    write_u32(image, 0x3CU, nt);
    write_u32(image, nt, 0x00004550UL);
    write_u16(image, nt + 4U, 0x8664U);
    write_u32(image, nt + 8U, 0x65ABC123UL);
    write_u16(image, nt + 20U, 0x00F0U);
    write_u16(image, nt + 24U, 0x020BU);
    write_u32(image, nt + 80U, 0x00123000UL);
}

static int test_valid_and_invalid_headers(void)
{
    BYTE image[512];
    DWORD timestamp = 0;
    DWORD image_size = 0;
    make_valid_pe(image);
    CHECK(dluma_build_identity_parse_pe_header(
        image, sizeof(image), &timestamp, &image_size));
    CHECK(timestamp == 0x65ABC123UL);
    CHECK(image_size == 0x00123000UL);

    CHECK(!dluma_build_identity_parse_pe_header(
        image, 0x83U, &timestamp, &image_size));
    CHECK(timestamp == 0);
    CHECK(image_size == 0);

    image[0] = 0;
    CHECK(!dluma_build_identity_parse_pe_header(
        image, sizeof(image), &timestamp, &image_size));
    make_valid_pe(image);
    write_u16(image, 0x84U, 0x014CU);
    CHECK(!dluma_build_identity_parse_pe_header(
        image, sizeof(image), &timestamp, &image_size));
    make_valid_pe(image);
    write_u16(image, 0x98U, 0x010BU);
    CHECK(!dluma_build_identity_parse_pe_header(
        image, sizeof(image), &timestamp, &image_size));
    make_valid_pe(image);
    write_u32(image, 0xD0U, 0x800U);
    CHECK(!dluma_build_identity_parse_pe_header(
        image, sizeof(image), &timestamp, &image_size));
    return 1;
}

static int test_deterministic_identity_format(void)
{
    static const char hash_a[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    static const char hash_b[] =
        "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    DlumaBuildIdentity identity;
    char first[DLUMA_BUILD_IDENTITY_TEXT_CAPACITY];
    char second[DLUMA_BUILD_IDENTITY_TEXT_CAPACITY];
    memset(&identity, 0, sizeof(identity));
    identity.pe_timestamp = 0x65ABC123UL;
    identity.size_of_image = 0x00123000UL;
    identity.file_size = 987654321ULL;
    strcpy(identity.sha256, hash_a);
    identity.pe_identity_valid = 1;
    identity.file_size_valid = 1;
    identity.file_hash_valid = 1;
    identity.complete = 1;

    dluma_build_identity_format(
        &identity, "0.3.0a1-test", first, sizeof(first));
    dluma_build_identity_format(
        &identity, "0.3.0a1-test", second, sizeof(second));
    CHECK(strcmp(first, second) == 0);
    CHECK(strstr(first, "schema=DLUMA_BUILD_IDENTITY_V1") != 0);
    CHECK(strstr(first, "version=0.3.0a1-test") != 0);
    CHECK(strstr(first, "pe_timestamp=0x65ABC123") != 0);
    CHECK(strstr(first, "size_of_image=0x00123000") != 0);
    CHECK(strstr(first, "file_size=987654321") != 0);
    CHECK(strstr(first, hash_a) != 0);
    CHECK(strstr(first, "complete=1") != 0);

    strcpy(identity.sha256, hash_b);
    dluma_build_identity_format(
        &identity, "0.3.0a1-test", second, sizeof(second));
    CHECK(strcmp(first, second) != 0);

    memset(&identity, 0, sizeof(identity));
    dluma_build_identity_format(&identity, "0.3.0a1-test",
                              second, sizeof(second));
    CHECK(strstr(second, "pe_timestamp=unavailable") != 0);
    CHECK(strstr(second, "size_of_image=unavailable") != 0);
    CHECK(strstr(second, "file_size=unavailable") != 0);
    CHECK(strstr(second, "sha256=unavailable") != 0);
    CHECK(strstr(second, "complete=0") != 0);
    return 1;
}

int main(void)
{
    if (!test_valid_and_invalid_headers() ||
        !test_deterministic_identity_format())
        return 1;
    puts("build identity fake harness: PASS");
    return 0;
}
