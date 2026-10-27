#include "dlw_common.h"
#include "dlw_path_security.h"
#include "dlw_build_identity.h"
#include "dlw_main_menu_native_presentation.h"
#include "dlw_main_menu_presentation_core.h"
#include "dlw_new_mod_ticker.h"

/*
 * This is intentionally a narrow Player-only adapter.  The named macOS
 * reconstruction established the CNewsFeedGroup lifecycle, but every RVA,
 * slot, ABI, and byte prefix below is validated against the exact Windows PE
 * before it can be used.
 */
#define DLUMA_MAIN_MENU_NATIVE_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_MAIN_MENU_NATIVE_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_MAIN_MENU_NATIVE_GAMEDLL_SHA256 \
    "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"
#define DLUMA_MAIN_MENU_NATIVE_VTABLE_RVA 0x016C6428UL
#define DLUMA_MAIN_MENU_NATIVE_POST_SLOT_OFFSET 0x0390UL
#define DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_OFFSET 0x03F8UL
#define DLUMA_MAIN_MENU_NATIVE_POST_SLOT_RVA 0x016C67B8UL
#define DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_RVA 0x016C6820UL
#define DLUMA_MAIN_MENU_NATIVE_POST_RVA 0x00860650UL
#define DLUMA_MAIN_MENU_NATIVE_INIT_RVA 0x00859520UL
#define DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET 0x0480UL
#define DLUMA_MAIN_MENU_NATIVE_ADD_MESSAGE_RVA 0x00863870UL
#define DLUMA_MAIN_MENU_NATIVE_CLEAR_TYPE_RVA 0x008639A0UL
#define DLUMA_MAIN_MENU_NATIVE_NEWS_POST_RVA 0x00863A50UL
#define DLUMA_MAIN_MENU_NATIVE_NEWS_INITIALIZE_RVA 0x00863B60UL
#define DLUMA_MAIN_MENU_NATIVE_NEWS_SHOW_RVA 0x00863CC0UL
#define DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY 2048U
#define DLUMA_MAIN_MENU_NATIVE_QUEUE_CAPACITY_MAX 128U
#define DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY_MAX 65536U
#define DLUMA_MAIN_MENU_NATIVE_STOP_DRAIN_MS 2000ULL

typedef struct DlumaTtlString
{
    const char *data;
    unsigned int size;
    unsigned int capacity;
} DlumaTtlString;

typedef struct DlumaTtlWideString
{
    const WCHAR *data;
    unsigned int size;
    unsigned int capacity;
} DlumaTtlWideString;

typedef void(__fastcall *DlumaMenuMainOnInitFn)(void *menu_main);
typedef __int64(__fastcall *DlumaMenuMainOnPostUpdateFn)(void *menu_main);
typedef void(__fastcall *DlumaNewsFeedAddMessageFn)(
    void *news_group, const DlumaTtlWideString *message, int message_type);
typedef __int64 (__fastcall *DlumaNewsFeedClearTypeFn)(void *news_group);
typedef void(__fastcall *DlumaNewsFeedOnPostUpdateFn)(
    void *news_group, float delta_seconds);
typedef void(__fastcall *DlumaNewsFeedShowMessageFn)(
    void *news_group, int message_index);
typedef float(__fastcall *DlumaGuiObjectGetTimeDeltaFn)(void *object);
typedef void *(__fastcall *DlumaFindElementFn)(
    void *element, const DlumaTtlString *id, unsigned char recursive);
typedef void(__fastcall *DlumaSetTextExplicitFn)(
    void *text_element, const DlumaTtlWideString *text);
typedef const DlumaTtlWideString *(__fastcall *DlumaGetTextExplicitFn)(
    void *text_element);

typedef struct DlumaNativeNewsGroupView
{
    void *root;
    void *message;
    int count;
    int capacity;
} DlumaNativeNewsGroupView;

typedef struct DlumaMainMenuTickerMessage
{
    const char *key;
    const char *fallback;
    unsigned char placeholder_count;
} DlumaMainMenuTickerMessage;

typedef struct DlumaMainMenuNativeState
{
    BYTE *gamedll_base;
    DWORD image_size;
    void **post_slot;
    void **init_slot;
    DlumaMenuMainOnPostUpdateFn post_original;
    DlumaMenuMainOnInitFn init_original;
    DlumaNewsFeedAddMessageFn add_message;
    DlumaNewsFeedClearTypeFn clear_type;
    DlumaNewsFeedOnPostUpdateFn news_post_update;
    DlumaNewsFeedShowMessageFn show_message;
    DlumaGuiObjectGetTimeDeltaFn get_time_delta;
    DlumaFindElementFn find_element;
    DlumaSetTextExplicitFn set_text_explicit;
    DlumaGetTextExplicitFn get_text_explicit;
    volatile LONG stopping;
    volatile LONG active_calls;
    unsigned char installed;
    unsigned char profile_validated_logged;
    unsigned char version_published_logged;
    unsigned char commit_published_logged;
    unsigned char group_validated_logged;
    unsigned char messages_enqueued_logged;
    unsigned char native_update_logged;
    unsigned char identity_pending;
    unsigned char identity_deferred_logged;
} DlumaMainMenuNativeState;

