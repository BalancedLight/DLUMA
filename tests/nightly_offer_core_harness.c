#include "../src/core/dlw_nightly_offer_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define COMMIT "1111111111111111111111111111111111111111"
#define DIGEST "2222222222222222222222222222222222222222222222222222222222222222"
#define MANIFEST "{\"schema\":\"dluma.nightly.v1\",\"commit\":\"" COMMIT "\",\"filename\":\"DLUMA-0.5.1-" COMMIT ".zip\",\"sha256\":\"" DIGEST "\"}"

int main(void)
{
    DlumaNightlyManifest manifest;
    char changed[sizeof(MANIFEST)];
    const char *ahead="{\"status\":\"ahead\",\"ahead_by\":2,\"behind_by\":0,\"commits\":[]}";
    const char *behind="{\"status\":\"behind\",\"ahead_by\":0,\"behind_by\":2}";
    const char *identical="{\"status\":\"identical\",\"ahead_by\":0,\"behind_by\":0}";
    const char *diverged="{\"status\":\"diverged\",\"ahead_by\":2,\"behind_by\":1}";
    const char *duplicate="{\"status\":\"behind\",\"status\":\"ahead\",\"ahead_by\":2,\"behind_by\":0}";
    assert(dluma_nightly_manifest_parse(MANIFEST,sizeof(MANIFEST)-1,&manifest));
    assert(!strcmp(manifest.commit,COMMIT) && !strcmp(manifest.sha256,DIGEST));
    memcpy(changed,MANIFEST,sizeof(changed));
    strstr(changed,".zip")[0]='\\';
    assert(!dluma_nightly_manifest_parse(changed,sizeof(changed)-1,&manifest));
    memcpy(changed,MANIFEST,sizeof(changed));
    strstr(changed,"DLUMA-0.5.1-")[12]='3';
    assert(!dluma_nightly_manifest_parse(changed,sizeof(changed)-1,&manifest));
    assert(!dluma_nightly_manifest_parse("{}",2,&manifest));
    assert(!dluma_nightly_manifest_parse(MANIFEST,sizeof(MANIFEST),&manifest));
    assert(dluma_nightly_compare_status(ahead,(unsigned)strlen(ahead))==1);
    assert(dluma_nightly_compare_status(behind,(unsigned)strlen(behind))==0);
    assert(dluma_nightly_compare_status(identical,(unsigned)strlen(identical))==0);
    assert(dluma_nightly_compare_status(diverged,(unsigned)strlen(diverged))==-1);
    assert(dluma_nightly_compare_status(duplicate,(unsigned)strlen(duplicate))==-1);
    assert(dluma_nightly_compare_status("{}",2)==-1);
    puts("Nightly offer core checks passed");
    return 0;
}
