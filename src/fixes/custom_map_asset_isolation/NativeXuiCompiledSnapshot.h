#ifndef DLUMA_NATIVE_XUI_COMPILED_SNAPSHOT_H
#define DLUMA_NATIVE_XUI_COMPILED_SNAPSHOT_H
#include "NativeXuiRegistrySnapshot.h"
namespace dluma::fixes::custom_map_asset_isolation::xui_compiled {
constexpr std::size_t kTables=32768,kRecords=1048576,kSymbols=65536,kStringBytes=16*1024*1024;
constexpr std::size_t kRanges=32768;
struct Record {std::uintptr_t address;std::uint32_t words[4];};
struct Table {
    std::uintptr_t address;std::int32_t id;
    std::uint32_t record_offset,record_count,first_word,last_word;
    // Native setter/destructor counter. It is retained as consumer evidence,
    // not treated as immutable content or as a complete reference census.
    std::int32_t binding_count;
};
enum class MapKind : std::uint8_t { NamedFrame, AnimationRange };
struct Symbol {std::uintptr_t node;std::uint32_t string_offset,string_bytes;std::int16_t value;MapKind map;};
struct AnimationRange {
    std::uintptr_t address,name_storage;
    std::uint32_t string_offset,string_bytes;
    std::int16_t start,end,id;
};
struct Snapshot {
    Table* tables;std::size_t table_capacity,table_count;
    AnimationRange* ranges;std::size_t range_capacity,range_count;
    Record* records;std::size_t record_capacity,record_count;
    Symbol* symbols;std::size_t symbol_capacity,symbol_count;
    char* strings;std::size_t string_capacity,string_bytes;
    std::uint32_t next_symbol_id;
    xui::Fence fence;xui::Result result;bool complete;
};
// Caller supplies the actual compiled-data writer fence. Deep values and
// native identities are repeated before publication; no borrowed payload is
// used as B0 storage. Signed map values preserve unresolved placeholders.
xui::Result Capture(const xui::Access&,Snapshot*)noexcept;
// True establishes preservation of B0 table/record identities and values and
// every original name/value pair. It does not authorize freeing the tail or
// prove that surviving clients no longer refer to project-created records.
bool BaselinePreserved(const Snapshot& baseline,const Snapshot& current)noexcept;
} // namespace
#endif