static DlumaMainMenuNativeState g_dluma_main_menu_native;

static const BYTE g_dluma_main_menu_native_init_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
    0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55};
static const BYTE g_dluma_main_menu_native_post_prefix[] = {
    0x40, 0x55, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB1,
    0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00};
static const BYTE g_dluma_main_menu_native_add_message_prefix[] = {
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x83, 0x79,
    0x18, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x40};
static const BYTE g_dluma_main_menu_native_clear_type_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20};
static const BYTE g_dluma_main_menu_native_news_post_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x83,
    0x79, 0x08, 0x00, 0x0F, 0x29, 0x7C, 0x24, 0x30};
static const BYTE g_dluma_main_menu_native_show_message_prefix[] = {
    0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x40, 0x48, 0x63, 0xF2, 0x48, 0x8B, 0xF9};

static void __fastcall dluma_main_menu_native_on_init_hook(void *menu_main);
static __int64 __fastcall dluma_main_menu_native_on_post_update_hook(
    void *menu_main);

static int dluma_main_menu_native_get_pe_identity(
    HMODULE module, DWORD *timestamp_out, DWORD *image_size_out)
{
    const BYTE *image;
    const BYTE *nt;
    const BYTE *optional_header;
    DWORD pe_offset;
    WORD optional_magic;
    if (timestamp_out)
        *timestamp_out = 0;
    if (image_size_out)
        *image_size_out = 0;
    if (!module || !dluma_is_readable(module, 0x40U))
        return 0;
    image = (const BYTE *)module;
    if (*(const WORD *)image != 0x5A4DU)
        return 0;
    pe_offset = *(const DWORD *)(image + 0x3CU);
    if (pe_offset < 0x40U || pe_offset > 0x100000U ||
        !dluma_is_readable(image + pe_offset, 0x80U))
        return 0;
    nt = image + pe_offset;
    optional_header = nt + 0x18U;
    optional_magic = *(const WORD *)optional_header;
    if (*(const DWORD *)nt != 0x00004550UL || optional_magic != 0x020BU)
        return 0;
    if (timestamp_out)
        *timestamp_out = *(const DWORD *)(nt + 0x08U);
    if (image_size_out)
        *image_size_out = *(const DWORD *)(optional_header + 0x38U);
    return 1;
}

static int dluma_main_menu_native_bytes_match(
    const BYTE *address, const BYTE *expected, unsigned int expected_size)
{
    return address && expected && expected_size &&
           dluma_is_readable(address, expected_size) &&
           memcmp(address, expected, expected_size) == 0;
}

static int dluma_main_menu_native_validate_text(
    const DlumaMainMenuNativeState *state, void *text_element)
{
    const DlumaTtlWideString *current;
    if (!state || !text_element || !state->get_text_explicit ||
        !dluma_is_readable(text_element, sizeof(void *)))
        return 0;
    /* GetTextExplicit is the companion IUIText export.  A readable,
     * bounded ttl::string response proves this named child uses the expected
     * IUIText contract before SetTextExplicit is called. */
    current = state->get_text_explicit(text_element);
    if (!current || !dluma_is_readable(current, sizeof(*current)) ||
        current->size > current->capacity ||
        current->capacity > DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY_MAX)
        return 0;
    if (current->size &&
        (!current->data ||
         !dluma_is_readable(current->data,
                          (SIZE_T)current->size * sizeof(WCHAR))))
        return 0;
    return 1;
}

static int dluma_main_menu_native_validate_group(
    const DlumaMainMenuNativeState *state, void *group,
    DlumaNativeNewsGroupView *out)
{
    DlumaNativeNewsGroupView view;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!state || !group ||
        !dluma_is_readable(group, 0x20U))
        return 0;
    view.root = *(void **)group;
    view.message = *(void **)((BYTE *)group + sizeof(void *));
    view.count = *(const int *)((BYTE *)group + 0x18U);
    view.capacity = *(const int *)((BYTE *)group + 0x1CU);
    if (!view.root || !dluma_is_readable(view.root, sizeof(void *)) ||
        !dluma_main_menu_native_validate_text(state, view.message) ||
        view.count < 0 || view.capacity < view.count ||
        view.capacity > (int)DLUMA_MAIN_MENU_NATIVE_QUEUE_CAPACITY_MAX)
        return 0;
    if (out)
        *out = view;
    return 1;
}

