#include "dlw_main_menu_presentation_core.h"

static int dluma_main_menu_presentation_is_hex(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int dluma_main_menu_presentation_sha256_prefix(
    const char *digest,
    char out[DLUMA_MAIN_MENU_PRESENTATION_SHA256_PREFIX_LENGTH + 1U])
{
    unsigned int index;
    if (!digest || !out)
        return 0;
    out[0] = 0;
    for (index = 0;
         index < DLUMA_MAIN_MENU_PRESENTATION_SHA256_HEX_LENGTH;
         ++index)
    {
        if (!digest[index] ||
            !dluma_main_menu_presentation_is_hex(digest[index]))
            return 0;
        if (index < DLUMA_MAIN_MENU_PRESENTATION_SHA256_PREFIX_LENGTH)
            out[index] = digest[index];
    }
    if (digest[DLUMA_MAIN_MENU_PRESENTATION_SHA256_HEX_LENGTH] != 0)
    {
        out[0] = 0;
        return 0;
    }
    out[DLUMA_MAIN_MENU_PRESENTATION_SHA256_PREFIX_LENGTH] = 0;
    return 1;
}

int dluma_main_menu_presentation_git_commit(
    const char *commit,
    char out[DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH + 1U])
{
    unsigned int index;
    if (!commit || !out)
        return 0;
    for (index = 0;
         index < DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH;
         ++index)
    {
        if (!dluma_main_menu_presentation_is_hex(commit[index]))
        {
            out[0] = 0;
            return 0;
        }
        out[index] = commit[index];
    }
    if (commit[DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH])
    {
        out[0] = 0;
        return 0;
    }
    out[DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH] = 0;
    return 1;
}

static unsigned int dluma_main_menu_presentation_text_length(const char *text)
{
    unsigned int length = 0;
    if (!text)
        return 0;
    while (text[length])
        ++length;
    return length;
}

static int dluma_main_menu_presentation_matches(
    const char *source, const char *token, unsigned int token_length)
{
    unsigned int index;
    if (!source || !token || !token_length)
        return 0;
    for (index = 0; index < token_length; ++index)
    {
        if (source[index] != token[index])
            return 0;
    }
    return 1;
}

static int dluma_main_menu_presentation_append(
    const char *source, unsigned int length, char *out,
    unsigned int capacity, unsigned int *used)
{
    unsigned int index;
    if (!source || !out || !used || !capacity ||
        length >= capacity - *used)
        return 0;
    for (index = 0; index < length; ++index)
        out[*used + index] = source[index];
    *used += length;
    out[*used] = 0;
    return 1;
}

int dluma_main_menu_presentation_expand(
    const char *source,
    const DlumaMainMenuPresentationPlaceholder *placeholders,
    unsigned int placeholder_count,
    char *out,
    unsigned int capacity)
{
    const char *cursor = source ? source : "";
    unsigned int used = 0;
    if (!out || !capacity)
        return 0;
    out[0] = 0;
    while (*cursor)
    {
        unsigned int index;
        for (index = 0; index < placeholder_count; ++index)
        {
            const char *token = placeholders && placeholders[index].token
                ? placeholders[index].token : "";
            unsigned int token_length =
                dluma_main_menu_presentation_text_length(token);
            const char *value = placeholders && placeholders[index].value
                ? placeholders[index].value : "";
            if (!token_length ||
                !dluma_main_menu_presentation_matches(
                    cursor, token, token_length))
                continue;
            if (!dluma_main_menu_presentation_append(
                    value,
                    dluma_main_menu_presentation_text_length(value),
                    out, capacity, &used))
            {
                out[0] = 0;
                return 0;
            }
            cursor += token_length;
            break;
        }
        if (index == placeholder_count)
        {
            if (!dluma_main_menu_presentation_append(
                    cursor, 1U, out, capacity, &used))
            {
                out[0] = 0;
                return 0;
            }
            ++cursor;
        }
    }
    return 1;
}

int dluma_main_menu_presentation_expand_localized(
    const char *localized,
    int localized_available,
    const char *english_fallback,
    const DlumaMainMenuPresentationPlaceholder *placeholders,
    unsigned int placeholder_count,
    char *out,
    unsigned int capacity)
{
    const char *source =
        localized_available && localized && localized[0]
            ? localized
            : english_fallback;
    if (!source || !source[0])
    {
        if (out && capacity)
            out[0] = 0;
        return 0;
    }
    return dluma_main_menu_presentation_expand(
        source, placeholders, placeholder_count, out, capacity);
}

static int dluma_main_menu_presentation_is_leap_year(unsigned int year)
{
    return year && (!(year % 400U) ||
                    (!(year % 4U) && (year % 100U)));
}

static unsigned int dluma_main_menu_presentation_days_in_month(
    unsigned int year, unsigned int month)
{
    static const unsigned char days[] = {
        0U, 31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U};
    if (!year || month < 1U || month > 12U)
        return 0U;
    if (month == 2U && dluma_main_menu_presentation_is_leap_year(year))
        return 29U;
    return days[month];
}

int dluma_main_menu_presentation_date_override(
    const unsigned short *command_line, unsigned int year,
    unsigned int *month_out, unsigned int *day_out)
{
    static const char prefix[] = "-dluma_date_override";
    unsigned int pos = 0;
    if (!command_line || !month_out || !day_out)
        return 0;
    while (pos < 32767U && command_line[pos])
    {
        unsigned short token[64];
        unsigned int used = 0, index, slash_count;
        int quoted = 0, overflow = 0;
        unsigned int month, day;
        while (pos < 32767U &&
               (command_line[pos] == ' ' || command_line[pos] == '\t'))
            ++pos;
        if (pos >= 32767U || !command_line[pos])
            break;
        while (pos < 32767U && command_line[pos] &&
               (quoted || (command_line[pos] != ' ' && command_line[pos] != '\t')))
        {
            unsigned short ch = command_line[pos++];
            if (ch == '\\')
            {
                slash_count = 1U;
                while (pos < 32767U && command_line[pos] == '\\')
                {
                    ++pos;
                    ++slash_count;
                }
                if (pos < 32767U && command_line[pos] == '"')
                {
                    for (index = 0; index < slash_count / 2U; ++index)
                    {
                        if (used < 63U) token[used++] = '\\';
                        else overflow = 1;
                    }
                    ++pos;
                    if (slash_count % 2U)
                    {
                        if (used < 63U) token[used++] = '"';
                        else overflow = 1;
                    }
                    else quoted = !quoted;
                    continue;
                }
                for (index = 0; index < slash_count; ++index)
                {
                    if (used < 63U) token[used++] = '\\';
                    else overflow = 1;
                }
                continue;
            }
            if (ch == '"') { quoted = !quoted; continue; }
            if (used < 63U) token[used++] = ch;
            else overflow = 1;
        }
        token[used] = 0;
        for (index = 0; prefix[index]; ++index)
        {
            unsigned short ch = token[index];
            if (!ch) break;
            if (ch >= 'A' && ch <= 'Z') ch = (unsigned short)(ch + 'a' - 'A');
            if (ch != (unsigned short)prefix[index]) break;
        }
        if (prefix[index] || (token[index] && token[index] != '='))
            continue;
        if (overflow || quoted || pos >= 32767U || token[index++] != '=' ||
            used != index + 5U || token[index + 2U] != '/' ||
            token[index] < '0' || token[index] > '9' ||
            token[index + 1U] < '0' || token[index + 1U] > '9' ||
            token[index + 3U] < '0' || token[index + 3U] > '9' ||
            token[index + 4U] < '0' || token[index + 4U] > '9')
            return -1;
        month = (unsigned int)(token[index] - '0') * 10U +
                (unsigned int)(token[index + 1U] - '0');
        day = (unsigned int)(token[index + 3U] - '0') * 10U +
              (unsigned int)(token[index + 4U] - '0');
        if (year < 1583U || year > 9999U ||
            !day || day > dluma_main_menu_presentation_days_in_month(year, month))
            return -1;
        *month_out = month;
        *day_out = day;
        return 1;
    }
    return 0;
}

static unsigned int dluma_main_menu_presentation_day_of_year(
    unsigned int year, unsigned int month, unsigned int day)
{
    unsigned int current;
    unsigned int ordinal = day;
    for (current = 1U; current < month; ++current)
        ordinal += dluma_main_menu_presentation_days_in_month(year, current);
    return ordinal;
}

static unsigned long long dluma_main_menu_presentation_day_number(
    unsigned int year, unsigned int month, unsigned int day)
{
    unsigned long long previous_year = (unsigned long long)year - 1ULL;
    return previous_year * 365ULL +
           previous_year / 4ULL -
           previous_year / 100ULL +
           previous_year / 400ULL +
           (unsigned long long)
               dluma_main_menu_presentation_day_of_year(year, month, day);
}

static void dluma_main_menu_presentation_easter(
    unsigned int year, unsigned int *month_out, unsigned int *day_out)
{
    unsigned int a = year % 19U;
    unsigned int b = year / 100U;
    unsigned int c = year % 100U;
    unsigned int d = b / 4U;
    unsigned int e = b % 4U;
    unsigned int f = (b + 8U) / 25U;
    unsigned int g = (b - f + 1U) / 3U;
    unsigned int h = (19U * a + b - d - g + 15U) % 30U;
    unsigned int i = c / 4U;
    unsigned int k = c % 4U;
    unsigned int l = (32U + 2U * e + 2U * i - h - k) % 7U;
    unsigned int m = (a + 11U * h + 22U * l) / 451U;
    unsigned int value = h + l - 7U * m + 114U;
    *month_out = value / 31U;
    *day_out = value % 31U + 1U;
}

DlumaMainMenuHoliday dluma_main_menu_presentation_holiday(
    unsigned int year, unsigned int month, unsigned int day)
{
    unsigned int easter_month;
    unsigned int easter_day;
    unsigned long long current_number;
    unsigned long long easter_number;
    unsigned long long may_first;
    unsigned long long june_first;
    unsigned int may_first_weekday;
    unsigned int june_first_weekday;
    unsigned int mothers_day;
    unsigned int fathers_day;
    unsigned long long mothers_number;
    unsigned long long fathers_number;
    unsigned long long november_first;
    unsigned int november_first_weekday;
    unsigned int first_thursday;
    unsigned int thanksgiving_day;
    unsigned long long thanksgiving_number;
    if (year < 1583U || year > 9999U ||
        !dluma_main_menu_presentation_days_in_month(year, month) ||
        day < 1U ||
        day > dluma_main_menu_presentation_days_in_month(year, month))
        return DLUMA_MAIN_MENU_HOLIDAY_NONE;

    if ((month == 12U && day == 31U) ||
        (month == 1U && day <= 2U))
        return DLUMA_MAIN_MENU_HOLIDAY_NEW_YEAR;

    if (month == 2U && day >= 13U && day <= 15U)
        return DLUMA_MAIN_MENU_HOLIDAY_VALENTINES;

    if (month == 3U && day >= 16U && day <= 18U)
        return DLUMA_MAIN_MENU_HOLIDAY_SAINT_PATRICKS;

    current_number =
        dluma_main_menu_presentation_day_number(year, month, day);
    dluma_main_menu_presentation_easter(
        year, &easter_month, &easter_day);
    easter_number = dluma_main_menu_presentation_day_number(
        year, easter_month, easter_day);
    if (current_number + 2ULL >= easter_number &&
        current_number <= easter_number + 1ULL)
        return DLUMA_MAIN_MENU_HOLIDAY_EASTER;

    may_first =
        dluma_main_menu_presentation_day_number(year, 5U, 1U);
    may_first_weekday = (unsigned int)(may_first % 7ULL);
    mothers_day = 1U + (7U - may_first_weekday) % 7U + 7U;
    mothers_number = dluma_main_menu_presentation_day_number(
        year, 5U, mothers_day);
    if (current_number + 1ULL >= mothers_number &&
        current_number <= mothers_number + 1ULL)
        return DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY;

    june_first =
        dluma_main_menu_presentation_day_number(year, 6U, 1U);
    june_first_weekday = (unsigned int)(june_first % 7ULL);
    fathers_day = 1U + (7U - june_first_weekday) % 7U + 14U;
    fathers_number = dluma_main_menu_presentation_day_number(
        year, 6U, fathers_day);
    if (current_number + 1ULL >= fathers_number &&
        current_number <= fathers_number + 1ULL)
        return DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY;

    if (month == 7U && day >= 3U && day <= 5U)
        return DLUMA_MAIN_MENU_HOLIDAY_INDEPENDENCE_DAY;

    if (month == 7U && day >= 6U && day <= 8U)
        return DLUMA_MAIN_MENU_HOLIDAY_TANABATA;

    if (month == 10U && day >= 8U && day <= 10U)
        return DLUMA_MAIN_MENU_HOLIDAY_LEIF_ERIKSON;

    if ((month == 10U && day >= 28U) ||
        (month == 11U && day == 1U))
        return DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN;

    november_first =
        dluma_main_menu_presentation_day_number(year, 11U, 1U);
    /* Day number 1 is Monday. Convert to Win32's Sunday=0 convention. */
    november_first_weekday =
        (unsigned int)(november_first % 7ULL);
    first_thursday =
        1U + (4U + 7U - november_first_weekday) % 7U;
    thanksgiving_day = first_thursday + 21U;
    thanksgiving_number = dluma_main_menu_presentation_day_number(
        year, 11U, thanksgiving_day);
    if (current_number + 1ULL >= thanksgiving_number &&
        current_number <= thanksgiving_number + 3ULL)
        return DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING;

    if (month == 12U && day >= 20U && day <= 30U)
        return DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS;

    return DLUMA_MAIN_MENU_HOLIDAY_NONE;
}

int dluma_main_menu_presentation_halloween_animation(
    unsigned int year, unsigned int month, unsigned int day,
    unsigned int startup_roll)
{
    return dluma_main_menu_presentation_holiday(year, month, day) ==
        DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN && startup_roll % 10U == 0U;
}

const DlumaMainMenuAppearanceSet *dluma_main_menu_presentation_appearances(
    unsigned int year, unsigned int month, unsigned int day)
{
    typedef struct AppearanceRule {
        DlumaMainMenuHoliday holiday;
        unsigned int month;
        unsigned int day;
        const DlumaMainMenuAppearanceSet *set;
    } AppearanceRule;
    static const DlumaMainMenuAppearance halloween[] = {
        {"player_1_tpp.msh", "player_1_zombie"},
        {"player_12_tpp.msh", "player_12_zombie"},
        {"player_2_tpp.msh", "player_2_zombie"},
        {"player_4_tpp.msh", "phosphor_paint_a"}
    };
    static const DlumaMainMenuAppearance christmas[] = {
        {"player_2_tpp.msh", "Christma_event"},
        {"player_9_tpp.msh", "Christmas_event"},
        {"player_14_tpp.msh", "Christmas_event"}
    };
    static const DlumaMainMenuAppearance anniversary[] = {
        {"player_11_tpp.msh", "TECHLAND_2"}
    };
    static const DlumaMainMenuAppearance parents_day[] = {
        {"player_acolyte_tpp.msh", "5th_anniversary"}
    };
    static const DlumaMainMenuAppearance tanabata[] = {
        {"player_11_tpp.msh", "JAP"}
    };
    static const DlumaMainMenuAppearanceSet halloween_set = {halloween, 4U};
    static const DlumaMainMenuAppearanceSet christmas_set = {christmas, 3U};
    static const DlumaMainMenuAppearanceSet anniversary_set = {anniversary, 1U};
    static const DlumaMainMenuAppearanceSet parents_day_set = {parents_day, 1U};
    static const DlumaMainMenuAppearanceSet tanabata_set = {tanabata, 1U};
    static const AppearanceRule rules[] = {
        {DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN, 0U, 0U, &halloween_set},
        {DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS, 0U, 0U, &christmas_set},
        {DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY, 0U, 0U, &parents_day_set},
        {DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY, 0U, 0U, &parents_day_set},
        {DLUMA_MAIN_MENU_HOLIDAY_TANABATA, 0U, 0U, &tanabata_set},
        {DLUMA_MAIN_MENU_HOLIDAY_NONE, 1U, 26U, &anniversary_set}
    };
    DlumaMainMenuHoliday holiday;
    unsigned int index;

    if (year < 1583U || year > 9999U ||
        !dluma_main_menu_presentation_days_in_month(year, month) ||
        day < 1U ||
        day > dluma_main_menu_presentation_days_in_month(year, month))
        return 0;

    holiday = dluma_main_menu_presentation_holiday(year, month, day);
    for (index = 0; index < sizeof(rules) / sizeof(rules[0]); ++index)
    {
        const AppearanceRule *rule = &rules[index];
        if ((rule->holiday != DLUMA_MAIN_MENU_HOLIDAY_NONE &&
             rule->holiday == holiday) ||
            (rule->holiday == DLUMA_MAIN_MENU_HOLIDAY_NONE &&
             rule->month == month && rule->day == day))
            return rule->set;
    }
    return 0;
}

const DlumaMainMenuPropSet *dluma_main_menu_presentation_props(
    unsigned int year, unsigned int month, unsigned int day)
{
    static const DlumaMainMenuPropProfile thanksgiving[] = {
        {"dlc_stick_chicken_b.msh", 0, DLUMA_MAIN_MENU_PROP_GRIP_ONE_HANDED,
         "TPP_Stand_menu_A"},
        {"dlc_stick_chicken.msh", 0, DLUMA_MAIN_MENU_PROP_GRIP_ONE_HANDED,
         "TPP_Stand_menu_A"}
    };
    static const DlumaMainMenuPropProfile christmas[] = {
        {"wn_christmascandy.msh", 0, DLUMA_MAIN_MENU_PROP_GRIP_ONE_HANDED,
         "TPP_Stand_menu_A"}
    };
    static const DlumaMainMenuPropSet thanksgiving_set = {thanksgiving, 2U};
    static const DlumaMainMenuPropSet christmas_set = {christmas, 1U};

    switch (dluma_main_menu_presentation_holiday(year, month, day))
    {
    case DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING:
        return &thanksgiving_set;
    case DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS:
        return &christmas_set;
    default:
        return 0;
    }
}
