#ifndef DLUMA_FIXES_CURVE_LOAD_ORIGIN_H
#define DLUMA_FIXES_CURVE_LOAD_ORIGIN_H
#include "NativeFileLifetimeLedger.h"
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::curves {
enum class CurveSourceOwnership : std::uint8_t { Unknown, SharedGlobal, ExclusiveProject };
struct CurveOpenedSource {
    file_lifetime::FileRecord file;
    std::uint64_t source_revision;
    std::uint32_t project_epoch;
    CurveSourceOwnership ownership;
};
struct CurveSourceReceipt {
    file_lifetime::SourceKey source;
    std::uint64_t source_revision,file_generation,open_root;
    std::uint32_t project_epoch;
    CurveSourceOwnership ownership;
    bool valid;
    std::uint64_t load_serial;
    std::uint32_t load_thread_id;
};
struct CurveLoadToken {std::uint64_t serial;std::uint32_t depth;bool active;};
bool BeginCurveLoadObservation(const char* requested_path,std::uint32_t project_epoch,CurveLoadToken*) noexcept;
// Called by the actual fs::open observer with its returned file/source identity,
// never by resolving the path again after native parsing completed.
void NoteOpenedCurveSource(const char* opened_path,const CurveOpenedSource*) noexcept;
bool CurrentCurveLoadSource(CurveSourceReceipt*) noexcept;
std::uint64_t CurrentCurveLoadSerial() noexcept;
bool EndCurveLoadObservation(CurveLoadToken*) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