static int dluma_main_menu_native_validate_menu(
    const DlumaMainMenuNativeState *state, void *menu_main)
{
    void *vtable;
    if (!state || !state->gamedll_base || !state->installed ||
        state->stopping || !menu_main ||
        !dluma_is_readable(menu_main, sizeof(vtable)))
        return 0;
    vtable = *(void **)menu_main;
    return vtable == state->gamedll_base + DLUMA_MAIN_MENU_NATIVE_VTABLE_RVA &&
           state->post_slot && state->init_slot &&
           dluma_is_readable(state->post_slot, sizeof(void *)) &&
           dluma_is_readable(state->init_slot, sizeof(void *)) &&
           *state->post_slot == (void *)dluma_main_menu_native_on_post_update_hook &&
           *state->init_slot == (void *)dluma_main_menu_native_on_init_hook;
}

static int dluma_main_menu_native_ttl_wide_from_utf8(
    const char *text, WCHAR storage[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY],
    DlumaTtlWideString *out)
{
    unsigned int length;
    if (!text || !storage || !out ||
        !dluma_utf8_to_wide(text, storage,
                          DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY))
        return 0;
    length = (unsigned int)wcslen(storage);
    if (!length || length >= DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY)
        return 0;
    out->data = storage;
    out->size = length;
    out->capacity = length;
    return 1;
}

static int dluma_main_menu_native_localized_utf8(
    const char *key, const char *fallback,
    const DlumaMainMenuPresentationPlaceholder *placeholders,
    unsigned int placeholder_count,
    char output[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY])
{
    char localized[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY];
    int localized_available;
    if (!key || !fallback || !output)
        return 0;
    localized[0] = 0;
    output[0] = 0;
    localized_available = dluma_text_get_utf8(
        key, fallback, localized, DLUMA_ARRAY_COUNT(localized));
    return dluma_main_menu_presentation_expand_localized(
        localized, localized_available, fallback,
        placeholders, placeholder_count, output,
        DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY);
}

static void dluma_main_menu_native_u32_to_text(
    unsigned int value, char out[16])
{
    char reversed[16];
    unsigned int count = 0;
    unsigned int index;
    do
    {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < DLUMA_ARRAY_COUNT(reversed));
    for (index = 0; index < count; ++index)
        out[index] = reversed[count - index - 1U];
    out[count] = 0;
}

static int dluma_main_menu_native_find_text(
    DlumaMainMenuNativeState *state, void *menu_main,
    const char *id, void **out)
{
    DlumaTtlString key;
    void *text;
    if (out)
        *out = 0;
    if (!state || !menu_main || !id || !state->find_element)
        return 0;
    key.data = id;
    key.size = (unsigned int)strlen(id);
    key.capacity = key.size;
    if (!key.size)
        return 0;
    text = state->find_element(menu_main, &key, 1);
    if (!dluma_main_menu_native_validate_text(state, text))
        return 0;
    if (out)
        *out = text;
    return 1;
}

static int dluma_main_menu_native_publish_identity(void *menu_main)
{
    DlumaMainMenuNativeState *state = &g_dluma_main_menu_native;
    DlumaMainMenuPresentationPlaceholder placeholder;
    char text[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY];
    WCHAR wide[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY];
    DlumaTtlWideString value;
    void *control;
    int version_published = 0;
    int commit_published = 0;
    if (!dluma_main_menu_native_validate_menu(state, menu_main) ||
        !state->set_text_explicit)
        return 0;

    placeholder.token = "{version}";
    placeholder.value = DLUMA_VERSION;
    if (dluma_main_menu_native_find_text(
            state, menu_main, "T_DLUMALoaderVersion", &control) &&
        dluma_main_menu_native_localized_utf8(
            "DLUMA_MainMenu_LoaderVersion", "DLUMA {version}",
            &placeholder, 1U, text) &&
        dluma_main_menu_native_ttl_wide_from_utf8(text, wide, &value))
    {
        state->set_text_explicit(control, &value);
        version_published = 1;
        if (!state->version_published_logged)
        {
            state->version_published_logged = 1;
            dluma_log_pair("INFO", "main_menu_loader_version_published", text);
        }
    }

    placeholder.token = "{commit}";
    placeholder.value = dluma_build_git_short();
    if (dluma_main_menu_native_find_text(
            state, menu_main, "T_DLUMALoaderCommit", &control) &&
        dluma_main_menu_native_localized_utf8(
            "DLUMA_MainMenu_LoaderCommit", "commit: {commit}",
            &placeholder, 1U, text) &&
        dluma_main_menu_native_ttl_wide_from_utf8(text, wide, &value))
    {
        state->set_text_explicit(control, &value);
        commit_published = 1;
        if (!state->commit_published_logged)
        {
            state->commit_published_logged = 1;
            dluma_log_pair("INFO", "main_menu_loader_commit_published", text);
        }
    }
    return version_published && commit_published;
}

