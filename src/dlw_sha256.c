#include "dlw_common.h"
#include "dlw_sha256_internal.h"

/* Small no-CRT SHA-256 implementation used for consent identities.  It hashes
 * exact bytes and has no dependency on bcrypt/advapi32, preserving the
 * loader's KERNEL32.dll + USER32.dll static-import boundary. */

static DWORD dluma_sha256_rotr(DWORD value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

static DWORD dluma_sha256_read_be32(const BYTE* bytes)
{
    return ((DWORD)bytes[0] << 24) | ((DWORD)bytes[1] << 16) |
           ((DWORD)bytes[2] << 8) | (DWORD)bytes[3];
}

static void dluma_sha256_write_be32(BYTE* bytes, DWORD value)
{
    bytes[0] = (BYTE)(value >> 24);
    bytes[1] = (BYTE)(value >> 16);
    bytes[2] = (BYTE)(value >> 8);
    bytes[3] = (BYTE)value;
}

static void dluma_sha256_transform(DlumaSha256Context* context,
                                 const BYTE block[64])
{
    static const DWORD constants[64] = {
        0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,
        0x3956c25bUL,0x59f111f1UL,0x923f82a4UL,0xab1c5ed5UL,
        0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,
        0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,
        0xe49b69c1UL,0xefbe4786UL,0x0fc19dc6UL,0x240ca1ccUL,
        0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
        0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,
        0xc6e00bf3UL,0xd5a79147UL,0x06ca6351UL,0x14292967UL,
        0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,
        0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,
        0xa2bfe8a1UL,0xa81a664bUL,0xc24b8b70UL,0xc76c51a3UL,
        0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
        0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,
        0x391c0cb3UL,0x4ed8aa4aUL,0x5b9cca4fUL,0x682e6ff3UL,
        0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,
        0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL
    };
    DWORD words[64];
    DWORD a, b, c, d, e, f, g, h;
    unsigned int i;
    for (i = 0; i < 16; ++i)
        words[i] = dluma_sha256_read_be32(block + i * 4U);
    for (i = 16; i < 64; ++i) {
        DWORD s0 = dluma_sha256_rotr(words[i - 15], 7) ^
                   dluma_sha256_rotr(words[i - 15], 18) ^
                   (words[i - 15] >> 3);
        DWORD s1 = dluma_sha256_rotr(words[i - 2], 17) ^
                   dluma_sha256_rotr(words[i - 2], 19) ^
                   (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (i = 0; i < 64; ++i) {
        DWORD sum1 = dluma_sha256_rotr(e, 6) ^ dluma_sha256_rotr(e, 11) ^
                     dluma_sha256_rotr(e, 25);
        DWORD choose = (e & f) ^ ((~e) & g);
        DWORD t1 = h + sum1 + choose + constants[i] + words[i];
        DWORD sum0 = dluma_sha256_rotr(a, 2) ^ dluma_sha256_rotr(a, 13) ^
                     dluma_sha256_rotr(a, 22);
        DWORD majority = (a & b) ^ (a & c) ^ (b & c);
        DWORD t2 = sum0 + majority;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void dluma_sha256_init(DlumaSha256Context* context)
{
    if (!context) return;
    context->state[0] = 0x6a09e667UL;
    context->state[1] = 0xbb67ae85UL;
    context->state[2] = 0x3c6ef372UL;
    context->state[3] = 0xa54ff53aUL;
    context->state[4] = 0x510e527fUL;
    context->state[5] = 0x9b05688cUL;
    context->state[6] = 0x1f83d9abUL;
    context->state[7] = 0x5be0cd19UL;
    context->total_bytes = 0;
    context->block_size = 0;
    memset(context->block, 0, sizeof(context->block));
}

void dluma_sha256_update(DlumaSha256Context* context, const void* data, SIZE_T size)
{
    const BYTE* bytes = (const BYTE*)data;
    SIZE_T offset = 0;
    if (!context || (!bytes && size)) return;
    context->total_bytes += (ULONGLONG)size;
    while (offset < size) {
        unsigned int available = 64U - context->block_size;
        SIZE_T remaining = size - offset;
        unsigned int take = remaining < (SIZE_T)available
            ? (unsigned int)remaining : available;
        memcpy(context->block + context->block_size, bytes + offset, take);
        context->block_size += take;
        offset += take;
        if (context->block_size == 64U) {
            dluma_sha256_transform(context, context->block);
            context->block_size = 0;
        }
    }
}

void dluma_sha256_final(DlumaSha256Context* context, BYTE digest[32])
{
    ULONGLONG bit_count;
    unsigned int i;
    if (!context || !digest) return;
    bit_count = context->total_bytes * 8ULL;
    context->block[context->block_size++] = 0x80U;
    if (context->block_size > 56U) {
        while (context->block_size < 64U)
            context->block[context->block_size++] = 0;
        dluma_sha256_transform(context, context->block);
        context->block_size = 0;
    }
    while (context->block_size < 56U)
        context->block[context->block_size++] = 0;
    for (i = 0; i < 8U; ++i)
        context->block[56U + i] = (BYTE)(bit_count >> (56U - i * 8U));
    dluma_sha256_transform(context, context->block);
    for (i = 0; i < 8U; ++i)
        dluma_sha256_write_be32(digest + i * 4U, context->state[i]);
    memset(context, 0, sizeof(*context));
}

void dluma_sha256_hex(const BYTE digest[32],
                    char out[DLUMA_SHA256_HEX_CAPACITY])
{
    static const char alphabet[] = "0123456789abcdef";
    unsigned int i;
    if (!digest || !out) return;
    for (i = 0; i < 32U; ++i) {
        out[i * 2U] = alphabet[digest[i] >> 4];
        out[i * 2U + 1U] = alphabet[digest[i] & 15U];
    }
    out[64] = 0;
}

void dluma_sha256_bytes_hex(const void* data, SIZE_T size,
                          char out[DLUMA_SHA256_HEX_CAPACITY])
{
    DlumaSha256Context context;
    BYTE digest[32];
    dluma_sha256_init(&context);
    dluma_sha256_update(&context, data, size);
    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, out);
}

static void dluma_sha256_update_u32(DlumaSha256Context* context, DWORD value)
{
    BYTE encoded[4];
    dluma_sha256_write_be32(encoded, value);
    dluma_sha256_update(context, encoded, sizeof(encoded));
}

void dluma_sha256_fields_hex(const char* schema,
                           const char* const* fields,
                           const unsigned int* lengths,
                           unsigned int field_count,
                           char out[DLUMA_SHA256_HEX_CAPACITY])
{
    DlumaSha256Context context;
    BYTE digest[32];
    unsigned int i;
    unsigned int schema_length = schema ? (unsigned int)strlen(schema) : 0;
    dluma_sha256_init(&context);
    dluma_sha256_update_u32(&context, schema_length);
    if (schema_length) dluma_sha256_update(&context, schema, schema_length);
    dluma_sha256_update_u32(&context, field_count);
    for (i = 0; i < field_count; ++i) {
        unsigned int length = lengths ? lengths[i] :
            (fields && fields[i] ? (unsigned int)strlen(fields[i]) : 0);
        dluma_sha256_update_u32(&context, length);
        if (length && fields && fields[i])
            dluma_sha256_update(&context, fields[i], length);
    }
    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, out);
}

int dluma_sha256_handle_hex(
    HANDLE file, ULONGLONG max_bytes,
    char out[DLUMA_SHA256_HEX_CAPACITY])
{
    LARGE_INTEGER size;
    LARGE_INTEGER zero;
    DlumaSha256Context context;
    BYTE digest[32];
    BYTE buffer[16384];
    ULONGLONG remaining;
    if (!file || file == INVALID_HANDLE_VALUE || !out) return 0;
    out[0] = 0;
    zero.QuadPart = 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > max_bytes ||
        !SetFilePointerEx(file, zero, 0, FILE_BEGIN))
        return 0;
    remaining = (ULONGLONG)size.QuadPart;
    dluma_sha256_init(&context);
    while (remaining) {
        DWORD wanted = remaining > sizeof(buffer)
            ? (DWORD)sizeof(buffer) : (DWORD)remaining;
        DWORD read = 0;
        if (!ReadFile(file, buffer, wanted, &read, 0) || read != wanted)
            return 0;
        dluma_sha256_update(&context, buffer, read);
        remaining -= read;
    }
    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, out);
    return 1;
}

int dluma_sha256_file_hex(const WCHAR* path, ULONGLONG max_bytes,
                        char out[DLUMA_SHA256_HEX_CAPACITY])
{
    HANDLE file;
    int result;
    if (!path || !out) return 0;
    out[0] = 0;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    result = dluma_sha256_handle_hex(file, max_bytes, out);
    CloseHandle(file);
    return result;
}
