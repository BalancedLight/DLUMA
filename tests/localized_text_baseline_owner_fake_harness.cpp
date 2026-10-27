#include "fixes/custom_map_asset_isolation/LocalizedTextBaselineOwner.h"
#include <cstdio>
#include <cstring>

namespace text = dluma::fixes::custom_map_asset_isolation::localized_texts;
using text::TextResult;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return false; } } while (0)

namespace {
struct Bucket { std::uintptr_t head=0,tail=0; std::uint32_t count=0,padding=0; };
struct Node { std::uintptr_t next=0,previous=0,key=0,value=0; };
struct FakeEngine {
    Bucket buckets[256]; Node nodes[16]; char keys[16][64]={}; std::uint16_t values[16][128]={};
    std::uint32_t count=0;
    text::TextFence state={1,1,0,"En",true,false,true,true,true};
    unsigned reads=0,clear_calls=0,insert_calls=0;
    unsigned change_after_reads=0,fail_insert_at=0,change_on_insert=0;
    bool clear_failure=false,invalid_prefix=false;
    static constexpr std::uintptr_t base=0x10000000;
    unsigned hash(const char* key) const {
        unsigned h=1;
        while(*key){unsigned char c=static_cast<unsigned char>(*key++);if(c>='a'&&c<='z')c&=0xdf;h=((h*33)^c)&255;}
        return h;
    }
    void add(const char* key,const std::uint16_t* value) {
        if(count>=16)return;
        unsigned i=count++;std::strcpy(keys[i],key);unsigned v=0;while(value[v]){values[i][v]=value[v];++v;}values[i][v]=0;
        auto& b=buckets[hash(key)];auto& n=nodes[i];n={0,b.tail,reinterpret_cast<std::uintptr_t>(keys[i]),reinterpret_cast<std::uintptr_t>(values[i])};
        if(b.tail)reinterpret_cast<Node*>(b.tail)->next=reinterpret_cast<std::uintptr_t>(&n);else b.head=reinterpret_cast<std::uintptr_t>(&n);
        b.tail=reinterpret_cast<std::uintptr_t>(&n);++b.count;
    }
    void add_ascii(const char* key,const char* value) {
        std::uint16_t wide[128]={};unsigned i=0;while(value[i]){wide[i]=static_cast<unsigned char>(value[i]);++i;}add(key,wide);
    }
    bool contains(const char* key,const char* value) const {
        for(unsigned i=0;i<count;++i)if(!std::strcmp(key,keys[i])) {
            unsigned j=0;while(value[j]){if(values[i][j]!=static_cast<unsigned char>(value[j]))return false;++j;}return values[i][j]==0;
        }
        return false;
    }
};
bool CopyRange(std::uintptr_t address,void* out,std::size_t size,const void* src,std::size_t total) {
    auto start=reinterpret_cast<std::uintptr_t>(src);
    if(address<start||size>total||address-start>total-size)return false;
    std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;
}
bool Read(void* user,std::uintptr_t address,void* out,std::size_t size) noexcept {
    auto& f=*static_cast<FakeEngine*>(user);++f.reads;
    if(f.change_after_reads&&f.reads==f.change_after_reads)++f.state.mutation_revision;
    static const std::uint8_t clear_prefix[16]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x1d,0x2f,0x59,0x8c};
    static const std::uint8_t add_prefix[16]={0x48,0x85,0xd2,0x0f,0x84,0x98,0x01,0x00,0x00,0x4c,0x8b,0xdc,0x49,0x89,0x4b,0x08};
    if(address==f.base+text::kClearTextsRva&&size==16){std::memcpy(out,clear_prefix,16);if(f.invalid_prefix)static_cast<char*>(out)[0]=0;return true;}
    if(address==f.base+text::kInsertTextRva&&size==16){std::memcpy(out,add_prefix,16);return true;}
    if(address==f.base+text::kTextBucketsRva&&size==sizeof(f.buckets)){std::memcpy(out,f.buckets,size);return true;}
    if(address==f.base+text::kTextCountRva&&size==4){std::memcpy(out,&f.count,size);return true;}
    return CopyRange(address,out,size,f.nodes,sizeof(f.nodes))||CopyRange(address,out,size,f.keys,sizeof(f.keys))||CopyRange(address,out,size,f.values,sizeof(f.values));
}
bool Fence(void* user,text::TextFence* out) noexcept {*out=static_cast<FakeEngine*>(user)->state;return true;}
bool Clear(void* user,std::uintptr_t function) noexcept {
    auto& f=*static_cast<FakeEngine*>(user);if(function!=f.base+text::kClearTextsRva)return false;++f.clear_calls;
    std::memset(f.buckets,0,sizeof(f.buckets));std::memset(f.nodes,0,sizeof(f.nodes));std::memset(f.keys,0,sizeof(f.keys));std::memset(f.values,0,sizeof(f.values));f.count=0;
    return !f.clear_failure;
}
bool Insert(void* user,std::uintptr_t function,const char* key,const std::uint16_t* value) noexcept {
    auto& f=*static_cast<FakeEngine*>(user);if(function!=f.base+text::kInsertTextRva)return false;
    ++f.insert_calls;if(f.fail_insert_at==f.insert_calls)return false;
    f.add(key,value);if(f.change_on_insert==f.insert_calls)++f.state.mutation_revision;return true;
}
struct Fixture {
    FakeEngine engine;
    text::LocalizedTextBaselineOwner owner={};
    text::TextEntry baseline_entries[16]={},scratch_entries[16]={};
    alignas(2) std::uint8_t baseline_bytes[4096]={},scratch_bytes[4096]={};
    text::NativeTextAccess access={};
    Fixture() {
        engine.add_ascii("stock_key","base");engine.add_ascii("global_mod_key","approved global");
        access.engine_base=engine.base;std::strcpy(access.engine_sha256,text::kPlayerEngineSha256);
        access.read=Read;access.fence=Fence;access.clear=Clear;access.insert=Insert;access.user=&engine;
        initialize();
    }
    TextResult initialize(std::size_t entries=16,std::size_t bytes=4096) {
        text::TextSnapshot a={baseline_entries,baseline_bytes,entries,bytes};
        text::TextSnapshot b={scratch_entries,scratch_bytes,16,4096};
        return text::InitializeOwner(&owner,&access,&a,&b);
    }
    TextResult baseline(){return text::CaptureCleanBaseline(&owner);}
    void custom(){engine.state.first_clean_frontend=false;engine.state.returned_frontend=true;engine.state.custom_epoch_count=1;engine.add_ascii("custom_only","outgoing");}
    TextResult prepare(){return text::PrepareTextReset(&owner,12);}
    TextResult clear(){return text::ClearCustomTexts(&owner,12);}
    TextResult restore(){return text::RestoreBaselineTexts(&owner,12);}
    TextResult verify(){return text::VerifyBaselineTexts(&owner,12);}
};
bool RestoresAllBaselineIncludingGlobalAdditions() {
    Fixture f;const std::uint16_t wide[]={0x03a9,0x4e2d,0xd83d,0xde00,0};f.engine.add("unicode_value",wide);
    CHECK(f.baseline()==TextResult::Succeeded);CHECK(f.owner.baseline.count==3);f.custom();
    CHECK(f.prepare()==TextResult::Succeeded);CHECK(f.clear()==TextResult::Succeeded);CHECK(f.engine.count==0);
    CHECK(f.owner.baseline.complete);CHECK(f.restore()==TextResult::Succeeded);CHECK(f.verify()==TextResult::Succeeded);
    CHECK(f.engine.contains("stock_key","base")&&f.engine.contains("global_mod_key","approved global"));
    CHECK(!f.engine.contains("custom_only","outgoing")&&f.engine.count==3);
    CHECK(f.engine.clear_calls==1&&f.engine.insert_calls==3);return true;
}
bool RefusesReturnedOrContaminatedBaseline() {
    Fixture f;f.engine.state.returned_frontend=true;CHECK(f.baseline()==TextResult::NotCleanStartup);
    f.engine.state.returned_frontend=false;f.engine.state.custom_epoch_count=1;CHECK(f.baseline()==TextResult::NotCleanStartup);
    CHECK(!f.owner.baseline.complete&&f.engine.clear_calls==0);return true;
}
bool LocaleChangeNeverRestoresOldLanguage() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();std::strcpy(f.engine.state.locale,"Fr");
    CHECK(f.prepare()==TextResult::LocaleMismatch);CHECK(f.engine.clear_calls==0);CHECK(f.engine.contains("custom_only","outgoing"));
    std::strcpy(f.engine.state.locale,"En");CHECK(f.prepare()==TextResult::Succeeded);CHECK(f.clear()==TextResult::Succeeded);
    std::strcpy(f.engine.state.locale,"Fr");CHECK(f.restore()==TextResult::LocaleMismatch);CHECK(f.engine.insert_calls==0);return true;
}
bool BoundedSnapshotsFailBeforeMutation() {
    Fixture f;CHECK(f.initialize(1)==TextResult::Succeeded);CHECK(f.baseline()==TextResult::CapacityExceeded);
    Fixture g;CHECK(g.initialize(16,4)==TextResult::Succeeded);CHECK(g.baseline()==TextResult::CapacityExceeded);
    CHECK(f.engine.clear_calls==0&&g.engine.clear_calls==0);return true;
}
bool MalformedListsAndHashOwnershipAreRejected() {
    Fixture f;f.engine.nodes[0].previous=1;CHECK(f.baseline()==TextResult::InvalidTable);
    Fixture g;g.engine.buckets[g.engine.hash("stock_key")].tail=1;CHECK(g.baseline()==TextResult::InvalidTable);
    Fixture h;h.engine.nodes[0].next=reinterpret_cast<std::uintptr_t>(&h.engine.nodes[0]);CHECK(h.baseline()==TextResult::InvalidTable);
    Fixture i;i.engine.add_ascii("STOCK_KEY","duplicate");CHECK(i.baseline()==TextResult::InvalidTable);
    return true;
}
bool CurrentTableIsRevalidatedBeforeNativeClear() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();CHECK(f.prepare()==TextResult::Succeeded);
    f.engine.nodes[0].previous=1;CHECK(f.clear()==TextResult::InvalidTable);CHECK(f.engine.clear_calls==0);return true;
}
bool RevisionChangesInvalidateCaptureAndReplay() {
    Fixture f;f.engine.change_after_reads=f.engine.reads+8;CHECK(f.baseline()==TextResult::ChangedDuringCapture);
    Fixture g;CHECK(g.baseline()==TextResult::Succeeded);g.custom();CHECK(g.prepare()==TextResult::Succeeded);CHECK(g.clear()==TextResult::Succeeded);
    g.engine.change_on_insert=2;CHECK(g.restore()==TextResult::ChangedDuringCapture);CHECK(g.owner.phase==text::TextPhase::Failed);return true;
}
bool QuiescenceAndExactImageAreRequired() {
    Fixture f;f.engine.state.producer_admission_closed=false;CHECK(f.baseline()==TextResult::AccessNotQuiescent);
    Fixture g;CHECK(g.baseline()==TextResult::Succeeded);g.engine.invalid_prefix=true;CHECK(g.prepare()==TextResult::ProfileMismatch);CHECK(g.engine.clear_calls==0);
    Fixture h;h.access.engine_sha256[0]='0';CHECK(h.initialize()==TextResult::ProfileMismatch);return true;
}
bool NativeFailuresCannotBeReportedAsRestored() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();CHECK(f.prepare()==TextResult::Succeeded);f.engine.clear_failure=true;
    CHECK(f.clear()==TextResult::NativeCallFailed);CHECK(f.restore()==TextResult::WrongPhase);CHECK(f.owner.baseline.complete);
    Fixture g;CHECK(g.baseline()==TextResult::Succeeded);g.custom();CHECK(g.prepare()==TextResult::Succeeded);CHECK(g.clear()==TextResult::Succeeded);
    g.engine.fail_insert_at=2;CHECK(g.restore()==TextResult::NativeCallFailed);CHECK(g.restore()==TextResult::WrongPhase);CHECK(g.owner.baseline.complete);return true;
}
bool VerifyDetectsLateCustomAndBaselineChanges() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();CHECK(f.prepare()==TextResult::Succeeded);CHECK(f.clear()==TextResult::Succeeded);CHECK(f.restore()==TextResult::Succeeded);
    f.engine.add_ascii("late_custom","bad");CHECK(f.verify()==TextResult::BaselineMismatch);CHECK(f.baseline()==TextResult::AlreadyComplete);CHECK(f.owner.baseline.count==2);return true;
}
bool GenerationsRepeatedCallsAndCancelAreExplicit() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();++f.engine.state.owner_generation;
    CHECK(f.prepare()==TextResult::OwnerMismatch);--f.engine.state.owner_generation;CHECK(f.prepare()==TextResult::Succeeded);
    CHECK(text::CancelPreparedTextReset(&f.owner,13)==TextResult::WrongTransaction);CHECK(text::CancelPreparedTextReset(&f.owner,12)==TextResult::Succeeded);
    CHECK(text::PrepareTextReset(&f.owner,13)==TextResult::Succeeded);CHECK(text::ClearCustomTexts(&f.owner,13)==TextResult::Succeeded);
    CHECK(text::ClearCustomTexts(&f.owner,13)==TextResult::AlreadyComplete);CHECK(text::RestoreBaselineTexts(&f.owner,13)==TextResult::Succeeded);
    CHECK(text::VerifyBaselineTexts(&f.owner,13)==TextResult::Succeeded);CHECK(text::VerifyBaselineTexts(&f.owner,13)==TextResult::AlreadyComplete);CHECK(f.engine.clear_calls==1);return true;
}
bool PreflightDoesNotTraverseAnOpenTable() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();
    f.engine.state.producer_admission_closed=false;f.engine.state.readers_drained=false;
    f.engine.nodes[0].previous=1;auto before=f.engine.reads;
    CHECK(text::PreflightTextReset(&f.owner,12)==TextResult::Succeeded);
    CHECK(f.engine.reads-before==2 && f.engine.clear_calls==0 && f.owner.phase==text::TextPhase::BaselineCaptured);
    CHECK(f.prepare()==TextResult::AccessNotQuiescent);
    f.engine.state.producer_admission_closed=true;f.engine.state.readers_drained=true;
    CHECK(f.prepare()==TextResult::InvalidTable);CHECK(f.engine.clear_calls==0);return true;
}
bool FinalReverifyDoesNotTrustEarlierVerification() {
    Fixture f;CHECK(f.baseline()==TextResult::Succeeded);f.custom();CHECK(f.prepare()==TextResult::Succeeded);
    CHECK(f.clear()==TextResult::Succeeded);CHECK(f.restore()==TextResult::Succeeded);CHECK(f.verify()==TextResult::Succeeded);
    f.engine.add_ascii("post_restore_key","intervening domain wrote text");
    CHECK(text::ReverifyBaselineTexts(&f.owner,12)==TextResult::BaselineMismatch);CHECK(f.owner.phase==text::TextPhase::Failed);return true;
}
} // namespace
int main() {
    const struct {const char* name;bool(*run)();} tests[]={
        {"complete B0 and global additions",RestoresAllBaselineIncludingGlobalAdditions},
        {"no returned-baseline adoption",RefusesReturnedOrContaminatedBaseline},
        {"locale preservation",LocaleChangeNeverRestoresOldLanguage},
        {"bounded memory",BoundedSnapshotsFailBeforeMutation},
        {"native table validation",MalformedListsAndHashOwnershipAreRejected},
        {"preclear revalidation",CurrentTableIsRevalidatedBeforeNativeClear},
        {"revision fences",RevisionChangesInvalidateCaptureAndReplay},
        {"native identity and quiescence",QuiescenceAndExactImageAreRequired},
        {"native failure retention",NativeFailuresCannotBeReportedAsRestored},
        {"B0 verification",VerifyDetectsLateCustomAndBaselineChanges},
        {"generation and cancel",GenerationsRepeatedCallsAndCancelAreExplicit},
        {"preflight before native gate closure",PreflightDoesNotTraverseAnOpenTable},
        {"final verification after other domains",FinalReverifyDoesNotTrustEarlierVerification},
    };
    unsigned passed=0;for(const auto& test:tests){if(!test.run()){std::printf("FAILED %s\n",test.name);return 1;}++passed;}
    std::printf("Localized text baseline owner: %u scenarios passed\n",passed);return 0;
}