static int dluma_main_menu_native_enqueue_message(
    DlumaMainMenuNativeState *state,
    void *menu_main,
    const DlumaMainMenuTickerMessage *message,
    const DlumaMainMenuPresentationPlaceholder *placeholders)
{
    DlumaNativeNewsGroupView before;
    DlumaNativeNewsGroupView after;
    char text[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY];
    WCHAR wide[DLUMA_MAIN_MENU_NATIVE_TEXT_CAPACITY];
    DlumaTtlWideString value;
    void *news_group;
    if (!state || !menu_main || !message || !message->key ||
        !message->fallback || !state->add_message)
        return 0;
    news_group =
        (BYTE *)menu_main + DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET;
    if (!dluma_main_menu_native_validate_group(state, news_group, &before) ||
        !dluma_main_menu_native_localized_utf8(
            message->key, message->fallback,
            message->placeholder_count ? placeholders : 0,
            message->placeholder_count, text) ||
        !dluma_main_menu_native_ttl_wide_from_utf8(text, wide, &value))
        return 0;
    /* Exact Player ABI: (group, ttl::wstring const *, message_type).
     * A stock one-entry queue normally has count == capacity here.
     * CNewsFeedGroup::AddMessage owns capacity growth, so rejecting that
     * state would prevent every loader ticker message from being added. */
    state->add_message(news_group, &value, 0);
    return dluma_main_menu_native_validate_group(
               state, news_group, &after) &&
           after.count == before.count + 1;
}

static int dluma_main_menu_native_holiday_message(
    DlumaMainMenuTickerMessage *message)
{
    SYSTEMTIME local_time;
    DlumaMainMenuHoliday holiday;
    if (!message)
        return 0;
    memset(message, 0, sizeof(*message));
    memset(&local_time, 0, sizeof(local_time));
    GetLocalTime(&local_time);
    holiday = dluma_main_menu_presentation_holiday(
        (unsigned int)local_time.wYear,
        (unsigned int)local_time.wMonth,
        (unsigned int)local_time.wDay);
    switch (holiday)
    {
    case DLUMA_MAIN_MENU_HOLIDAY_NEW_YEAR:
        message->key = "DLUMA_Ticker_NewYear";
        message->fallback =
            "Happy %COLOR(00c600)New Year!%COLOR(RESET) New year, new you? Get creating!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_VALENTINES:
        message->key = "DLUMA_Ticker_Valentines";
        message->fallback =
            "Happy %COLOR(ff007f)Valentine's Day!%COLOR(RESET) Don't forget to give your loved ones a gift!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_SAINT_PATRICKS:
        message->key = "DLUMA_Ticker_SaintPatricks";
        message->fallback =
            "Happy %COLOR(088c3a)Saint Patrick's Day!%COLOR(RESET) Maybe you'll find a pot of gold somewhere in Harran!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_EASTER:
        message->key = "DLUMA_Ticker_Easter";
        message->fallback =
            "Happy %COLOR(ffcc00)Easter!%COLOR(RESET) Try and find some Easter eggs in custom maps!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY:
        message->key = "DLUMA_Ticker_MothersDay";
        message->fallback =
            "Happy %COLOR(fd5d5e)Mother's Day!%COLOR(RESET) \"&Gazi_MothersDay_01DLG01&\"";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY:
        message->key = "DLUMA_Ticker_FathersDay";
        message->fallback =
            "Happy %COLOR(90c9cb)Father's Day!%COLOR(RESET) Give thanks to your World's Best Dad!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_INDEPENDENCE_DAY:
        message->key = "DLUMA_Ticker_IndependenceDay";
        message->fallback =
            "Happy %COLOR(ff0000)Independence Day!%COLOR(RESET) Light some firecrackers around Harran!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_TANABATA:
        message->key = "DLUMA_Ticker_Tanabata";
        message->fallback =
            "Happy %COLOR(1f8c9c)Tanabata!%COLOR(RESET) Write your wishes on a tanzaku and hang them on a bamboo tree! Good night, and %COLOR(f28c18)good luck!%COLOR(RESET)";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_LEIF_ERIKSON:
        message->key = "DLUMA_Ticker_LeifErikson";
        message->fallback =
            "Happy %COLOR(1F4E79)Leif Erikson%COLOR(RESET) day! \"Hinga Dinga Durgen!\"";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN:
        message->key = "DLUMA_Ticker_Halloween";
        message->fallback =
            "Happy %COLOR(ff6600)Halloween!%COLOR(RESET) Watch out for Volatiles while trick-or-treating!";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING:
        message->key = "DLUMA_Ticker_Thanksgiving";
        message->fallback =
            "Happy %COLOR(ff6600)Thanksgiving!%COLOR(RESET) Enjoy the Dying Light session with some turkey and pumpkin pie! %COLOR(666666)(Zombie meat isn't all that good.)%COLOR(RESET)";
        return 1;
    case DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS:
        message->key = "DLUMA_Ticker_HappyHolidays";
        message->fallback =
            "%COLOR(00c600)Happy %COLOR(c60000)Holidays!%COLOR(RESET) Enjoy the Dying Light session with some hot cocoa!";
        return 1;
    default:
        return 0;
    }
}

