#include "dlw_common.h"
#include "dlw_custom_map_loading_core.h"
#include "dlw_path_security.h"
#include "dlw_workshop_enrichment_core.h"
#include <stddef.h>

#define DLUMA_WORKSHOP_MENU_MAX_PROJECTS 256U
#define DLUMA_WORKSHOP_MENU_MAX_REMOTE 64U
#define DLUMA_WORKSHOP_MENU_QUERY_BATCH 8U
#define DLUMA_WORKSHOP_MENU_HTTP_SLOTS 2U
#define DLUMA_WORKSHOP_MENU_IMAGE_JOBS 2U
#define DLUMA_WORKSHOP_MENU_PIXEL_JOBS 4U
#define DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES 256U
#define DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES (8UL * 1024UL * 1024UL)
#define DLUMA_WORKSHOP_MENU_CACHE_META_MAX_BYTES 1024U
#define DLUMA_WORKSHOP_MENU_DETAILS_MAX_ATTEMPTS 2U
#define DLUMA_WORKSHOP_MENU_PREVIEW_MAX_ATTEMPTS 2U
#define DLUMA_WORKSHOP_MENU_PERSONA_MAX_ATTEMPTS 2U
#define DLUMA_WORKSHOP_MENU_RETRY_DELAY_MS 5000ULL
#define DLUMA_WORKSHOP_MENU_PERSONA_TIMEOUT_MS 15000ULL
#define DLUMA_STEAM_API_TIMESTAMP 0x56995865UL
#define DLUMA_STEAM_API_IMAGE_SIZE 0x0003D000UL
#define DLUMA_STEAM_UGC_DETAILS_SIZE 9784U
#define DLUMA_STEAM_UGC_DESCRIPTION_OFFSET 153U
#define DLUMA_STEAM_UGC_DESCRIPTION_SIZE 8000U
#define DLUMA_STEAM_UGC_OWNER_OFFSET 8160U
#define DLUMA_STEAM_UGC_UPDATED_OFFSET 8172U
#define DLUMA_STEAM_UGC_QUERY_CALLBACK 3401
#define DLUMA_STEAM_HTTP_CALLBACK 2101
#define DLUMA_STEAM_HTTP_GET 1
#define DLUMA_STEAM_HTTP_OK 200

typedef struct DlumaWorkshopMenuRemoteState {
    ULONGLONG published_id;
    ULONGLONG owner_id;
    unsigned int updated_time;
    unsigned int cache_updated_time;
    ULONGLONG details_retry_after_ms;
    ULONGLONG author_started_ms;
    ULONGLONG preview_retry_after_ms;
    unsigned char requested;
    unsigned char details_ready;
    unsigned char details_failed;
    unsigned char details_attempts;
    unsigned char author_ready;
    unsigned char author_failed;
    unsigned char author_attempts;
    unsigned char preview_ready;
    unsigned char preview_failed;
    unsigned char preview_attempts;
    unsigned char download_started;
    unsigned char cache_status_logged;
    unsigned char details_status_logged;
    unsigned char author_status_logged;
    unsigned char preview_status_logged;
    char title[DLUMA_WORKSHOP_MENU_TITLE_CAPACITY];
    char description[DLUMA_WORKSHOP_STEAM_DESCRIPTION_CAPACITY];
    char author[DLUMA_WORKSHOP_MENU_AUTHOR_CAPACITY];
    char preview_url[1024];
    WCHAR preview_path[DLUMA_MAX_WPATH];
} DlumaWorkshopMenuRemoteState;

typedef struct DlumaWorkshopCacheMetadata {
    ULONGLONG published_id;
    unsigned int updated_time;
    DWORD image_size;
    unsigned char png;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
} DlumaWorkshopCacheMetadata;

typedef struct DlumaWorkshopMenuCacheEntry {
    ULONGLONG published_id;
    unsigned int updated_time;
    DWORD image_size;
    unsigned char png;
    unsigned char metadata_valid;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR image_path[DLUMA_MAX_WPATH];
} DlumaWorkshopMenuCacheEntry;

typedef struct DlumaWorkshopLocalFileStamp {
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    DWORD size_high;
    DWORD size_low;
    DWORD last_write_high;
    DWORD last_write_low;
    unsigned char present;
} DlumaWorkshopLocalFileStamp;

typedef struct DlumaWorkshopLocalMetadataCacheEntry {
    DlumaWorkshopMenuItem item;
    WCHAR final_project_root[DLUMA_MAX_WPATH];
    DlumaWorkshopLocalFileStamp project;
    DlumaWorkshopLocalFileStamp description;
    DlumaWorkshopLocalFileStamp preview;
    DlumaWorkshopLocalFileStamp data;
    DlumaWorkshopLocalFileStamp maps;
    ULONGLONG last_used;
    unsigned char occupied;
    unsigned char subscribed;
} DlumaWorkshopLocalMetadataCacheEntry;

typedef struct DlumaSteamUgcQueryCompleted {
    ULONGLONG handle;
    int result;
    unsigned int returned;
    unsigned int total;
    unsigned char cached;
    unsigned char padding[3];
} DlumaSteamUgcQueryCompleted;

typedef struct DlumaSteamHttpCompleted {
    unsigned int request;
    unsigned int padding0;
    ULONGLONG context;
    unsigned char successful;
    unsigned char padding1[3];
    int status_code;
    unsigned int body_size;
    unsigned int padding2;
} DlumaSteamHttpCompleted;

typedef struct DlumaSteamHttpCallResultBase {
    void** vtable;
    unsigned char flags;
    unsigned char padding[3];
    int callback_id;
    ULONGLONG api_call;
    volatile LONG completed;
    volatile LONG io_failure;
    volatile LONG wrong_call;
    DlumaSteamHttpCompleted result;
} DlumaSteamHttpCallResultBase;

typedef struct DlumaWorkshopMenuHttpSlot {
    DlumaSteamHttpCallResultBase callback;
    unsigned int request;
    ULONGLONG published_id;
    unsigned char registered;
} DlumaWorkshopMenuHttpSlot;

typedef struct DlumaWorkshopMenuImageJob {
    volatile LONG state;
    BYTE* data;
    DWORD size;
    ULONGLONG published_id;
    unsigned int updated_time;
    unsigned char png;
    unsigned char succeeded;
    unsigned char metadata_succeeded;
    WCHAR final_path[DLUMA_MAX_WPATH];
} DlumaWorkshopMenuImageJob;

typedef enum DlumaWorkshopPixelLayout {
    DLUMA_WORKSHOP_PIXEL_LAYOUT_UNKNOWN = 0,
    DLUMA_WORKSHOP_PIXEL_LAYOUT_BGRA8_CACHE,
    DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD
} DlumaWorkshopPixelLayout;

/* States: 0 free, 1 worker requested, 2 game-thread ready, 3 owned while a
 * producer/consumer copies fields.  The worker never calls engine APIs. */
typedef struct DlumaWorkshopPreviewPixelJob {
    volatile LONG state;
    ULONGLONG asset_key;
    WCHAR dds_path[DLUMA_MAX_WPATH];
    BYTE *pixels;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    DlumaWorkshopPixelLayout layout;
    unsigned char succeeded;
} DlumaWorkshopPreviewPixelJob;

typedef void* (__cdecl *DlumaSteamAccessorFn)(void);
typedef ULONGLONG (__cdecl *DlumaSteamCreateDetailsQueryFn)(
    void* ugc, const ULONGLONG* published_ids, unsigned int count);
typedef ULONGLONG (__cdecl *DlumaSteamSendQueryFn)(void* ugc, ULONGLONG query);
typedef unsigned char (__cdecl *DlumaSteamGetQueryResultFn)(
    void* ugc, ULONGLONG query, unsigned int index, void* details);
typedef unsigned char (__cdecl *DlumaSteamGetPreviewUrlFn)(
    void* ugc, ULONGLONG query, unsigned int index,
    char* url, unsigned int capacity);
typedef unsigned char (__cdecl *DlumaSteamReleaseQueryFn)(
    void* ugc, ULONGLONG query);
typedef void* (__cdecl *DlumaSteamSetUgcCallResultFn)(
    ULONGLONG api_call,
    void (__cdecl *callback)(const DlumaSteamUgcQueryCompleted*,
                             unsigned char));
typedef void (__cdecl *DlumaSteamRemoveUgcCallResultFn)(void* call_result);
typedef void (__cdecl *DlumaSteamRegisterCallResultFn)(
    void* callback_base, ULONGLONG api_call);
typedef void (__cdecl *DlumaSteamUnregisterCallResultFn)(
    void* callback_base, ULONGLONG api_call);
typedef const char* (__cdecl *DlumaSteamGetPersonaNameFn)(
    void* friends, ULONGLONG steam_id);
typedef unsigned char (__cdecl *DlumaSteamRequestUserInformationFn)(
    void* friends, ULONGLONG steam_id, unsigned char name_only);
typedef unsigned int (__cdecl *DlumaSteamCreateHttpRequestFn)(
    void* http, int method, const char* absolute_url);
typedef unsigned char (__cdecl *DlumaSteamSendHttpRequestFn)(
    void* http, unsigned int request, ULONGLONG* api_call);
typedef unsigned char (__cdecl *DlumaSteamGetHttpBodySizeFn)(
    void* http, unsigned int request, unsigned int* body_size);
typedef unsigned char (__cdecl *DlumaSteamGetHttpBodyDataFn)(
    void* http, unsigned int request, BYTE* body, unsigned int body_size);
typedef unsigned char (__cdecl *DlumaSteamReleaseHttpRequestFn)(
    void* http, unsigned int request);

typedef struct DlumaWorkshopMenuSteamApi {
    HMODULE module;
    void* ugc;
    void* friends;
    void* http;
    DlumaSteamCreateDetailsQueryFn create_details_query;
    DlumaSteamSendQueryFn send_query;
    DlumaSteamGetQueryResultFn get_query_result;
    DlumaSteamGetPreviewUrlFn get_preview_url;
    DlumaSteamReleaseQueryFn release_query;
    DlumaSteamSetUgcCallResultFn set_ugc_call_result;
    DlumaSteamRemoveUgcCallResultFn remove_ugc_call_result;
    DlumaSteamRegisterCallResultFn register_call_result;
    DlumaSteamUnregisterCallResultFn unregister_call_result;
    DlumaSteamGetPersonaNameFn get_persona_name;
    DlumaSteamRequestUserInformationFn request_user_information;
    DlumaSteamCreateHttpRequestFn create_http_request;
    DlumaSteamSendHttpRequestFn send_http_request;
    DlumaSteamGetHttpBodySizeFn get_http_body_size;
    DlumaSteamGetHttpBodyDataFn get_http_body_data;
    DlumaSteamReleaseHttpRequestFn release_http_request;
    unsigned char attempted;
    unsigned char ready;
    unsigned char details_available;
    unsigned char author_available;
    unsigned char preview_available;
} DlumaWorkshopMenuSteamApi;

static volatile LONG g_dluma_workshop_metadata_sequence;
static unsigned int g_dluma_workshop_metadata_count;
static DlumaWorkshopMenuItem
    g_dluma_workshop_metadata[DLUMA_WORKSHOP_MENU_MAX_PROJECTS];
static DlumaWorkshopMenuRemoteState
    g_dluma_workshop_remote[DLUMA_WORKSHOP_MENU_MAX_REMOTE];
static unsigned int g_dluma_workshop_remote_count;
static DlumaWorkshopMenuSteamApi g_dluma_workshop_steam;
static ULONGLONG g_dluma_workshop_query_handle = ~0ULL;
static ULONGLONG g_dluma_workshop_query_call;
static void* g_dluma_workshop_query_call_result;
static volatile LONG g_dluma_workshop_query_completion_ready;
static volatile LONG g_dluma_workshop_query_completion_failed;
static DlumaSteamUgcQueryCompleted g_dluma_workshop_query_completion;
static ULONGLONG g_dluma_workshop_query_ids[DLUMA_WORKSHOP_MENU_QUERY_BATCH];
static unsigned int g_dluma_workshop_query_count;
static DlumaWorkshopMenuHttpSlot
    g_dluma_workshop_http[DLUMA_WORKSHOP_MENU_HTTP_SLOTS];
static DlumaWorkshopMenuImageJob
    g_dluma_workshop_image_jobs[DLUMA_WORKSHOP_MENU_IMAGE_JOBS];
static DlumaWorkshopPreviewPixelJob
    g_dluma_workshop_pixel_jobs[DLUMA_WORKSHOP_MENU_PIXEL_JOBS];
static volatile LONG g_dluma_workshop_cache_sequence;
static volatile LONG g_dluma_workshop_cache_scan_requested = 1;
static unsigned int g_dluma_workshop_cache_count;
static DlumaWorkshopMenuCacheEntry
    g_dluma_workshop_cache[DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES];
static volatile LONG g_dluma_workshop_steam_status_logged;
static volatile LONG g_dluma_workshop_callback_active_calls;
static volatile LONG g_dluma_workshop_callback_adapter_status_logged;
static volatile LONG g_dluma_workshop_cache_directory_status;
static ULONGLONG g_dluma_workshop_presentation_signature;
static ULONGLONG g_dluma_workshop_presentation_revision;
static DlumaWorkshopLocalMetadataCacheEntry
    g_dluma_workshop_local_metadata_cache[DLUMA_WORKSHOP_MENU_MAX_PROJECTS];
static ULONGLONG g_dluma_workshop_local_metadata_cache_clock;

static void dluma_workshop_apply_cache_to_state(
    DlumaWorkshopMenuRemoteState* state);
static void dluma_workshop_build_cache_path(
    WCHAR* out, unsigned int capacity,
    ULONGLONG published_id, const WCHAR* extension);
static int dluma_workshop_build_hashed_dds_path(
    WCHAR* out, unsigned int capacity,
    ULONGLONG published_id, const char* sha256);

typedef char DlumaSteamUgcQueryResultSizeMustBe24[
    sizeof(DlumaSteamUgcQueryCompleted) == 24 ? 1 : -1];
typedef char DlumaSteamHttpResultSizeMustBe32[
    sizeof(DlumaSteamHttpCompleted) == 32 ? 1 : -1];
typedef char DlumaSteamHttpCallbackIdOffsetMustBe12[
    offsetof(DlumaSteamHttpCallResultBase, callback_id) == 12 ? 1 : -1];
typedef char DlumaSteamHttpApiCallOffsetMustBe16[
    offsetof(DlumaSteamHttpCallResultBase, api_call) == 16 ? 1 : -1];

static void __fastcall dluma_workshop_http_call_result_run(
    DlumaSteamHttpCallResultBase* callback, const void* result);

static void __fastcall dluma_workshop_http_call_result_run_call(
    DlumaSteamHttpCallResultBase* callback, const void* result,
    unsigned char io_failure, ULONGLONG api_call)
{
    dluma_atomic_increment(&g_dluma_workshop_callback_active_calls);
    if (callback && result &&
        dluma_atomic_compare_exchange(&callback->completed, 0, 0) == 0)
    {
        if (callback->api_call != api_call)
            dluma_atomic_exchange(&callback->wrong_call, 1);
        memcpy(&callback->result, result, sizeof(callback->result));
        dluma_atomic_exchange(&callback->io_failure,
                            io_failure ? 1 : 0);
        dluma_atomic_exchange(&callback->completed, 1);
    }
    dluma_atomic_decrement(&g_dluma_workshop_callback_active_calls);
}

static void __fastcall dluma_workshop_http_call_result_run(
    DlumaSteamHttpCallResultBase* callback, const void* result)
{
    dluma_workshop_http_call_result_run_call(
        callback, result, 0, callback ? callback->api_call : 0);
}

