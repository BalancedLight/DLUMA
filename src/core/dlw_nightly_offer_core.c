#include "dlw_nightly_offer_core.h"
#include "dlw_json_reader.h"
#include <string.h>

int dluma_nightly_text_compare(const char *left, const char *right)
{
    unsigned int index=0;
    if (!left || !right) return left==right ? 0 : left ? 1 : -1;
    while (left[index] && left[index]==right[index]) ++index;
    return (unsigned char)left[index]-(unsigned char)right[index];
}

static int hex_length(const char *text, unsigned int length)
{
    unsigned int i;
    if (!text) return 0;
    for (i=0;i<length;++i)
        if (!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f'))) return 0;
    return !text[length];
}

int dluma_nightly_commit_valid(const char *commit)
{
    return hex_length(commit,40);
}

static int archive_name_valid(const DlumaNightlyManifest *manifest)
{
    const char *p=manifest->filename;
    unsigned int dots=0;
    if (memcmp(p,"DLUMA-",6)) return 0;
    p+=6;
    for (;;) {
        if (*p<'0' || *p>'9') return 0;
        do { ++p; } while (*p>='0' && *p<='9');
        if (*p!= '.') break;
        ++dots; ++p;
    }
    return dots && *p++=='-' && strlen(p)==44 && !memcmp(p,manifest->commit,40) && !dluma_nightly_text_compare(p+40,".zip");
}

int dluma_nightly_manifest_parse(const void *bytes, unsigned int size, DlumaNightlyManifest *out)
{
    DlumaJsonReader reader;
    DlumaNightlyManifest manifest;
    char key[32], schema[32];
    unsigned int seen=0;
    int first=1, next;
    if (!out) return 0;
    memset(out,0,sizeof(*out)); memset(&manifest,0,sizeof(manifest));
    if (!bytes || !size || size>16384) return 0;
    dluma_json_reader_initialize(&reader,bytes,size);
    if (!dluma_json_object_begin(&reader)) return 0;
    for (;;) {
        unsigned int bit;
        char *destination;
        unsigned int capacity;
        next=dluma_json_object_next(&reader,&first);
        if (!next) break;
        if (next<0 || !dluma_json_string(&reader,key,sizeof(key)) || !dluma_json_take(&reader,':')) return 0;
        if (!dluma_nightly_text_compare(key,"schema")) { bit=1; destination=schema; capacity=sizeof(schema); }
        else if (!dluma_nightly_text_compare(key,"commit")) { bit=2; destination=manifest.commit; capacity=sizeof(manifest.commit); }
        else if (!dluma_nightly_text_compare(key,"filename")) { bit=4; destination=manifest.filename; capacity=sizeof(manifest.filename); }
        else if (!dluma_nightly_text_compare(key,"sha256")) { bit=8; destination=manifest.sha256; capacity=sizeof(manifest.sha256); }
        else return 0;
        if (seen & bit || !dluma_json_string(&reader,destination,capacity)) return 0;
        seen|=bit;
    }
    if (!dluma_json_object_end(&reader) || !dluma_json_finished(&reader) || seen!=15 ||
        dluma_nightly_text_compare(schema,"dluma.nightly.v1") || !dluma_nightly_commit_valid(manifest.commit) ||
        !hex_length(manifest.sha256,64) || !archive_name_valid(&manifest)) return 0;
    *out=manifest;
    return 1;
}

int dluma_nightly_compare_status(const void *bytes, unsigned int size)
{
    DlumaJsonReader reader;
    char key[256], status[16];
    unsigned int seen=0;
    uint64_t ahead=0, behind=1;
    int first=1, next;
    if (!bytes || !size || size>1024u*1024u) return -1;
    dluma_json_reader_initialize(&reader,bytes,size);
    if (!dluma_json_object_begin(&reader)) return -1;
    for (;;) {
        unsigned int bit=0;
        next=dluma_json_object_next(&reader,&first);
        if (!next) break;
        if (next<0 || !dluma_json_string(&reader,key,sizeof(key)) || !dluma_json_take(&reader,':')) return -1;
        if (!dluma_nightly_text_compare(key,"status")) {
            bit=1;
            if (!dluma_json_string(&reader,status,sizeof(status))) return -1;
        } else if (!dluma_nightly_text_compare(key,"ahead_by")) {
            bit=2;
            if (!dluma_json_u64(&reader,&ahead)) return -1;
        } else if (!dluma_nightly_text_compare(key,"behind_by")) {
            bit=4;
            if (!dluma_json_u64(&reader,&behind)) return -1;
        } else if (!dluma_json_skip_value(&reader)) return -1;
        if (bit && (seen & bit)) return -1;
        seen|=bit;
    }
    if (!dluma_json_object_end(&reader) || !dluma_json_finished(&reader) || seen!=7) return -1;
    if (!dluma_nightly_text_compare(status,"ahead") && ahead>0 && behind==0) return 1;
    if (!dluma_nightly_text_compare(status,"identical") && !ahead && !behind) return 0;
    if (!dluma_nightly_text_compare(status,"behind") && !ahead && behind>0) return 0;
    return -1;
}
int dluma_nightly_compare_is_ahead(const void *bytes, unsigned int size)
{
    return dluma_nightly_compare_status(bytes,size)==1;
}