static void dluma_main_menu_native_enqueue_messages(void *menu_main)
{
    static const DlumaMainMenuTickerMessage messages[] = {
        {"DLUMA_Ticker_Welcome",
         "Thank you for using the %COLOR(f28c18)DLUMA.%COLOR(RESET) This mod is currently in very early development and prone to errors. More fixes and features are coming soon. Good Night, and Good Luck!", 0},
        {"DLUMA_Ticker_Stats",
         "Mods active: %COLOR(f28c18){active}%COLOR(RESET) | Mods installed: %COLOR(f28c18){installed}%COLOR(RESET) | Mods with errors: %COLOR(ff0000){errors}%COLOR(RESET)", 3},
        {"DLUMA_Ticker_ReportErrors",
         "Please report any errors to the %COLOR(f28c18)DLUMA%COLOR(RESET) Steam forum!", 0},
        {"DLUMA_Ticker_TechlandThankYou",
         "Welcome to %COLOR(f28c18)Dying Light%COLOR(RESET). The whole Techland team wants to thank you for purchasing our game. We've put our heart and soul into bringing you this title. We hope you enjoy it. - Techland", 0}};
    DlumaMainMenuNativeState *state = &g_dluma_main_menu_native;
    DlumaNativeNewsGroupView group;
    DlumaMainMenuTickerMessage holiday_message;
    const DlumaMainMenuTickerMessage new_mod_message = {
        "DLUMA_Ticker_NewMods",
        "%COLOR(f28c18)New global mods have been detected!%COLOR(RESET) They are disabled until reviewed. Open the %COLOR(f28c18)MODS%COLOR(RESET) page to review and enable them!",
        0};
    const DlumaMainMenuTickerMessage developer_mode_message = {
        "DLUMA_Ticker_DevMode",
        "Developer mode is %COLOR(f28c18)enabled%COLOR(RESET). Cheats and developer keys are available.",
        0};
    DlumaMainMenuPresentationPlaceholder placeholders[3];
    char active_text[16];
    char installed_text[16];
    char errors_text[16];
    unsigned int active;
    unsigned int installed;
    unsigned int errors;
    unsigned int index;
    unsigned int first_loader_index;
    unsigned int enqueued = 0;
    unsigned int expected = DLUMA_ARRAY_COUNT(messages);
    int new_mod_pending;
    int holiday_pending;
    int developer_mode_pending;
    if (!dluma_main_menu_native_validate_menu(state, menu_main) ||
        !state->add_message || !state->clear_type ||
        !dluma_main_menu_native_validate_group(
            state, (BYTE *)menu_main + DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET,
            &group))
        return;
    if (!state->group_validated_logged)
    {
        state->group_validated_logged = 1;
        dluma_log_pair("INFO", "main_menu_news_group_validated",
                     "stock_G_NewsFeed_and_T_Message");
    }
    /* NAT notices are message type 1 in the exact Windows implementation.
     * Remove that class through CNewsFeedGroup::ClearType before publishing
     * the three loader-owned type-0 messages. */
    state->clear_type(
        (BYTE *)menu_main + DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET);
    if (!dluma_main_menu_native_validate_group(
            state, (BYTE *)menu_main + DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET,
            &group))
        return;
    first_loader_index = (unsigned int)group.count;
    dluma_ui_get_main_menu_mod_counts(&active, &installed, &errors);
    dluma_main_menu_native_u32_to_text(active, active_text);
    dluma_main_menu_native_u32_to_text(installed, installed_text);
    dluma_main_menu_native_u32_to_text(errors, errors_text);
    placeholders[0].token = "{active}";
    placeholders[0].value = active_text;
    placeholders[1].token = "{installed}";
    placeholders[1].value = installed_text;
    placeholders[2].token = "{errors}";
    placeholders[2].value = errors_text;

    new_mod_pending = dluma_new_mod_ticker_has_pending();
    if (new_mod_pending)
    {
        ++expected;
        if (dluma_main_menu_native_enqueue_message(
                state, menu_main, &new_mod_message, 0))
        {
            ++enqueued;
            (void)dluma_new_mod_ticker_mark_presented();
        }
    }
    holiday_pending =
        dluma_main_menu_native_holiday_message(&holiday_message);
    if (holiday_pending)
    {
        ++expected;
        if (dluma_main_menu_native_enqueue_message(
                state, menu_main, &holiday_message, 0))
            ++enqueued;
    }
    developer_mode_pending = g_dluma.config.developer_mode != 0;
    if (developer_mode_pending)
        ++expected;
    for (index = 0; index < DLUMA_ARRAY_COUNT(messages); ++index)
    {
        if (index == 2U && developer_mode_pending &&
            dluma_main_menu_native_enqueue_message(
                state, menu_main, &developer_mode_message, 0))
            ++enqueued;
        if (dluma_main_menu_native_enqueue_message(
                state, menu_main, &messages[index], placeholders))
            ++enqueued;
    }
    /* Stock NAT/online notices can already occupy index zero when OnInit
     * returns.  Use the exact CNewsFeedGroup ShowMessage routine to begin at
     * the first loader entry rather than waiting for an unrelated notice to
     * finish its marquee cycle. */
    if (state->show_message && enqueued &&
        first_loader_index < DLUMA_MAIN_MENU_NATIVE_QUEUE_CAPACITY_MAX)
        state->show_message((BYTE *)menu_main +
                                DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET,
                            (int)first_loader_index);
    if (!state->messages_enqueued_logged)
    {
        state->messages_enqueued_logged = 1;
        dluma_log_u64(
            enqueued == expected ? "INFO" : "WARN",
            "main_menu_news_messages_enqueued", (ULONGLONG)enqueued);
    }
}

