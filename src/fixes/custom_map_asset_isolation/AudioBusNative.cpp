#define _HAS_EXCEPTIONS 0
#include "AudioBusNative.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::audio_bus_native {
namespace {
struct Span {unsigned offset,size;};
constexpr Span bus_spans[]={{32,128},{160,28},{192,4},{596,12},{612,56},{668,1},{672,28},{700,1}};
constexpr Span cache_spans[]={{56,1},{72,24},{96,3},{124,48},{172,4}};
constexpr Span global_spans[]={{0xAD51CC,4},{0xACCBC4,4},{0xACCBDC,4},{0xAD5248,4},{0xAD524C,4},
    {0xAD5250,4},{0xAD5254,1},{0xAD5258,8},{0xAD5260,1},{0xAD5264,20},
    {0xAD5190,12},{0xAD5200,4},{0xAD51F0,16},{0xAD5230,4},{0xAD5204,4}};
bool EqualText(const char* a,const char* b,unsigned size) noexcept {
    for(unsigned i=0;i<size;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;
}
bool Read(const Access& a,std::uintptr_t p,void* v,std::size_t n) noexcept{return p&&p<=UINTPTR_MAX-n&&a.read(a.user,p,v,n);}
bool Write(const Access& a,std::uintptr_t p,const void* v,std::size_t n) noexcept{return a.write&&p&&p<=UINTPTR_MAX-n&&a.write(a.user,p,v,n);}
bool Text(const Access& a,std::uintptr_t p,char* out,unsigned n) noexcept {
    for(unsigned i=0;i<n;++i){if(!Read(a,p+i,out+i,1))return false;if(!out[i])return true;}return false;
}
bool Owner(const Access& a,Fence* f,bool mutate) noexcept {
    std::uintptr_t actual=0,table=0;
    return a.exact&&a.engine&&a.read&&a.fence&&a.fence(a.user,f,mutate)&&f->owner&&f->generation&&f->thread&&
        f->worker_parked&&f->sound_owned&&f->definitions_owned&&(!mutate||f->producers_closed)&&
        Read(a,a.engine+0xAC8D78,&actual,8)&&actual==f->owner&&Read(a,actual,&table,8)&&table==a.engine+0x8A9628;
}
bool SameFence(const Fence& a,const Fence& b) noexcept{return a.owner==b.owner&&a.generation==b.generation&&a.thread==b.thread&&
    a.worker_parked==b.worker_parked&&a.sound_owned==b.sound_owned&&a.definitions_owned==b.definitions_owned&&a.producers_closed==b.producers_closed;}
bool BackendEqual(const audio_dsp_native::Identity& a,const audio_dsp_native::Identity& b) noexcept {
    return a.owner==b.owner&&a.generation==b.generation&&a.system==b.system&&a.buses==b.buses&&
        !std::memcmp(a.groups,b.groups,sizeof(a.groups))&&!std::memcmp(a.dsp,b.dsp,sizeof(a.dsp))&&
        (!a.master_effect||a.master_effect==b.master_effect);
}
bool VectorRead(const Access& a,std::uintptr_t p,Vector* v,unsigned bound,unsigned stride) noexcept {
    return Read(a,p,v,16)&&v->count<=v->capacity&&v->capacity<=bound&&(!v->capacity||v->data)&&
        v->data<=UINTPTR_MAX-static_cast<std::uintptr_t>(v->capacity)*stride;
}
bool ListenerRead(const Access& a,std::uintptr_t manager,Listener* out) noexcept {
    for(unsigned role=0;role<3;++role){auto& r=out->role[role];
        if(!Read(a,manager+7248+8ULL*role,&r.address,8)||r.address<manager||r.address>manager+4832||
            (r.address-manager)%2416||!Read(a,r.address+2400,&r.count,4)||r.count>10)return false;
        for(unsigned prior=0;prior<role;++prior)if(out->role[prior].address==r.address)return false;
        unsigned char ready=0;if(!Read(a,r.address+2405,&ready,1)||ready>1||(role==1&&ready))return false;
        // The worker also reads the first listener when count is zero; retain
        // that initialized numeric row. Remaining uncounted rows are inert.
        const unsigned count=r.count?r.count:1;
        for(unsigned i=0;i<count;++i){if(!Read(a,r.address+240ULL*i,r.rows[i],240))return false;
            // Native publish sequence is not content and remains monotonic.
            std::memset(r.rows[i]+60,0,4);
        }
    }
    return true;
}
bool ParametersRead(const Access& a,Snapshot* s) noexcept {
    Vector v={};if(!VectorRead(a,s->fence.owner+7456,&v,128,24))return false;s->parameter_count=v.count;
    for(unsigned i=0;i<v.count;++i){auto& p=s->parameters[i];std::uintptr_t name=0;
        if(!Read(a,v.data+24ULL*i,&name,8)||!Read(a,v.data+24ULL*i+8,&p.type,4)||p.type>2||
            !Read(a,v.data+24ULL*i+16,&p.address,8)||!Text(a,name,p.name,sizeof(p.name))||!p.name[0]||
            p.address<a.engine||p.address>=a.engine+0xE8D000||!Read(a,p.address,&p.value,p.type==2?1:4))return false;
        for(unsigned prior=0;prior<i;++prior)if(EqualText(s->parameters[prior].name,p.name,96))return false;
    }
    Vector after={};return VectorRead(a,s->fence.owner+7456,&after,128,24)&&v.data==after.data&&v.count==after.count&&v.capacity==after.capacity;
}
bool ProgramRead(const Access& a,Snapshot* s) noexcept {
    Vector v={};if(!VectorRead(a,a.engine+0xAD5308,&v,4096,104))return false;
    s->on_start_count=v.count;
    for(unsigned i=0;i<v.count;++i){std::uintptr_t keyword=0,table=0,execute=0,apply=0;
        if(!Read(a,v.data+104ULL*i,&keyword,8)||!Read(a,keyword,&table,8)||!Read(a,table+40,&execute,8)||
            !Read(a,table+48,&apply,8)||table<a.engine||execute<a.engine||apply<a.engine||
            table>=a.engine+0xE8D000||execute>=a.engine+0xE8D000||apply>=a.engine+0xE8D000)return false;
        s->on_start[i]={static_cast<std::uint32_t>(table-a.engine),static_cast<std::uint32_t>(execute-a.engine),static_cast<std::uint32_t>(apply-a.engine)};
    }
    Vector after={};return VectorRead(a,a.engine+0xAD5308,&after,4096,104)&&v.data==after.data&&v.count==after.count&&v.capacity==after.capacity;
}
bool ReadBody(const Access& a,Snapshot* s) noexcept {
    const auto owner=s->fence.owner;auto& b=s->backend;b.owner=owner;b.generation=s->fence.generation;
    Vector groups={};unsigned char initialized=0,dirty=0;std::uint32_t effects_dirty=0,cache_flags=0;
    if(!Read(a,owner+15252,&b.buses,4)||b.buses<3||b.buses>14||!Read(a,owner+7536,&b.system,8)||!b.system||
        !VectorRead(a,owner+7912,&groups,64,8)||groups.count!=b.buses||!Read(a,owner+26272,&initialized,1)||initialized!=1||
        !Read(a,owner+15256,&effects_dirty,4)||effects_dirty||!Read(a,owner+25968,&dirty,1)||dirty||
        !Read(a,a.engine+0xB4F8F0,&dirty,1)||dirty||!Read(a,a.engine+0xDD1C54,&cache_flags,4)||(cache_flags&3)!=3||
        !Read(a,owner+26264,&b.master_effect,8))return false;
    for(unsigned i=0;i<b.buses;++i){auto& bus=s->buses[i];const auto p=owner+15272+740ULL*i;
        if(!Text(a,p,reinterpret_cast<char*>(bus.fields),32)||!Read(a,groups.data+8ULL*i,&b.groups[i],8)||!b.groups[i]||
            !Read(a,p+588,&bus.effect_count,4)||bus.effect_count>7)return false;
        for(const auto span:bus_spans)if(!Read(a,p+span.offset,bus.fields+span.offset,span.size))return false;
        if(!Read(a,p+188,&dirty,1)||dirty)return false;
        if(bus.effect_count&&!Read(a,p+196,bus.effects,56ULL*bus.effect_count))return false;
        for(unsigned d=0;d<3;++d)if(!Read(a,owner+25632+112ULL*d+8ULL*i,&b.dsp[i][d],8)||(d<2&&!b.dsp[i][d]))return false;
    }
    for(unsigned i=0;i<2;++i){auto& e=s->environment[i];
        if(!VectorRead(a,owner+8+24ULL*i,&e.vector,16,48))return false;
        for(unsigned n=0;n<e.vector.count;++n){if(!Read(a,e.vector.data+48ULL*n,e.entries[n],32)||
            !Text(a,e.vector.data+48ULL*n+32,reinterpret_cast<char*>(e.entries[n]+32),16))return false;}
        if(!ListenerRead(a,owner+(i?7936:176),&s->listener[i]))return false;
    }
    for(const auto span:cache_spans)if(!Read(a,owner+span.offset,s->environment_cache+span.offset,span.size))return false;
    for(unsigned i=0;i<15;++i)if(!Read(a,a.engine+global_spans[i].offset,s->globals[i],global_spans[i].size))return false;
    return Read(a,a.engine+0xA933E0,s->reverbs,sizeof(s->reverbs))&&Read(a,a.engine+0xDD1BF8,s->transition[0],44)&&
        Read(a,a.engine+0xDD1C28,s->transition[1],44)&&Read(a,owner+15216,s->ambient,28)&&
        Read(a,owner+26136,s->current_reverb,44)&&Read(a,owner+26180,s->reverb_input,40)&&ParametersRead(a,s)&&ProgramRead(a,s);
}
bool WriteBody(const Access& a,const Snapshot& s,const Snapshot& current) noexcept {
    const auto owner=s.fence.owner;const std::uint32_t zero=0;const unsigned char byte_zero=0;
    for(unsigned i=0;i<s.backend.buses;++i){const auto& bus=s.buses[i];const auto p=owner+15272+740ULL*i;
        if(!Write(a,p,bus.fields,std::strlen(reinterpret_cast<const char*>(bus.fields))+1))return false;
        for(const auto span:bus_spans){
            // The first three maximum gains belong to current user settings.
            if(span.offset==160&&i<3){if(!Write(a,p+160,bus.fields+160,20)||!Write(a,p+184,bus.fields+184,4))return false;}
            else if(!Write(a,p+span.offset,bus.fields+span.offset,span.size))return false;
        }
        if((bus.effect_count&&!Write(a,p+196,bus.effects,56ULL*bus.effect_count))||!Write(a,p+588,&bus.effect_count,4)||
            !Write(a,p+188,&byte_zero,1)||!Write(a,p+592,&zero,4)||!Write(a,p+608,&zero,4))return false;
    }
    for(unsigned i=0;i<2;++i){const auto& e=s.environment[i];const auto data=current.environment[i].vector.data;
        if((e.vector.count&&!Write(a,data,e.entries,48ULL*e.vector.count))||!Write(a,owner+16+24ULL*i,&e.vector.count,4))return false;
        for(unsigned role=0;role<3;++role){const auto& r=s.listener[i].role[role];const auto address=current.listener[i].role[role].address;
            const unsigned count=r.count?r.count:1;
            for(unsigned n=0;n<count;++n){
                if(!Write(a,address+240ULL*n,r.rows[n],60)||!Write(a,address+240ULL*n+64,r.rows[n]+64,176))return false;
            }
            if(!Write(a,address+2400,&r.count,4)||!Write(a,address+2404,&byte_zero,1)||!Write(a,address+2405,&byte_zero,1))return false;
        }
    }
    for(const auto span:cache_spans)if(!Write(a,owner+span.offset,s.environment_cache+span.offset,span.size))return false;
    for(unsigned i=0;i<15;++i)if(!Write(a,a.engine+global_spans[i].offset,s.globals[i],global_spans[i].size))return false;
    for(unsigned i=0;i<s.parameter_count;++i){const auto& p=s.parameters[i];if(!Write(a,p.address,&p.value,p.type==2?1:4))return false;}
    return Write(a,a.engine+0xA933E0,s.reverbs,sizeof(s.reverbs))&&Write(a,a.engine+0xDD1BF8,s.transition[0],44)&&
        Write(a,a.engine+0xDD1C28,s.transition[1],44)&&Write(a,owner+15216,s.ambient,28)&&
        Write(a,owner+26136,s.current_reverb,44)&&Write(a,owner+26180,s.reverb_input,40)&&
        Write(a,owner+26220,&byte_zero,1)&&Write(a,owner+15256,&zero,4)&&Write(a,owner+25968,&byte_zero,1)&&Write(a,a.engine+0xB4F8F0,&byte_zero,1);
}
}
Result Capture(const Access& a,Snapshot* s) noexcept {
    if(!s)return Result::InvalidArgument;*s={};if(!Owner(a,&s->fence,false))return Result::Unauthorized;
    if(!ReadBody(a,s))return Result::ReadFailed;Fence after={};if(!Owner(a,&after,false)||!SameFence(s->fence,after))return Result::Changed;
    s->complete=true;return Result::Succeeded;
}
bool CanRestore(const Snapshot& a,const Snapshot& b) noexcept {
    if(!a.complete||!b.complete||!BackendEqual(a.backend,b.backend)||a.parameter_count!=b.parameter_count)return false;
    if(!a.backend.master_effect&&a.globals[8][0])return false;
    for(unsigned i=0;i<2;++i)if(a.environment[i].vector.count>b.environment[i].vector.capacity)return false;
    for(unsigned i=0;i<a.parameter_count;++i){const auto& x=a.parameters[i];const auto& y=b.parameters[i];
        if(x.address!=y.address||x.type!=y.type||!EqualText(x.name,y.name,96))return false;}
    return true;
}
bool ReplayProgramSupported(const Snapshot& s) noexcept {
    if(!s.complete||s.on_start_count>4096)return false;
    // Each tuple is an exact native keyword class and both interpreter paths.
    // CallProc and bank-unload commands need different replay owners, so an
    // immutable B0 containing either is rejected before destructive reset.
    constexpr Opcode supported[]={
        {0x8ACAC8,0x111820,0x1119F0},{0x8ACB08,0x111D50,0x111DC0},{0x8ACB48,0x111D50,0x111DC0},
        {0x8ACC08,0x110EE0,0x1122B0},{0x8ACC48,0x110EE0,0x112440},{0x8ACC88,0x110EE0,0x1124C0},
        {0x8ACCC8,0x110EE0,0x112540},{0x8ACD08,0x110EE0,0x1125C0},{0x8ACD48,0x110EE0,0x112650},
        {0x8ACD88,0x110EE0,0x1126A0},{0x8ACDC8,0x110EE0,0x1126E0},{0x8ACE08,0x110EE0,0x112720},
        {0x8ACE48,0x110EE0,0x112760},{0x8ACE88,0x110EE0,0x112930},{0x8ACEC8,0x110EE0,0x1129C0},
        {0x8ACF08,0x110EE0,0x112A10},{0x8ACF48,0x110EE0,0x112A80},{0x8ACF88,0x110EE0,0x112AD0},
        {0x8ACFC8,0x110EE0,0x112B20},{0x8AD008,0x110EE0,0x112BA0},{0x8AD048,0x110EE0,0x112C80},
        {0x8AD088,0x110EE0,0x112D10},{0x8AD0C8,0x110EE0,0x112D70},{0x8AD108,0x110EE0,0x112E00},
    };
    for(unsigned i=0;i<s.on_start_count;++i){bool found=false;for(const auto& known:supported){const auto& actual=s.on_start[i];
        found|=actual.table==known.table&&actual.execute==known.execute&&actual.apply==known.apply;}if(!found)return false;}
    return true;
}
bool ReplayProgramUnchanged(const Access& a,const Snapshot& s) noexcept {
    Fence fence={};if(!ReplayProgramSupported(s)||!Owner(a,&fence,false)||fence.owner!=s.fence.owner||fence.generation!=s.fence.generation)return false;
    for(unsigned i=0;i<s.on_start_count;++i){std::uintptr_t execute=0,apply=0;const auto& op=s.on_start[i];
        if(!Read(a,a.engine+op.table+40,&execute,8)||!Read(a,a.engine+op.table+48,&apply,8)||
            execute!=a.engine+op.execute||apply!=a.engine+op.apply)return false;
    }
    return true;
}
Result Restore(const Access& a,const Snapshot& s,Snapshot* scratch) noexcept {
    Fence before={};if(!scratch||!s.complete)return Result::InvalidArgument;
    if(!Owner(a,&before,true)||s.fence.owner!=before.owner||s.fence.generation!=before.generation)return Result::Unauthorized;
    const auto captured=Capture(a,scratch);if(captured!=Result::Succeeded)return captured;
    if(!CanRestore(s,*scratch))return Result::Changed;
    if(!WriteBody(a,s,*scratch))return Result::WriteFailed;Fence after={};
    return Owner(a,&after,true)&&SameFence(before,after)?Result::Succeeded:Result::Changed;
}
bool Same(const Snapshot& a,const Snapshot& b,const float maximum[3]) noexcept {
    if(!maximum||!CanRestore(a,b)||std::memcmp(a.reverbs,b.reverbs,sizeof(a.reverbs))||
        std::memcmp(a.transition,b.transition,sizeof(a.transition))||std::memcmp(a.ambient,b.ambient,sizeof(a.ambient))||std::memcmp(a.current_reverb,b.current_reverb,sizeof(a.current_reverb))||
        std::memcmp(a.reverb_input,b.reverb_input,sizeof(a.reverb_input))||
        std::memcmp(a.environment_cache,b.environment_cache,sizeof(a.environment_cache))||std::memcmp(a.globals,b.globals,sizeof(a.globals))||
        a.on_start_count!=b.on_start_count||std::memcmp(a.on_start,b.on_start,sizeof(Opcode)*a.on_start_count))return false;
    for(unsigned i=0;i<a.backend.buses;++i){const auto& x=a.buses[i];const auto& y=b.buses[i];
        if(!EqualText(reinterpret_cast<const char*>(x.fields),reinterpret_cast<const char*>(y.fields),32)||
            x.effect_count!=y.effect_count||std::memcmp(x.effects,y.effects,x.effect_count*56ULL))return false;
        for(const auto span:bus_spans){
            if(span.offset==160&&i<3){float current=0;std::memcpy(&current,y.fields+180,4);
                if(std::memcmp(&current,&maximum[i],4)||std::memcmp(x.fields+160,y.fields+160,20)||
                    std::memcmp(x.fields+184,y.fields+184,4))return false;}
            else if(std::memcmp(x.fields+span.offset,y.fields+span.offset,span.size))return false;
        }
    }
    for(unsigned i=0;i<2;++i){if(a.environment[i].vector.count!=b.environment[i].vector.count||
        std::memcmp(a.environment[i].entries,b.environment[i].entries,48ULL*a.environment[i].vector.count))return false;
        for(unsigned role=0;role<3;++role){const auto& x=a.listener[i].role[role];const auto& y=b.listener[i].role[role];
            if(x.count!=y.count||std::memcmp(x.rows,y.rows,240ULL*(x.count?x.count:1)))return false;}
    }
    for(unsigned i=0;i<a.parameter_count;++i)if(a.parameters[i].value!=b.parameters[i].value)return false;
    return true;
}
} // namespace
