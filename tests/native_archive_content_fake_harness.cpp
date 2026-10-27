#include "fixes/custom_map_asset_isolation/NativeArchiveContent.h"
#include <cstdio>
#include <cstring>
namespace a=dluma::fixes::custom_map_asset_isolation::archive_content;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
#define CHECK(v) do {if(!(v)){std::printf("FAIL %d %s\n",__LINE__,#v);return false;}}while(0)
namespace {
template<class T> void Put(unsigned char* p,T v){std::memcpy(p,&v,sizeof(v));}
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    unsigned char file[0x140A8]={},member[28]={},scratch[4]={};
    char data[16]="abc";
    f::FileRecord identity={};a::Access access={};a::Digest digest={};
    std::uint64_t position=0;unsigned reads=0,seeks=0;
    bool authorized=true,fail_read=false,fail_seek=false,change_member=false,lose_owner=false;
    std::size_t short_read=0;
    Fixture(){
        identity={reinterpret_cast<std::uintptr_t>(file),{0x200,3},5,7,f::FileKind::Zip,f::Life::Live};
        Put(file,base+0x449D8);Put(file+24,identity.source.address);Put(file+32,reinterpret_cast<std::uintptr_t>(member));
        Put(member+16,3U);Put(member+24,static_cast<std::uint16_t>(8));
        access={base,&Memory,&Authorize,&Read,&Seek,this,true};
    }
    static bool Memory(void*,std::uintptr_t p,void* out,std::size_t n)noexcept{std::memcpy(out,reinterpret_cast<void*>(p),n);return true;}
    static bool Authorize(void* user,const f::FileRecord& id)noexcept{
        auto& v=*static_cast<Fixture*>(user);return v.authorized && id.file==v.identity.file &&
            id.generation==v.identity.generation && id.root==v.identity.root;
    }
    static std::int64_t Read(void* user,std::uintptr_t,void* out,std::size_t n)noexcept{
        auto& v=*static_cast<Fixture*>(user);++v.reads;
        Put(v.file+40,1U);
        if(v.fail_read)return -1;
        if(v.short_read && n>v.short_read)n=v.short_read;
        std::memcpy(out,v.data+v.position,n);v.position+=n;Put(v.file+81992,v.position);
        if(v.change_member)v.member[4]^=1;
        if(v.lose_owner)v.authorized=false;
        return static_cast<std::int64_t>(n);
    }
    static bool Seek(void* user,std::uintptr_t,std::uint64_t offset)noexcept{
        auto& v=*static_cast<Fixture*>(user);++v.seeks;
        if(v.fail_seek)return false;
        v.position=offset;Put(v.file+81992,offset);return true;
    }
    a::Result Run(std::uint64_t maximum=1024){return a::CaptureFresh(access,identity,maximum,scratch,sizeof(scratch),&digest);}
};
bool HashesActualDecodedBytes(){Fixture v;CHECK(v.Run()==a::Result::Succeeded);
    constexpr unsigned char abc[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    CHECK(v.digest.complete && v.digest.bytes==3 && !std::memcmp(v.digest.sha256,abc,32));
    CHECK(v.seeks==1 && !v.position && v.digest.cursor_restored);return true;}
bool PartialNativeReadsAreCompleted(){Fixture v;v.short_read=1;CHECK(v.Run()==a::Result::Succeeded);CHECK(v.reads==3 && v.seeks==1);return true;}
bool NativeReadFailureRestoresCursor(){Fixture v;v.fail_read=true;CHECK(v.Run()==a::Result::ReadFailed);
    CHECK(v.seeks==1 && v.digest.cursor_restored && !v.digest.complete);return true;}
bool RestorationFailureCannotPublish(){Fixture v;v.fail_seek=true;CHECK(v.Run()==a::Result::RestoreFailed);
    CHECK(!v.digest.complete && !v.digest.cursor_restored);return true;}
bool LostOwnerIsNeverCalledAgain(){Fixture v;v.lose_owner=true;CHECK(v.Run()==a::Result::RestoreFailed);
    CHECK(v.reads==1 && !v.seeks && !v.digest.complete);return true;}
bool ChangedMemberCannotPublish(){Fixture v;v.change_member=true;CHECK(v.Run()==a::Result::Changed);
    CHECK(v.digest.cursor_restored && !v.digest.complete);return true;}
bool SizeBoundRejectsBeforeReading(){Fixture v;CHECK(v.Run(2)==a::Result::TooLarge);CHECK(!v.reads && !v.seeks);return true;}
bool UsedFileIsNotFresh(){Fixture v;CHECK(v.Run()==a::Result::Succeeded);CHECK(v.Run()==a::Result::NotFresh);
    CHECK(v.reads==1 && v.seeks==1 && !v.digest.complete);return true;}
bool NonzeroCursorIsNotFresh(){Fixture v;Put(v.file+81992,std::uint64_t{1});CHECK(v.Run()==a::Result::NotFresh);
    CHECK(!v.reads && !v.seeks);return true;}
bool MissingAuthorityRejectsBeforeReading(){Fixture v;v.authorized=false;CHECK(v.Run()==a::Result::Unauthorized);
    CHECK(!v.reads && !v.seeks);return true;}
bool EmptyMemberHasAnActualDigest(){Fixture v;Put(v.member+16,0U);CHECK(v.Run()==a::Result::Succeeded);
    CHECK(v.digest.complete && v.digest.cursor_restored && v.digest.sha256[0]==0xe3 && !v.reads && !v.seeks);return true;}
bool SameResourceIdentityDoesNotSubstituteBytes(){Fixture first,second;second.data[2]='d';
    CHECK(first.Run()==a::Result::Succeeded && second.Run()==a::Result::Succeeded);
    CHECK(std::memcmp(first.digest.sha256,second.digest.sha256,32));return true;}
bool NativeStorageCannotBeScratch(){Fixture v;CHECK(a::CaptureFresh(v.access,v.identity,1024,v.file,4,&v.digest)==a::Result::InvalidArgument);
    CHECK(!v.reads && !v.seeks);return true;}
bool CacheRequiresExactFileAndProvider(){Fixture v;static a::Cache cache;CHECK(v.Run()==a::Result::Succeeded);
    CHECK(a::Publish(&cache,0,v.digest));a::Digest found={};CHECK(a::Copy(&cache,v.identity,&found) && found.complete);
    auto other=v.identity;++other.root;CHECK(!a::Copy(&cache,other,&found));
    other=v.identity;++other.source.generation;CHECK(!a::Copy(&cache,other,&found));return true;}
bool ReusedLedgerSlotCannotReturnOldContent(){Fixture v;static a::Cache cache;CHECK(v.Run()==a::Result::Succeeded);
    CHECK(a::Publish(&cache,2,v.digest));auto next=v.digest;++next.file.generation;++next.file.root;next.sha256[0]^=1;
    CHECK(a::Publish(&cache,2,next));a::Digest found={};CHECK(!a::Copy(&cache,v.identity,&found));
    CHECK(a::Copy(&cache,next.file,&found) && found.sha256[0]==next.sha256[0]);
    CHECK(!a::Publish(&cache,2,v.digest));return true;}
bool CacheKeepsFailureDistinctFromEmptyContent(){Fixture v;static a::Cache cache;CHECK(v.Run(2)==a::Result::TooLarge);
    CHECK(a::Publish(&cache,3,v.digest));a::Digest found={};CHECK(a::Copy(&cache,v.identity,&found));
    CHECK(found.result==a::Result::TooLarge && !found.complete);return true;}
bool IncompleteCursorCannotBePublishedAsSuccess(){Fixture v;static a::Cache cache;CHECK(v.Run()==a::Result::Succeeded);
    v.digest.cursor_restored=false;CHECK(!a::Publish(&cache,4,v.digest));return true;}
}
int main(){bool(*cases[])()={HashesActualDecodedBytes,PartialNativeReadsAreCompleted,NativeReadFailureRestoresCursor,
    RestorationFailureCannotPublish,LostOwnerIsNeverCalledAgain,ChangedMemberCannotPublish,SizeBoundRejectsBeforeReading,
    UsedFileIsNotFresh,NonzeroCursorIsNotFresh,MissingAuthorityRejectsBeforeReading,EmptyMemberHasAnActualDigest,
    SameResourceIdentityDoesNotSubstituteBytes,NativeStorageCannotBeScratch,CacheRequiresExactFileAndProvider,
    ReusedLedgerSlotCannotReturnOldContent,CacheKeepsFailureDistinctFromEmptyContent,IncompleteCursorCannotBePublishedAsSuccess};
    for(auto test:cases)if(!test())return 1;
    std::puts("native decoded archive content: 17 scenarios passed");return 0;}
