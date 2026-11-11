#include "dlw_character_state_store.h"
#include "../dlw_common.h"
#include <string.h>

/* The zero-filled fixed-width disk image is versioned independently from the
 * coordinator journal. Padding is canonicalized and authenticated on read. */
typedef struct StateDisk {
    unsigned char magic[8];
    uint32_t version, phase, boundary_open, source_size, target_size, world_size;
    uint64_t id, scope, source_profile, target_profile;
    unsigned char save_identity[32];
    unsigned char source[DLUMA_CHARACTER_STATE_SECTION_MAX];
    unsigned char target[DLUMA_CHARACTER_STATE_SECTION_MAX];
    unsigned char world[DLUMA_CHARACTER_STATE_SECTION_MAX];
    unsigned char digest[32];
} StateDisk;

static int valid(const DlumaCharacterStateRecord *r)
{
    unsigned i; int nonzero = 0;
    if (!r || r->journal.version != 1 || !r->journal.id ||
        !r->journal.request.scope_id || !r->journal.request.source_profile ||
        !r->journal.request.target_profile ||
        r->journal.request.source_profile == r->journal.request.target_profile ||
        r->journal.phase < DLUMA_CHARACTER_PREPARED || r->journal.phase > DLUMA_CHARACTER_RESTORING ||
        r->boundary_open > 1 || !r->source_size || !r->target_size || !r->world_size ||
        r->source_size > DLUMA_CHARACTER_STATE_SECTION_MAX ||
        r->target_size > DLUMA_CHARACTER_STATE_SECTION_MAX || r->world_size > DLUMA_CHARACTER_STATE_SECTION_MAX) return 0;
    for (i = 0; i < 32; ++i) nonzero |= r->save_identity[i];
    return nonzero != 0;
}
static void pack(const DlumaCharacterStateRecord *r, StateDisk *d)
{
    DlumaSha256Context hash;
    memset(d, 0, sizeof(*d)); memcpy(d->magic, "DLCST01", 7);
    d->version = 1; d->phase = (uint32_t)r->journal.phase; d->boundary_open = r->boundary_open;
    d->source_size = r->source_size; d->target_size = r->target_size; d->world_size = r->world_size;
    d->id = r->journal.id; d->scope = r->journal.request.scope_id;
    d->source_profile = r->journal.request.source_profile; d->target_profile = r->journal.request.target_profile;
    memcpy(d->save_identity, r->save_identity, 32); memcpy(d->source, r->source, r->source_size);
    memcpy(d->target, r->target, r->target_size); memcpy(d->world, r->world, r->world_size);
    dluma_sha256_init(&hash); dluma_sha256_update(&hash, d, offsetof(StateDisk, digest));
    dluma_sha256_final(&hash, d->digest);
}
static int unpack(const StateDisk *d, const unsigned char identity[32], DlumaCharacterStateRecord *r)
{
    DlumaSha256Context hash; unsigned char digest[32];
    dluma_sha256_init(&hash); dluma_sha256_update(&hash, d, offsetof(StateDisk, digest)); dluma_sha256_final(&hash, digest);
    if (memcmp(d->magic, "DLCST01\0", 8) || d->version != 1 || memcmp(digest, d->digest, 32) || memcmp(identity, d->save_identity, 32) ||
        d->source_size > DLUMA_CHARACTER_STATE_SECTION_MAX || d->target_size > DLUMA_CHARACTER_STATE_SECTION_MAX || d->world_size > DLUMA_CHARACTER_STATE_SECTION_MAX) return 0;
    memset(r, 0, sizeof(*r)); r->journal.version = 1; r->journal.id = d->id;
    r->journal.phase = (DlumaCharacterPhase)d->phase; r->journal.request.scope_id = d->scope;
    r->journal.request.source_profile = d->source_profile; r->journal.request.target_profile = d->target_profile;
    r->boundary_open = d->boundary_open; r->source_size = d->source_size; r->target_size = d->target_size; r->world_size = d->world_size;
    memcpy(r->save_identity, d->save_identity, 32); memcpy(r->source, d->source, r->source_size);
    memcpy(r->target, d->target, r->target_size); memcpy(r->world, d->world, r->world_size);
    return valid(r);
}
int dluma_character_state_store_load(const WCHAR *path, const unsigned char identity[32], DlumaCharacterStateRecord *out)
{
    HANDLE f; DWORD read = 0; LARGE_INTEGER size; int ok = 0;
    StateDisk *d; DlumaCharacterStateRecord *tmp;
    if (!path || !identity || !out) return 0;
    d = (StateDisk*)HeapAlloc(GetProcessHeap(), 0, sizeof(*d));
    tmp = (DlumaCharacterStateRecord*)HeapAlloc(GetProcessHeap(), 0, sizeof(*tmp));
    if (!d || !tmp) goto done;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) goto done;
    if (GetFileSizeEx(f, &size) && size.QuadPart == sizeof(*d) && ReadFile(f, d, sizeof(*d), &read, NULL) && read == sizeof(*d))
        ok = unpack(d, identity, tmp);
    CloseHandle(f); if (ok) memcpy(out, tmp, sizeof(*out));
