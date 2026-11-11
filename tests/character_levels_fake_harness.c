#include "../src/core/dlw_character_levels.c"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static const char* names[8]={"None","Runner","Fighter","Status","Reputation","Legend","Driver","Hellraid"};
static void number(unsigned char* bytes,size_t* at,uint64_t value,unsigned width)
{ unsigned i; for(i=0;i<width;++i) bytes[(*at)++]=(unsigned char)(value>>(i*8)); }
static DlumaCharacterLevels sample(void)
{
    DlumaCharacterLevels s={0}; unsigned i;
    for(i=0;i<8;++i) {
        s.levels[i].points=(uint16_t)(100+i); s.levels[i].level=(uint16_t)(200+i);
        s.levels[i].total_xp=UINT64_C(0xFEDCBA9876543200)+i;
        s.levels[i].current_xp=UINT32_MAX-i;
    }
    s.levels[7].points=s.levels[7].level=UINT16_MAX; return s;
}
/* Independent wire fixture: all fields and names are supplied explicitly.
 * Reverse order/legacy width/alias must canonicalize to the same output. */
static size_t fixture(unsigned char* bytes,DlumaCharacterLevels s,unsigned width,int reverse,int alias,int overflow)
{
    size_t at=0; unsigned i;
    memcpy(bytes,"DCLV",4); at=4;
    number(bytes,&at,1,2); number(bytes,&at,16,2); number(bytes,&at,8,2);
    number(bytes,&at,width,2); number(bytes,&at,0,4);
    for(i=0;i<8;++i) {
        unsigned index=reverse?7-i:i; const char* name=(alias && index==3)?"StatusLevel":names[index];
        size_t count=strlen(name); bytes[at++]=(unsigned char)count;
        memcpy(bytes+at,name,count); at+=count;
        number(bytes,&at,s.levels[index].points,2); number(bytes,&at,s.levels[index].level,2);
        number(bytes,&at,s.levels[index].total_xp,8);
        number(bytes,&at,overflow && index==2?UINT64_C(0x100000000):s.levels[index].current_xp,width);
    }
    return at;
}
static int rejected_without_change(const unsigned char* bytes,size_t size)
{
    DlumaCharacterLevels output,before;
    memset(&output,0xA5,sizeof(output)); before=output;
    return dluma_character_levels_decode(bytes,size,&output)!=DLUMA_LEVELS_OK && !memcmp(&output,&before,sizeof(output));
}
int main(void)
{
    unsigned char canonical[DLUMA_CHARACTER_LEVELS_MAX_INPUT_BYTES],input[DLUMA_CHARACTER_LEVELS_MAX_INPUT_BYTES+1],output[DLUMA_CHARACTER_LEVELS_CANONICAL_BYTES];
    DlumaCharacterLevels s=sample(),decoded={0}; size_t canonical_size,size,written=99,i;
    canonical_size=fixture(canonical,s,4,0,0,0);
    CHECK(canonical_size==205 && canonical_size==DLUMA_CHARACTER_LEVELS_CANONICAL_BYTES);
    CHECK(dluma_character_levels_decode(canonical,canonical_size,&decoded)==DLUMA_LEVELS_OK);
    CHECK(!memcmp(&decoded,&s,sizeof(s)));
    CHECK(dluma_character_levels_encode(&s,output,sizeof(output),&written)==DLUMA_LEVELS_OK);
    CHECK(written==canonical_size && !memcmp(output,canonical,canonical_size));
    size=fixture(input,s,8,1,1,0);
    CHECK(dluma_character_levels_decode(input,size,&decoded)==DLUMA_LEVELS_OK);
    CHECK(!memcmp(&decoded,&s,sizeof(s)));
    CHECK(dluma_character_levels_encode(&decoded,output,sizeof(output),&written)==DLUMA_LEVELS_OK);
    CHECK(written==canonical_size && !memcmp(output,canonical,canonical_size));
    size=fixture(input,s,8,0,0,1);
    CHECK(rejected_without_change(input,size));
    for(i=0;i<canonical_size;++i) CHECK(rejected_without_change(canonical,i));
    memcpy(input,canonical,canonical_size); input[canonical_size]=0;
    CHECK(rejected_without_change(input,canonical_size+1));
    memcpy(input,canonical,canonical_size); input[4]=2;
    CHECK(rejected_without_change(input,canonical_size));
    memcpy(input,canonical,canonical_size); input[8]=7;
    CHECK(rejected_without_change(input,canonical_size));
    input[8]=9; CHECK(rejected_without_change(input,canonical_size));
    memcpy(input,canonical,canonical_size); input[10]=2;
    CHECK(rejected_without_change(input,canonical_size));
    memcpy(input,canonical,canonical_size); input[12]=1;
    CHECK(rejected_without_change(input,canonical_size));
    memcpy(input,canonical,canonical_size); input[16]=255;
    CHECK(rejected_without_change(input,canonical_size));
    input[16]=0; CHECK(rejected_without_change(input,canonical_size));
    CHECK(rejected_without_change(input,sizeof(input)));
    CHECK(rejected_without_change(input,(size_t)-1));
    memcpy(input,canonical,canonical_size); input[0]='X';
    CHECK(rejected_without_change(input,canonical_size));
    memcpy(input,canonical,canonical_size); input[17]='n';
    CHECK(rejected_without_change(input,canonical_size));
    /* Duplicate canonical name; an alias of that same slot must also reject. */
    memcpy(input,canonical,canonical_size); memcpy(input+17,"Nope",4);
    CHECK(rejected_without_change(input,canonical_size));
    size=fixture(input,s,4,0,1,0);
    /* Locate Reputation record and replace its 10-byte name with StatusLevel
     * by expanding one byte, leaving both Status aliases in this payload. */
    for(i=16;i+11<size;++i) if(input[i]==10 && !memcmp(input+i+1,"Reputation",10)) break;
    CHECK(i+11<size);
    memmove(input+i+12,input+i+11,size-i-11); input[i]=11; memcpy(input+i+1,"StatusLevel",11); ++size;
    CHECK(rejected_without_change(input,size));
    for(i=0;i<sizeof(output);++i) {
        memset(output,0x7A,sizeof(output)); written=99;
        CHECK(dluma_character_levels_encode(&s,output,i,&written)==DLUMA_LEVELS_NO_SPACE && written==0);
        { size_t j; for(j=0;j<sizeof(output);++j) CHECK(output[j]==0x7A); }
    }
    CHECK(dluma_character_levels_decode(0,canonical_size,&decoded)==DLUMA_LEVELS_INVALID);
    CHECK(dluma_character_levels_decode(canonical,canonical_size,0)==DLUMA_LEVELS_INVALID);
    CHECK(dluma_character_levels_encode(0,output,sizeof(output),&written)==DLUMA_LEVELS_INVALID);
    CHECK(dluma_character_levels_encode(&s,0,sizeof(output),&written)==DLUMA_LEVELS_INVALID);
    CHECK(dluma_character_levels_encode(&s,output,sizeof(output),0)==DLUMA_LEVELS_INVALID);
    puts("PASS: pointer-free character levels"); return 0;
}
