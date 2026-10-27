#define _HAS_EXCEPTIONS 0
#include "CurveLoadOrigin.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
constexpr unsigned maximum_depth=8,maximum_path=512;
struct Scope {char path[maximum_path];std::uint32_t epoch;std::uint64_t serial;CurveSourceReceipt source;bool ambiguous;};
struct State {Scope scopes[maximum_depth];std::uint64_t serial;unsigned depth;bool lost;};
__declspec(thread) State state={};
bool Normalize(const char* input,char* out) noexcept {
    if(!input || !*input)return false;unsigned n=0;
    while(input[0]=='.' && (input[1]=='/' || input[1]=='\\'))input+=2;
    while(*input){if(n+1>=maximum_path)return false;auto c=static_cast<unsigned char>(*input++);
        if(c=='\\')c='/';if(c>='A' && c<='Z')c|=0x20;
        if(c=='/' && n && out[n-1]=='/')continue;out[n++]=static_cast<char>(c);}
    out[n]=0;return n!=0;
}
bool SameSource(file_lifetime::SourceKey a,file_lifetime::SourceKey b) noexcept {return a.address==b.address && a.generation==b.generation;}
}
bool BeginCurveLoadObservation(const char* path,std::uint32_t epoch,CurveLoadToken* token) noexcept {
    if(!token || token->active || state.lost || state.depth>=maximum_depth){state.lost=true;return false;}
    auto& scope=state.scopes[state.depth];scope={};
    if(!Normalize(path,scope.path)){state.lost=true;return false;}
    scope.epoch=epoch;scope.serial=++state.serial;
    if(!scope.serial){state.lost=true;return false;}
    *token={scope.serial,state.depth,true};++state.depth;return true;
}
void NoteOpenedCurveSource(const char* path,const CurveOpenedSource* opened) noexcept {
    if(!state.depth || state.lost)return;auto& scope=state.scopes[state.depth-1];
    char normalized[maximum_path]={};if(!Normalize(path,normalized))return;
    const auto length=std::strlen(scope.path);
    if(length!=std::strlen(normalized) || std::memcmp(scope.path,normalized,length))return;
    if(!opened || opened->file.life!=file_lifetime::Life::Live || !opened->file.file ||
        !opened->file.generation || !opened->file.root || !opened->file.source.address ||
        !opened->file.source.generation || !opened->source_revision || opened->ownership==CurveSourceOwnership::Unknown ||
        (opened->ownership==CurveSourceOwnership::ExclusiveProject &&
            (!opened->project_epoch || opened->project_epoch!=scope.epoch))){scope.ambiguous=true;return;}
    const CurveSourceReceipt receipt={opened->file.source,opened->source_revision,opened->file.generation,
        opened->file.root,opened->project_epoch,opened->ownership,true,scope.serial,GetCurrentThreadId()};
    if(scope.source.valid){
        // Public/native open wrappers may report the same nested factory root.
        // Different logical opens or a different winner make the load unsafe.
        if(!SameSource(scope.source.source,receipt.source) || scope.source.source_revision!=receipt.source_revision ||
            scope.source.open_root!=receipt.open_root || scope.source.project_epoch!=receipt.project_epoch ||
            scope.source.ownership!=receipt.ownership)scope.ambiguous=true;
        return;
    }
    scope.source=receipt;
}
bool CurrentCurveLoadSource(CurveSourceReceipt* out) noexcept {
    if(!out)return false;*out={};if(!state.depth || state.lost)return false;
    const auto& scope=state.scopes[state.depth-1];
    if(scope.ambiguous || !scope.source.valid)return false;*out=scope.source;return true;
}
std::uint64_t CurrentCurveLoadSerial() noexcept {
    return state.depth && !state.lost?state.scopes[state.depth-1].serial:0;
}
bool EndCurveLoadObservation(CurveLoadToken* token) noexcept {
    if(!token || !token->active || !state.depth || token->depth!=state.depth-1 ||
        state.scopes[token->depth].serial!=token->serial){state.lost=true;return false;}
    --state.depth;token->active=false;return !state.lost;
}
} // namespace dluma::fixes::custom_map_asset_isolation::curves
