#ifndef DLUMA_NATIVE_FILE_LIFETIME_LEDGER_H
#define DLUMA_NATIVE_FILE_LIFETIME_LEDGER_H
#include "../../dlw_win32.h"
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime {
constexpr std::size_t kFiles=8192,kSources=256,kStreams=2048,kFactoryDepth=32;
constexpr std::size_t kCompletionCapacity=8192;
struct CompletionEvent { std::uintptr_t control;volatile LONGLONG sequence; };
struct CompletionRing { volatile LONGLONG next,consumed;volatile LONG overflow;LONG reserved[3];CompletionEvent events[kCompletionCapacity]; };
static_assert(offsetof(CompletionRing,events)==32&&sizeof(CompletionEvent)==16,"native completion probe layout");
enum class FileKind : std::uint8_t { Unknown,Win32,AsyncWin32,Gzip,Zip,OwnedMemory };
enum class Life : std::uint8_t { Empty,Live,Destroying,Ended };
enum class Failure : LONG { None,Contention,Capacity,HookCoverage,InvalidToken,UnknownClass,
    SourceReuse,AddressReuse,UnknownFile,UnobservedFactory,SourceIdentityMismatch,StreamReuse };
struct SourceKey { std::uintptr_t address; std::uint64_t generation; };
struct Token { std::uint64_t generation; std::uint32_t slot; bool active; };
struct FactoryToken { std::uint64_t root; std::uint32_t depth; bool active; };
struct FactoryReplayAuthorization {
    std::uintptr_t authority;
    std::uint64_t authority_generation, observation, transaction;
    std::uint32_t epoch, thread_id;
};
using ValidateFactoryReplayFn = bool(*)(void*, const FactoryReplayAuthorization&) noexcept;
struct ThreadScope {
    std::uint64_t root; std::uint32_t depth;
    FactoryReplayAuthorization replay;
};
struct SourceRecord { SourceKey key;std::uint64_t revision;std::uint32_t live_files,streams;bool covered;Life life; };
struct SourceDestructionToken { SourceKey key;std::uint32_t slot;bool active; };
struct FileRecord { std::uintptr_t file;SourceKey source;std::uint64_t generation,root;FileKind kind;Life life; };
struct StreamRecord { std::uintptr_t control;SourceKey source;std::uint64_t generation;Life life;std::uint32_t pending; };
struct Ledger {
    void* lock; // Zero-initialized native SRWLOCK storage.
    volatile LONG failure;
    std::uint64_t revision,next_generation,transaction;
    std::uint32_t active_factories,active_destructors;
    bool hooks_complete,admission_closed;
    ValidateFactoryReplayFn validate_factory_replay;
    void* factory_replay_user;
    SourceRecord sources[kSources];FileRecord files[kFiles];StreamRecord streams[kStreams];
    CompletionRing completions;
};
struct Proof {
    SourceKey source;std::uint64_t ledger_revision,source_revision,transaction;
    std::uint32_t live_files,deferred_controls,active_factories,active_destructors;
    Failure failure;bool source_covered,source_destroyed,admission_closed,ready;
    std::uint64_t pending_completion_events;
};
struct OutstandingWork {
    std::uint64_t ledger_revision, transaction, pending_control_references;
    std::uint64_t completion_published, completion_consumed, pending_completion_events;
    std::uint32_t active_factories, active_destructors, live_files, source_less_files;
    std::uint32_t live_controls, destroying_controls, source_less_controls;
    Failure failure;
    bool hooks_complete, admission_closed, counts_complete, reads_drained;
};
void Initialize(Ledger*) noexcept;
/* Called only after the complete Windows plan has actually been installed.
 * Existing sources are never promoted by this event. */
bool CompleteHookInstallation(Ledger*,std::uint64_t installed_mask,std::uint64_t required_mask) noexcept;
bool RecordSourceConstruction(Ledger*,SourceKey) noexcept;
bool ObserveExistingSource(Ledger*,SourceKey) noexcept;
bool SnapshotSourceKey(Ledger*,std::uintptr_t,SourceKey*) noexcept;
bool SnapshotFileIdentity(Ledger*,std::uintptr_t,FileRecord*) noexcept;
/* Read-only global work census, including native files/controls whose native
 * SourceKey is zero. Live baseline files are permitted by reads_drained;
 * outstanding factories, destructors, deferred controls and completion-ring
 * publication debt are not. This never drains or mutates the completion ring. */
bool SnapshotOutstandingWork(Ledger*,OutstandingWork*) noexcept;
bool ObserveSourceAddress(Ledger*,std::uintptr_t,bool constructed_before_publication,SourceKey*) noexcept;
SourceDestructionToken BeginSourceDestruction(Ledger*,std::uintptr_t) noexcept;
bool EndSourceDestruction(Ledger*,SourceDestructionToken*) noexcept;
bool BeginFactory(Ledger*,ThreadScope*,FactoryToken*) noexcept;
// Install only before the native observation seal. The validator checks the
// actual registry/TLS authority; path/source/content matching happens at the
// request-bearing bridge before it asks for this opaque authorization.
bool ConfigureFactoryReplayValidator(Ledger*,ValidateFactoryReplayFn,void*) noexcept;
bool BeginReplayFactory(Ledger*,ThreadScope*,FactoryToken*,const FactoryReplayAuthorization&) noexcept;
bool EndFactory(Ledger*,ThreadScope*,FactoryToken*) noexcept;
/* Observe both inner construction and outer factory returns. The same native
 * object in one nested factory operation is counted once. In-place constructor
 * hooks always forward; only the nullable factory wrappers enforce admission. */
Token ObserveConstructed(Ledger*,const ThreadScope&,std::uintptr_t,SourceKey,FileKind) noexcept;
Token BeginDestroy(Ledger*,std::uintptr_t,SourceKey,FileKind) noexcept;
bool EndDestroy(Ledger*,Token*) noexcept;
/* IOQueue schedule retains SFsFile* after read_async returns. Completion is
 * recorded at the verified final queue-reference-release seam AFTER callbacks,
 * including cancellation. An event or read_async return is insufficient. */
Token ObserveDeferredControl(Ledger*,std::uintptr_t file,std::uintptr_t control,SourceKey observed_source={}) noexcept;
bool CompleteDeferredControl(Ledger*,std::uintptr_t control) noexcept;
bool PublishCompletedControl(CompletionRing*,std::uintptr_t) noexcept;
bool DrainCompletedControls(Ledger*) noexcept;
Token BeginControlFree(Ledger*,std::uintptr_t control) noexcept;
bool EndControlFree(Ledger*,Token*) noexcept;
bool CloseAdmission(Ledger*,std::uint64_t transaction) noexcept;
bool ReopenAdmission(Ledger*,std::uint64_t transaction) noexcept;
bool SnapshotSource(Ledger*,SourceKey,Proof*) noexcept;
bool ValidateProof(Ledger*,const Proof&) noexcept;
bool CompleteSourceRetirement(Ledger*,const Proof&) noexcept;
void RecordCoverageLoss(Ledger*,Failure) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::file_lifetime
#endif
