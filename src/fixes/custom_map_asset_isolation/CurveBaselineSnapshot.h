#ifndef DLUMA_FIXES_CURVE_BASELINE_SNAPSHOT_H
#define DLUMA_FIXES_CURVE_BASELINE_SNAPSHOT_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::curves {

constexpr char kPlayerGameDllSha256[]="cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95";
constexpr std::uintptr_t kManagerRva=0x1D07FD8;
constexpr std::uintptr_t kVectorRva=0x1C9EC28;
constexpr std::uintptr_t kManagerVtableRva=0x1906338;
constexpr std::uintptr_t kSplineVtableRva=0x196DCC8;
constexpr std::size_t kMaximumCurves=8192;
constexpr std::size_t kMaximumCurveStorage=32U*1024U*1024U;
constexpr std::size_t kMaximumCurveName=65535;

enum class CurveResult : std::uint8_t {
    Succeeded, InvalidArgument, ProfileMismatch, AccessNotQuiescent,
    ReadFailed, ChangedDuringCapture, InvalidShape, UnsupportedType,
    CapacityExceeded, InvalidSnapshot, BinaryLimitExceeded,
};
enum class CurvePrefixKind : std::uint8_t {
    InvalidSnapshot, Identical, AppendedTail, BaselineMissing,
    BaselineIdentityChanged, BaselineValueChanged,
};
struct CurveReadFence {
    // Advances on replacement/reload of the manager's owned objects and on
    // approved global baseline changes; append-only custom additions use the
    // mutation revision without concealing an identity-generation change.
    std::uint64_t owner_generation;
    std::uint64_t mutation_revision;
    bool authoritative_thread;
    bool access_closed;
    bool readers_drained;
};
using CurveReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
using CurveFenceFn=bool(*)(void*,CurveReadFence*) noexcept;
struct NativeCurveAccess {
    std::uintptr_t module_base;
    char module_sha256[65];
    CurveReadFn read;
    CurveFenceFn fence;
    void* user;
};
struct CurvePoint { std::uint32_t x_bits,y_bits; std::int32_t type; };
struct CurveRecord {
    std::uintptr_t native_identity;
    std::uintptr_t native_name,native_samples,native_points;
    std::uint32_t name_offset,name_bytes;
    std::uint32_t samples_offset,sample_count;
    std::uint32_t points_offset,point_count;
    std::uint32_t requested_samples;
    std::uint32_t bounds_bits[4];
    std::uint32_t sample_sum_bits;
};
struct CurveSnapshot {
    CurveRecord* records;
    std::size_t record_capacity;
    std::uint8_t* storage;
    std::size_t storage_capacity;
    std::size_t count;
    std::size_t storage_used;
    CurveReadFence fence;
    std::uintptr_t manager_identity;
    bool complete;
};
struct CurvePrefixAssessment {
    CurvePrefixKind kind;
    std::size_t first_difference;
    std::size_t tail_begin;
    std::size_t tail_count;
};

// Read-only utility. Production must supply the actual native owner gate's
// fence. This module does not claim caller-supplied true flags prove closure.
CurveResult CaptureCurveSnapshot(const NativeCurveAccess* access,CurveSnapshot* out) noexcept;
CurveResult ValidateCurveSnapshot(const CurveSnapshot* snapshot) noexcept;
// Identity plus all observable deep values and order must match. A result of
// AppendedTail is a detach candidate, not a consumer-release/destruction proof.
CurvePrefixAssessment ClassifyCurvePrefix(const CurveSnapshot* baseline,
    const CurveSnapshot* current) noexcept;
CurveResult MeasureCurveBaselineBinary(const CurveSnapshot* snapshot,
    std::size_t* bytes) noexcept;
// Exact native loader format, not the incomplete native Save implementation.
// Encodes all names in order, including global additions and reverse curves.
// Generated sample bytes/sum remain in the snapshot for rebuild verification.
CurveResult EncodeCurveBaseline(const CurveSnapshot* snapshot,std::uint8_t* output,
    std::size_t capacity,std::size_t* written) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
