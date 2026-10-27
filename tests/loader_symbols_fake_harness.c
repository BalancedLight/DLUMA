#include <stdio.h>
#include <string.h>

#include "../src/dlw_loader_symbols.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed: %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static uint32_t table_hash(
    const unsigned char* bytes, size_t size, size_t hash_offset)
{
    uint32_t value = 2166136261U;
    size_t index;
    for (index = 0; index < size; ++index) {
        unsigned char current =
            index >= hash_offset &&
                    index < hash_offset + sizeof(uint32_t)
                ? 0U
                : bytes[index];
        value ^= current;
        value *= 16777619U;
    }
    return value;
}

static void build_table(unsigned char* table, size_t capacity)
{
    static const unsigned char magic[8] = {
        'D','L','W','S','Y','M','1',0
    };
    static const char strings[] =
        "wide_function\0src/wide.c\0"
        "exact_frame\0src/exact.c\0";
    DlumaLoaderSymbolManifestHeader* header =
        (DlumaLoaderSymbolManifestHeader*)table;
    DlumaLoaderSymbolManifestEntry* entries;
    uint32_t strings_offset =
        (uint32_t)(sizeof(*header) +
                   2U * sizeof(DlumaLoaderSymbolManifestEntry));
    uint32_t wide_name = strings_offset;
    uint32_t wide_file = wide_name + 14U;
    uint32_t exact_name = wide_file + 11U;
    uint32_t exact_file = exact_name + 12U;
    memset(table, 0, capacity);
    memcpy(header->magic, magic, sizeof(magic));
    header->version = DLUMA_LOADER_SYMBOL_MANIFEST_VERSION;
    header->header_size = (uint32_t)sizeof(*header);
    header->capacity = (uint32_t)capacity;
    header->entry_count = 2U;
    header->entries_offset = (uint32_t)sizeof(*header);
    header->strings_offset = strings_offset;
    header->total_size =
        strings_offset + (uint32_t)sizeof(strings);
    header->image_timestamp = 0x12345678U;
    header->image_size = 0x00200000U;
    entries = (DlumaLoaderSymbolManifestEntry*)(
        table + header->entries_offset);
    entries[0].start_rva = 0x100U;
    entries[0].end_rva = 0x200U;
    entries[0].source_line = 20U;
    entries[0].name_offset = wide_name;
    entries[0].file_offset = wide_file;
    entries[0].classification =
        DLUMA_LOADER_SYMBOL_FUNCTION_BODY;
    entries[1].start_rva = 0x150U;
    entries[1].end_rva = 0x151U;
    entries[1].source_line = 77U;
    entries[1].name_offset = exact_name;
    entries[1].file_offset = exact_file;
    entries[1].classification =
        DLUMA_LOADER_SYMBOL_EPILOGUE;
    memcpy(table + strings_offset, strings, sizeof(strings));
    header->table_hash = table_hash(
        table, header->total_size,
        offsetof(DlumaLoaderSymbolManifestHeader, table_hash));
}

int main(void)
{
    unsigned char table[512];
    DlumaLoaderSymbolLookup lookup;
    DlumaLoaderSymbolManifestHeader* header =
        (DlumaLoaderSymbolManifestHeader*)table;
    uint32_t original_hash;
    build_table(table, sizeof(table));

    CHECK(dluma_loader_symbol_lookup_in_table(
        table, sizeof(table), 0x120U, &lookup));
    CHECK(lookup.start_rva == 0x100U);
    CHECK(lookup.end_rva == 0x200U);
    CHECK(lookup.source_line == 20U);
    CHECK(strcmp(lookup.name, "wide_function") == 0);
    CHECK(strcmp(lookup.source_file, "src/wide.c") == 0);

    CHECK(dluma_loader_symbol_lookup_in_table(
        table, sizeof(table), 0x150U, &lookup));
    CHECK(lookup.start_rva == 0x150U);
    CHECK(lookup.end_rva == 0x151U);
    CHECK(lookup.source_line == 77U);
    CHECK(lookup.classification == DLUMA_LOADER_SYMBOL_EPILOGUE);
    CHECK(strcmp(lookup.name, "exact_frame") == 0);
    CHECK(strcmp(
        dluma_loader_symbol_classification_name(lookup.classification),
        "epilogue") == 0);

    CHECK(!dluma_loader_symbol_lookup_in_table(
        table, sizeof(table), 0x250U, &lookup));
    CHECK(!dluma_loader_symbol_manifest_available());

    original_hash = header->table_hash;
    header->table_hash ^= 1U;
    CHECK(!dluma_loader_symbol_lookup_in_table(
        table, sizeof(table), 0x120U, &lookup));
    header->table_hash = original_hash;

    table[header->strings_offset] = '\n';
    header->table_hash = table_hash(
        table, header->total_size,
        offsetof(DlumaLoaderSymbolManifestHeader, table_hash));
    CHECK(!dluma_loader_symbol_lookup_in_table(
        table, sizeof(table), 0x120U, &lookup));

    puts("loader symbol table fake harness: PASS");
    return 0;
}