static int __fastcall dluma_workshop_http_call_result_size(
    DlumaSteamHttpCallResultBase* callback)
{
    (void)callback;
    return (int)sizeof(DlumaSteamHttpCompleted);
}

static void* g_dluma_workshop_http_call_result_vtable[] = {
    (void*)dluma_workshop_http_call_result_run,
    (void*)dluma_workshop_http_call_result_run_call,
    (void*)dluma_workshop_http_call_result_size
};

static void __cdecl dluma_workshop_ugc_call_result_callback(
    const DlumaSteamUgcQueryCompleted* result, unsigned char io_failure)
{
    dluma_atomic_increment(&g_dluma_workshop_callback_active_calls);
    if (result && g_dluma_workshop_query_call &&
        dluma_atomic_compare_exchange(
            &g_dluma_workshop_query_completion_ready, 0, 0) == 0)
    {
        memcpy(&g_dluma_workshop_query_completion, result,
               sizeof(g_dluma_workshop_query_completion));
        dluma_atomic_exchange(&g_dluma_workshop_query_completion_failed,
                            io_failure ? 1 : 0);
        dluma_atomic_exchange(&g_dluma_workshop_query_completion_ready, 1);
    }
    dluma_atomic_decrement(&g_dluma_workshop_callback_active_calls);
}

static int dluma_workshop_text_prefix_icase(const char* text, const char* prefix)
{
    unsigned int i;
    if (!text || !prefix)
        return 0;
    for (i = 0; prefix[i]; ++i)
        if (dluma_ascii_lower(text[i]) != dluma_ascii_lower(prefix[i]))
            return 0;
    return 1;
}

static int dluma_workshop_preview_url_supported(const char* url)
{
    return dluma_workshop_text_prefix_icase(url, "https://") ||
           dluma_workshop_text_prefix_icase(url, "http://");
}

static int dluma_workshop_field_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int dluma_workshop_parse_field(
    const char* text, DWORD size, const char* field,
    char* out, unsigned int capacity, int* truncated_out)
{
    unsigned int field_length;
    DWORD i;
    if (!text || !field || !field[0] || !out || capacity < 2)
        return 0;
    out[0] = 0;
    if (truncated_out) *truncated_out = 0;
    field_length = (unsigned int)strlen(field);
    for (i = 0; i + field_length + 2U < size; ++i)
    {
        DWORD cursor;
        unsigned int j;
        unsigned int count = 0;
        int closed = 0;
        if (i && dluma_workshop_field_char(text[i - 1]))
            continue;
        for (j = 0; j < field_length; ++j)
            if (dluma_ascii_lower(text[i + j]) != dluma_ascii_lower(field[j]))
                break;
        if (j != field_length ||
            (i + field_length < size &&
             dluma_workshop_field_char(text[i + field_length])))
            continue;
        cursor = i + field_length;
        while (cursor < size && dluma_is_space(text[cursor]))
            ++cursor;
        if (cursor >= size || text[cursor++] != '(')
            continue;
        while (cursor < size && dluma_is_space(text[cursor]))
            ++cursor;
        if (cursor >= size || text[cursor++] != '"')
            continue;
        while (cursor < size)
        {
            char c = text[cursor++];
            if (c == '"')
            {
                closed = 1;
                break;
            }
            if (c == '\\' && cursor < size)
            {
                char escaped = text[cursor++];
                if (escaped == 'n') c = '\n';
                else if (escaped == 'r') c = '\r';
                else if (escaped == 't') c = '\t';
                else if (escaped == '"') c = '"';
                else if (escaped == '\\') c = '\\';
                else
                {
                    if (count + 1 < capacity)
                        out[count++] = '\\';
                    else if (truncated_out)
                        *truncated_out = 1;
                    c = escaped;
                }
            }
            if (count + 1 < capacity)
                out[count++] = c;
            else if (truncated_out)
                *truncated_out = 1;
        }
        out[count] = 0;
        if (closed)
            return 1;
        out[0] = 0;
    }
    return 0;
}

static void dluma_workshop_normalize_title(char* text)
{
    unsigned int read = 0;
    unsigned int write = 0;
    int pending_space = 0;
    if (!text)
        return;
    while (text[read] && dluma_is_space(text[read]))
        ++read;
    for (; text[read]; ++read)
    {
        unsigned char c = (unsigned char)text[read];
        if (c <= 0x20U || c == 0x7FU)
        {
            pending_space = write != 0;
            continue;
        }
        if (pending_space)
            text[write++] = ' ';
        pending_space = 0;
        text[write++] = (char)c;
    }
    text[write] = 0;
}

static void dluma_workshop_normalize_description(char* text)
{
    unsigned int read = 0;
    unsigned int write = 0;
    unsigned int start;
    if (!text)
        return;
    while (text[read])
    {
        unsigned char c = (unsigned char)text[read++];
        if (c == '\r')
        {
            if (text[read] == '\n')
                ++read;
            c = '\n';
        }
        else if (c == '\t')
            c = ' ';
        else if (c < 0x20U && c != '\n')
            continue;
        if (c == '\n' && write && text[write - 1] == '\n' &&
            write > 1 && text[write - 2] == '\n')
            continue;
        text[write++] = (char)c;
    }
    text[write] = 0;
    start = 0;
    while (text[start] && dluma_is_space(text[start]))
        ++start;
    if (start)
    {
        unsigned int i = 0;
        do
        {
            text[i] = text[start + i];
        } while (text[i++]);
    }
    write = (unsigned int)strlen(text);
    while (write && dluma_is_space(text[write - 1]))
        text[--write] = 0;
}

static int dluma_workshop_decimal_u64(
    const char* text, ULONGLONG* value_out)
{
    ULONGLONG value = 0;
    unsigned int i;
    if (value_out) *value_out = 0;
    if (!text || !text[0] || !value_out)
        return 0;
    for (i = 0; text[i]; ++i)
    {
        ULONGLONG digit;
        if (!dluma_is_digit(text[i]))
            return 0;
        digit = (ULONGLONG)(text[i] - '0');
        if (value > (~0ULL - digit) / 10ULL)
            return 0;
        value = value * 10ULL + digit;
    }
    if (!value)
        return 0;
    *value_out = value;
    return 1;
}

static int dluma_workshop_cache_text_value(
    const char* text, DWORD size, const char* key,
    char* out, unsigned int capacity)
{
    DWORD cursor = 0;
    unsigned int key_length;
    if (out && capacity) out[0] = 0;
    if (!text || !key || !key[0] || !out || capacity < 2)
        return 0;
    key_length = (unsigned int)strlen(key);
    while (cursor < size)
    {
        DWORD line_start = cursor;
        DWORD line_end;
        unsigned int count;
        while (cursor < size && text[cursor] != '\r' && text[cursor] != '\n')
            ++cursor;
        line_end = cursor;
        while (cursor < size && (text[cursor] == '\r' || text[cursor] == '\n'))
            ++cursor;
        if (line_end <= line_start + key_length ||
            text[line_start + key_length] != '=')
            continue;
        for (count = 0; count < key_length; ++count)
            if (dluma_ascii_lower(text[line_start + count]) !=
                dluma_ascii_lower(key[count]))
                break;
        if (count != key_length)
            continue;
        count = 0;
        line_start += key_length + 1U;
        while (line_start < line_end && count + 1U < capacity)
            out[count++] = text[line_start++];
        out[count] = 0;
        return line_start == line_end;
    }
    return 0;
}

static int dluma_workshop_cache_hex_digest(const char* text)
{
    unsigned int i;
    if (!text)
        return 0;
    for (i = 0; i < DLUMA_SHA256_HEX_CAPACITY - 1U; ++i)
    {
        char c = text[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return text[DLUMA_SHA256_HEX_CAPACITY - 1U] == 0;
}

static int dluma_workshop_parse_cache_metadata_text(
    const char* text, DWORD size, DlumaWorkshopCacheMetadata* out)
{
    char version[16];
    char published_id[32];
    char updated_time[32];
    char image_size[32];
    char format[16];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG parsed_id = 0;
    ULONGLONG parsed_updated = 0;
    ULONGLONG parsed_size = 0;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!text || !size || size > DLUMA_WORKSHOP_MENU_CACHE_META_MAX_BYTES ||
        !dluma_workshop_cache_text_value(text, size, "version",
                                       version, DLUMA_ARRAY_COUNT(version)) ||
        dluma_stricmp(version, "1") != 0 ||
        !dluma_workshop_cache_text_value(text, size, "published_id",
                                       published_id,
                                       DLUMA_ARRAY_COUNT(published_id)) ||
        !dluma_workshop_decimal_u64(published_id, &parsed_id) ||
        !dluma_workshop_cache_text_value(text, size, "updated_time",
                                       updated_time,
                                       DLUMA_ARRAY_COUNT(updated_time)) ||
        !dluma_workshop_decimal_u64(updated_time, &parsed_updated) ||
        parsed_updated > 0xFFFFFFFFULL ||
        !dluma_workshop_cache_text_value(text, size, "format",
                                       format, DLUMA_ARRAY_COUNT(format)) ||
        (dluma_stricmp(format, "png") != 0 &&
         dluma_stricmp(format, "jpg") != 0) ||
        !dluma_workshop_cache_text_value(text, size, "size",
                                       image_size,
                                       DLUMA_ARRAY_COUNT(image_size)) ||
        !dluma_workshop_decimal_u64(image_size, &parsed_size) ||
        parsed_size < 8U || parsed_size > DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES ||
        !dluma_workshop_cache_text_value(text, size, "sha256",
                                       sha256, DLUMA_ARRAY_COUNT(sha256)) ||
        !dluma_workshop_cache_hex_digest(sha256))
        return 0;
    out->published_id = parsed_id;
    out->updated_time = (unsigned int)parsed_updated;
    out->image_size = (DWORD)parsed_size;
    out->png = (unsigned char)(dluma_stricmp(format, "png") == 0 ? 1 : 0);
    dluma_str_copy(out->sha256, DLUMA_ARRAY_COUNT(out->sha256), sha256);
    return 1;
}

int dluma_workshop_menu_parse_desc_text(
    const char* text, DWORD size,
    char* title_out, unsigned int title_capacity,
    char* description_out, unsigned int description_capacity,
    char* id_out, unsigned int id_capacity)
{
    int found = 0;
    int id_truncated = 0;
    ULONGLONG parsed_id = 0;
    if (title_out && title_capacity) title_out[0] = 0;
    if (description_out && description_capacity) description_out[0] = 0;
    if (id_out && id_capacity) id_out[0] = 0;
    if (!text)
        return 0;
    if (title_out && title_capacity &&
        dluma_workshop_parse_field(
            text, size, "title", title_out, title_capacity, 0))
    {
        dluma_workshop_normalize_title(title_out);
        if (title_out[0]) found |= 1;
    }
    if (description_out && description_capacity &&
        dluma_workshop_parse_field(
            text, size, "desc", description_out, description_capacity, 0))
    {
        dluma_workshop_normalize_description(description_out);
        if (description_out[0]) found |= 2;
    }
    if (id_out && id_capacity &&
        dluma_workshop_parse_field(
            text, size, "id", id_out, id_capacity, &id_truncated))
    {
        dluma_workshop_normalize_title(id_out);
        if (!id_truncated &&
            dluma_workshop_decimal_u64(id_out, &parsed_id))
            found |= 4;
        else
            id_out[0] = 0;
    }
    return found;
}

#ifndef DLUMA_WORKSHOP_MENU_PARSER_ONLY
static const WCHAR* dluma_workshop_leaf(const WCHAR* path)
{
    const WCHAR* leaf = path;
    unsigned int i;
    if (!path)
        return L"";
    for (i = 0; path[i]; ++i)
        if (path[i] == L'\\' || path[i] == L'/')
            leaf = path + i + 1;
    return leaf;
}

static int dluma_workshop_wide_decimal_id(
    const WCHAR* text, char* out, unsigned int capacity)
{
    unsigned int i = 0;
    if (out && capacity) out[0] = 0;
    if (!text || !text[0] || !out || capacity < 2)
        return 0;
    while (text[i])
    {
        if (text[i] < L'0' || text[i] > L'9' || i + 1U >= capacity)
            return 0;
        out[i] = (char)text[i];
        ++i;
    }
    out[i] = 0;
    return i != 0 && dluma_stricmp(out, "0") != 0;
}

static int dluma_workshop_project_is_subscribed(const WCHAR* project)
{
    return dluma_wcontains_icase(
        project, L"\\workshop\\content\\239140\\") ||
           dluma_wcontains_icase(
        project, L"/workshop/content/239140/");
}

static int dluma_workshop_project_already_added(
    const DlumaWorkshopMenuItem* items, unsigned int count,
    const WCHAR* project)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
        if (dluma_wstricmp(items[i].project_root, project) == 0)
            return 1;
    return 0;
}

static ULONGLONG dluma_workshop_local_preview_hash(const WCHAR* path)
{
    ULONGLONG hash = 1469598103934665603ULL;
    unsigned int i;
    for (i = 0; path && path[i]; ++i)
    {
        WCHAR value = path[i];
        if (value >= L'A' && value <= L'Z')
            value = (WCHAR)(value + (L'a' - L'A'));
        else if (value == L'/')
            value = L'\\';
        hash ^= (ULONGLONG)(value & 0xffU);
        hash *= 1099511628211ULL;
        hash ^= (ULONGLONG)((value >> 8U) & 0xffU);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int dluma_workshop_append_dds_generation_suffix(
    WCHAR* leaf, unsigned int capacity)
{
    char generation_text[16];
    WCHAR generation_wide[16];
    if (!leaf || !capacity)
        return 0;
    dluma_u64_to_text(
        DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION,
        generation_text, DLUMA_ARRAY_COUNT(generation_text));
    if (!dluma_utf8_to_wide(
            generation_text, generation_wide,
            DLUMA_ARRAY_COUNT(generation_wide)))
        return 0;
    dluma_wstr_append(leaf, capacity, L"_d");
    dluma_wstr_append(leaf, capacity, generation_wide);
    dluma_wstr_append(leaf, capacity, L".dds");
    return leaf[0] != 0;
}

static int dluma_workshop_prepare_local_preview(
    const WCHAR* source_path, WCHAR* output_path,
    unsigned int output_capacity)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR leaf[128];
    WCHAR number_wide[32];
    WCHAR digest_wide[24];
    char number[32];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int i;
    int dds_was_valid;
    if (output_path && output_capacity)
        output_path[0] = 0;
    if (!source_path || !source_path[0] ||
        !output_path || output_capacity == 0)
        return 0;
    if (!dluma_sha256_file_hex(
            source_path, DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES,
            sha256))
        return 0;
    dluma_u64_to_text(
        dluma_workshop_local_preview_hash(source_path),
        number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(
            number, number_wide, DLUMA_ARRAY_COUNT(number_wide)))
        return 0;
    dluma_wstr_copy(leaf, DLUMA_ARRAY_COUNT(leaf), L"local_");
    dluma_wstr_append(leaf, DLUMA_ARRAY_COUNT(leaf), number_wide);
    dluma_wstr_append(leaf, DLUMA_ARRAY_COUNT(leaf), L"_");
    for (i = 0; i < 16U && sha256[i]; ++i)
        digest_wide[i] = (WCHAR)(unsigned char)sha256[i];
    digest_wide[i] = 0;
    dluma_wstr_append(leaf, DLUMA_ARRAY_COUNT(leaf), digest_wide);
    if (!dluma_workshop_append_dds_generation_suffix(
            leaf, DLUMA_ARRAY_COUNT(leaf)))
        return 0;
    dluma_path_join(
        directory, DLUMA_ARRAY_COUNT(directory),
        g_dluma.module_dir, L"dluma_runtime\\workshop_thumbnails");
    dluma_path_join(output_path, output_capacity, directory, leaf);
    dds_was_valid = dluma_workshop_preview_validate_dds(output_path);
    if (!dluma_workshop_preview_prepare_dds(
            source_path, output_path) ||
        !dluma_workshop_preview_validate_dds(output_path))
    {
        output_path[0] = 0;
        return 0;
    }
    if (!dds_was_valid)
        (void)dluma_workshop_preview_cache_note_dds_published();
    return 1;
}

static int dluma_workshop_local_stamp(
    const WCHAR* path, int directory,
    DlumaWorkshopLocalFileStamp* stamp,
    WCHAR* final_path, unsigned int final_capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT;
    if (!path || !path[0] || !stamp)
        return 0;
    memset(stamp, 0, sizeof(*stamp));
    if (final_path && final_capacity)
        final_path[0] = 0;
    if (directory)
        flags |= FILE_FLAG_BACKUP_SEMANTICS;
    file = CreateFileW(
        path, directory ? 0 : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, flags, 0);
    if (file == INVALID_HANDLE_VALUE)
        return directory ? 0 : 1;
    memset(&info, 0, sizeof(info));
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) !=
            (directory != 0) ||
        (final_path && final_capacity &&
         !dluma_get_final_path_checked(file, final_path, final_capacity))) {
        CloseHandle(file);
        return 0;
    }
    stamp->volume_serial = info.dwVolumeSerialNumber;
    stamp->file_index_high = info.nFileIndexHigh;
    stamp->file_index_low = info.nFileIndexLow;
    stamp->size_high = info.nFileSizeHigh;
    stamp->size_low = info.nFileSizeLow;
    stamp->last_write_high =
        info.ftLastWriteTime.dwHighDateTime;
    stamp->last_write_low =
        info.ftLastWriteTime.dwLowDateTime;
    stamp->present = 1;
    CloseHandle(file);
    return 1;
}

