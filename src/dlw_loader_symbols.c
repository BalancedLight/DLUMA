#include "dlw_common.h"
#include "dlw_loader_symbols.h"

#if defined(_MSC_VER)
#pragma section(".dlwsym", read)
__declspec(allocate(".dlwsym"))
#endif
const unsigned char
    g_dluma_loader_symbol_manifest[DLUMA_LOADER_SYMBOL_MANIFEST_CAPACITY] = {
        'D','L','W','S','Y','M','0','0'
    };

static const unsigned char g_dluma_loader_symbol_magic[8] = {
    'D','L','W','S','Y','M','1',0
};

static uint32_t dluma_loader_symbol_hash(
    const unsigned char* bytes,
    size_t size,
    size_t hash_offset)
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

static int dluma_loader_symbol_string_valid(
    const unsigned char* table,
    const DlumaLoaderSymbolManifestHeader* header,
    uint32_t offset)
{
    uint32_t index;
    if (!table || !header ||
        offset < header->strings_offset ||
        offset >= header->total_size)
        return 0;
    for (index = offset; index < header->total_size; ++index) {
        unsigned char current = table[index];
        if (!current) return 1;
        if (current == '\r' || current == '\n') return 0;
    }
    return 0;
}

static int dluma_loader_symbol_header_valid(
    const unsigned char* table,
    size_t table_size,
    const DlumaLoaderSymbolManifestHeader** header_out)
{
    const DlumaLoaderSymbolManifestHeader* header;
    uint64_t entries_end;
    size_t hash_offset =
        offsetof(DlumaLoaderSymbolManifestHeader, table_hash);
    if (header_out) *header_out = 0;
    if (!table ||
        table_size < sizeof(DlumaLoaderSymbolManifestHeader))
        return 0;
    header =
        (const DlumaLoaderSymbolManifestHeader*)table;
    if (memcmp(
            header->magic, g_dluma_loader_symbol_magic,
            sizeof(header->magic)) != 0 ||
        header->version != DLUMA_LOADER_SYMBOL_MANIFEST_VERSION ||
        header->header_size !=
            sizeof(DlumaLoaderSymbolManifestHeader) ||
        header->capacity > table_size ||
        header->total_size > header->capacity ||
        header->total_size <
            sizeof(DlumaLoaderSymbolManifestHeader) ||
        header->entries_offset <
            sizeof(DlumaLoaderSymbolManifestHeader) ||
        header->strings_offset < header->entries_offset ||
        header->strings_offset > header->total_size)
        return 0;
    entries_end =
        (uint64_t)header->entries_offset +
        (uint64_t)header->entry_count *
            sizeof(DlumaLoaderSymbolManifestEntry);
    if (entries_end != header->strings_offset ||
        entries_end > header->total_size)
        return 0;
    if (header->table_hash != dluma_loader_symbol_hash(
            table, header->total_size, hash_offset))
        return 0;
    if (header_out) *header_out = header;
    return 1;
}

int dluma_loader_symbol_lookup_in_table(
    const unsigned char* table,
    size_t table_size,
    uint32_t rva,
    DlumaLoaderSymbolLookup* output)
{
    const DlumaLoaderSymbolManifestHeader* header = 0;
    const DlumaLoaderSymbolManifestEntry* entries;
    const DlumaLoaderSymbolManifestEntry* best = 0;
    uint32_t best_size = 0xFFFFFFFFU;
    uint32_t index;
    if (output) memset(output, 0, sizeof(*output));
    if (!output ||
        !dluma_loader_symbol_header_valid(
            table, table_size, &header))
        return 0;
    entries = (const DlumaLoaderSymbolManifestEntry*)(
        table + header->entries_offset);
    for (index = 0; index < header->entry_count; ++index) {
        const DlumaLoaderSymbolManifestEntry* entry =
            &entries[index];
        uint32_t range_size;
        if (entry->start_rva >= entry->end_rva ||
            rva < entry->start_rva ||
            rva >= entry->end_rva)
            continue;
        range_size = entry->end_rva - entry->start_rva;
        if (!best || range_size < best_size) {
            best = entry;
            best_size = range_size;
        }
    }
    if (!best ||
        !dluma_loader_symbol_string_valid(
            table, header, best->name_offset) ||
        !dluma_loader_symbol_string_valid(
            table, header, best->file_offset))
        return 0;
    output->query_rva = rva;
    output->start_rva = best->start_rva;
    output->end_rva = best->end_rva;
    output->source_line = best->source_line;
    output->classification = best->classification;
    output->name = (const char*)table + best->name_offset;
    output->source_file =
        (const char*)table + best->file_offset;
    return 1;
}

int dluma_loader_symbol_lookup(
    uint32_t rva,
    DlumaLoaderSymbolLookup* output)
{
    return dluma_loader_symbol_lookup_in_table(
        g_dluma_loader_symbol_manifest,
        sizeof(g_dluma_loader_symbol_manifest),
        rva, output);
}

const char* dluma_loader_symbol_classification_name(
    uint16_t classification)
{
    switch (classification) {
    case DLUMA_LOADER_SYMBOL_FUNCTION_BODY:
        return "function_body";
    case DLUMA_LOADER_SYMBOL_EPILOGUE:
        return "epilogue";
    case DLUMA_LOADER_SYMBOL_GENERATED_THUNK:
        return "generated_thunk";
    case DLUMA_LOADER_SYMBOL_RELAY:
        return "relay";
    default:
        return "unknown";
    }
}

int dluma_loader_symbol_manifest_available(void)
{
    const DlumaLoaderSymbolManifestHeader* header = 0;
    return dluma_loader_symbol_header_valid(
        g_dluma_loader_symbol_manifest,
        sizeof(g_dluma_loader_symbol_manifest),
        &header);
}
