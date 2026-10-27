#define _HAS_EXCEPTIONS 0
#include "AudioContentCapture.h"
extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockShared(PVOID);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockShared(PVOID);
}
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_content_capture {
namespace {
bool Text(const char* p, std::size_t n) noexcept
{ return p && *p && std::memchr(p, 0, n); }
bool Digest(const unsigned char* p) noexcept
{ unsigned char bits = 0; for (unsigned i = 0; i != 32; ++i) bits |= p[i]; return bits != 0; }
bool Close(Capture* c) noexcept
{
    bool ok = true;
    if (c->handle) {
        ok = c->operations.close_owned(c->operations.user, c->handle);
        /* Failed close leaves both handle and its provider lease pinned. */
        if (!ok) return false;
        c->handle = 0;
    }
    if (c->lease.token) c->operations.release_loaded_content_lease(c->operations.user, &c->lease);
    c->lease = {};
    return ok;
}
bool Fail(Capture* c, Failure why) noexcept
{
    if (c) { c->failure = Close(c) ? why : Failure::CloseFailed; c->state = State::Failed; }
    return false;
}
bool Stable(Capture* c) noexcept
{
    Fence f = {};
    return c->operations.read_fence(c->operations.user, &f) &&
        !f.first_project_seen && !f.active_audio_producers &&
        f.source_generation == c->fence.source_generation &&
        f.creator_generation == c->fence.creator_generation &&
        f.observation_revision == c->fence.observation_revision;
}
bool Add(Capture* c, Target target, const audio_startup::AudioStartupProvider& p) noexcept
{
    if (c->count == kCapacity) return Fail(c, Failure::Capacity);
    if (!target.enter_sequence || target.exit_sequence <= target.enter_sequence ||
        !target.owner || !Text(target.logical_path, sizeof(target.logical_path)))
        return Fail(c, Failure::InvalidInput);
    if (!p.exact_source || !p.source || !p.path_complete ||
        !p.source_registry_generation_observed ||
        p.source_registry_generation > c->fence.source_generation)
        return Fail(c, Failure::IncompleteProvider);
    target.source = p.source; target.source_generation = p.source_registry_generation;
    if (!c->operations.resolve_observed_path(c->operations.user, &target) ||
        !Text(target.read_path, sizeof(target.read_path))) return Fail(c, Failure::IncompleteProvider);
    std::uint32_t at = c->count;
    while (at && c->entries[at - 1].target.enter_sequence > target.enter_sequence) {
        c->entries[at] = c->entries[at - 1]; --at;
    }
    c->entries[at] = {}; c->entries[at].target = target; ++c->count;
    return true;
}
} // namespace
bool ObserveParsedInput(ParsedInputScope* s,const void* input,std::uint32_t count,
                        const char* path,std::uint32_t thread,std::uintptr_t base,
                        std::uintptr_t caller,std::uintptr_t option,std::size_t maximum,
                        ReadableFn readable,void* user) noexcept
{
    if (!s) return false;
    if (s->observations == UINT32_MAX) { s->lost = true; return false; }
    ++s->observations;
    const std::uint64_t bytes = static_cast<std::uint64_t>(count) * 2U;
    if (s->observations != 1 || !s->audio_load_sequence ||
        !s->source || !s->exact_provider || !thread || thread != s->thread_id ||
        !Text(s->logical_path,sizeof(s->logical_path)) || !path ||
        !Text(path,sizeof(s->logical_path)) || (std::strlen(path)!=std::strlen(s->logical_path) || std::memcmp(path,s->logical_path,std::strlen(path))) ||
        !base || base > UINTPTR_MAX-0x6B7BBEU || caller != base+0x6B7BBEU || option ||
        !input || !count || !maximum || bytes > maximum || bytes > SIZE_MAX ||
        !readable || !readable(user,input,static_cast<std::size_t>(bytes))) {
        s->lost = true; return false;
    }
    DlumaSha256Context hash;
    dluma_sha256_init(&hash);
    dluma_sha256_update(&hash,input,static_cast<SIZE_T>(bytes));
    dluma_sha256_final(&hash,s->sha256);
    s->bytes = bytes;
    return true;
}
bool ParsedInputReady(const ParsedInputScope& s) noexcept
{ return !s.lost && s.observations == 1 && s.bytes && Digest(s.sha256); }
bool PublishParsedInput(const ParsedInputScope& s,
                        audio_startup::AudioStartupScriptObservation* out) noexcept
{
    if (!out) return false;
    out->parser_input_complete = false;
    out->parser_input_observations = s.observations;
    if (!ParsedInputReady(s) || out->enter_sequence != s.audio_load_sequence ||
        out->provider.source != s.source || !out->provider.exact_source ||
        out->provider.source_registry_generation != s.source_generation ||
        !out->request_complete || !out->parse_succeeded ||
        !Text(out->logical_path,sizeof(out->logical_path)) ||
        (std::strlen(out->logical_path)!=std::strlen(s.logical_path) || std::memcmp(out->logical_path,s.logical_path,std::strlen(s.logical_path)))) return false;
    std::memcpy(out->parser_input_sha256,s.sha256,32);
    out->parser_input_bytes = s.bytes;
    out->parser_input_complete = true;
    return true;
}
void Initialize(Capture* c, const Operations* operations) noexcept
{
    if (!c) return;
    std::memset(c, 0, sizeof(*c));
    if (operations) c->operations = *operations;
}
bool Prepare(Capture* c, const audio_startup::AudioStartupObservationSnapshot& s,
             std::uint64_t creator, std::uint64_t maximum) noexcept
{
    if (!c || c->state != State::Empty) return false;
    const auto& ops = c->operations;
    if (!ops.read_fence || !ops.resolve_observed_path || !ops.acquire_loaded_content_lease ||
        !ops.validate_loaded_content_lease || !ops.release_loaded_content_lease ||
        !ops.open_owned || !ops.read_owned || !ops.close_owned || !creator || !maximum)
        return Fail(c, Failure::InvalidInput);
    if (!audio_startup::AudioStartupObservationLossEmpty(s.loss)) return Fail(c, Failure::ObservationLoss);
    if (!s.coverage_started_before_audio_singletons || !s.hook_plan_complete || !s.revision)
        return Fail(c, Failure::IncompleteCoverage);
    if (s.script_count > audio_startup::kAudioStartupScriptCapacity ||
        s.bank_count > audio_startup::kAudioStartupBankCapacity) return Fail(c, Failure::Capacity);
    if (!ops.read_fence(ops.user, &c->fence) || c->fence.first_project_seen ||
        c->fence.active_audio_producers || c->fence.creator_generation != creator ||
        c->fence.observation_revision != s.revision)
        return Fail(c, Failure::FenceChanged);
    c->maximum_file_bytes = maximum;
    for (std::uint32_t i = 0; i < s.script_count; ++i) {
        const auto& o = s.scripts[i];
        if (!o.request_complete || !o.parse_succeeded) return Fail(c, Failure::IncompleteCoverage);
        Target t = {}; t.kind = Kind::Script; t.owner = o.loader;
        t.enter_sequence = o.enter_sequence; t.exit_sequence = o.exit_sequence;
        std::memcpy(t.logical_path, o.logical_path, sizeof(t.logical_path));
        if (!Add(c, t, o.provider)) return false;
        if (o.parser_input_observations) {
            if (!o.parser_input_complete || o.parser_input_observations != 1 ||
                !o.parser_input_bytes || o.parser_input_bytes > maximum || !Digest(o.parser_input_sha256))
                return Fail(c,Failure::IncompleteCoverage);
            for (std::uint32_t j=0;j<c->count;++j) if (c->entries[j].target.enter_sequence==o.enter_sequence) {
                c->entries[j].format=ContentFormat::EngineParserUtf16DefinitionAndBody;
                std::memcpy(c->entries[j].sha256,o.parser_input_sha256,32);
                c->entries[j].length=o.parser_input_bytes;
                c->entries[j].lease_content_identity=o.enter_sequence;
            }
        }
    }
    for (std::uint32_t i = 0; i < s.bank_count; ++i) {
        const auto& o = s.banks[i];
        if (!o.observation_complete || !o.bank_name_complete || !o.localized_observed || o.reload ||
            o.owner == audio_startup::AudioStartupBankOwner::Unknown ||
            o.provider_outcome == audio_startup::AudioStartupProviderOutcome::LoadFailed ||
            (o.provider_outcome != audio_startup::AudioStartupProviderOutcome::NaturalOpenResolvedPath &&
             o.provider_outcome != audio_startup::AudioStartupProviderOutcome::CacheHitPriorExact))
            return Fail(c, Failure::IncompleteCoverage);
        Target t = {}; t.kind = o.owner == audio_startup::AudioStartupBankOwner::Fmod ? Kind::FmodBank : Kind::SpeechBank;
        t.owner = o.manager; t.enter_sequence = o.enter_sequence; t.exit_sequence = o.exit_sequence;
        t.localized = o.localized; std::memcpy(t.logical_path, o.bank_name, sizeof(t.logical_path));
        if (!Add(c, t, o.provider)) return false;
    }
    /* Intervals may be disjoint or bank children inside one script. Crossing
     * intervals and nested scripts require a different replay contract. */
    for (std::uint32_t i = 0; i < c->count; ++i) {
        auto& current = c->entries[i].target;
        for (std::uint32_t j = 0; j < i; ++j) {
            const auto& prior = c->entries[j].target;
            if (prior.enter_sequence == current.enter_sequence || prior.exit_sequence == current.exit_sequence ||
                prior.exit_sequence == current.enter_sequence || prior.enter_sequence == current.exit_sequence)
                return Fail(c, Failure::AmbiguousOrder);
            if (prior.exit_sequence < current.enter_sequence) continue;
            if (prior.kind != Kind::Script || current.kind == Kind::Script ||
                current.exit_sequence >= prior.exit_sequence || current.parent_sequence)
                return Fail(c, Failure::AmbiguousOrder);
            current.parent_sequence = prior.enter_sequence;
        }
    }
    if (!Stable(c)) return Fail(c, Failure::FenceChanged);
    c->state = State::Prepared; return true;
}
State Step(Capture* c) noexcept
{
    if (!c || (c->state != State::Prepared && c->state != State::Reading)) return c ? c->state : State::Failed;
    if (!Stable(c)) { Fail(c, Failure::FenceChanged); return c->state; }
    if (c->completed == c->count) { c->state = State::Complete; return c->state; }
    auto& entry = c->entries[c->completed];
    /* This digest was captured from the live parser input while owned by the
     * native call. It is already a historical fact, not a later disk hash.
     * Replay must compare its own authenticated parser input before parsing. */
    if (entry.format == ContentFormat::EngineParserUtf16DefinitionAndBody) {
        ++c->completed;
        c->state=c->completed==c->count?State::Complete:State::Prepared;
        return c->state;
    }
    if (!c->handle) {
        if (!c->operations.acquire_loaded_content_lease(c->operations.user, &entry.target, &c->lease) ||
            !c->lease.token || !c->lease.loaded_content_identity ||
            (c->lease.compare_loaded_digest && !Digest(c->lease.expected_sha256))) {
            Fail(c, Failure::LeaseUnavailable); return c->state;
        }
        std::uintptr_t source = 0;
        c->handle = c->operations.open_owned(c->operations.user, &entry.target, &source, &entry.length);
        if (!c->handle) { Fail(c, Failure::OpenFailed); return c->state; }
        if (source != entry.target.source) { Fail(c, Failure::ProviderChanged); return c->state; }
        if (entry.length == UINT64_MAX || entry.length > c->maximum_file_bytes) {
            Fail(c, Failure::InvalidLength); return c->state;
        }
        c->read_bytes = 0; dluma_sha256_init(&c->sha); c->state = State::Reading;
    }
    if (!Stable(c) || !c->operations.validate_loaded_content_lease(c->operations.user, &entry.target, &c->lease)) {
        Fail(c, Failure::ContentChanged); return c->state;
    }
    const std::uint64_t remaining = entry.length - c->read_bytes;
    const auto wanted = static_cast<std::size_t>(remaining < kReadChunk ? remaining : kReadChunk);
    /* At the advertised end, one extra byte verifies EOF rather than silently
     * accepting a concurrently grown file. Zero-length resources are hashed. */
    const auto got = c->operations.read_owned(c->operations.user, c->handle, c->buffer, wanted ? wanted : 1U);
    if (got == UINT64_MAX || got > wanted || (wanted && !got)) {
        Fail(c, Failure::ReadFailed); return c->state;
    }
    if (got) { dluma_sha256_update(&c->sha, c->buffer, static_cast<SIZE_T>(got)); c->read_bytes += got; }
    if (!Stable(c) || !c->operations.validate_loaded_content_lease(c->operations.user, &entry.target, &c->lease)) {
        Fail(c, Failure::ContentChanged); return c->state;
    }
    if (!wanted) {
        dluma_sha256_final(&c->sha, entry.sha256);
        if (c->lease.compare_loaded_digest && std::memcmp(entry.sha256, c->lease.expected_sha256, 32)) {
            Fail(c, Failure::ContentChanged); return c->state;
        }
        entry.lease_content_identity = c->lease.loaded_content_identity;
        if (!Close(c)) { c->failure = Failure::CloseFailed; c->state = State::Failed; return c->state; }
        ++c->completed; c->state = c->completed == c->count ? State::Complete : State::Prepared;
    }
    return c->state;
}
void Cancel(Capture* c) noexcept
{
    if (!c || c->state == State::Complete || c->state == State::Cancelled) return;
    c->failure = Close(c) ? Failure::Cancelled : Failure::CloseFailed;
    c->state = c->failure == Failure::Cancelled ? State::Cancelled : State::Failed;
}
bool RevalidateCompleted(Capture* c) noexcept
{
    if (!c || c->state != State::Complete || !Stable(c)) return false;
    for (std::uint32_t i = 0; i < c->count; ++i) {
        if (c->entries[i].format==ContentFormat::EngineParserUtf16DefinitionAndBody) continue;
        Lease lease = {};
        if (!c->operations.acquire_loaded_content_lease(c->operations.user, &c->entries[i].target, &lease)) return false;
        const bool ok = lease.token && lease.loaded_content_identity == c->entries[i].lease_content_identity &&
            c->operations.validate_loaded_content_lease(c->operations.user, &c->entries[i].target, &lease) &&
            (!lease.compare_loaded_digest || !std::memcmp(c->entries[i].sha256, lease.expected_sha256, 32));
        if (lease.token) c->operations.release_loaded_content_lease(c->operations.user, &lease);
        if (!ok) return false;
    }
    return Stable(c);
}
bool ValidateNativeFilesystem(NativeFilesystem* n, bool exact) noexcept
{
    if (!n) return false; n->validated = false;
    if (!exact || !n->base || !n->read_memory || n->base > UINTPTR_MAX - 0x60000U) return false;
    static const std::uint32_t offsets[] = {0x166B0U,0x16D00U,0x16D40U,0x16CE0U,0x3C80U,0x23C40U,0x23E20U};
    static const unsigned char prefixes[][16] = {
        {0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x41,0x54,0x48,0x8d,0xac,0x24,0x30,0xff,0xff},
        {0x48,0x85,0xc9,0x75,0x05,0x48,0x83,0xc8,0xff,0xc3,0x48,0x8b,0x01,0x48,0xff,0x60},
        {0x48,0x85,0xc9,0x75,0x05,0x48,0x83,0xc8,0xff,0xc3,0x48,0x8b,0x01,0x48,0xff,0x60},
        {0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x0a,0x48,0x8b,0x01,0xba,0x01,0x00,0x00},
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},
        {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x81,0xec,0x40,0x02,0x00,0x00,0x33,0xff,0x48},
        {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x33,0xff,0xf6,0x41,0x40,0x01},
    };
    unsigned char actual[16];
    for (unsigned i = 0; i != 7; ++i)
        if (!n->read_memory(n->user,n->base+offsets[i],actual,sizeof(actual)) ||
            std::memcmp(actual,prefixes[i],sizeof(actual))) return false;
    n->validated = true; return true;
}
std::uintptr_t OpenNativeOwned(NativeFilesystem* n, const Target* t,
                              std::uintptr_t* source, std::uint64_t* length) noexcept
{
    if (!n || !n->validated || !t || !source || !length || !Text(t->read_path,sizeof(t->read_path))) return 0;
    *source = 0; *length = UINT64_MAX;
    const auto h = t->physical_native_path
        ? reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,const char*,int,unsigned)>(n->base+0x3C80U)(t->source,t->read_path,0,0)
        : reinterpret_cast<std::uintptr_t(__fastcall*)(const char*,int,unsigned)>(n->base+0x166B0U)(t->read_path,0,0);
    if (!h) return 0;
    if (h > UINTPTR_MAX-0x20U || !n->read_memory(n->user,h+0x18U,source,sizeof(*source))) {
        (void)CloseNativeOwned(n,h); return 0;
    }
    *length = reinterpret_cast<std::uint64_t(__fastcall*)(std::uintptr_t)>(n->base+0x16D40U)(h);
    return h;
}
std::uint64_t ReadNativeOwned(NativeFilesystem* n, std::uintptr_t h,void* out,std::size_t size) noexcept
{
    if (!n || !n->validated || !h || !out || !size || size > kReadChunk) return UINT64_MAX;
    return reinterpret_cast<std::uint64_t(__fastcall*)(std::uintptr_t,void*,std::uint64_t)>(n->base+0x16D00U)(h,out,size);
}
bool CloseNativeOwned(NativeFilesystem* n,std::uintptr_t h) noexcept
{
    if (!n || !n->validated || !h) return false;
    reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(n->base+0x16CE0U)(h); return true;
}
std::uint64_t LengthNativeOwned(NativeFilesystem* n,std::uintptr_t h) noexcept
{
    return n&&n->validated&&h
        ? reinterpret_cast<std::uint64_t(__fastcall*)(std::uintptr_t)>(n->base+0x16D40U)(h) : UINT64_MAX;
}
std::uintptr_t OpenNativeFromSource(NativeFilesystem* n,std::uintptr_t source,const char* logical) noexcept
{
    if(!n||!n->validated||!source||!Text(logical,256)||n->base>UINTPTR_MAX-0x60000U)return 0;
    alignas(16) unsigned char path[80]={};
    using Construct=void*(__fastcall*)(void*,const char*,bool);
    using Destroy=void(__fastcall*)(void*);
    using Open=std::uintptr_t(__fastcall*)(std::uintptr_t,void*,std::uint32_t);
    reinterpret_cast<Construct>(n->base+0x23C40U)(path,logical,true);
    AcquireSRWLockShared(reinterpret_cast<PVOID>(n->base+0x53AD0U));
    const auto read=[&](std::uintptr_t at,void* out,std::size_t bytes) noexcept {
        return at&&at<=UINTPTR_MAX-bytes&&n->read_memory(n->user,at,out,bytes);
    };
    std::uintptr_t head=0;std::uint32_t count=0;
    bool valid=read(n->base+0x53398U,&head,8)&&read(n->base+0x533A8U,&count,4)&&count<=256U;
    bool found=false;std::uint32_t traversed=0;
    for(std::uintptr_t link=head;valid&&link;){std::uintptr_t candidate=0,next=0;
        if(link<8||++traversed>count||!read(link-8,&candidate,8)||!read(link,&next,8)){valid=false;break;}
        if(candidate==source)found=true;link=next;
    }
    valid=valid&&traversed==count&&found;
    std::uintptr_t vtable=0,target=0,result=0;
    if(valid&&read(source,&vtable,8)&&
       (vtable==n->base+0x3F4A8U||vtable==n->base+0x44A48U)&&read(vtable+160,&target,8)&&
       target==n->base+(vtable==n->base+0x3F4A8U?0x67B0U:0x256A0U))
        result=reinterpret_cast<Open>(target)(source,path,0);
    ReleaseSRWLockShared(reinterpret_cast<PVOID>(n->base+0x53AD0U));
    reinterpret_cast<Destroy>(n->base+0x23E20U)(path);
    return result;
}
} // namespace dluma::fixes::custom_map_asset_isolation::audio_content_capture