static int dluma_workshop_local_stamp_equal(
    const DlumaWorkshopLocalFileStamp* left,
    const DlumaWorkshopLocalFileStamp* right)
{
    return left && right &&
        left->present == right->present &&
        (!left->present ||
         (left->volume_serial == right->volume_serial &&
          left->file_index_high == right->file_index_high &&
          left->file_index_low == right->file_index_low &&
          left->size_high == right->size_high &&
          left->size_low == right->size_low &&
          left->last_write_high == right->last_write_high &&
          left->last_write_low == right->last_write_low));
}

static void dluma_workshop_local_optional_directory_stamp(
    const WCHAR* path, DlumaWorkshopLocalFileStamp* stamp)
{
    if (!stamp)
        return;
    memset(stamp, 0, sizeof(*stamp));
    (void)dluma_workshop_local_stamp(path, 1, stamp, 0, 0);
}

static DlumaWorkshopLocalMetadataCacheEntry*
dluma_workshop_local_metadata_cache_find(
    const WCHAR* final_project_root,
    const DlumaWorkshopLocalFileStamp* project,
    const DlumaWorkshopLocalFileStamp* description,
    const DlumaWorkshopLocalFileStamp* preview,
    const DlumaWorkshopLocalFileStamp* data,
    const DlumaWorkshopLocalFileStamp* maps,
    unsigned char subscribed)
{
    unsigned int index;
    for (index = 0;
         index < DLUMA_WORKSHOP_MENU_MAX_PROJECTS;
         ++index) {
        DlumaWorkshopLocalMetadataCacheEntry* entry =
            &g_dluma_workshop_local_metadata_cache[index];
        if (entry->occupied &&
            entry->subscribed == subscribed &&
            dluma_wstricmp(
                entry->final_project_root,
                final_project_root) == 0 &&
            dluma_workshop_local_stamp_equal(
                &entry->project, project) &&
            dluma_workshop_local_stamp_equal(
                &entry->description, description) &&
            dluma_workshop_local_stamp_equal(
                &entry->preview, preview) &&
            dluma_workshop_local_stamp_equal(
                &entry->data, data) &&
            dluma_workshop_local_stamp_equal(
                &entry->maps, maps))
            return entry;
    }
    return 0;
}

static DlumaWorkshopLocalMetadataCacheEntry*
dluma_workshop_local_metadata_cache_slot(void)
{
    unsigned int index;
    unsigned int oldest_index = 0;
    ULONGLONG oldest_use = ~0ULL;
    for (index = 0;
         index < DLUMA_WORKSHOP_MENU_MAX_PROJECTS;
         ++index) {
        DlumaWorkshopLocalMetadataCacheEntry* entry =
            &g_dluma_workshop_local_metadata_cache[index];
        if (!entry->occupied)
            return entry;
        if (entry->last_used < oldest_use) {
            oldest_use = entry->last_used;
            oldest_index = index;
        }
    }
    return &g_dluma_workshop_local_metadata_cache[oldest_index];
}

static void dluma_workshop_local_metadata_cache_store(
    const DlumaWorkshopMenuItem* item,
    const WCHAR* final_project_root,
    const DlumaWorkshopLocalFileStamp* project,
    const DlumaWorkshopLocalFileStamp* description,
    const DlumaWorkshopLocalFileStamp* preview,
    const DlumaWorkshopLocalFileStamp* data,
    const DlumaWorkshopLocalFileStamp* maps,
    unsigned char subscribed)
{
    DlumaWorkshopLocalMetadataCacheEntry* entry;
    if (!item || !final_project_root || !project ||
        !description || !preview || !data || !maps)
        return;
    entry = dluma_workshop_local_metadata_cache_slot();
    memset(entry, 0, sizeof(*entry));
    entry->item = *item;
    dluma_wstr_copy(
        entry->final_project_root,
        DLUMA_ARRAY_COUNT(entry->final_project_root),
        final_project_root);
    entry->project = *project;
    entry->description = *description;
    entry->preview = *preview;
    entry->data = *data;
    entry->maps = *maps;
    entry->subscribed = subscribed;
    entry->last_used =
        ++g_dluma_workshop_local_metadata_cache_clock;
    entry->occupied = 1;
}

static unsigned int dluma_workshop_collect_project(
    DlumaWorkshopMenuItem* items, unsigned int count,
    const WCHAR* project, int include_empty_project)
{
    WCHAR desc_path[DLUMA_MAX_WPATH];
    WCHAR data_path[DLUMA_MAX_WPATH];
    WCHAR maps_path[DLUMA_MAX_WPATH];
    WCHAR source_preview[DLUMA_MAX_WPATH];
    WCHAR final_project_root[DLUMA_MAX_WPATH];
    DlumaWorkshopLocalFileStamp project_stamp;
    DlumaWorkshopLocalFileStamp description_stamp;
    DlumaWorkshopLocalFileStamp preview_stamp;
    DlumaWorkshopLocalFileStamp data_stamp;
    DlumaWorkshopLocalFileStamp maps_stamp;
    DlumaWorkshopLocalMetadataCacheEntry* cached;
    char* text = 0;
    DWORD size = 0;
    DlumaWorkshopMenuItem* item;
    unsigned char subscribed;
    if (!items || !project || !project[0] ||
        count >= DLUMA_WORKSHOP_MENU_MAX_PROJECTS ||
        dluma_workshop_project_already_added(items, count, project))
        return count;
    dluma_path_join(desc_path, DLUMA_ARRAY_COUNT(desc_path), project, L"desc.scr");
    dluma_path_join(data_path, DLUMA_ARRAY_COUNT(data_path), project, L"data");
    dluma_path_join(
        maps_path, DLUMA_ARRAY_COUNT(maps_path), project, L"data\\maps");
    if (!include_empty_project &&
        !dluma_path_exists(desc_path) && !dluma_dir_exists(data_path))
        return count;
    dluma_path_join(
        source_preview, DLUMA_ARRAY_COUNT(source_preview),
        project, L"out\\preview.png");
    subscribed = (unsigned char)
        (dluma_workshop_project_is_subscribed(project) ? 1 : 0);
    if (!dluma_workshop_local_stamp(
            project, 1, &project_stamp,
            final_project_root,
            DLUMA_ARRAY_COUNT(final_project_root)) ||
        !dluma_workshop_local_stamp(
            desc_path, 0, &description_stamp, 0, 0) ||
        !dluma_workshop_local_stamp(
            source_preview, 0, &preview_stamp, 0, 0))
        return count;
    dluma_workshop_local_optional_directory_stamp(data_path, &data_stamp);
    dluma_workshop_local_optional_directory_stamp(maps_path, &maps_stamp);
    cached = dluma_workshop_local_metadata_cache_find(
        final_project_root, &project_stamp, &description_stamp,
        &preview_stamp, &data_stamp, &maps_stamp, subscribed);
    if (cached) {
        items[count] = cached->item;
        dluma_wstr_copy(
            items[count].project_root,
            DLUMA_ARRAY_COUNT(items[count].project_root), project);
        cached->last_used =
            ++g_dluma_workshop_local_metadata_cache_clock;
        return count + 1U;
    }

    item = &items[count];
    memset(item, 0, sizeof(*item));
    dluma_wstr_copy(item->project_root, DLUMA_ARRAY_COUNT(item->project_root), project);
    item->subscribed = subscribed;
    item->exact_project = 1;
    item->has_data_maps = (unsigned char)(
        data_stamp.present && maps_stamp.present ? 1 : 0);
    if (dluma_read_file_utf8(desc_path, &text, &size) && text)
    {
        int fields = dluma_workshop_menu_parse_desc_text(
            text, size,
            item->title, DLUMA_ARRAY_COUNT(item->title),
            item->description, DLUMA_ARRAY_COUNT(item->description),
            item->workshop_id, DLUMA_ARRAY_COUNT(item->workshop_id));
        item->title_from_desc = (unsigned char)((fields & 1) ? 1 : 0);
        item->description_from_desc =
            (unsigned char)((fields & 2) ? 1 : 0);
        dluma_free(text);
    }
    /* Subscribed folders are named by PublishedFileId.  This exact path
     * identity is a safer key than a possibly absent desc.scr id and lets the
     * native remote row match metadata without guessing by folder title. */
    if (item->subscribed && !item->workshop_id[0])
        (void)dluma_workshop_wide_decimal_id(
            dluma_workshop_leaf(project), item->workshop_id,
            DLUMA_ARRAY_COUNT(item->workshop_id));
    if (!item->title[0] &&
        !dluma_wide_to_utf8(
            dluma_workshop_leaf(project), item->title,
            DLUMA_ARRAY_COUNT(item->title)))
        (void)dluma_text_get_utf8("DLUMA_Workshop_UnnamedMap", "UNNAMED MAP",
                                item->title, DLUMA_ARRAY_COUNT(item->title));
    if (!item->title[0])
        (void)dluma_text_get_utf8("DLUMA_Workshop_UnnamedMap", "UNNAMED MAP",
                                item->title, DLUMA_ARRAY_COUNT(item->title));
    if (!item->description[0])
        (void)dluma_text_get_utf8("DLUMA_Common_NoDescription",
                                "NO DESCRIPTION PROVIDED", item->description,
                                DLUMA_ARRAY_COUNT(item->description));
    if (!item->subscribed)
    {
        if (preview_stamp.present &&
            dluma_workshop_prepare_local_preview(
                source_preview, item->preview_path,
                DLUMA_ARRAY_COUNT(item->preview_path)))
            item->preview_available = 1;
    }
    {
        DlumaWorkshopLocalFileStamp project_after;
        DlumaWorkshopLocalFileStamp description_after;
        DlumaWorkshopLocalFileStamp preview_after;
        DlumaWorkshopLocalFileStamp data_after;
        DlumaWorkshopLocalFileStamp maps_after;
        WCHAR final_project_after[DLUMA_MAX_WPATH];
        if (dluma_workshop_local_stamp(
                project, 1, &project_after,
                final_project_after,
                DLUMA_ARRAY_COUNT(final_project_after)) &&
            dluma_workshop_local_stamp(
                desc_path, 0, &description_after, 0, 0) &&
            dluma_workshop_local_stamp(
                source_preview, 0, &preview_after, 0, 0) &&
            dluma_wstricmp(
                final_project_root, final_project_after) == 0 &&
            dluma_workshop_local_stamp_equal(
                &project_stamp, &project_after) &&
            dluma_workshop_local_stamp_equal(
                &description_stamp, &description_after) &&
            dluma_workshop_local_stamp_equal(
                &preview_stamp, &preview_after))
        {
            dluma_workshop_local_optional_directory_stamp(
                data_path, &data_after);
            dluma_workshop_local_optional_directory_stamp(
                maps_path, &maps_after);
            if (dluma_workshop_local_stamp_equal(
                    &data_stamp, &data_after) &&
                dluma_workshop_local_stamp_equal(
                    &maps_stamp, &maps_after))
                dluma_workshop_local_metadata_cache_store(
                    item, final_project_root, &project_stamp,
                    &description_stamp, &preview_stamp, &data_stamp,
                    &maps_stamp, subscribed);
        }
    }
    return count + 1U;
}

void dluma_workshop_menu_refresh_metadata(void)
{
    DlumaWorkshopMenuItem* items;
    unsigned int count = 0;
    unsigned int root_index;
    items = (DlumaWorkshopMenuItem*)dluma_alloc(
        sizeof(DlumaWorkshopMenuItem) * DLUMA_WORKSHOP_MENU_MAX_PROJECTS);
    if (!items)
        return;
    memset(items, 0,
           sizeof(DlumaWorkshopMenuItem) * DLUMA_WORKSHOP_MENU_MAX_PROJECTS);
    for (root_index = 0;
         root_index < g_dluma.workshop_root_count &&
         count < DLUMA_WORKSHOP_MENU_MAX_PROJECTS;
         ++root_index)
    {
        WCHAR pattern[DLUMA_MAX_WPATH];
        WIN32_FIND_DATAW data;
        HANDLE find;
        const WCHAR* root = g_dluma.workshop_roots[root_index];
        count = dluma_workshop_collect_project(items, count, root, 0);
        dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*");
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            WCHAR candidate[DLUMA_MAX_WPATH];
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                data.cFileName[0] == L'.')
                continue;
            dluma_path_join(candidate, DLUMA_ARRAY_COUNT(candidate),
                          root, data.cFileName);
            count = dluma_workshop_collect_project(
                items, count, candidate, 1);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    dluma_atomic_increment(&g_dluma_workshop_metadata_sequence);
    memcpy(g_dluma_workshop_metadata, items,
           sizeof(DlumaWorkshopMenuItem) * count);
    if (count < DLUMA_WORKSHOP_MENU_MAX_PROJECTS)
        memset(&g_dluma_workshop_metadata[count], 0,
               sizeof(DlumaWorkshopMenuItem) *
                   (DLUMA_WORKSHOP_MENU_MAX_PROJECTS - count));
    g_dluma_workshop_metadata_count = count;
    dluma_atomic_increment(&g_dluma_workshop_metadata_sequence);
    dluma_free(items);
}