static void __fastcall dluma_main_menu_native_on_init_hook(void *menu_main)
{
    DlumaMainMenuNativeState *state = &g_dluma_main_menu_native;
    DlumaMenuMainOnInitFn original;
    dluma_atomic_increment(&state->active_calls);
    original = state->init_original;
    if (original)
        original(menu_main);
    if (dluma_main_menu_native_validate_menu(state, menu_main))
    {
        dluma_ui_note_live_frontend_screen(menu_main);
        state->identity_pending = 1;
        dluma_main_menu_native_enqueue_messages(menu_main);
    }
    dluma_atomic_decrement(&state->active_calls);
}

static __int64 __fastcall dluma_main_menu_native_on_post_update_hook(
    void *menu_main)
{
    DlumaMainMenuNativeState *state = &g_dluma_main_menu_native;
    DlumaMenuMainOnPostUpdateFn original;
    __int64 result = 0;
    DlumaNativeNewsGroupView group;
    float delta;
    dluma_atomic_increment(&state->active_calls);
    original = state->post_original;
    if (original)
        result = original(menu_main);
    if (dluma_main_menu_native_validate_menu(state, menu_main) &&
        state->identity_pending)
    {
        if (dluma_main_menu_native_publish_identity(menu_main))
            state->identity_pending = 0;
        else if (!state->identity_deferred_logged)
        {
            state->identity_deferred_logged = 1;
            dluma_log_line("INFO", "main_menu_identity_waiting_for_xui_controls");
        }
    }
    if (dluma_main_menu_native_validate_menu(state, menu_main))
    {
        dluma_ui_note_live_frontend_screen(menu_main);
        dluma_ui_first_launch_main_menu_post(menu_main);
    }
    /* The exact Windows MenuMainDI post-update does not advance its embedded
     * CNewsFeedGroup. ShowMessage starts T_Message at parent width + 5, so
     * this validated update is the only path that moves queued text through
     * the clipped news bar. */
    if (dluma_main_menu_native_validate_menu(state, menu_main) &&
        state->news_post_update && state->get_time_delta &&
        dluma_main_menu_native_validate_group(
            state, (BYTE *)menu_main + DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET,
            &group) &&
        group.count > 0)
    {
        delta = state->get_time_delta(menu_main);
        if (delta == delta && delta >= 0.0f && delta <= 1.0f)
        {
            state->news_post_update(
                (BYTE *)menu_main + DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET,
                delta);
            if (!state->native_update_logged)
            {
                state->native_update_logged = 1;
                dluma_log_line("INFO", "main_menu_news_native_update_active");
            }
        }
    }
    dluma_atomic_decrement(&state->active_calls);
    return result;
}

