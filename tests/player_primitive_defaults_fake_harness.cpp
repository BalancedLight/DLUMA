#include "fixes/custom_map_asset_isolation/PlayerPrimitiveDefaults.h"
#include <cstdio>
#include <cstring>
namespace defaults=dluma::fixes::custom_map_asset_isolation::player_primitive_defaults;
namespace {
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
struct Fake {unsigned numbers=0,booleans=0,strings=0;bool bad_number=false,bad_string=false;};
bool Number(void* u,std::uint32_t id,std::uint32_t* out)noexcept{auto& f=*static_cast<Fake*>(u);++f.numbers;*out=f.bad_number&&id==5?0x7FC00000U:0x3F800000U;return true;}
bool Boolean(void* u,std::uint32_t id,bool* out)noexcept{++static_cast<Fake*>(u)->booleans;*out=id==901;return true;}
bool String(void* u,std::uint32_t id,char* out,std::size_t n)noexcept{auto& f=*static_cast<Fake*>(u);++f.strings;if(f.bad_string)std::memset(out,'x',n);else std::strcpy(out,id==1039?"Tap":"");return true;}
defaults::Operations Ops(Fake& f){return {&f,Number,Boolean,String,true};}
bool AllTypedDefaultsWithoutOwner(){Fake f;defaults::Snapshot s={};CHECK(defaults::Capture(Ops(f),&s));CHECK(f.numbers==895&&f.booleans==136&&f.strings==5&&s.captured_values==1036);CHECK(s.compiled_primitive_defaults_complete&&!s.template_effective_defaults_complete);CHECK(s.values[896].kind==defaults::Kind::Reserved&&std::strcmp(s.values[1039].text,"Tap")==0);return true;}
bool InvalidNumberIsNotBaseline(){Fake f;f.bad_number=true;defaults::Snapshot s={};CHECK(!defaults::Capture(Ops(f),&s));CHECK(!s.compiled_primitive_defaults_complete);return true;}
bool BoundedStringsRequired(){Fake f;f.bad_string=true;defaults::Snapshot s={};CHECK(!defaults::Capture(Ops(f),&s));CHECK(!s.compiled_primitive_defaults_complete);return true;}
bool ExactTargetsRequired(){Fake f;auto o=Ops(f);o.exact_native_targets=false;defaults::Snapshot s={};CHECK(!defaults::Capture(o,&s));CHECK(f.numbers==0&&f.booleans==0&&f.strings==0);return true;}
}
int main(){bool(*tests[])()={AllTypedDefaultsWithoutOwner,InvalidNumberIsNotBaseline,BoundedStringsRequired,ExactTargetsRequired};for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);++i){if(!tests[i]())return 1;std::printf("PASS primitive_defaults_%u\n",i);}return 0;}