int dluma_workshop_menu_find_metadata(
    const char* workshop_id, const char* native_title, int subscribed,
    DlumaWorkshopMenuItem* out)
{
    unsigned int attempt;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    for (attempt = 0; attempt < 4U; ++attempt)
    {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_workshop_metadata_sequence, 0, 0);
        unsigned int count;
        unsigned int i;
        unsigned int matches = 0;
        DlumaWorkshopMenuItem candidate;
        if (before & 1)
            continue;
        count = g_dluma_workshop_metadata_count;
        if (count > DLUMA_WORKSHOP_MENU_MAX_PROJECTS)
            count = DLUMA_WORKSHOP_MENU_MAX_PROJECTS;
        memset(&candidate, 0, sizeof(candidate));
        for (i = 0; i < count; ++i)
        {
            const DlumaWorkshopMenuItem* item = &g_dluma_workshop_metadata[i];
            int match = 0;
            if ((item->subscribed != 0) != (subscribed != 0))
                continue;
            if (workshop_id && workshop_id[0] && item->workshop_id[0])
                match = dluma_stricmp(workshop_id, item->workshop_id) == 0;
            else if (native_title && native_title[0])
                match = dluma_stricmp(native_title, item->title) == 0;
            if (match)
            {
                candidate = *item;
                ++matches;
            }
        }
        if (before == dluma_atomic_compare_exchange(
                          &g_dluma_workshop_metadata_sequence, 0, 0) &&
            !(before & 1))
        {
            if (matches == 1U)
            {
                *out = candidate;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

int dluma_workshop_menu_find_metadata_for_project(
    const WCHAR* project_root, DlumaWorkshopMenuItem* out)
{
    unsigned int attempt;
    if (!project_root || !project_root[0] || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    for (attempt = 0; attempt < 4U; ++attempt)
    {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_workshop_metadata_sequence, 0, 0);
        unsigned int count;
        unsigned int i;
        unsigned int matches = 0;
        DlumaWorkshopMenuItem candidate;
        if (before & 1)
            continue;
        count = g_dluma_workshop_metadata_count;
        if (count > DLUMA_WORKSHOP_MENU_MAX_PROJECTS)
            count = DLUMA_WORKSHOP_MENU_MAX_PROJECTS;
        memset(&candidate, 0, sizeof(candidate));
        for (i = 0; i < count; ++i)
        {
            const DlumaWorkshopMenuItem* item = &g_dluma_workshop_metadata[i];
            if (dluma_wstricmp(project_root, item->project_root) == 0)
            {
                candidate = *item;
                ++matches;
            }
        }
        /* The exact-project owner normally carries the same discovered root
         * spelling. If a validated handle-derived canonical form differs,
         * compare copied candidates by directory identity. Never fall back to
         * map name or title. */
        if (!matches)
        {
            for (i = 0; i < count; ++i)
            {
                DlumaWorkshopMenuItem item;
                if (before != dluma_atomic_compare_exchange(
                                  &g_dluma_workshop_metadata_sequence, 0, 0))
                    break;
                item = g_dluma_workshop_metadata[i];
                if (before != dluma_atomic_compare_exchange(
                                  &g_dluma_workshop_metadata_sequence, 0, 0))
                    break;
                if (dluma_project_roots_same_identity(
                        project_root, item.project_root))
                {
                    candidate = item;
                    ++matches;
                }
            }
        }
        if (before == dluma_atomic_compare_exchange(
                          &g_dluma_workshop_metadata_sequence, 0, 0) &&
            !(before & 1))
        {
            if (matches == 1U)
            {
                *out = candidate;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

static int dluma_workshop_parse_u64(
    const char* text, ULONGLONG* value_out)
{
    return dluma_workshop_decimal_u64(text, value_out);
}

static DlumaWorkshopMenuRemoteState* dluma_workshop_remote_for_id(
    ULONGLONG published_id, int create)
{
    unsigned int i;
    for (i = 0; i < g_dluma_workshop_remote_count; ++i)
        if (g_dluma_workshop_remote[i].published_id == published_id)
            return &g_dluma_workshop_remote[i];
    if (!create || !published_id ||
        g_dluma_workshop_remote_count >= DLUMA_WORKSHOP_MENU_MAX_REMOTE)
        return 0;
    i = g_dluma_workshop_remote_count++;
    memset(&g_dluma_workshop_remote[i], 0,
           sizeof(g_dluma_workshop_remote[i]));
    g_dluma_workshop_remote[i].published_id = published_id;
    return &g_dluma_workshop_remote[i];
}

void dluma_workshop_menu_request_remote(const char* workshop_id)
{
    ULONGLONG value;
    DlumaWorkshopMenuRemoteState* state;
    if (!dluma_workshop_parse_u64(workshop_id, &value))
        return;
    state = dluma_workshop_remote_for_id(value, 1);
    if (state)
    {
        state->requested = 1;
        dluma_workshop_apply_cache_to_state(state);
    }
}

int dluma_workshop_menu_get_remote(
    const char* workshop_id, DlumaWorkshopMenuRemoteItem* out)
{
    ULONGLONG value;
    DlumaWorkshopMenuRemoteState* state;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!dluma_workshop_parse_u64(workshop_id, &value))
        return 0;
    state = dluma_workshop_remote_for_id(value, 0);
    if (!state)
        return 0;
    dluma_u64_to_text(value, out->workshop_id,
                    DLUMA_ARRAY_COUNT(out->workshop_id));
    dluma_str_copy(out->title, DLUMA_ARRAY_COUNT(out->title), state->title);
    dluma_str_copy(out->description, DLUMA_ARRAY_COUNT(out->description),
                 state->description);
    dluma_str_copy(out->author, DLUMA_ARRAY_COUNT(out->author), state->author);
    dluma_str_copy(out->preview_url, DLUMA_ARRAY_COUNT(out->preview_url),
                 state->preview_url);
    dluma_wstr_copy(out->preview_path, DLUMA_ARRAY_COUNT(out->preview_path),
                  state->preview_path);
    out->updated_time = state->updated_time;
    out->details_ready = state->details_ready;
    out->author_ready = state->author_ready;
    out->preview_ready = state->preview_ready;
    out->failed = (unsigned char)
        (state->details_failed && state->author_failed &&
         state->preview_failed && !state->preview_ready);
    return 1;
}

int dluma_workshop_menu_get_loading_metadata(
    const WCHAR* project_root, const char* workshop_id,
    DlumaWorkshopLoadingMetadata* out)
{
    DlumaWorkshopMenuItem local;
    DlumaWorkshopMenuRemoteItem remote = {0};
    ULONGLONG parsed_id = 0;
    int has_local;
    int has_remote = 0;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!project_root || !project_root[0])
        return 0;
    dluma_wstr_copy(out->project_root, DLUMA_ARRAY_COUNT(out->project_root),
                  project_root);
    has_local = dluma_workshop_menu_find_metadata_for_project(
        project_root, &local);
    if (has_local)
    {
        out->exact_project = local.exact_project;
        if (local.workshop_id[0])
            dluma_str_copy(out->workshop_id,
                         DLUMA_ARRAY_COUNT(out->workshop_id),
                         local.workshop_id);
        if (local.title_from_desc)
        {
            dluma_str_copy(out->title, DLUMA_ARRAY_COUNT(out->title),
                         local.title);
            out->title_authoritative = 1;
        }
        if (local.description_from_desc)
        {
            dluma_str_copy(out->description,
                         DLUMA_ARRAY_COUNT(out->description),
                         local.description);
            out->description_authoritative = 1;
        }
    }
    if (workshop_id && workshop_id[0] &&
        dluma_workshop_parse_u64(workshop_id, &parsed_id))
        dluma_str_copy(out->workshop_id,
                     DLUMA_ARRAY_COUNT(out->workshop_id), workshop_id);
    else
        parsed_id = 0;
    if (!parsed_id && out->workshop_id[0])
        (void)dluma_workshop_parse_u64(out->workshop_id, &parsed_id);
    out->published = parsed_id ? 1U : 0U;
    out->local_project = parsed_id ? 0U : 1U;
    if (parsed_id)
        has_remote = dluma_workshop_menu_get_remote(
            out->workshop_id, &remote);
    if (has_remote && remote.details_ready)
    {
        out->details_ready = 1;
        out->description_authoritative = 1;
        dluma_str_copy(out->description,
                     DLUMA_ARRAY_COUNT(out->description),
                     remote.description);
        if (remote.title[0])
        {
            dluma_str_copy(out->title, DLUMA_ARRAY_COUNT(out->title),
                         remote.title);
            out->title_authoritative = 1;
        }
    }
    if (has_remote && remote.author_ready && remote.author[0])
    {
        dluma_str_copy(out->author, DLUMA_ARRAY_COUNT(out->author),
                     remote.author);
        out->author_ready = 1;
    }
    if (parsed_id)
    {
        if (has_remote && remote.preview_ready &&
            remote.preview_path[0])
        {
            dluma_wstr_copy(out->preview_path,
                          DLUMA_ARRAY_COUNT(out->preview_path),
                          remote.preview_path);
            out->preview_ready = 1;
        }
    }
    else if (has_local && local.preview_available &&
             local.preview_path[0])
    {
        dluma_wstr_copy(out->preview_path,
                      DLUMA_ARRAY_COUNT(out->preview_path),
                      local.preview_path);
        out->preview_ready = 1;
    }
    if (!out->title[0] && has_local)
    {
        dluma_str_copy(out->title, DLUMA_ARRAY_COUNT(out->title),
                     local.title);
        out->title_authoritative = local.title[0] ? 1U : 0U;
    }
    return 1;
}

static int dluma_workshop_module_identity(
    HMODULE module, DWORD* timestamp_out, DWORD* image_size_out)
{
    const BYTE* base = (const BYTE*)module;
    DWORD pe_offset;
    if (timestamp_out) *timestamp_out = 0;
    if (image_size_out) *image_size_out = 0;
    if (!base || !dluma_is_readable(base, 0x40) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    memcpy(&pe_offset, base + 0x3c, sizeof(pe_offset));
    if (pe_offset > 0x100000U ||
        !dluma_is_readable(base + pe_offset, 0x54) ||
        base[pe_offset] != 'P' || base[pe_offset + 1] != 'E')
        return 0;
    if (timestamp_out)
        memcpy(timestamp_out, base + pe_offset + 8, sizeof(DWORD));
    if (image_size_out)
        memcpy(image_size_out, base + pe_offset + 0x50, sizeof(DWORD));
    return 1;
}

static int dluma_workshop_validate_steam_call_results(
    DlumaWorkshopMenuSteamApi* api)
{
    static const BYTE set_ugc_prefix[] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20
    };
    static const BYTE remove_ugc_prefix[] = {
        0x48, 0x85, 0xc9, 0x74, 0x35, 0x53, 0x48, 0x83,
        0xec, 0x20, 0x48, 0x8b, 0x51, 0x10
    };
    static const BYTE register_prefix[] = {
        0xe9, 0x3b, 0xd5, 0xff, 0xff
    };
    static const BYTE unregister_prefix[] = {
        0xe9, 0x5b, 0xd0, 0xff, 0xff
    };
    if (!api || !api->set_ugc_call_result ||
        !api->remove_ugc_call_result || !api->register_call_result ||
        !api->unregister_call_result ||
        !dluma_is_readable((const void*)api->set_ugc_call_result,
                         sizeof(set_ugc_prefix)) ||
        memcmp((const void*)api->set_ugc_call_result, set_ugc_prefix,
               sizeof(set_ugc_prefix)) != 0 ||
        !dluma_is_readable((const void*)api->remove_ugc_call_result,
                         sizeof(remove_ugc_prefix)) ||
        memcmp((const void*)api->remove_ugc_call_result,
               remove_ugc_prefix, sizeof(remove_ugc_prefix)) != 0 ||
        !dluma_is_readable((const void*)api->register_call_result,
                         sizeof(register_prefix)) ||
        memcmp((const void*)api->register_call_result, register_prefix,
               sizeof(register_prefix)) != 0 ||
        !dluma_is_readable((const void*)api->unregister_call_result,
                         sizeof(unregister_prefix)) ||
        memcmp((const void*)api->unregister_call_result,
               unregister_prefix, sizeof(unregister_prefix)) != 0)
        return 0;
    return 1;
}

static void dluma_workshop_log_steam_status(const char* status)
{
    if (dluma_atomic_compare_exchange(
            &g_dluma_workshop_steam_status_logged, 1, 0) == 0)
        dluma_log_pair(status && dluma_stricmp(status, "ready") == 0
                         ? "INFO" : "WARN",
                     "workshop_steam_api_status",
                     status ? status : "unknown");
}

static int dluma_workshop_resolve_steam(void)
{
    DlumaSteamAccessorFn accessor;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    if (api->attempted)
        return api->ready != 0;
    api->attempted = 1;
    api->module = GetModuleHandleW(L"steam_api64.dll");
    if (!api->module ||
        !dluma_workshop_module_identity(
            api->module, &timestamp, &image_size) ||
        timestamp != DLUMA_STEAM_API_TIMESTAMP ||
        image_size != DLUMA_STEAM_API_IMAGE_SIZE)
    {
        dluma_log_pair("WARN", "workshop_steam_details_api", "unavailable");
        dluma_log_pair("WARN", "workshop_steam_persona_api", "unavailable");
        dluma_log_pair("WARN", "workshop_steam_preview_api", "cache_only");
        dluma_workshop_log_steam_status(
            api->module ? "unsupported_identity_cache_only"
                        : "module_unavailable_cache_only");
        return 0;
    }
#define DLUMA_STEAM_PROC(member, type, name) \
    api->member = (type)GetProcAddress(api->module, name)
    DLUMA_STEAM_PROC(create_details_query, DlumaSteamCreateDetailsQueryFn,
                   "SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest");
    DLUMA_STEAM_PROC(send_query, DlumaSteamSendQueryFn,
                   "SteamAPI_ISteamUGC_SendQueryUGCRequest");
    DLUMA_STEAM_PROC(get_query_result, DlumaSteamGetQueryResultFn,
                   "SteamAPI_ISteamUGC_GetQueryUGCResult");
    DLUMA_STEAM_PROC(get_preview_url, DlumaSteamGetPreviewUrlFn,
                   "SteamAPI_ISteamUGC_GetQueryUGCPreviewURL");
    DLUMA_STEAM_PROC(release_query, DlumaSteamReleaseQueryFn,
                   "SteamAPI_ISteamUGC_ReleaseQueryUGCRequest");
    DLUMA_STEAM_PROC(set_ugc_call_result, DlumaSteamSetUgcCallResultFn,
                   "CSteamUGCQueryCompleted_t_SetCallResult");
    DLUMA_STEAM_PROC(remove_ugc_call_result,
                   DlumaSteamRemoveUgcCallResultFn,
                   "CSteamUGCQueryCompleted_t_RemoveCallResult");
    DLUMA_STEAM_PROC(register_call_result,
                   DlumaSteamRegisterCallResultFn,
                   "SteamAPI_RegisterCallResult");
    DLUMA_STEAM_PROC(unregister_call_result,
                   DlumaSteamUnregisterCallResultFn,
                   "SteamAPI_UnregisterCallResult");
    DLUMA_STEAM_PROC(get_persona_name, DlumaSteamGetPersonaNameFn,
                   "SteamAPI_ISteamFriends_GetFriendPersonaName");
    DLUMA_STEAM_PROC(request_user_information,
                   DlumaSteamRequestUserInformationFn,
                   "SteamAPI_ISteamFriends_RequestUserInformation");
    DLUMA_STEAM_PROC(create_http_request, DlumaSteamCreateHttpRequestFn,
                   "SteamAPI_ISteamHTTP_CreateHTTPRequest");
    DLUMA_STEAM_PROC(send_http_request, DlumaSteamSendHttpRequestFn,
                   "SteamAPI_ISteamHTTP_SendHTTPRequest");
    DLUMA_STEAM_PROC(get_http_body_size, DlumaSteamGetHttpBodySizeFn,
                   "SteamAPI_ISteamHTTP_GetHTTPResponseBodySize");
    DLUMA_STEAM_PROC(get_http_body_data, DlumaSteamGetHttpBodyDataFn,
                   "SteamAPI_ISteamHTTP_GetHTTPResponseBodyData");
    DLUMA_STEAM_PROC(release_http_request, DlumaSteamReleaseHttpRequestFn,
                   "SteamAPI_ISteamHTTP_ReleaseHTTPRequest");
#undef DLUMA_STEAM_PROC
    accessor = (DlumaSteamAccessorFn)GetProcAddress(api->module, "SteamUGC");
    api->ugc = accessor ? accessor() : 0;
    accessor = (DlumaSteamAccessorFn)GetProcAddress(api->module, "SteamFriends");
    api->friends = accessor ? accessor() : 0;
    accessor = (DlumaSteamAccessorFn)GetProcAddress(api->module, "SteamHTTP");
    api->http = accessor ? accessor() : 0;
    api->details_available = (unsigned char)(
        api->ugc && api->create_details_query &&
        api->send_query && api->get_query_result && api->release_query &&
        api->set_ugc_call_result && api->remove_ugc_call_result &&
        dluma_workshop_validate_steam_call_results(api));
    api->author_available = (unsigned char)(
        api->friends && api->get_persona_name &&
        api->request_user_information);
    api->preview_available = (unsigned char)(
        api->details_available && api->get_preview_url && api->http &&
        api->create_http_request && api->send_http_request &&
        api->get_http_body_size && api->get_http_body_data &&
        api->release_http_request && api->register_call_result &&
        api->unregister_call_result);
    api->ready = api->details_available;
    dluma_log_pair(api->details_available ? "INFO" : "WARN",
                 "workshop_steam_details_api",
                 api->details_available ? "ready" : "unavailable");
    dluma_log_pair(api->author_available ? "INFO" : "WARN",
                 "workshop_steam_persona_api",
                 api->author_available ? "ready" : "unavailable");
    dluma_log_pair(api->preview_available ? "INFO" : "WARN",
                 "workshop_steam_preview_api",
                 api->preview_available ? "ready" : "cache_only");
    if (dluma_atomic_compare_exchange(
            &g_dluma_workshop_callback_adapter_status_logged, 1, 0) == 0)
        dluma_log_pair(api->details_available ? "INFO" : "WARN",
                     "workshop_steam_call_result_adapter",
                     api->details_available
                         ? "validated_registered_callbacks"
                         : "validation_failed_cache_only");
    dluma_workshop_log_steam_status(
        api->details_available ? "ready" : "details_unavailable_cache_only");
    return api->details_available != 0;
}

static void dluma_workshop_build_cache_path(
    WCHAR* out, unsigned int capacity,
    ULONGLONG published_id, const WCHAR* extension)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR leaf[128];
    char id_text[32];
    dluma_u64_to_text(published_id, id_text, DLUMA_ARRAY_COUNT(id_text));
    if (!dluma_utf8_to_wide(id_text, leaf, DLUMA_ARRAY_COUNT(leaf)))
    {
        if (out && capacity) out[0] = 0;
        return;
    }
    dluma_wstr_append(leaf, DLUMA_ARRAY_COUNT(leaf), extension);
    dluma_path_join(directory, DLUMA_ARRAY_COUNT(directory),
                  g_dluma.module_dir, L"dluma_runtime\\workshop_thumbnails");
    dluma_path_join(out, capacity, directory, leaf);
}

static int dluma_workshop_build_hashed_dds_path(
    WCHAR* out, unsigned int capacity,
    ULONGLONG published_id, const char* sha256)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR leaf[160];
    WCHAR id_wide[32];
    WCHAR digest_wide[24];
    char id_text[32];
    unsigned int i;
    if (out && capacity)
        out[0] = 0;
    if (!out || !capacity || !published_id || !sha256 ||
        !sha256[0])
        return 0;
    dluma_u64_to_text(
        published_id, id_text, DLUMA_ARRAY_COUNT(id_text));
    if (!dluma_utf8_to_wide(
            id_text, id_wide, DLUMA_ARRAY_COUNT(id_wide)))
        return 0;
    for (i = 0; i < 16U && sha256[i]; ++i)
        digest_wide[i] = (WCHAR)(unsigned char)sha256[i];
    digest_wide[i] = 0;
    if (i != 16U)
        return 0;
    dluma_wstr_copy(leaf, DLUMA_ARRAY_COUNT(leaf), id_wide);
    dluma_wstr_append(leaf, DLUMA_ARRAY_COUNT(leaf), L"_");
    dluma_wstr_append(leaf, DLUMA_ARRAY_COUNT(leaf), digest_wide);
    if (!dluma_workshop_append_dds_generation_suffix(
            leaf, DLUMA_ARRAY_COUNT(leaf)))
        return 0;
    dluma_path_join(
        directory, DLUMA_ARRAY_COUNT(directory),
        g_dluma.module_dir,
        L"dluma_runtime\\workshop_thumbnails");
    dluma_path_join(out, capacity, directory, leaf);
    return out[0] != 0;
}

static int dluma_workshop_cache_snapshot_find(
    ULONGLONG published_id, DlumaWorkshopMenuCacheEntry* out)
{
    unsigned int attempt;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!published_id || !out)
        return 0;
    for (attempt = 0; attempt < 4U; ++attempt)
    {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_workshop_cache_sequence, 0, 0);
        unsigned int count;
        unsigned int i;
        if (!before)
            return -1;
        if (before & 1)
            continue;
        count = g_dluma_workshop_cache_count;
        if (count > DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES)
            count = DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES;
        for (i = 0; i < count; ++i)
            if (g_dluma_workshop_cache[i].published_id == published_id)
            {
                *out = g_dluma_workshop_cache[i];
                break;
            }
        if (before == dluma_atomic_compare_exchange(
                          &g_dluma_workshop_cache_sequence, 0, 0) &&
            !(before & 1))
            return i < count ? 1 : 0;
    }
    return -1;
}

static void dluma_workshop_apply_cache_to_state(
    DlumaWorkshopMenuRemoteState* state)
{
    DlumaWorkshopMenuCacheEntry entry;
    int found;
    if (!state || !state->published_id)
        return;
    found = dluma_workshop_cache_snapshot_find(state->published_id, &entry);
    if (found > 0 && entry.image_path[0])
    {
        dluma_wstr_copy(state->preview_path,
                      DLUMA_ARRAY_COUNT(state->preview_path),
                      entry.image_path);
        state->preview_ready = 1;
        state->cache_updated_time = entry.updated_time;
        if (!state->cache_status_logged)
        {
            state->cache_status_logged = 1;
            dluma_log_u64("INFO", "workshop_preview_cache_hit",
                        state->published_id);
        }
    }
    else if (found == 0 && !state->cache_status_logged)
    {
        state->cache_status_logged = 1;
        dluma_log_u64("INFO", "workshop_preview_cache_miss",
                    state->published_id);
    }
}

static void dluma_workshop_apply_cache_snapshot(void)
{
    unsigned int i;
    for (i = 0; i < g_dluma_workshop_remote_count; ++i)
        if (g_dluma_workshop_remote[i].requested)
            dluma_workshop_apply_cache_to_state(&g_dluma_workshop_remote[i]);
}

static int dluma_workshop_query_contains(ULONGLONG published_id)
{
    return dluma_workshop_enrichment_batch_contains(
        g_dluma_workshop_query_ids,
        g_dluma_workshop_query_count,
        published_id);
}

static void dluma_workshop_mark_query_failure(
    ULONGLONG now, const char* reason)
{
    unsigned int i;
    dluma_log_pair("WARN", "workshop_query_result",
                 reason ? reason : "failed");
    for (i = 0; i < g_dluma_workshop_query_count; ++i)
    {
        DlumaWorkshopMenuRemoteState* state =
            dluma_workshop_remote_for_id(g_dluma_workshop_query_ids[i], 0);
        if (!state || state->details_ready)
            continue;
        if (state->details_attempts >=
            DLUMA_WORKSHOP_MENU_DETAILS_MAX_ATTEMPTS)
        {
            state->details_failed = 1;
            state->author_failed = 1;
        }
        else
        {
            state->details_retry_after_ms =
                now + DLUMA_WORKSHOP_MENU_RETRY_DELAY_MS;
        }
    }
}

static void dluma_workshop_start_query(void)
{
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    ULONGLONG now = GetTickCount64();
    unsigned int i;
    if (g_dluma_workshop_query_call ||
        g_dluma_workshop_query_handle != ~0ULL)
        return;
    g_dluma_workshop_query_count = 0;
    for (i = 0; i < g_dluma_workshop_remote_count &&
                g_dluma_workshop_query_count < DLUMA_WORKSHOP_MENU_QUERY_BATCH;
         ++i)
    {
        DlumaWorkshopMenuRemoteState* state = &g_dluma_workshop_remote[i];
        if (state->requested && !state->details_ready &&
            !state->details_failed &&
            now >= state->details_retry_after_ms)
            g_dluma_workshop_query_ids[g_dluma_workshop_query_count++] =
                state->published_id;
    }
    if (!g_dluma_workshop_query_count)
        return;
    for (i = 0; i < g_dluma_workshop_query_count; ++i)
    {
        DlumaWorkshopMenuRemoteState* state =
            dluma_workshop_remote_for_id(g_dluma_workshop_query_ids[i], 0);
        if (state && state->details_attempts < 255U)
            ++state->details_attempts;
    }
    dluma_log_u64("INFO", "workshop_query_batch_count",
                g_dluma_workshop_query_count);
    g_dluma_workshop_query_handle = api->create_details_query(
        api->ugc, g_dluma_workshop_query_ids, g_dluma_workshop_query_count);
    if (g_dluma_workshop_query_handle == ~0ULL)
    {
        dluma_workshop_mark_query_failure(now, "create_failed");
        g_dluma_workshop_query_count = 0;
        return;
    }
    g_dluma_workshop_query_call = api->send_query(
        api->ugc, g_dluma_workshop_query_handle);
    if (!g_dluma_workshop_query_call)
    {
        api->release_query(api->ugc, g_dluma_workshop_query_handle);
        g_dluma_workshop_query_handle = ~0ULL;
        dluma_workshop_mark_query_failure(now, "send_failed");
        g_dluma_workshop_query_count = 0;
        return;
    }
    memset(&g_dluma_workshop_query_completion, 0,
           sizeof(g_dluma_workshop_query_completion));
    dluma_atomic_exchange(&g_dluma_workshop_query_completion_failed, 0);
    dluma_atomic_exchange(&g_dluma_workshop_query_completion_ready, 0);
    g_dluma_workshop_query_call_result = api->set_ugc_call_result(
        g_dluma_workshop_query_call,
        dluma_workshop_ugc_call_result_callback);
    if (!g_dluma_workshop_query_call_result)
    {
        api->release_query(api->ugc, g_dluma_workshop_query_handle);
        g_dluma_workshop_query_handle = ~0ULL;
        g_dluma_workshop_query_call = 0;
        dluma_workshop_mark_query_failure(
            now, "call_result_registration_failed");
        g_dluma_workshop_query_count = 0;
        return;
    }
    dluma_log_pair("INFO", "workshop_query_completion_mode",
                 "registered_call_result");
}

static void dluma_workshop_finish_query(void)
{
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    DlumaSteamUgcQueryCompleted completed;
    unsigned char accepted[DLUMA_WORKSHOP_MENU_QUERY_BATCH];
    unsigned char failed = 0;
    ULONGLONG now = GetTickCount64();
    unsigned int i;
    if (!g_dluma_workshop_query_call)
        return;
    if (dluma_atomic_compare_exchange(
            &g_dluma_workshop_query_completion_ready, 0, 0) == 0)
        return;
    completed = g_dluma_workshop_query_completion;
    failed = (unsigned char)(dluma_atomic_compare_exchange(
        &g_dluma_workshop_query_completion_failed, 0, 0) != 0);
    if (g_dluma_workshop_query_call_result)
    {
        api->remove_ugc_call_result(
            g_dluma_workshop_query_call_result);
        g_dluma_workshop_query_call_result = 0;
    }
    memset(accepted, 0, sizeof(accepted));
    if (failed || completed.handle != g_dluma_workshop_query_handle ||
        completed.result != 1)
    {
        dluma_workshop_mark_query_failure(now, "completion_failed");
    }
    else
    {
        unsigned int returned = completed.returned;
        if (returned > g_dluma_workshop_query_count)
            returned = g_dluma_workshop_query_count;
        for (i = 0; i < returned; ++i)
        {
            BYTE details[DLUMA_STEAM_UGC_DETAILS_SIZE];
            ULONGLONG published_id = 0;
            ULONGLONG owner_id = 0;
            unsigned int updated = 0;
            DlumaWorkshopMenuRemoteState* state;
            char url[1024];
            unsigned int title_length = 0;
            unsigned int description_length = 0;
            memset(details, 0, sizeof(details));
            url[0] = 0;
            if (!api->get_query_result(
                    api->ugc, g_dluma_workshop_query_handle, i, details))
                continue;
            memcpy(&published_id, details, sizeof(published_id));
            memcpy(&owner_id, details + DLUMA_STEAM_UGC_OWNER_OFFSET,
                   sizeof(owner_id));
            memcpy(&updated, details + DLUMA_STEAM_UGC_UPDATED_OFFSET,
                   sizeof(updated));
            if (!dluma_workshop_query_contains(published_id))
            {
                dluma_log_u64("WARN", "workshop_query_unexpected_item_id",
                            published_id);
                continue;
            }
            state = dluma_workshop_remote_for_id(published_id, 0);
            if (!state)
                continue;
            {
                unsigned int query_index;
                for (query_index = 0;
                     query_index < g_dluma_workshop_query_count;
                     ++query_index)
                    if (g_dluma_workshop_query_ids[query_index] == published_id)
                    {
                        accepted[query_index] = 1;
                        break;
                    }
            }
            while (title_length < 128U && details[24U + title_length])
                ++title_length;
            if (title_length >= DLUMA_ARRAY_COUNT(state->title))
                title_length = DLUMA_ARRAY_COUNT(state->title) - 1U;
            memcpy(state->title, details + 24U, title_length);
            state->title[title_length] = 0;
            dluma_workshop_normalize_title(state->title);
            while (description_length < DLUMA_STEAM_UGC_DESCRIPTION_SIZE &&
                   details[DLUMA_STEAM_UGC_DESCRIPTION_OFFSET +
                           description_length])
                ++description_length;
            state->description[0] = 0;
            if (description_length < DLUMA_STEAM_UGC_DESCRIPTION_SIZE)
            {
                if (!dluma_custom_map_loading_plain_text(
                        (const char*)details +
                            DLUMA_STEAM_UGC_DESCRIPTION_OFFSET,
                        description_length, state->description,
                        DLUMA_ARRAY_COUNT(state->description)))
                    dluma_log_u64(
                        "WARN", "workshop_description_invalid_utf8",
                        published_id);
            }
            else
                dluma_log_u64(
                    "WARN", "workshop_description_unterminated",
                    published_id);
            state->owner_id = owner_id;
            state->updated_time = updated;
            if (api->get_preview_url && api->get_preview_url(
                    api->ugc, g_dluma_workshop_query_handle, i,
                    url, DLUMA_ARRAY_COUNT(url)) &&
                dluma_workshop_preview_url_supported(url))
            {
                dluma_str_copy(state->preview_url,
                             DLUMA_ARRAY_COUNT(state->preview_url), url);
                if (!state->preview_status_logged)
                {
                    state->preview_status_logged = 1;
                    dluma_log_pair("INFO", "workshop_preview_url_status",
                                 dluma_workshop_text_prefix_icase(
                                     url, "https://")
                                     ? "https_accepted" : "http_accepted");
                }
                if (!api->preview_available)
                    state->preview_failed = 1;
            }
            else
            {
                state->preview_failed = 1;
                if (!state->preview_status_logged)
                {
                    state->preview_status_logged = 1;
                    dluma_log_pair("WARN", "workshop_preview_url_status",
                                 "missing_or_unsupported");
                }
            }
            state->details_ready = 1;
            state->details_failed = 0;
            state->details_retry_after_ms = 0;
            if (!state->details_status_logged)
            {
                state->details_status_logged = 1;
                dluma_log_u64("INFO", "workshop_query_item_ready",
                            state->published_id);
            }
            if (state->owner_id && api->author_available)
            {
                (void)api->request_user_information(
                    api->friends, state->owner_id, 1);
                state->author_attempts = 1;
                state->author_started_ms = now;
                dluma_log_u64("INFO", "workshop_author_request_owner",
                            state->owner_id);
            }
            else
            {
                state->author_failed = 1;
                if (state->owner_id && !state->author_status_logged)
                {
                    state->author_status_logged = 1;
                    dluma_log_u64("WARN", "workshop_author_api_unavailable",
                                state->published_id);
                }
            }
        }
        for (i = 0; i < g_dluma_workshop_query_count; ++i)
            if (!accepted[i])
            {
                DlumaWorkshopMenuRemoteState* state =
                    dluma_workshop_remote_for_id(
                        g_dluma_workshop_query_ids[i], 0);
                if (!state || state->details_ready)
                    continue;
                if (state->details_attempts >=
                    DLUMA_WORKSHOP_MENU_DETAILS_MAX_ATTEMPTS)
                {
                    state->details_failed = 1;
                    state->author_failed = 1;
                }
                else
                {
                    state->details_retry_after_ms =
                        now + DLUMA_WORKSHOP_MENU_RETRY_DELAY_MS;
                }
            }
        dluma_log_pair("INFO", "workshop_query_result", "completed");
    }
    api->release_query(api->ugc, g_dluma_workshop_query_handle);
    g_dluma_workshop_query_handle = ~0ULL;
    g_dluma_workshop_query_call = 0;
    g_dluma_workshop_query_count = 0;
    dluma_atomic_exchange(&g_dluma_workshop_query_completion_ready, 0);
    dluma_atomic_exchange(&g_dluma_workshop_query_completion_failed, 0);
    memset(&g_dluma_workshop_query_completion, 0,
           sizeof(g_dluma_workshop_query_completion));
}

static void dluma_workshop_poll_authors(void)
{
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    ULONGLONG now = GetTickCount64();
    unsigned int i;
    if (!api->author_available)
        return;
    for (i = 0; i < g_dluma_workshop_remote_count; ++i)
    {
        DlumaWorkshopMenuRemoteState* state = &g_dluma_workshop_remote[i];
        const char* name;
        if (!state->requested || !state->details_ready ||
            state->author_ready || state->author_failed ||
            !state->owner_id)
            continue;
        name = api->get_persona_name(api->friends, state->owner_id);
        if (name && name[0] &&
            dluma_stricmp(name, "[unknown]") != 0)
        {
            dluma_str_copy(state->author,
                         DLUMA_ARRAY_COUNT(state->author), name);
            state->author_ready = state->author[0] ? 1 : 0;
            if (state->author_ready && !state->author_status_logged)
            {
                state->author_status_logged = 1;
                dluma_log_u64("INFO", "workshop_author_ready",
                            state->published_id);
            }
            continue;
        }
        if (dluma_workshop_enrichment_should_retry_persona(
                api->author_available, state->details_ready,
                state->owner_id != 0, state->author_ready,
                state->author_failed, state->author_attempts,
                DLUMA_WORKSHOP_MENU_PERSONA_MAX_ATTEMPTS,
                state->author_started_ms, now,
                DLUMA_WORKSHOP_MENU_RETRY_DELAY_MS,
                DLUMA_WORKSHOP_MENU_PERSONA_TIMEOUT_MS))
        {
            (void)api->request_user_information(
                api->friends, state->owner_id, 1);
            ++state->author_attempts;
        }
        if (dluma_workshop_enrichment_persona_timed_out(
                state->author_ready, state->author_started_ms, now,
                DLUMA_WORKSHOP_MENU_PERSONA_TIMEOUT_MS))
        {
            state->author_failed = 1;
            if (!state->author_status_logged)
            {
                state->author_status_logged = 1;
                dluma_log_u64("WARN", "workshop_author_unresolved",
                            state->published_id);
            }
        }
    }
}

static int dluma_workshop_enqueue_image(
    DlumaWorkshopMenuRemoteState* state, BYTE* data,
    DWORD size, int png)
{
    unsigned int i;
    for (i = 0; i < DLUMA_WORKSHOP_MENU_IMAGE_JOBS; ++i)
    {
        DlumaWorkshopMenuImageJob* job = &g_dluma_workshop_image_jobs[i];
        if (dluma_atomic_compare_exchange(&job->state, 3, 0) == 0)
        {
            job->data = data;
            job->size = size;
            job->published_id = state->published_id;
            job->updated_time = state->updated_time;
            job->png = (unsigned char)(png ? 1 : 0);
            job->succeeded = 0;
            job->final_path[0] = 0;
            dluma_atomic_exchange(&job->state, 1);
            return 1;
        }
    }
    return 0;
}

static void dluma_workshop_poll_http(void)
{
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    ULONGLONG now = GetTickCount64();
    unsigned int i;
    if (!api->preview_available)
        return;
    for (i = 0; i < DLUMA_WORKSHOP_MENU_HTTP_SLOTS; ++i)
    {
        DlumaWorkshopMenuHttpSlot* slot = &g_dluma_workshop_http[i];
        DlumaSteamHttpCompleted completed;
        DlumaWorkshopMenuRemoteState* state;
        BYTE* body = 0;
        unsigned int body_size = 0;
        int valid_image = 0;
        int png = 0;
        if (!slot->request || !slot->callback.api_call ||
            dluma_atomic_compare_exchange(
                &slot->callback.completed, 0, 0) == 0)
            continue;
        state = dluma_workshop_remote_for_id(slot->published_id, 0);
        completed = slot->callback.result;
        if (slot->registered)
        {
            api->unregister_call_result(
                &slot->callback, slot->callback.api_call);
            slot->registered = 0;
        }
        if (dluma_atomic_compare_exchange(
                &slot->callback.io_failure, 0, 0) == 0 &&
            dluma_atomic_compare_exchange(
                &slot->callback.wrong_call, 0, 0) == 0 &&
            completed.successful &&
            completed.request == slot->request &&
            completed.status_code == DLUMA_STEAM_HTTP_OK &&
            api->get_http_body_size(
                api->http, slot->request, &body_size) &&
            body_size >= 8U && body_size <= DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES)
        {
            body = (BYTE*)dluma_alloc(body_size);
            if (body && api->get_http_body_data(
                            api->http, slot->request, body, body_size))
            {
                png = body_size >= 8U &&
                      body[0] == 0x89U && body[1] == 'P' &&
                      body[2] == 'N' && body[3] == 'G' &&
                      body[4] == 0x0dU && body[5] == 0x0aU &&
                      body[6] == 0x1aU && body[7] == 0x0aU;
                valid_image = png ||
                              (body[0] == 0xffU && body[1] == 0xd8U &&
                               body[body_size - 2U] == 0xffU &&
                               body[body_size - 1U] == 0xd9U);
            }
        }
        if (state && body && valid_image)
        {
            if (dluma_workshop_enqueue_image(state, body, body_size, png))
            {
                body = 0;
                dluma_log_u64("INFO", "workshop_preview_http_downloaded",
                            state->published_id);
            }
            else
            {
                state->download_started = 0;
                if (state->preview_attempts)
                    --state->preview_attempts;
                state->preview_retry_after_ms = now + 1000ULL;
            }
        }
        else if (state)
        {
            state->download_started = 0;
            if (state->preview_attempts >=
                DLUMA_WORKSHOP_MENU_PREVIEW_MAX_ATTEMPTS)
                state->preview_failed = 1;
            else
                state->preview_retry_after_ms =
                    now + DLUMA_WORKSHOP_MENU_RETRY_DELAY_MS;
            dluma_log_u64("WARN", "workshop_preview_http_failed",
                        state->published_id);
        }
        if (body)
            dluma_free(body);
        api->release_http_request(api->http, slot->request);
        memset(slot, 0, sizeof(*slot));
    }
}

static void dluma_workshop_start_http(void)
{
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    ULONGLONG now = GetTickCount64();
    unsigned int slot_index;
    unsigned int remote_index = 0;
    if (!api->preview_available)
        return;
    for (slot_index = 0; slot_index < DLUMA_WORKSHOP_MENU_HTTP_SLOTS;
         ++slot_index)
    {
        DlumaWorkshopMenuHttpSlot* slot = &g_dluma_workshop_http[slot_index];
        if (slot->request)
            continue;
        while (remote_index < g_dluma_workshop_remote_count)
        {
            DlumaWorkshopMenuRemoteState* state =
                &g_dluma_workshop_remote[remote_index++];
            unsigned int request;
            ULONGLONG api_call = 0;
            int cache_current = dluma_workshop_enrichment_cache_is_current(
                state->preview_ready, state->cache_updated_time,
                state->updated_time);
            if (!dluma_workshop_enrichment_should_start_http(
                    api->preview_available, state->requested,
                    state->details_ready, cache_current,
                    state->download_started, state->preview_url[0] != 0,
                    state->preview_failed, state->preview_attempts,
                    DLUMA_WORKSHOP_MENU_PREVIEW_MAX_ATTEMPTS,
                    now, state->preview_retry_after_ms))
                continue;
            if (state->preview_attempts < 255U)
                ++state->preview_attempts;
            request = api->create_http_request(
                api->http, DLUMA_STEAM_HTTP_GET, state->preview_url);
            if (!request || !api->send_http_request(
                                api->http, request, &api_call) ||
                !api_call)
            {
                if (request)
                    api->release_http_request(api->http, request);
                if (state->preview_attempts >=
                    DLUMA_WORKSHOP_MENU_PREVIEW_MAX_ATTEMPTS)
                    state->preview_failed = 1;
                else
                    state->preview_retry_after_ms =
                        now + DLUMA_WORKSHOP_MENU_RETRY_DELAY_MS;
                dluma_log_u64("WARN", "workshop_preview_http_start_failed",
                            state->published_id);
                continue;
            }
            slot->request = request;
            slot->published_id = state->published_id;
            slot->callback.vtable =
                g_dluma_workshop_http_call_result_vtable;
            slot->callback.flags = 0;
            slot->callback.callback_id = DLUMA_STEAM_HTTP_CALLBACK;
            slot->callback.api_call = api_call;
            dluma_atomic_exchange(&slot->callback.completed, 0);
            dluma_atomic_exchange(&slot->callback.io_failure, 0);
            dluma_atomic_exchange(&slot->callback.wrong_call, 0);
            memset(&slot->callback.result, 0,
                   sizeof(slot->callback.result));
            api->register_call_result(&slot->callback, api_call);
            slot->registered = 1;
            state->download_started = 1;
            dluma_log_u64("INFO", "workshop_preview_http_started",
                        state->published_id);
            break;
        }
    }
}

static void dluma_workshop_consume_image_jobs(void)
{
    unsigned int i;
    for (i = 0; i < DLUMA_WORKSHOP_MENU_IMAGE_JOBS; ++i)
    {
        DlumaWorkshopMenuImageJob* job = &g_dluma_workshop_image_jobs[i];
        DlumaWorkshopMenuRemoteState* state;
        if (dluma_atomic_compare_exchange(&job->state, 0, 2) != 2)
            continue;
        state = dluma_workshop_remote_for_id(job->published_id, 0);
        if (state)
        {
            if (job->succeeded && job->final_path[0])
            {
                dluma_wstr_copy(state->preview_path,
                              DLUMA_ARRAY_COUNT(state->preview_path),
                              job->final_path);
                state->preview_ready = 1;
                state->preview_failed = 0;
                state->download_started = 0;
                state->cache_updated_time = job->updated_time;
                dluma_log_u64("INFO", "workshop_preview_cache_updated",
                            state->published_id);
                dluma_log_pair(job->metadata_succeeded ? "INFO" : "WARN",
                             "workshop_preview_cache_metadata",
                             job->metadata_succeeded ? "written"
                                                     : "image_only");
            }
            else
            {
                state->download_started = 0;
                if (!dluma_workshop_enrichment_cached_preview_survives_failure(
                        state->preview_ready))
                    state->preview_failed = 1;
                dluma_log_u64("WARN", "workshop_preview_cache_write_failed",
                            state->published_id);
            }
        }
        memset(job, 0, sizeof(*job));
    }
}

static ULONGLONG dluma_workshop_presentation_mix_byte(
    ULONGLONG hash, unsigned char value)
{
    hash ^= (ULONGLONG)value;
    return hash * 1099511628211ULL;
}

static ULONGLONG dluma_workshop_presentation_mix_u64(
    ULONGLONG hash, ULONGLONG value)
{
    unsigned int i;
    for (i = 0; i < 8U; ++i)
    {
        hash = dluma_workshop_presentation_mix_byte(
            hash, (unsigned char)(value & 0xffU));
        value >>= 8U;
    }
    return hash;
}

static ULONGLONG dluma_workshop_presentation_mix_text(
    ULONGLONG hash, const char* text, unsigned int capacity)
{
    unsigned int i;
    for (i = 0; text && i < capacity && text[i]; ++i)
        hash = dluma_workshop_presentation_mix_byte(
            hash, (unsigned char)text[i]);
    return dluma_workshop_presentation_mix_byte(hash, 0xffU);
}

static ULONGLONG dluma_workshop_presentation_mix_wtext(
    ULONGLONG hash, const WCHAR* text, unsigned int capacity)
{
    unsigned int i;
    for (i = 0; text && i < capacity && text[i]; ++i)
    {
        hash = dluma_workshop_presentation_mix_byte(
            hash, (unsigned char)(text[i] & 0xffU));
        hash = dluma_workshop_presentation_mix_byte(
            hash, (unsigned char)((text[i] >> 8U) & 0xffU));
    }
    return dluma_workshop_presentation_mix_byte(hash, 0xfeU);
}

static void dluma_workshop_update_presentation_revision(void)
{
    ULONGLONG signature = 1469598103934665603ULL;
    unsigned int i;
    signature = dluma_workshop_presentation_mix_u64(
        signature, g_dluma_workshop_remote_count);
    for (i = 0; i < g_dluma_workshop_remote_count; ++i)
    {
        const DlumaWorkshopMenuRemoteState* state =
            &g_dluma_workshop_remote[i];
        unsigned char flags = (unsigned char)(
            (state->requested ? 0x01U : 0U) |
            (state->details_ready ? 0x02U : 0U) |
            (state->author_ready ? 0x04U : 0U) |
            (state->preview_ready ? 0x08U : 0U) |
            (state->details_failed ? 0x10U : 0U) |
            (state->author_failed ? 0x20U : 0U) |
            (state->preview_failed ? 0x40U : 0U));
        signature = dluma_workshop_presentation_mix_u64(
            signature, state->published_id);
        signature = dluma_workshop_presentation_mix_u64(
            signature, state->updated_time);
        signature = dluma_workshop_presentation_mix_byte(signature, flags);
        signature = dluma_workshop_presentation_mix_text(
            signature, state->title, DLUMA_ARRAY_COUNT(state->title));
        signature = dluma_workshop_presentation_mix_text(
            signature, state->description,
            DLUMA_ARRAY_COUNT(state->description));
        signature = dluma_workshop_presentation_mix_text(
            signature, state->author, DLUMA_ARRAY_COUNT(state->author));
        signature = dluma_workshop_presentation_mix_wtext(
            signature, state->preview_path,
            DLUMA_ARRAY_COUNT(state->preview_path));
    }
    if (signature != g_dluma_workshop_presentation_signature)
    {
        g_dluma_workshop_presentation_signature = signature;
        ++g_dluma_workshop_presentation_revision;
        if (!g_dluma_workshop_presentation_revision)
            ++g_dluma_workshop_presentation_revision;
    }
}

void dluma_workshop_menu_game_thread_tick(void)
{
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    LONG cache_directory_status;
    if (g_dluma.stop_requested)
        return;
    cache_directory_status = dluma_atomic_compare_exchange(
        &g_dluma_workshop_cache_directory_status, 0, 0);
    if (cache_directory_status == 1 || cache_directory_status == -1)
    {
        dluma_log_pair(cache_directory_status > 0 ? "INFO" : "WARN",
                     "workshop_preview_cache_directory",
                     cache_directory_status > 0 ? "ready" : "unavailable");
        dluma_atomic_exchange(&g_dluma_workshop_cache_directory_status,
                            cache_directory_status > 0 ? 2 : -2);
    }
    (void)dluma_workshop_preview_cache_refresh_pending_on_game_thread();
    dluma_workshop_consume_image_jobs();
    dluma_workshop_apply_cache_snapshot();
    if (dluma_workshop_resolve_steam())
    {
        dluma_workshop_finish_query();
        if (api->author_available)
            dluma_workshop_poll_authors();
        if (api->preview_available)
            dluma_workshop_poll_http();
        dluma_workshop_start_query();
        if (api->preview_available)
            dluma_workshop_start_http();
    }
    dluma_workshop_update_presentation_revision();
}

ULONGLONG dluma_workshop_menu_presentation_revision(void)
{
    return g_dluma_workshop_presentation_revision;
}

static int dluma_workshop_write_binary(
    const WCHAR* path, const BYTE* data, DWORD size)
{
    HANDLE file;
    DWORD written = 0;
    WCHAR directory[DLUMA_MAX_WPATH];
    if (!path || !path[0] || !data || !size)
        return 0;
    dluma_wstr_copy(directory, DLUMA_ARRAY_COUNT(directory), path);
    dluma_path_dirname(directory);
    if (!dluma_mkdirs(directory))
        return 0;
    file = CreateFileW(path, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!WriteFile(file, data, size, &written, 0) || written != size)
    {
        CloseHandle(file);
        return 0;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    return 1;
}

static void dluma_workshop_clear_owned_pixel_job(DlumaWorkshopPreviewPixelJob *job)
{
    /* State 3 remains published throughout payload replacement. Clearing the
     * complete struct would expose a free slot to another UI/worker entrant. */
    memset((BYTE *)job + sizeof(job->state), 0,
           sizeof(*job) - sizeof(job->state));
}

int dluma_workshop_menu_request_preview_pixels(
    ULONGLONG asset_key, const WCHAR *dds_path)
{
    unsigned int i;
    DlumaWorkshopPreviewPixelJob *slot = 0;
    if (!asset_key || !dds_path || !dds_path[0])
        return 0;
    for (i = 0; i < DLUMA_WORKSHOP_MENU_PIXEL_JOBS; ++i)
    {
        DlumaWorkshopPreviewPixelJob *job = &g_dluma_workshop_pixel_jobs[i];
        LONG state = dluma_atomic_compare_exchange(&job->state, 0, 0);
        if (state == 3)
            return 0;
        if (state == 1 || state == 2)
        {
            int matches;
            if (dluma_atomic_compare_exchange(&job->state, 3, state) != state)
                return 0;
            matches = job->asset_key == asset_key &&
                dluma_wstricmp(job->dds_path, dds_path) == 0;
            dluma_atomic_exchange(&job->state, state);
            if (matches) return 1;
        }
        if (state == 0 && !slot)
            slot = job;
    }
    if (!slot || dluma_atomic_compare_exchange(&slot->state, 3, 0) != 0)
        return 0;
    dluma_workshop_clear_owned_pixel_job(slot);
    slot->asset_key = asset_key;
    dluma_wstr_copy(slot->dds_path, DLUMA_ARRAY_COUNT(slot->dds_path), dds_path);
    dluma_atomic_exchange(&slot->state, 1);
    return 1;
}

/* Dynamic texture updates consume byte order directly, whereas ImageLib
 * A8R8G8B8/DDS cache pixels are laid out BGRA in memory.  Convert once while
 * the worker owns the job so UI presentation and row rebinding are pure. */
static void dluma_workshop_bgra_to_rgba_in_place(BYTE *pixels, SIZE_T pixel_count)
{
    SIZE_T i;
    if (!pixels) return;
    for (i = 0; i < pixel_count; ++i)
    {
        BYTE *pixel = pixels + i * 4U;
        BYTE blue = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = blue;
    }
}

int dluma_workshop_menu_take_preview_pixels(
    ULONGLONG asset_key, const WCHAR *dds_path,
    BYTE **pixels_out, unsigned int *width_out,
    unsigned int *height_out, unsigned int *stride_out)
{
    unsigned int i;
    if (!pixels_out || !width_out || !height_out || !stride_out)
        return 0;
    *pixels_out = 0;
    *width_out = 0;
    *height_out = 0;
    *stride_out = 0;
    for (i = 0; i < DLUMA_WORKSHOP_MENU_PIXEL_JOBS; ++i)
    {
        DlumaWorkshopPreviewPixelJob *job = &g_dluma_workshop_pixel_jobs[i];
        if (dluma_atomic_compare_exchange(&job->state, 3, 2) != 2)
            continue;
        if (job->asset_key == asset_key && dds_path &&
            dluma_wstricmp(job->dds_path, dds_path) == 0)
        {
            if (job->layout != DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD)
            {
                /* A malformed job must not remain visible for repeated UI
                 * polls, and it must never be treated as a BGRA upload. */
                if (job->pixels) dluma_free(job->pixels);
                dluma_workshop_clear_owned_pixel_job(job);
                dluma_atomic_exchange(&job->state, 0);
                return 1;
            }
            *pixels_out = job->pixels;
            *width_out = job->width;
            *height_out = job->height;
            *stride_out = job->stride;
            job->pixels = 0;
            dluma_workshop_clear_owned_pixel_job(job);
            dluma_atomic_exchange(&job->state, 0);
            return 1;
        }
        dluma_atomic_exchange(&job->state, 2);
    }
    return 0;
}

static int dluma_workshop_validate_cached_image(
    const WCHAR* path, int png, DWORD* size_out,
    char sha256_out[DLUMA_SHA256_HEX_CAPACITY])
{
    HANDLE file;
    BYTE header[8];
    BYTE trailer[2];
    DWORD read = 0;
    LARGE_INTEGER file_size;
    LARGE_INTEGER offset;
    ULONGLONG size;
    int valid = 0;
    if (size_out) *size_out = 0;
    if (sha256_out) sha256_out[0] = 0;
    if (!path || !path[0] || !size_out || !sha256_out)
        return 0;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    file_size.QuadPart = 0;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart < 8 ||
        (ULONGLONG)file_size.QuadPart >
            DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES)
    {
        CloseHandle(file);
        return 0;
    }
    size = (ULONGLONG)file_size.QuadPart;
    memset(header, 0, sizeof(header));
    memset(trailer, 0, sizeof(trailer));
    if (ReadFile(file, header, sizeof(header), &read, 0) &&
        read == sizeof(header))
    {
        if (png)
        {
            valid = header[0] == 0x89U && header[1] == 'P' &&
                    header[2] == 'N' && header[3] == 'G' &&
                    header[4] == 0x0dU && header[5] == 0x0aU &&
                    header[6] == 0x1aU && header[7] == 0x0aU;
        }
        else if (header[0] == 0xffU && header[1] == 0xd8U)
        {
            offset.QuadPart = (LONGLONG)size - 2;
            if (SetFilePointerEx(file, offset, 0, FILE_BEGIN) &&
                ReadFile(file, trailer, sizeof(trailer), &read, 0) &&
                read == sizeof(trailer))
                valid = trailer[0] == 0xffU && trailer[1] == 0xd9U;
        }
    }
    CloseHandle(file);
    if (!valid ||
        !dluma_sha256_file_hex(path, DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES,
                             sha256_out))
        return 0;
    *size_out = (DWORD)size;
    return 1;
}

static int dluma_workshop_read_cache_metadata(
    ULONGLONG published_id, int png, DWORD image_size,
    const char* image_sha256, unsigned int* updated_time_out)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char* text = 0;
    DWORD size = 0;
    DlumaWorkshopCacheMetadata metadata;
    int valid = 0;
    if (updated_time_out) *updated_time_out = 0;
    dluma_workshop_build_cache_path(
        path, DLUMA_ARRAY_COUNT(path), published_id, L".meta");
    if (dluma_read_file_utf8(path, &text, &size) && text)
    {
        valid = dluma_workshop_parse_cache_metadata_text(
                    text, size, &metadata) &&
                metadata.published_id == published_id &&
                metadata.png == (unsigned char)(png ? 1 : 0) &&
                metadata.image_size == image_size &&
                dluma_stricmp(metadata.sha256, image_sha256) == 0;
        if (valid && updated_time_out)
            *updated_time_out = metadata.updated_time;
        dluma_free(text);
    }
    if (!valid && dluma_path_exists(path))
        DeleteFileW(path);
    return valid;
}

