#include "dlw_common.h"
#include "dlw_main_menu_presentation_core.h"
#include "dlw_seasonal_date.h"

static WORD dluma_seasonal_day_of_week(unsigned int year,
                                       unsigned int month,
                                       unsigned int day)
{
    static const unsigned char offsets[] = {
        0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U};
    if (month < 3U)
        --year;
    return (WORD)((year + year / 4U - year / 100U + year / 400U +
                   offsets[month - 1U] + day) % 7U);
}

#ifdef DLUMA_MAIN_MENU_APPEARANCE_DATE
static int dluma_seasonal_private_date_valid(unsigned int year,
                                             unsigned int month,
                                             unsigned int day)
{
    static const unsigned char days[] = {
        0U, 31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U};
    unsigned int limit;
    if (year < 1583U || year > 9999U || month < 1U || month > 12U || !day)
        return 0;
    limit = days[month];
    if (month == 2U && (!(year % 400U) || (!(year % 4U) && year % 100U)))
        ++limit;
    return day <= limit;
}
#endif

void dluma_seasonal_date_get(SYSTEMTIME *out)
{
    static volatile LONG logged_active;
    static volatile LONG logged_invalid;
    unsigned int month, day;
    int result;

    if (!out)
        return;
    GetLocalTime(out);
    result = dluma_main_menu_presentation_date_override(
        GetCommandLineW(), out->wYear, &month, &day);
    if (result > 0)
    {
        out->wMonth = (WORD)month;
        out->wDay = (WORD)day;
        out->wDayOfWeek = dluma_seasonal_day_of_week(out->wYear, month, day);
        if (dluma_atomic_compare_exchange(&logged_active, 1, 0) == 0)
            dluma_log_line("INFO", "seasonal_date_override_active");
    }
    else if (result < 0 &&
             dluma_atomic_compare_exchange(&logged_invalid, 1, 0) == 0)
        dluma_log_line("WARN", "seasonal_date_override_invalid");
#ifdef DLUMA_MAIN_MENU_APPEARANCE_DATE
    else if (result == 0)
    {
        unsigned int test_year = (unsigned int)(DLUMA_MAIN_MENU_APPEARANCE_DATE / 10000);
        unsigned int test_month = (unsigned int)((DLUMA_MAIN_MENU_APPEARANCE_DATE / 100) % 100);
        unsigned int test_day = (unsigned int)(DLUMA_MAIN_MENU_APPEARANCE_DATE % 100);
        if (dluma_seasonal_private_date_valid(test_year, test_month, test_day))
        {
            out->wYear = (WORD)test_year;
            out->wMonth = (WORD)test_month;
            out->wDay = (WORD)test_day;
            out->wDayOfWeek = dluma_seasonal_day_of_week(test_year, test_month, test_day);
        }
    }
#endif
}
