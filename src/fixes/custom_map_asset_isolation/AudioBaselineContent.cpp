#define _HAS_EXCEPTIONS 0
#include "AudioBaselineContent.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::audio_baseline_content {
namespace {
namespace lease = speech_content_lease;
namespace start = audio_startup;
bool Fail(State* s,Failure f) noexcept { if(s){dluma_atomic_exchange(&s->failure,static_cast<LONG>(f));dluma_atomic_exchange(&s->phase,static_cast<LONG>(Phase::Failed));}return false; }
bool Text(const char* p,std::size_t n) noexcept{return p&&*p&&std::memchr(p,0,n);}
template<std::size_t A,std::size_t B> bool SameText(const char(&a)[A],const char(&b)[B]) noexcept{
    for(std::size_t i=0;i<A&&i<B;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;
}
bool Digest(const unsigned char* p) noexcept{unsigned char v=0;for(unsigned i=0;i<32;++i)v|=p[i];return v!=0;}
bool Source(const file_lifetime::SourceKey& a,const file_lifetime::SourceKey& b) noexcept{return a.address==b.address&&a.generation==b.generation;}
bool Add(State* s,Input input) noexcept {
    if(s->input_count>=sizeof(s->inputs)/sizeof(s->inputs[0]))return Fail(s,Failure::Capacity);
    if(!input.enter_sequence||input.exit_sequence<=input.enter_sequence||!Text(input.logical_path,sizeof(input.logical_path))||
       !input.owner||!input.source.address||!input.source.generation||!Digest(input.sha256))return Fail(s,Failure::ObservationLoss);
    std::uint32_t at=s->input_count;
    while(at&&s->inputs[at-1].enter_sequence>input.enter_sequence){s->inputs[at]=s->inputs[at-1];--at;}
    s->inputs[at]=input;++s->input_count;return true;
}
}
bool ObserveNatural(State* s,const NaturalBank& n,lease::ReadMemoryFn read,void* user) noexcept {
    if(!s)return false;
    if(dluma_atomic_compare_exchange(&s->phase,0,0)!=static_cast<LONG>(Phase::Observing))return false;
    const auto kind=n.kind==lease::NativeBankFile::Fmod?file_lifetime::FileKind::AsyncWin32:file_lifetime::FileKind::Win32;
    if(!n.manager||!n.load_enter_sequence||!n.open.speech_scope_generation||!n.contextual_source.address||!n.contextual_source.generation||
       n.file.file!=n.open.native_file||!n.file.generation||!n.file.root||n.file.life!=file_lifetime::Life::Live||n.file.kind!=kind||
       (n.file.source.address&&!Source(n.file.source,n.contextual_source))||
       n.open.provider_identity!=n.contextual_source.address||n.open.provider_generation!=n.contextual_source.generation)
        return Fail(s,Failure::IncompleteOpen);
    const LONG index=dluma_atomic_increment(&s->count)-1;
    if(index<0||static_cast<std::size_t>(index)>=kBanks)return Fail(s,Failure::Capacity);
    auto& b=s->banks[index];b.natural=n;
    const bool ok=lease::CaptureNaturalBankPin(&b.pin,n.open,n.kind,read,user);
    dluma_atomic_exchange(&b.published,ok?1:-1);
    return ok?true:Fail(s,Failure::IncompleteOpen);
}
bool Freeze(State* s,std::uint64_t generation,std::uint64_t revision) noexcept {
    if(!s||!generation||!revision||s->baseline_generation||
       dluma_atomic_compare_exchange(&s->phase,0,0)!=static_cast<LONG>(Phase::Observing))return false;
    const LONG count=dluma_atomic_compare_exchange(&s->count,0,0);
    if(count<0||static_cast<std::size_t>(count)>kBanks)return Fail(s,Failure::Capacity);
    for(LONG i=0;i<count;++i){
        if(dluma_atomic_compare_exchange(&s->banks[i].published,0,0)!=1)return false;
        for(LONG j=0;j<i;++j)if(s->banks[j].natural.load_enter_sequence==s->banks[i].natural.load_enter_sequence)
            return Fail(s,Failure::DuplicateOpen);
    }
    s->frozen_count=static_cast<std::uint32_t>(count);s->frozen_revision=revision;s->baseline_generation=generation;
    dluma_atomic_exchange(&s->phase,static_cast<LONG>(Phase::Hashing));return true;
}
Phase Step(State* s,std::uint64_t maximum) noexcept {
    if(!s)return Phase::Failed;
    const auto phase=static_cast<Phase>(dluma_atomic_compare_exchange(&s->phase,0,0));
    if(phase!=Phase::Hashing)return phase;
    if(dluma_atomic_compare_exchange(&s->count,0,0)!=static_cast<LONG>(s->frozen_count)){
        Fail(s,Failure::InvalidFreeze);return Phase::Failed;
    }
    if(s->hashing_index==s->frozen_count){dluma_atomic_exchange(&s->phase,static_cast<LONG>(Phase::ContentCaptured));return Phase::ContentCaptured;}
    auto& b=s->banks[s->hashing_index];
    if(s->job.state==lease::State::Empty&&!lease::BeginRead(&s->job,b.pin,maximum)){Fail(s,Failure::HashFailed);return Phase::Failed;}
    const auto status=lease::Step(&s->job);
    if(status==lease::State::Failed){Fail(s,Failure::HashFailed);return Phase::Failed;}
    if(status==lease::State::Hashed){
        if(!lease::HistoricalContentHeld(s->job,b.pin)){Fail(s,Failure::FileIdentityChanged);return Phase::Failed;}
        b.identity=s->job.identity;b.bytes=s->job.total;std::memcpy(b.sha256,s->job.sha256,32);b.hashed=true;
        if(!lease::CloseRead(&s->job)){Fail(s,Failure::HashFailed);return Phase::Failed;}
        std::memset(&s->job,0,sizeof(s->job));++s->hashing_index;
    }
    return Phase::Hashing;
}
bool Join(State* s,const start::AudioStartupObservationSnapshot& observed,
          const cold_reboot::audio_snapshot::AudioBaselineWindowsSnapshot& raw) noexcept {
    if(!s||s->inventory_joined||static_cast<Phase>(dluma_atomic_compare_exchange(&s->phase,0,0))!=Phase::ContentCaptured||
       s->frozen_revision!=observed.revision||raw.observation_revision!=observed.revision||
       !observed.hook_plan_complete||!observed.coverage_started_before_audio_singletons||
       !start::AudioStartupObservationLossEmpty(observed.loss)||!raw.raw_snapshot_complete||
       observed.script_count>start::kAudioStartupScriptCapacity||observed.bank_count>kBanks||
       raw.bank_count>cold_reboot::audio_snapshot::kWindowsAudioBankCapacity||
       raw.speech_bank_count>cold_reboot::audio_snapshot::kWindowsSpeechBankCapacity||
       raw.script_count>cold_reboot::audio_snapshot::kWindowsAudioScriptCapacity)return Fail(s,Failure::InvalidFreeze);
    s->input_count=0;
    for(std::uint32_t i=0;i<observed.script_count;++i){
        const auto& o=observed.scripts[i];
        if(!o.parse_succeeded||!o.parser_input_complete||o.parser_input_observations!=1||!o.parser_input_bytes||
           !o.provider.exact_source||!o.provider.source_registry_generation_observed||!o.provider.instance_generation)
            return Fail(s,Failure::ScriptInputMissing);
        Input in={};in.enter_sequence=o.enter_sequence;in.exit_sequence=o.exit_sequence;in.owner=o.loader;
        in.source={o.provider.source,o.provider.instance_generation};in.source_registry_generation=o.provider.source_registry_generation;
        in.bank_pin_index=UINT32_MAX;in.bytes=o.parser_input_bytes;in.script=true;
        std::memcpy(in.logical_path,o.logical_path,sizeof(in.logical_path));std::memcpy(in.sha256,o.parser_input_sha256,32);
        if(!Add(s,in))return false;
    }
    for(std::uint32_t i=0;i<observed.bank_count;++i){
        const auto& o=observed.banks[i];
        if(!o.observation_complete||!o.localized_observed||o.reload||o.result<0||
           o.owner==start::AudioStartupBankOwner::Unknown)return Fail(s,Failure::ObservationLoss);
        std::uint32_t found=UINT32_MAX;
        for(std::uint32_t j=0;j<s->frozen_count;++j){const auto& b=s->banks[j];
            if(b.natural.load_enter_sequence==o.enter_sequence){if(found!=UINT32_MAX)return Fail(s,Failure::DuplicateOpen);found=j;}}
        if(found==UINT32_MAX&&o.result==1){
            // A cache hit inherits only a previous exact occurrence, with no
            // intervening replacement. Name alone never supplies provenance.
            const start::AudioStartupBankObservation* prior=nullptr;
            for(std::uint32_t j=0;j<observed.bank_count;++j){const auto& p=observed.banks[j];
                if(p.owner==o.owner&&p.manager==o.manager&&p.exit_sequence<o.enter_sequence&&
                   SameText(p.bank_name,o.bank_name)&&(!prior||p.exit_sequence>prior->exit_sequence))prior=&p;}
            if(prior&&prior->result==0&&!prior->reload&&prior->localized==o.localized)
                for(std::uint32_t j=0;j<s->frozen_count;++j)if(s->banks[j].natural.load_enter_sequence==prior->enter_sequence)found=j;
        }
        if(found==UINT32_MAX)return Fail(s,Failure::BankJoinMissing);
        const auto& b=s->banks[found];
        if(b.natural.manager!=o.manager||!b.hashed||
           (b.natural.kind==lease::NativeBankFile::Fmod)!=(o.owner==start::AudioStartupBankOwner::Fmod))return Fail(s,Failure::BankJoinMissing);
        if(o.result!=1&&(o.native_file!=b.natural.file.file||
           o.native_file_instance_generation!=b.natural.file.generation))return Fail(s,Failure::FileIdentityChanged);
        if(o.result!=1&&(!o.provider.exact_source||o.provider.source!=b.natural.contextual_source.address||
           o.provider.instance_generation!=b.natural.contextual_source.generation||!SameText(o.provider.path,b.pin.path)))return Fail(s,Failure::ProviderChanged);
        Input in={};in.enter_sequence=o.enter_sequence;in.exit_sequence=o.exit_sequence;in.owner=o.manager;
        in.source=b.natural.contextual_source;in.source_registry_generation=o.provider.source_registry_generation;
        in.native_file_generation=b.natural.file.generation;in.bank_pin_index=found;in.bank_owner=o.owner;
        in.localized=o.localized;in.cache_hit=o.result==1;in.bytes=b.bytes;
        std::memcpy(in.logical_path,o.bank_name,sizeof(in.logical_path));std::memcpy(in.sha256,b.sha256,32);
        if(!Add(s,in))return false;
    }
    for(std::uint32_t i=0;i<raw.bank_count;++i){const auto& bank=raw.banks[i];bool joined=false;
        for(std::uint32_t j=0;j<s->input_count;++j){const auto& in=s->inputs[j];
            if(!in.script&&in.bank_owner==start::AudioStartupBankOwner::Fmod&&SameText(in.logical_path,bank.name)&&
               in.owner==raw.bank_manager&&in.localized==bank.localised&&
               s->banks[in.bank_pin_index].natural.file.file==bank.native_file)joined=true;}
        if(!joined)return Fail(s,Failure::BankJoinMissing);
    }
    for(std::uint32_t i=0;i<raw.script_count;++i){bool joined=false;
        for(std::uint32_t j=0;j<s->input_count;++j)if(s->inputs[j].script&&
            SameText(raw.scripts[i].logical_path,s->inputs[j].logical_path))joined=true;
        if(!joined)return Fail(s,Failure::ScriptInputMissing);
    }
    for(std::uint32_t i=0;i<raw.speech_bank_count;++i){const auto& bank=raw.speech_banks[i];std::uint32_t matches=0;
        for(std::uint32_t j=0;j<s->input_count;++j){const auto& in=s->inputs[j];
            if(in.script||in.bank_owner!=start::AudioStartupBankOwner::Speech)continue;
            std::uint32_t hash=0;for(const char* p=in.logical_path;*p;++p)hash=hash*41U+
                static_cast<std::uint32_t>(static_cast<std::int8_t>(*p));
            if(hash==bank.bank_hash&&in.localized==bank.localised&&in.owner==raw.speech_manager)++matches;
        }
        if(matches!=1)return Fail(s,Failure::BankJoinMissing);
    }
    for(std::uint32_t i=0;i<s->input_count;++i)for(std::uint32_t j=0;j<i;++j){auto& in=s->inputs[i];const auto& p=s->inputs[j];
        if(p.enter_sequence==in.enter_sequence||p.exit_sequence==in.exit_sequence)return Fail(s,Failure::AmbiguousOrder);
        if(p.exit_sequence<in.enter_sequence)continue;
        if(!p.script||in.script||in.exit_sequence>=p.exit_sequence||in.parent_sequence)return Fail(s,Failure::AmbiguousOrder);
        in.parent_sequence=p.enter_sequence;
    }
    s->inventory_joined=AllBankContentHeld(*s);return s->inventory_joined;
}
bool AllBankContentHeld(const State& s) noexcept {
    if(s.hashing_index!=s.frozen_count)return false;
    for(std::uint32_t i=0;i<s.frozen_count;++i)if(!s.banks[i].hashed||!lease::PinStillNamesContent(s.banks[i].pin,s.banks[i].identity))return false;
    return true;
}
bool Close(State* s) noexcept {
    if(!s)return false;bool ok=lease::CloseRead(&s->job);
    const LONG count=dluma_atomic_compare_exchange(&s->count,0,0);
    for(LONG i=0;i<count&&static_cast<std::size_t>(i)<kBanks;++i)if(!lease::ReleasePin(&s->banks[i].pin))ok=false;
    return ok;
}
}