static void dluma_workshop_scan_cache_extension(
    DlumaWorkshopMenuCacheEntry* entries, unsigned int* count,
    const WCHAR* extension, int png, int* dds_published)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    unsigned int extension_length = (unsigned int)wcslen(extension);
    dluma_path_join(directory, DLUMA_ARRAY_COUNT(directory),
                  g_dluma.module_dir,
                  L"dluma_runtime\\workshop_thumbnails");
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), directory,
                  png ? L"*.png" : L"*.jpg");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        WCHAR id_wide[64];
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR dds_path[DLUMA_MAX_WPATH];
        char id_text[32];
        char sha256[DLUMA_SHA256_HEX_CAPACITY];
        ULONGLONG published_id = 0;
        DWORD image_size = 0;
        unsigned int updated_time = 0;
        unsigned int name_length;
        unsigned int i;
        unsigned int existing = 0xFFFFFFFFUL;
        int metadata_valid;
        int dds_was_valid;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        name_length = (unsigned int)wcslen(data.cFileName);
        if (name_length <= extension_length ||
            name_length - extension_length >= DLUMA_ARRAY_COUNT(id_wide))
            continue;
        for (i = 0; i < name_length - extension_length; ++i)
            id_wide[i] = data.cFileName[i];
        id_wide[i] = 0;
        if (!dluma_workshop_wide_decimal_id(
                id_wide, id_text, DLUMA_ARRAY_COUNT(id_text)) ||
            !dluma_workshop_decimal_u64(id_text, &published_id))
            continue;
        dluma_path_join(path, DLUMA_ARRAY_COUNT(path), directory,
                      data.cFileName);
        if (!dluma_workshop_validate_cached_image(
                path, png, &image_size, sha256))
        {
            WCHAR metadata_path[DLUMA_MAX_WPATH];
            WCHAR invalid_dds_path[DLUMA_MAX_WPATH];
            DeleteFileW(path);
            dluma_workshop_build_cache_path(
                metadata_path, DLUMA_ARRAY_COUNT(metadata_path),
                published_id, L".meta");
            DeleteFileW(metadata_path);
            dluma_workshop_build_cache_path(
                invalid_dds_path, DLUMA_ARRAY_COUNT(invalid_dds_path),
                published_id, L".dds");
            DeleteFileW(invalid_dds_path);
            dluma_log_u64("WARN", "workshop_preview_cache_corrupt_removed",
                        published_id);
            continue;
        }
        if (!dluma_workshop_build_hashed_dds_path(
                dds_path, DLUMA_ARRAY_COUNT(dds_path),
                published_id, sha256))
            continue;
        dds_was_valid = dluma_workshop_preview_validate_dds(dds_path);
        if (!dluma_workshop_preview_prepare_dds(path, dds_path) ||
            !dluma_workshop_preview_validate_dds(dds_path))
        {
            dluma_log_u64(
                "WARN", "workshop_preview_dds_unavailable",
                published_id);
            continue;
        }
        if (!dds_was_valid && dds_published)
            *dds_published = 1;
        metadata_valid = dluma_workshop_read_cache_metadata(
            published_id, png, image_size, sha256, &updated_time);
        for (i = 0; i < *count; ++i)
            if (entries[i].published_id == published_id)
            {
                existing = i;
                break;
            }
        if (existing != 0xFFFFFFFFUL)
        {
            if (entries[existing].metadata_valid && !metadata_valid)
                continue;
            if (entries[existing].metadata_valid == metadata_valid &&
                entries[existing].png)
                continue;
            i = existing;
        }
        else
        {
            if (*count >= DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES)
                continue;
            i = (*count)++;
        }
        memset(&entries[i], 0, sizeof(entries[i]));
        entries[i].published_id = published_id;
        entries[i].updated_time = updated_time;
        entries[i].image_size = image_size;
        entries[i].png = (unsigned char)(png ? 1 : 0);
        entries[i].metadata_valid =
            (unsigned char)(metadata_valid ? 1 : 0);
        dluma_str_copy(entries[i].sha256,
                     DLUMA_ARRAY_COUNT(entries[i].sha256), sha256);
        dluma_wstr_copy(entries[i].image_path,
                      DLUMA_ARRAY_COUNT(entries[i].image_path), dds_path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static int dluma_workshop_cache_dds_filename(
    const WCHAR* name, int* current_generation)
{
    unsigned int index = 0;
    unsigned int digit_start;
    unsigned int digest;
    unsigned int generation = 0;
    unsigned int generation_digits = 0;
    if (current_generation)
        *current_generation = 0;
    if (!name || !name[0])
        return 0;
    if ((name[0] == L'l' || name[0] == L'L') &&
        (name[1] == L'o' || name[1] == L'O') &&
        (name[2] == L'c' || name[2] == L'C') &&
        (name[3] == L'a' || name[3] == L'A') &&
        (name[4] == L'l' || name[4] == L'L') &&
        name[5] == L'_')
        index = 6U;
    digit_start = index;
    while (name[index] >= L'0' && name[index] <= L'9')
        ++index;
    if (index == digit_start)
        return 0;
    if (dluma_wstricmp(name + index, L".dds") == 0)
        return 1;
    if (name[index] != L'_')
        return 0;
    ++index;
    for (digest = 0; digest < 16U; ++digest, ++index)
        if (!((name[index] >= L'0' && name[index] <= L'9') ||
              (name[index] >= L'a' && name[index] <= L'f') ||
              (name[index] >= L'A' && name[index] <= L'F')))
            return 0;
    if (dluma_wstricmp(name + index, L".dds") == 0)
        return 1;
    if (name[index] != L'_' ||
        (name[index + 1U] != L'd' &&
         name[index + 1U] != L'D'))
        return 0;
    index += 2U;
    while (name[index] >= L'0' && name[index] <= L'9')
    {
        if (generation > 1000000U)
            return 0;
        generation =
            generation * 10U + (unsigned int)(name[index] - L'0');
        ++generation_digits;
        ++index;
    }
    if (!generation_digits ||
        dluma_wstricmp(name + index, L".dds") != 0)
        return 0;
    if (current_generation)
        *current_generation =
            generation == DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION;
    return 1;
}

static void dluma_workshop_remove_stale_dds_generations(void)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    unsigned int removed = 0;
    dluma_path_join(
        directory, DLUMA_ARRAY_COUNT(directory),
        g_dluma.module_dir, L"dluma_runtime\\workshop_thumbnails");
    dluma_path_join(
        pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*.dds");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        WCHAR path[DLUMA_MAX_WPATH];
        int current_generation = 0;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !dluma_workshop_cache_dds_filename(
                data.cFileName, &current_generation) ||
            current_generation)
            continue;
        dluma_path_join(
            path, DLUMA_ARRAY_COUNT(path), directory, data.cFileName);
        if (DeleteFileW(path))
            ++removed;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (removed)
        dluma_log_u64(
            "INFO", "workshop_preview_stale_dds_removed", removed);
}