static int dluma_main_menu_native_patch_slot(
    void **slot, void *expected, void *replacement)
{
    DWORD old_protect;
    DWORD restored_protect;
    if (!slot || !expected || !replacement ||
        !dluma_is_readable(slot, sizeof(*slot)) || *slot != expected ||
        !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return 0;
    if (*slot != expected)
    {
        (void)VirtualProtect(slot, sizeof(*slot), old_protect,
                             &restored_protect);
        return 0;
    }
    *slot = replacement;
    if (!VirtualProtect(slot, sizeof(*slot), old_protect, &restored_protect))
    {
        *slot = expected;
        (void)VirtualProtect(slot, sizeof(*slot), old_protect,
                             &restored_protect);
        return 0;
    }
    (void)FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return 1;
}

static int dluma_main_menu_native_restore_slot(
    void **slot, void *hook, void *original)
{
    DWORD old_protect;
    DWORD restored_protect;
    if (!slot || !hook || !original || !dluma_is_readable(slot, sizeof(*slot)))
        return 0;
    if (*slot == original)
        return 1;
    if (*slot != hook ||
        !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return 0;
    if (*slot != hook)
    {
        (void)VirtualProtect(slot, sizeof(*slot), old_protect,
                             &restored_protect);
        return 0;
    }
    *slot = original;
    if (!VirtualProtect(slot, sizeof(*slot), old_protect, &restored_protect))
    {
        *slot = hook;
        (void)VirtualProtect(slot, sizeof(*slot), old_protect,
                             &restored_protect);
        return 0;
    }
    (void)FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return 1;
}

static int dluma_main_menu_native_validate_windows_profile(
    DlumaMainMenuNativeState *state)
{
    HMODULE gamedll;
    HMODULE engine;
    DWORD timestamp;
    DWORD image_size;
    WCHAR path[DLUMA_MAX_WPATH];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    BYTE *base;
    if (!state)
        return 0;
    gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!gamedll ||
        !dluma_main_menu_native_get_pe_identity(
            gamedll, &timestamp, &image_size) ||
        timestamp != DLUMA_MAIN_MENU_NATIVE_GAMEDLL_TIMESTAMP ||
        image_size != DLUMA_MAIN_MENU_NATIVE_GAMEDLL_IMAGE_SIZE)
        return 0;
    digest[0] = 0;
    if (!dluma_get_module_path_checked(
            gamedll, path, DLUMA_ARRAY_COUNT(path)) ||
        !dluma_sha256_file_hex(path, 1024ULL * 1024ULL * 1024ULL, digest) ||
        dluma_stricmp(digest, DLUMA_MAIN_MENU_NATIVE_GAMEDLL_SHA256) != 0)
        return 0;
    base = (BYTE *)gamedll;
    if (!dluma_is_readable(base + DLUMA_MAIN_MENU_NATIVE_VTABLE_RVA,
                         DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_OFFSET +
                             sizeof(void *)) ||
        DLUMA_MAIN_MENU_NATIVE_POST_SLOT_RVA !=
            DLUMA_MAIN_MENU_NATIVE_VTABLE_RVA +
                DLUMA_MAIN_MENU_NATIVE_POST_SLOT_OFFSET ||
        DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_RVA !=
            DLUMA_MAIN_MENU_NATIVE_VTABLE_RVA +
                DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_OFFSET ||
        *(void **)(base + DLUMA_MAIN_MENU_NATIVE_POST_SLOT_RVA) !=
            base + DLUMA_MAIN_MENU_NATIVE_POST_RVA ||
        *(void **)(base + DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_RVA) !=
            base + DLUMA_MAIN_MENU_NATIVE_INIT_RVA ||
        !dluma_main_menu_native_bytes_match(
            base + DLUMA_MAIN_MENU_NATIVE_INIT_RVA,
            g_dluma_main_menu_native_init_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_main_menu_native_init_prefix)) ||
        !dluma_main_menu_native_bytes_match(
            base + DLUMA_MAIN_MENU_NATIVE_POST_RVA,
            g_dluma_main_menu_native_post_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_main_menu_native_post_prefix)) ||
        !dluma_main_menu_native_bytes_match(
            base + DLUMA_MAIN_MENU_NATIVE_ADD_MESSAGE_RVA,
            g_dluma_main_menu_native_add_message_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_main_menu_native_add_message_prefix)) ||
        !dluma_main_menu_native_bytes_match(
            base + DLUMA_MAIN_MENU_NATIVE_CLEAR_TYPE_RVA,
            g_dluma_main_menu_native_clear_type_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_main_menu_native_clear_type_prefix)) ||
        !dluma_main_menu_native_bytes_match(
            base + DLUMA_MAIN_MENU_NATIVE_NEWS_POST_RVA,
            g_dluma_main_menu_native_news_post_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_main_menu_native_news_post_prefix)) ||
        !dluma_main_menu_native_bytes_match(
            base + DLUMA_MAIN_MENU_NATIVE_NEWS_SHOW_RVA,
            g_dluma_main_menu_native_show_message_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_main_menu_native_show_message_prefix)) ||
        !dluma_is_readable(base + DLUMA_MAIN_MENU_NATIVE_NEWS_INITIALIZE_RVA, 1U) ||
        !dluma_is_readable(base + DLUMA_MAIN_MENU_NATIVE_NEWS_SHOW_RVA, 1U))
        return 0;
    engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!engine)
        return 0;
    state->find_element = (DlumaFindElementFn)GetProcAddress(
        engine,
        "?FindElement@IUIElement@@QEAAPEAV1@AEBV?$string_base@D@ttl@@_N@Z");
    state->set_text_explicit = (DlumaSetTextExplicitFn)GetProcAddress(
        engine,
        "?SetTextExplicit@IUIText@@UEAAXAEBV?$string_base@_W@ttl@@@Z");
    state->get_text_explicit = (DlumaGetTextExplicitFn)GetProcAddress(
        engine,
        "?GetTextExplicit@IUIText@@UEAAAEBV?$string_base@_W@ttl@@XZ");
    state->get_time_delta = (DlumaGuiObjectGetTimeDeltaFn)GetProcAddress(
        engine, "?GetTimeDelta@IObject@gui@@QEBAMXZ");
    if (!state->find_element || !state->set_text_explicit ||
        !state->get_text_explicit || !state->get_time_delta)
        return 0;
    state->gamedll_base = base;
    state->image_size = image_size;
    state->post_slot = (void **)(base + DLUMA_MAIN_MENU_NATIVE_POST_SLOT_RVA);
    state->init_slot = (void **)(base + DLUMA_MAIN_MENU_NATIVE_INIT_SLOT_RVA);
    state->post_original = (DlumaMenuMainOnPostUpdateFn)(base + DLUMA_MAIN_MENU_NATIVE_POST_RVA);
    state->init_original = (DlumaMenuMainOnInitFn)(base + DLUMA_MAIN_MENU_NATIVE_INIT_RVA);
    state->add_message = (DlumaNewsFeedAddMessageFn)(base + DLUMA_MAIN_MENU_NATIVE_ADD_MESSAGE_RVA);
    state->clear_type = (DlumaNewsFeedClearTypeFn)(base + DLUMA_MAIN_MENU_NATIVE_CLEAR_TYPE_RVA);
    state->news_post_update = (DlumaNewsFeedOnPostUpdateFn)(base + DLUMA_MAIN_MENU_NATIVE_NEWS_POST_RVA);
    state->show_message = (DlumaNewsFeedShowMessageFn)(base + DLUMA_MAIN_MENU_NATIVE_NEWS_SHOW_RVA);
    return 1;
}

