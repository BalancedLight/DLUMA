#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_character_state_store.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d\n", __LINE__); return 1; } } while (0)
int wmain(int argc, wchar_t **argv)
{
    DlumaCharacterStateRecord record, loaded;
    HANDLE f; DWORD written; unsigned char byte = 0; wchar_t temp[MAX_PATH];
    CHECK(argc == 2);
    memset(&record, 0, sizeof(record));
    record.journal.version = 1; record.journal.id = 7;
    record.journal.request.scope_id = 9; record.journal.request.source_profile = 10;
    record.journal.request.target_profile = 11; record.journal.phase = DLUMA_CHARACTER_PREPARED;
    memset(record.save_identity, 0x42, sizeof(record.save_identity));
    record.source_size = 3; memcpy(record.source, "src", 3);
    record.target_size = 3; memcpy(record.target, "old", 3);
    record.world_size = 5; memcpy(record.world, "world", 5);
    CHECK(dluma_character_state_store_create(argv[1], &record));
    CHECK(!dluma_character_state_store_create(argv[1], &record));
    CHECK(swprintf_s(temp, MAX_PATH, L"%ls.tmp", argv[1]) > 0);
    f = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CHECK(f != INVALID_HANDLE_VALUE); CloseHandle(f);
    record.journal.phase = DLUMA_CHARACTER_APPLYING;
    CHECK(!dluma_character_state_store_update(argv[1], &record));
    CHECK(dluma_character_state_store_load(argv[1], record.save_identity, &loaded));
    CHECK(loaded.journal.phase == DLUMA_CHARACTER_PREPARED);
    /* Test controls this deliberately empty file; real pending staging data
     * must be reconciled, not discarded by the store automatically. */
    CHECK(DeleteFileW(temp));
    f = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(f != INVALID_HANDLE_VALUE);
    CHECK(!dluma_character_state_store_update(argv[1], &record));
    CloseHandle(f);
    CHECK(dluma_character_state_store_load(argv[1], record.save_identity, &loaded));
    CHECK(loaded.journal.phase == DLUMA_CHARACTER_PREPARED);
    record.journal.phase = DLUMA_CHARACTER_APPLYING;
    record.target[0] = 'X'; CHECK(!dluma_character_state_store_update(argv[1], &record));
    record.target[0] = 'o';
    CHECK(dluma_character_state_store_update(argv[1], &record));
    record.journal.phase = DLUMA_CHARACTER_ACTIVE;
    CHECK(dluma_character_state_store_update(argv[1], &record));
    record.journal.phase = DLUMA_CHARACTER_RETURNING;
    memcpy(record.target, "new", 3); record.boundary_open = 1;
    CHECK(dluma_character_state_store_update(argv[1], &record));
    memset(&loaded, 0, sizeof(loaded));
    CHECK(dluma_character_state_store_load(argv[1], record.save_identity, &loaded));
    CHECK(loaded.journal.phase == DLUMA_CHARACTER_RETURNING && loaded.boundary_open);
    CHECK(!memcmp(loaded.target, "new", 3) && !memcmp(loaded.source, "src", 3));
    record.world[0] = 'X'; CHECK(!dluma_character_state_store_update(argv[1], &record));
    record.world[0] = 'w';
    record.source[0] = 'X';
    CHECK(!dluma_character_state_store_update(argv[1], &record));
    record.source[0] = 's'; record.save_identity[0] ^= 1;
    CHECK(!dluma_character_state_store_update(argv[1], &record));
    CHECK(!dluma_character_state_store_load(argv[1], record.save_identity, &loaded));
    record.save_identity[0] ^= 1;
    CHECK(dluma_character_state_store_load(argv[1], record.save_identity, &loaded));
    f = CreateFileW(argv[1], GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(f != INVALID_HANDLE_VALUE);
    CHECK(WriteFile(f, &byte, 1, &written, NULL) && written == 1); CloseHandle(f);
    CHECK(!dluma_character_state_store_load(argv[1], record.save_identity, &loaded));
    puts("PASS: durable character store"); return 0;
}