static void dluma_workshop_remove_interrupted_cache_temps(void)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    unsigned int removed = 0;
    dluma_path_join(directory, DLUMA_ARRAY_COUNT(directory),
                  g_dluma.module_dir,
                  L"dluma_runtime\\workshop_thumbnails");
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*.tmp");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR dds_name[DLUMA_MAX_WPATH];
        const WCHAR* suffix;
        unsigned int name_length;
        unsigned int i = 0;
        unsigned int digit_start = 0;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        name_length = (unsigned int)wcslen(data.cFileName);
        if (name_length > 4U &&
            dluma_wstricmp(
                data.cFileName + name_length - 4U, L".tmp") == 0 &&
            name_length - 4U < DLUMA_ARRAY_COUNT(dds_name))
        {
            memcpy(
                dds_name, data.cFileName,
                (name_length - 4U) * sizeof(WCHAR));
            dds_name[name_length - 4U] = 0;
            if (dluma_workshop_cache_dds_filename(dds_name, 0))
            {
                dluma_path_join(
                    path, DLUMA_ARRAY_COUNT(path),
                    directory, data.cFileName);
                if (DeleteFileW(path))
                    ++removed;
                continue;
            }
        }
        if ((data.cFileName[0] == L'l' ||
             data.cFileName[0] == L'L') &&
            (data.cFileName[1] == L'o' ||
             data.cFileName[1] == L'O') &&
            (data.cFileName[2] == L'c' ||
             data.cFileName[2] == L'C') &&
            (data.cFileName[3] == L'a' ||
             data.cFileName[3] == L'A') &&
            (data.cFileName[4] == L'l' ||
             data.cFileName[4] == L'L') &&
            data.cFileName[5] == L'_')
        {
            i = 6U;
            digit_start = i;
        }
        while (data.cFileName[i] >= L'0' && data.cFileName[i] <= L'9')
            ++i;
        if (i == digit_start)
            continue;
        suffix = data.cFileName + i;
        if (*suffix == L'_')
        {
            unsigned int digest = 0;
            ++suffix;
            while (digest < 16U &&
                   ((*suffix >= L'0' && *suffix <= L'9') ||
                    (*suffix >= L'a' && *suffix <= L'f') ||
                    (*suffix >= L'A' && *suffix <= L'F')))
            {
                ++suffix;
                ++digest;
            }
            if (digest != 16U)
                continue;
        }
        if (dluma_wstricmp(suffix, L".png.tmp") != 0 &&
            dluma_wstricmp(suffix, L".jpg.tmp") != 0 &&
            dluma_wstricmp(suffix, L".dds.tmp") != 0 &&
            dluma_wstricmp(suffix, L".meta.tmp") != 0)
            continue;
        dluma_path_join(path, DLUMA_ARRAY_COUNT(path), directory, data.cFileName);
        if (DeleteFileW(path))
            ++removed;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (removed)
        dluma_log_u64("INFO", "workshop_preview_cache_temp_removed", removed);
}