int dluma_main_menu_native_presentation_start(void)
{
    DlumaMainMenuNativeState *state = &g_dluma_main_menu_native;
    if (state->installed)
        return 1;
    memset(state, 0, sizeof(*state));
    if (!dluma_main_menu_native_validate_windows_profile(state))
    {
        memset(state, 0, sizeof(*state));
        dluma_log_line("WARN", "main_menu_native_profile_unavailable_fail_closed");
        return 0;
    }
    if (!dluma_main_menu_native_patch_slot(
            state->post_slot, (void *)state->post_original,
            (void *)dluma_main_menu_native_on_post_update_hook))
    {
        memset(state, 0, sizeof(*state));
        dluma_log_line("WARN", "main_menu_on_post_hook_not_installed");
        return 0;
    }
    if (!dluma_main_menu_native_patch_slot(
            state->init_slot, (void *)state->init_original,
            (void *)dluma_main_menu_native_on_init_hook))
    {
        (void)dluma_main_menu_native_restore_slot(
            state->post_slot, (void *)dluma_main_menu_native_on_post_update_hook,
            (void *)state->post_original);
        memset(state, 0, sizeof(*state));
        dluma_log_line("WARN", "main_menu_hook_transaction_rolled_back");
        return 0;
    }
    state->installed = 1;
    state->profile_validated_logged = 1;
    dluma_log_line("INFO", "main_menu_native_profile_validated");
    dluma_log_line("INFO", "main_menu_on_post_hook_installed");
    dluma_log_line("INFO", "main_menu_on_init_hook_installed");
    return 1;
}

int dluma_main_menu_native_presentation_stop(void)
{
    DlumaMainMenuNativeState *state = &g_dluma_main_menu_native;
    ULONGLONG started;
    int post_restored;
    int init_restored;
    if (!state->installed)
        return 1;
    dluma_atomic_exchange(&state->stopping, 1);
    post_restored = dluma_main_menu_native_restore_slot(
        state->post_slot, (void *)dluma_main_menu_native_on_post_update_hook,
        (void *)state->post_original);
    init_restored = dluma_main_menu_native_restore_slot(
        state->init_slot, (void *)dluma_main_menu_native_on_init_hook,
        (void *)state->init_original);
    if (!post_restored || !init_restored)
    {
        dluma_log_line("WARN", "main_menu_native_hook_restore_ownership_lost_retained");
        return 0;
    }
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(&state->active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < DLUMA_MAIN_MENU_NATIVE_STOP_DRAIN_MS)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&state->active_calls, 0, 0) != 0)
    {
        dluma_log_line("WARN", "main_menu_native_hook_drain_timeout_retained");
        return 0;
    }
    memset(state, 0, sizeof(*state));
    return 1;
}

int dluma_main_menu_native_presentation_is_installed(void)
{
    return g_dluma_main_menu_native.installed != 0;
}