done:
    if (d) HeapFree(GetProcessHeap(), 0, d); if (tmp) HeapFree(GetProcessHeap(), 0, tmp); return ok;
}
static int publish(const WCHAR *path, const DlumaCharacterStateRecord *r, int replace)
{
    WCHAR temp[32768]; size_t n; HANDLE f; DWORD written; int ok = 0; StateDisk *d;
    if (!path || !valid(r)) return 0; n = wcslen(path); if (!n || n + 5 >= 32768) return 0;
    memcpy(temp, path, n * sizeof(WCHAR)); memcpy(temp + n, L".tmp", 5 * sizeof(WCHAR));
    d = (StateDisk*)HeapAlloc(GetProcessHeap(), 0, sizeof(*d)); if (!d) return 0; pack(r, d);
    f = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        ok = WriteFile(f, d, sizeof(*d), &written, NULL) && written == sizeof(*d) && FlushFileBuffers(f);
        if (!CloseHandle(f)) ok = 0;
        if (ok) ok = MoveFileExW(temp, path, MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)) != 0;
        if (!ok) DeleteFileW(temp);
    }
    HeapFree(GetProcessHeap(), 0, d); return ok;
}
int dluma_character_state_store_create(const WCHAR *path, const DlumaCharacterStateRecord *r)
{
    return r && r->journal.phase == DLUMA_CHARACTER_PREPARED && !r->boundary_open && publish(path, r, 0);
}
int dluma_character_state_store_update(const WCHAR *path, const DlumaCharacterStateRecord *r)
{
    DlumaCharacterStateRecord *old; int ok = 0; int phase_ok;
    if (!valid(r)) return 0;
    old = (DlumaCharacterStateRecord*)HeapAlloc(GetProcessHeap(), 0, sizeof(*old)); if (!old) return 0;
    if (!dluma_character_state_store_load(path, r->save_identity, old)) goto done;
    phase_ok = r->journal.phase == old->journal.phase ||
        (old->journal.phase == DLUMA_CHARACTER_PREPARED && (r->journal.phase == DLUMA_CHARACTER_APPLYING || r->journal.phase == DLUMA_CHARACTER_RESTORING)) ||
        (old->journal.phase == DLUMA_CHARACTER_APPLYING && (r->journal.phase == DLUMA_CHARACTER_ACTIVE || r->journal.phase == DLUMA_CHARACTER_RESTORING)) ||
        (old->journal.phase == DLUMA_CHARACTER_ACTIVE && r->journal.phase == DLUMA_CHARACTER_RETURNING) ||
        (old->journal.phase == DLUMA_CHARACTER_RETURNING && r->journal.phase == DLUMA_CHARACTER_RESTORING);
    if (!phase_ok || old->journal.id != r->journal.id || memcmp(&old->journal.request, &r->journal.request, sizeof(r->journal.request)) ||
        old->source_size != r->source_size || memcmp(old->source, r->source, r->source_size) ||
        ((old->journal.phase != DLUMA_CHARACTER_ACTIVE || r->journal.phase != DLUMA_CHARACTER_RETURNING) &&
         (old->target_size != r->target_size || memcmp(old->target, r->target, r->target_size))) ||
        (old->boundary_open && (old->world_size != r->world_size || memcmp(old->world, r->world, r->world_size)))) goto done;
    ok = publish(path, r, 1);
done:
    HeapFree(GetProcessHeap(), 0, old); return ok;
}