static void dluma_workshop_scan_cache_worker(void)
{
    DlumaWorkshopMenuCacheEntry* entries;
    unsigned int count = 0;
    int dds_published = 0;
    entries = (DlumaWorkshopMenuCacheEntry*)dluma_alloc(
        sizeof(DlumaWorkshopMenuCacheEntry) *
        DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES);
    if (!entries)
        return;
    memset(entries, 0,
           sizeof(DlumaWorkshopMenuCacheEntry) *
               DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES);
    dluma_workshop_remove_interrupted_cache_temps();
    dluma_workshop_remove_stale_dds_generations();
    dluma_workshop_scan_cache_extension(
        entries, &count, L".png", 1, &dds_published);
    dluma_workshop_scan_cache_extension(
        entries, &count, L".jpg", 0, &dds_published);
    if (dds_published)
        (void)dluma_workshop_preview_cache_note_dds_published();
    dluma_atomic_increment(&g_dluma_workshop_cache_sequence);
    memcpy(g_dluma_workshop_cache, entries,
           sizeof(DlumaWorkshopMenuCacheEntry) * count);
    if (count < DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES)
        memset(&g_dluma_workshop_cache[count], 0,
               sizeof(DlumaWorkshopMenuCacheEntry) *
                   (DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES - count));
    g_dluma_workshop_cache_count = count;
    dluma_atomic_increment(&g_dluma_workshop_cache_sequence);
    dluma_free(entries);
    dluma_log_u64("INFO", "workshop_preview_cache_entries", count);
}

