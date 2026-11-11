#include "dlw_character_levels.h"
#include <string.h>

static const char* const level_names[DLUMA_CHARACTER_LEVEL_COUNT]={
    "None","Runner","Fighter","Status","Reputation","Legend","Driver","Hellraid"
};
static const unsigned char level_name_sizes[DLUMA_CHARACTER_LEVEL_COUNT]={4,6,7,6,10,6,6,8};
typedef struct LevelCursor { const unsigned char* bytes; size_t size, at; } LevelCursor;

static int level_read(LevelCursor* cursor,unsigned width,uint64_t* out)
{
    uint64_t value=0; unsigned i;
    if (width>8 || cursor->at>cursor->size || width>cursor->size-cursor->at) return 0;
    for(i=0;i<width;++i) value|=(uint64_t)cursor->bytes[cursor->at++]<<(i*8);
    *out=value; return 1;
}
static void level_write(unsigned char* bytes,size_t* at,uint64_t value,unsigned width)
{
    unsigned i; for(i=0;i<width;++i) bytes[(*at)++]=(unsigned char)(value>>(i*8));
}
static int level_index(const unsigned char* name,unsigned size)
{
    unsigned i;
    for(i=0;i<DLUMA_CHARACTER_LEVEL_COUNT;++i)
        if(size==level_name_sizes[i] && !memcmp(name,level_names[i],size)) return (int)i;
    if(size==11 && !memcmp(name,"StatusLevel",11)) return 3;
    return -1;
}
DlumaCharacterLevelsResult dluma_character_levels_encode(
    const DlumaCharacterLevels* levels,void* bytes,size_t capacity,size_t* written)
{
    DlumaCharacterLevels copy; unsigned char* output=(unsigned char*)bytes;
    size_t at=4; unsigned i;
    if(written) *written=0;
    if(!levels || !bytes || !written) return DLUMA_LEVELS_INVALID;
    if(capacity<DLUMA_CHARACTER_LEVELS_CANONICAL_BYTES) return DLUMA_LEVELS_NO_SPACE;
    copy=*levels;
    memcpy(output,"DCLV",4);
    level_write(output,&at,1,2); level_write(output,&at,16,2);
    level_write(output,&at,DLUMA_CHARACTER_LEVEL_COUNT,2);
    level_write(output,&at,4,2); level_write(output,&at,0,4);
    for(i=0;i<DLUMA_CHARACTER_LEVEL_COUNT;++i) {
        const DlumaCharacterLevel* value=&copy.levels[i];
        unsigned size=level_name_sizes[i]; output[at++]=(unsigned char)size;
        memcpy(output+at,level_names[i],size); at+=size;
        level_write(output,&at,value->points,2); level_write(output,&at,value->level,2);
        level_write(output,&at,value->total_xp,8); level_write(output,&at,value->current_xp,4);
    }
    *written=at; return DLUMA_LEVELS_OK;
}
DlumaCharacterLevelsResult dluma_character_levels_decode(
    const void* bytes,size_t size,DlumaCharacterLevels* out)
{
    LevelCursor cursor={(const unsigned char*)bytes,size,4}; DlumaCharacterLevels copy={0};
    uint64_t version,header,count,width,reserved,name_size,points,level,total,current;
    unsigned i,seen=0;
    if(!bytes || !out || size<16 || size>DLUMA_CHARACTER_LEVELS_MAX_INPUT_BYTES ||
        memcmp(bytes,"DCLV",4)) return DLUMA_LEVELS_INVALID;
    if(!level_read(&cursor,2,&version) || !level_read(&cursor,2,&header) ||
        !level_read(&cursor,2,&count) || !level_read(&cursor,2,&width) ||
        !level_read(&cursor,4,&reserved)) return DLUMA_LEVELS_INVALID;
    if(version!=1 || header!=16 || (width!=4 && width!=8)) return DLUMA_LEVELS_UNSUPPORTED;
    if(count!=DLUMA_CHARACTER_LEVEL_COUNT || reserved) return DLUMA_LEVELS_INVALID;
    for(i=0;i<DLUMA_CHARACTER_LEVEL_COUNT;++i) {
        int index; DlumaCharacterLevel* value;
        if(!level_read(&cursor,1,&name_size) || !name_size || name_size>11 ||
            name_size>cursor.size-cursor.at) return DLUMA_LEVELS_INVALID;
        index=level_index(cursor.bytes+cursor.at,(unsigned)name_size);
        if(index<0 || (seen&(1u<<index))) return DLUMA_LEVELS_INVALID;
        seen|=1u<<index; cursor.at+=(size_t)name_size;
        if(!level_read(&cursor,2,&points) || !level_read(&cursor,2,&level) ||
            !level_read(&cursor,8,&total) || !level_read(&cursor,(unsigned)width,&current))
            return DLUMA_LEVELS_INVALID;
        if(current>UINT32_MAX) return DLUMA_LEVELS_OVERFLOW;
        value=&copy.levels[index]; value->points=(uint16_t)points; value->level=(uint16_t)level;
        value->total_xp=total; value->current_xp=(uint32_t)current;
    }
    if(seen!=0xFFu || cursor.at!=cursor.size) return DLUMA_LEVELS_INVALID;
    *out=copy; return DLUMA_LEVELS_OK;
}
