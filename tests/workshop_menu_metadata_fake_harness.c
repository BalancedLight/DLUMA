#define DLUMA_WORKSHOP_MENU_PARSER_ONLY 1
#include "../src/dlw_workshop_menu.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

char dluma_ascii_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

int dluma_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int dluma_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

int dluma_stricmp(const char *first, const char *second)
{
    unsigned int i = 0;
    while (first[i] && second[i])
    {
        char a = dluma_ascii_lower(first[i]);
        char b = dluma_ascii_lower(second[i]);
        if (a != b) return (unsigned char)a - (unsigned char)b;
        ++i;
    }
    return (unsigned char)first[i] - (unsigned char)second[i];
}

void dluma_str_copy(char *destination, unsigned int capacity,
                  const char *source)
{
    unsigned int i = 0;
    if (!destination || !capacity) return;
    if (source)
        while (source[i] && i + 1U < capacity)
        {
            destination[i] = source[i];
            ++i;
        }
    destination[i] = 0;
}

static int parse(const char *text, char *title, unsigned int title_capacity,
                 char *description, unsigned int description_capacity,
                 char *id, unsigned int id_capacity)
{
    return dluma_workshop_menu_parse_desc_text(
        text, (DWORD)strlen(text), title, title_capacity,
        description, description_capacity, id, id_capacity);
}

static int parse_cache(const char *text, DlumaWorkshopCacheMetadata *metadata)
{
    return dluma_workshop_parse_cache_metadata_text(
        text, (DWORD)strlen(text), metadata);
}

int main(void)
{
    char title[64];
    char description[128];
    char id[16];
    char large_id[32];
    char oversized_metadata[DLUMA_WORKSHOP_MENU_CACHE_META_MAX_BYTES + 2U];
    DlumaWorkshopCacheMetadata metadata;
    const char *valid_metadata =
        "version=1\r\n"
        "published_id=3728869100\r\n"
        "updated_time=1720000000\r\n"
        "format=png\r\n"
        "size=12345\r\n"
        "sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n";
    int fields;

    fields = parse("sub details() {}", title, 64, description, 128, id, 16);
    assert(fields == 0 && !title[0] && !description[0] && !id[0]);

    fields = parse(
        "title(\"Map Title\"); desc(\"Map description\"); id(\"3728869100\");",
        title, 64, description, 128, id, 16);
    assert(fields == 7);
    assert(strcmp(title, "Map Title") == 0);
    assert(strcmp(description, "Map description") == 0);
    assert(strcmp(id, "3728869100") == 0);

    fields = parse("title(\"\"); desc(\" \t\"); id(\"0\");",
                   title, 64, description, 128, id, 16);
    assert(fields == 0 && !title[0] && !description[0] && !id[0]);

    fields = parse(
        "title(\"  A\\n  Map  \"); desc(\"first\r\nsecond\tline\n\n\nthird\");",
        title, 64, description, 128, id, 16);
    assert(fields == 3);
    assert(strcmp(title, "A Map") == 0);
    assert(strcmp(description, "first\nsecond line\n\nthird") == 0);

    fields = parse("title(\"unterminated);",
                   title, 64, description, 128, id, 16);
    assert(fields == 0 && !title[0] && !description[0]);

    fields = parse("title(\"ABCDEFGHIJKLMNOPQRSTUVWXYZ\");",
                   title, 8, description, 128, id, 16);
    assert(fields == 1);
    assert(strcmp(title, "ABCDEFG") == 0);

    fields = parse("desc(\"12345678901234567890\");",
                   title, 64, description, 10, id, 16);
    assert(fields == 2);
    assert(strcmp(description, "123456789") == 0);

    fields = parse("id(\"12345678901234567890\");",
                   title, 64, description, 128, id, 8);
    assert(fields == 0 && !id[0]);

    fields = parse("id(\"12bad34\");", title, 64, description, 128, id, 16);
    assert(fields == 0 && !id[0]);

    fields = parse("id(\"18446744073709551616\");",
                   title, 64, description, 128, large_id, 32);
    assert(fields == 0 && !large_id[0]);

    fields = parse(
        "notitle(\"wrong\"); title(\"Quoted \\\"Map\\\"\"); "
        "desc(\"line\\npath\\\\file\");",
        title, 64, description, 128, id, 16);
    assert(fields == 3);
    assert(strcmp(title, "Quoted \"Map\"") == 0);
    assert(strcmp(description, "line\npath\\file") == 0);

    assert(dluma_workshop_preview_url_supported("http://steam.test/a.png"));
    assert(dluma_workshop_preview_url_supported("HTTPS://steam.test/a.jpg"));
    assert(!dluma_workshop_preview_url_supported("ftp://steam.test/a.png"));
    assert(!dluma_workshop_preview_url_supported("steam.test/a.png"));

    assert(parse_cache(valid_metadata, &metadata));
    assert(metadata.published_id == 3728869100ULL);
    assert(metadata.updated_time == 1720000000U);
    assert(metadata.png == 1 && metadata.image_size == 12345U);

    assert(parse_cache(
        "VERSION=1\nPUBLISHED_ID=9\nUPDATED_TIME=8\nFORMAT=JPG\nSIZE=8\n"
        "SHA256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",
        &metadata));
    assert(metadata.png == 0);
    assert(!parse_cache("version=2\n", &metadata));
    assert(!parse_cache("version=1\npublished_id=0\n", &metadata));
    assert(!parse_cache(
        "version=1\npublished_id=1\nupdated_time=1\nformat=gif\nsize=8\n"
        "sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",
        &metadata));
    assert(!parse_cache(
        "version=1\npublished_id=1\nupdated_time=1\nformat=png\nsize=7\n"
        "sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",
        &metadata));
    assert(!parse_cache(
        "version=1\npublished_id=1\nupdated_time=1\nformat=png\nsize=8\n"
        "sha256=not-a-valid-digest\n",
        &metadata));
    memset(oversized_metadata, 'x', sizeof(oversized_metadata));
    assert(!dluma_workshop_parse_cache_metadata_text(
        oversized_metadata, (DWORD)sizeof(oversized_metadata), &metadata));

    puts("workshop_menu_metadata_fake_harness: 20/20 ok");
    return 0;
}