static int dluma_workshop_write_cache_metadata(
    ULONGLONG published_id, unsigned int updated_time,
    int png, DWORD image_size, const char* sha256)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR temp[DLUMA_MAX_WPATH];
    char text[512];
    char number[32];
    text[0] = 0;
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "version=1\r\n");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "published_id=");
    dluma_u64_to_text(published_id, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nupdated_time=");
    dluma_u64_to_text(updated_time, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nformat=");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), png ? "png" : "jpg");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nsize=");
    dluma_u64_to_text(image_size, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nsha256=");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), sha256 ? sha256 : "");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\n");
    dluma_workshop_build_cache_path(
        path, DLUMA_ARRAY_COUNT(path), published_id, L".meta");
    dluma_wstr_copy(temp, DLUMA_ARRAY_COUNT(temp), path);
    dluma_wstr_append(temp, DLUMA_ARRAY_COUNT(temp), L".tmp");
    if (!dluma_write_utf8_file(temp, text, 0) ||
        !MoveFileExW(temp, path,
                     MOVEFILE_REPLACE_EXISTING |
                         MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp);
        return 0;
    }
    return 1;
}

void dluma_workshop_menu_worker_tick(void)
{
    unsigned int i;
    int cache_changed = 0;
    /* CPU-only stage for the renderer-backed presentation route. */
    for (i = 0; i < DLUMA_WORKSHOP_MENU_PIXEL_JOBS; ++i)
    {
        DlumaWorkshopPreviewPixelJob *job = &g_dluma_workshop_pixel_jobs[i];
        BYTE *pixels = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int stride = 0;
        if (dluma_atomic_compare_exchange(&job->state, 3, 1) != 1)
            continue;
        job->succeeded = (unsigned char)
            (dluma_workshop_preview_read_dds_top_mip_bgra(
                 job->dds_path, &pixels, &width, &height, &stride) ? 1 : 0);
        if (job->succeeded && pixels && width == 512U && height == 512U &&
            stride == 2048U)
        {
            job->pixels = pixels;
            job->width = width;
            job->height = height;
            job->stride = stride;
            job->layout = DLUMA_WORKSHOP_PIXEL_LAYOUT_BGRA8_CACHE;
            dluma_workshop_bgra_to_rgba_in_place(
                job->pixels, (SIZE_T)width * (SIZE_T)height);
            job->layout = DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD;
        }
        else if (pixels)
        {
            dluma_free(pixels);
            job->succeeded = 0;
        }
        dluma_atomic_exchange(&job->state, 2);
    }
    for (i = 0; i < DLUMA_WORKSHOP_MENU_IMAGE_JOBS; ++i)
    {
        DlumaWorkshopMenuImageJob* job = &g_dluma_workshop_image_jobs[i];
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR dds_path[DLUMA_MAX_WPATH];
        WCHAR temp[DLUMA_MAX_WPATH];
        WCHAR alternate[DLUMA_MAX_WPATH];
        char sha256[DLUMA_SHA256_HEX_CAPACITY];
        int source_saved;
        int dds_was_valid = 0;
        if (dluma_atomic_compare_exchange(&job->state, 3, 1) != 1)
            continue;
        sha256[0] = 0;
        dluma_workshop_build_cache_path(
            path, DLUMA_ARRAY_COUNT(path), job->published_id,
            job->png ? L".png" : L".jpg");
        dluma_wstr_copy(temp, DLUMA_ARRAY_COUNT(temp), path);
        dluma_wstr_append(temp, DLUMA_ARRAY_COUNT(temp), L".tmp");
        dluma_sha256_bytes_hex(job->data, job->size, sha256);
        source_saved =
            (sha256[0] &&
             dluma_workshop_write_binary(temp, job->data, job->size) &&
             MoveFileExW(temp, path,
                         MOVEFILE_REPLACE_EXISTING |
                         MOVEFILE_WRITE_THROUGH));
        if (!source_saved)
            DeleteFileW(temp);
        else
        {
            if (dluma_workshop_build_hashed_dds_path(
                    dds_path, DLUMA_ARRAY_COUNT(dds_path),
                    job->published_id, sha256))
            {
                dds_was_valid =
                    dluma_workshop_preview_validate_dds(dds_path);
                job->succeeded = (unsigned char)
                    (dluma_workshop_preview_prepare_dds(
                         path, dds_path) &&
                     dluma_workshop_preview_validate_dds(dds_path)
                         ? 1 : 0);
            }
            else
            {
                job->succeeded = 0;
            }
            dluma_wstr_copy(job->final_path,
                          DLUMA_ARRAY_COUNT(job->final_path),
                          job->succeeded ? dds_path : L"");
            if (job->succeeded)
            {
                if (!dds_was_valid)
                    (void)
                        dluma_workshop_preview_cache_note_dds_published();
                job->metadata_succeeded = (unsigned char)
                    (dluma_workshop_write_cache_metadata(
                        job->published_id, job->updated_time,
                        job->png != 0, job->size, sha256) ? 1 : 0);
            }
            dluma_workshop_build_cache_path(
                alternate, DLUMA_ARRAY_COUNT(alternate),
                job->published_id, job->png ? L".jpg" : L".png");
            DeleteFileW(alternate);
            cache_changed = 1;
        }
        dluma_free(job->data);
        job->data = 0;
        job->size = 0;
        dluma_atomic_exchange(&job->state, 2);
    }
    if (cache_changed)
        dluma_atomic_exchange(&g_dluma_workshop_cache_scan_requested, 1);
    if (dluma_atomic_exchange(&g_dluma_workshop_cache_scan_requested, 0))
    {
        WCHAR directory[DLUMA_MAX_WPATH];
        dluma_path_join(directory, DLUMA_ARRAY_COUNT(directory),
                      g_dluma.module_dir,
                      L"dluma_runtime\\workshop_thumbnails");
        if (dluma_mkdirs(directory))
        {
            (void)dluma_atomic_compare_exchange(
                &g_dluma_workshop_cache_directory_status, 1, 0);
            dluma_workshop_scan_cache_worker();
        }
        else
        {
            (void)dluma_atomic_compare_exchange(
                &g_dluma_workshop_cache_directory_status, -1, 0);
        }
    }
}

void dluma_workshop_menu_shutdown(void)
{
    unsigned int i;
    ULONGLONG callback_wait_started;
    DlumaWorkshopMenuSteamApi* api = &g_dluma_workshop_steam;
    if (api->remove_ugc_call_result &&
        g_dluma_workshop_query_call_result)
    {
        api->remove_ugc_call_result(
            g_dluma_workshop_query_call_result);
        g_dluma_workshop_query_call_result = 0;
    }
    if (api->unregister_call_result)
        for (i = 0; i < DLUMA_WORKSHOP_MENU_HTTP_SLOTS; ++i)
            if (g_dluma_workshop_http[i].registered)
            {
                api->unregister_call_result(
                    &g_dluma_workshop_http[i].callback,
                    g_dluma_workshop_http[i].callback.api_call);
                g_dluma_workshop_http[i].registered = 0;
            }
    callback_wait_started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_workshop_callback_active_calls, 0, 0) != 0 &&
           GetTickCount64() - callback_wait_started < 2000ULL)
        Sleep(1);
    if (api->details_available && g_dluma_workshop_query_handle != ~0ULL)
        api->release_query(api->ugc, g_dluma_workshop_query_handle);
    if (api->preview_available)
        for (i = 0; i < DLUMA_WORKSHOP_MENU_HTTP_SLOTS; ++i)
            if (g_dluma_workshop_http[i].request)
                api->release_http_request(
                    api->http, g_dluma_workshop_http[i].request);
    for (i = 0; i < DLUMA_WORKSHOP_MENU_IMAGE_JOBS; ++i)
    {
        if (g_dluma_workshop_image_jobs[i].data)
            dluma_free(g_dluma_workshop_image_jobs[i].data);
        memset(&g_dluma_workshop_image_jobs[i], 0,
               sizeof(g_dluma_workshop_image_jobs[i]));
    }
    for (i = 0; i < DLUMA_WORKSHOP_MENU_PIXEL_JOBS; ++i)
    {
        if (g_dluma_workshop_pixel_jobs[i].pixels)
            dluma_free(g_dluma_workshop_pixel_jobs[i].pixels);
        memset(&g_dluma_workshop_pixel_jobs[i], 0,
               sizeof(g_dluma_workshop_pixel_jobs[i]));
    }
    memset(g_dluma_workshop_http, 0, sizeof(g_dluma_workshop_http));
    memset(g_dluma_workshop_remote, 0, sizeof(g_dluma_workshop_remote));
    dluma_atomic_increment(&g_dluma_workshop_cache_sequence);
    memset(g_dluma_workshop_cache, 0, sizeof(g_dluma_workshop_cache));
    g_dluma_workshop_cache_count = 0;
    dluma_atomic_increment(&g_dluma_workshop_cache_sequence);
    memset(&g_dluma_workshop_steam, 0, sizeof(g_dluma_workshop_steam));
    g_dluma_workshop_remote_count = 0;
    g_dluma_workshop_query_handle = ~0ULL;
    g_dluma_workshop_query_call = 0;
    g_dluma_workshop_query_call_result = 0;
    g_dluma_workshop_query_count = 0;
    memset(&g_dluma_workshop_query_completion, 0,
           sizeof(g_dluma_workshop_query_completion));
    dluma_atomic_exchange(&g_dluma_workshop_query_completion_ready, 0);
    dluma_atomic_exchange(&g_dluma_workshop_query_completion_failed, 0);
    g_dluma_workshop_steam_status_logged = 0;
    g_dluma_workshop_callback_adapter_status_logged = 0;
    g_dluma_workshop_cache_directory_status = 0;
    g_dluma_workshop_presentation_signature = 0;
    g_dluma_workshop_presentation_revision = 0;
    dluma_atomic_exchange(&g_dluma_workshop_cache_scan_requested, 1);
}
#endif
