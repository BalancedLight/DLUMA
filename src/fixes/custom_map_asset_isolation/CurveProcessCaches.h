#ifndef DLUMA_FIXES_CURVE_PROCESS_CACHES_H
#define DLUMA_FIXES_CURVE_PROCESS_CACHES_H
#include "CurveBaselineSnapshot.h"

namespace dluma::fixes::custom_map_asset_isolation::curves {
constexpr std::size_t kCurveProcessCacheCount=6;
constexpr std::size_t kCurveWeightCount=20;
struct CurveProcessCacheDescriptor {std::uintptr_t id_rva,flag_rva;const char* name;};
struct CurveProcessCacheEntry {std::int32_t id;std::uint32_t flag;};
struct CurveProcessCacheSnapshot {
    CurveProcessCacheEntry entries[kCurveProcessCacheCount];
    std::uint32_t weights[kCurveWeightCount],weight_sum;
    std::uint8_t weights_valid;
    CurveReadFence fence;
    bool complete;
};
struct CurveProcessCacheWrite {std::uintptr_t address;std::uint32_t expected,desired;std::uint8_t size;};
struct CurveProcessCachePlan {CurveProcessCacheWrite writes[36];std::size_t count;};
const CurveProcessCacheDescriptor* CurveProcessCacheCatalog(std::size_t*) noexcept;
bool CaptureCurveProcessCaches(const NativeCurveAccess*,const CurveSnapshot*,CurveProcessCacheSnapshot*) noexcept;
bool SameCurveProcessCaches(const CurveProcessCacheSnapshot*,const CurveProcessCacheSnapshot*) noexcept;
bool PlanCurveProcessCacheRestore(std::uintptr_t,const CurveProcessCacheSnapshot*,const CurveProcessCacheSnapshot*,CurveProcessCachePlan*) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
