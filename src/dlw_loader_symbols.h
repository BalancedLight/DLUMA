#ifndef DLUMA_LOADER_SYMBOLS_H
#define DLUMA_LOADER_SYMBOLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_LOADER_SYMBOL_MANIFEST_CAPACITY (1024U * 1024U)
#define DLUMA_LOADER_SYMBOL_MANIFEST_VERSION 1U

typedef enum DlumaLoaderSymbolClassification {
    DLUMA_LOADER_SYMBOL_FUNCTION_BODY = 1,
    DLUMA_LOADER_SYMBOL_EPILOGUE = 2,
    DLUMA_LOADER_SYMBOL_GENERATED_THUNK = 3,
    DLUMA_LOADER_SYMBOL_RELAY = 4
} DlumaLoaderSymbolClassification;

#pragma pack(push, 1)
typedef struct DlumaLoaderSymbolManifestHeader {
    unsigned char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t capacity;
    uint32_t entry_count;
    uint32_t entries_offset;
    uint32_t strings_offset;
    uint32_t image_timestamp;
    uint32_t image_size;
    uint32_t table_hash;
} DlumaLoaderSymbolManifestHeader;

typedef struct DlumaLoaderSymbolManifestEntry {
    uint32_t start_rva;
    uint32_t end_rva;
    uint32_t source_line;
    uint32_t name_offset;
    uint32_t file_offset;
    uint16_t classification;
    uint16_t reserved;
} DlumaLoaderSymbolManifestEntry;
#pragma pack(pop)

typedef struct DlumaLoaderSymbolLookup {
    uint32_t query_rva;
    uint32_t start_rva;
    uint32_t end_rva;
    uint32_t source_line;
    uint16_t classification;
    const char* name;
    const char* source_file;
} DlumaLoaderSymbolLookup;

int dluma_loader_symbol_lookup(
    uint32_t rva,
    DlumaLoaderSymbolLookup* output);

int dluma_loader_symbol_lookup_in_table(
    const unsigned char* table,
    size_t table_size,
    uint32_t rva,
    DlumaLoaderSymbolLookup* output);

const char* dluma_loader_symbol_classification_name(
    uint16_t classification);

int dluma_loader_symbol_manifest_available(void);

#ifdef __cplusplus
}
#endif

#endif
