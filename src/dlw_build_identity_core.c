#include "dlw_build_identity.h"

#define DLUMA_PE_DOS_MAGIC 0x5A4DU
#define DLUMA_PE_SIGNATURE 0x00004550UL
#define DLUMA_PE_MACHINE_AMD64 0x8664U
#define DLUMA_PE_OPTIONAL_MAGIC_PE32_PLUS 0x020BU

static WORD dluma_build_identity_read_u16(const BYTE* input)
{
    return (WORD)((WORD)input[0] | ((WORD)input[1] << 8));
}

static DWORD dluma_build_identity_read_u32(const BYTE* input)
{
    return (DWORD)input[0] |
           ((DWORD)input[1] << 8) |
           ((DWORD)input[2] << 16) |
           ((DWORD)input[3] << 24);
}

int dluma_build_identity_parse_pe_header(
    const BYTE* image,
    SIZE_T available,
    DWORD* timestamp,
    DWORD* size_of_image)
{
    DWORD nt_offset;
    SIZE_T nt;
    WORD optional_size;
    DWORD parsed_size;
    if (timestamp) *timestamp = 0;
    if (size_of_image) *size_of_image = 0;
    if (!image || available < 0x40U ||
        dluma_build_identity_read_u16(image) != DLUMA_PE_DOS_MAGIC)
        return 0;

    nt_offset = dluma_build_identity_read_u32(image + 0x3CU);
    nt = (SIZE_T)nt_offset;
    /* The fields used below end at optional-header offset 0x3C. */
    if (nt > available || available - nt < 84U ||
        dluma_build_identity_read_u32(image + nt) != DLUMA_PE_SIGNATURE ||
        dluma_build_identity_read_u16(image + nt + 4U) !=
            DLUMA_PE_MACHINE_AMD64)
        return 0;

    optional_size = dluma_build_identity_read_u16(image + nt + 20U);
    if (optional_size < 60U ||
        dluma_build_identity_read_u16(image + nt + 24U) !=
            DLUMA_PE_OPTIONAL_MAGIC_PE32_PLUS)
        return 0;

    parsed_size = dluma_build_identity_read_u32(image + nt + 80U);
    if (parsed_size < 0x1000UL) return 0;
    if (timestamp)
        *timestamp = dluma_build_identity_read_u32(image + nt + 8U);
    if (size_of_image) *size_of_image = parsed_size;
    return 1;
}

static void dluma_build_identity_append(
    char* out,
    unsigned int capacity,
    const char* text)
{
    unsigned int used = 0;
    if (!out || !capacity || !text) return;
    while (used + 1U < capacity && out[used]) ++used;
    while (used + 1U < capacity && *text) out[used++] = *text++;
    out[used] = 0;
}

static void dluma_build_identity_append_hex32(
    char* out,
    unsigned int capacity,
    DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char encoded[11];
    unsigned int index;
    encoded[0] = '0';
    encoded[1] = 'x';
    for (index = 0; index < 8U; ++index) {
        unsigned int shift = (7U - index) * 4U;
        encoded[2U + index] = digits[(value >> shift) & 0xFU];
    }
    encoded[10] = 0;
    dluma_build_identity_append(out, capacity, encoded);
}

static void dluma_build_identity_append_u64(
    char* out,
    unsigned int capacity,
    ULONGLONG value)
{
    char reverse[32];
    char encoded[32];
    unsigned int count = 0;
    unsigned int index;
    do {
        reverse[count++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value && count < sizeof(reverse));
    for (index = 0; index < count; ++index)
        encoded[index] = reverse[count - index - 1U];
    encoded[count] = 0;
    dluma_build_identity_append(out, capacity, encoded);
}

void dluma_build_identity_format(
    const DlumaBuildIdentity* identity,
    const char* loader_version,
    char* out,
    unsigned int capacity)
{
    if (!out || !capacity) return;
    out[0] = 0;
    dluma_build_identity_append(out, capacity,
                              "schema=DLUMA_BUILD_IDENTITY_V1 version=");
    dluma_build_identity_append(out, capacity,
                              loader_version ? loader_version : "<unknown>");
    dluma_build_identity_append(out, capacity, " pe_timestamp=");
    if (identity && identity->pe_identity_valid)
        dluma_build_identity_append_hex32(out, capacity,
                                        identity->pe_timestamp);
    else
        dluma_build_identity_append(out, capacity, "unavailable");
    dluma_build_identity_append(out, capacity, " size_of_image=");
    if (identity && identity->pe_identity_valid)
        dluma_build_identity_append_hex32(out, capacity,
                                        identity->size_of_image);
    else
        dluma_build_identity_append(out, capacity, "unavailable");
    dluma_build_identity_append(out, capacity, " file_size=");
    if (identity && identity->file_size_valid)
        dluma_build_identity_append_u64(out, capacity, identity->file_size);
    else
        dluma_build_identity_append(out, capacity, "unavailable");
    dluma_build_identity_append(out, capacity, " sha256=");
    if (identity && identity->file_hash_valid && identity->sha256[0])
        dluma_build_identity_append(out, capacity, identity->sha256);
    else
        dluma_build_identity_append(out, capacity, "unavailable");
    dluma_build_identity_append(out, capacity, " complete=");
    dluma_build_identity_append(
        out, capacity, identity && identity->complete ? "1" : "0");
}
