#include "../src/dlw_orderly_close_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #condition);            \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int test_internal_close_is_one_shot_and_preserves_reason(void)
{
    DlumaOrderlyCloseToken token;
    DlumaOrderlyCloseSnapshot snapshot;
    dluma_orderly_close_token_initialize(&token);
    CHECK(dluma_orderly_close_token_publish(
        &token,
        DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED,
        7, "mandatory_preflight_failed:audio_bank_tree_delta",
        41, 3, 1000));
    CHECK(!dluma_orderly_close_token_publish(
        &token,
        DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED,
        8, "duplicate", 41, 3, 1001));
    CHECK(dluma_orderly_close_token_mark_delivered(
        &token, 7, 52, 1002, 5000));
    CHECK(!dluma_orderly_close_token_consume(
        &token, 8, 52, 1003, 5000));
    CHECK(!token.consumed);
    CHECK(dluma_orderly_close_token_consume(
        &token, 7, 52, 1003, 5000));
    CHECK(!dluma_orderly_close_token_consume(
        &token, 7, 52, 1004, 5000));
    CHECK(dluma_orderly_close_token_snapshot(&token, &snapshot));
    CHECK(snapshot.origin ==
          DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED);
    CHECK(snapshot.generation == 7);
    CHECK(snapshot.epoch == 3);
    CHECK(snapshot.requested_thread == 41);
    CHECK(snapshot.delivered_thread == 52);
    CHECK(snapshot.internal_token_consumed);
    CHECK(strcmp(
        snapshot.reason,
        "mandatory_preflight_failed:audio_bank_tree_delta") == 0);
    return 1;
}

static int test_stale_or_unrelated_close_cannot_consume_token(void)
{
    DlumaOrderlyCloseToken token;
    dluma_orderly_close_token_initialize(&token);
    CHECK(dluma_orderly_close_token_publish(
        &token,
        DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED,
        1, "mandatory_preflight_failed:player_delta_reset_unavailable",
        4, 9, 100));
    CHECK(!dluma_orderly_close_token_mark_delivered(
        &token, 1, 5, 6001, 5000));
    CHECK(!token.delivered);
    CHECK(!token.consumed);
    return 1;
}

static int test_user_and_system_origin_names_are_distinct(void)
{
    CHECK(strcmp(
        dluma_orderly_close_origin_name(
            DLUMA_CLOSE_ORIGIN_USER_ALT_F4),
        "user_alt_f4") == 0);
    CHECK(strcmp(
        dluma_orderly_close_origin_name(
            DLUMA_CLOSE_ORIGIN_SYSTEM_MENU),
        "system_menu_close") == 0);
    CHECK(strcmp(
        dluma_orderly_close_origin_name(
            DLUMA_CLOSE_ORIGIN_SYSTEM_SESSION_END),
        "system_session_end") == 0);
    CHECK(strcmp(
        dluma_orderly_close_origin_name(
            DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED),
        "asset_isolation_fail_closed") == 0);
    return 1;
}

int main(void)
{
    if (!test_internal_close_is_one_shot_and_preserves_reason() ||
        !test_stale_or_unrelated_close_cannot_consume_token() ||
        !test_user_and_system_origin_names_are_distinct())
        return 1;
    puts("orderly close provenance fake harness: PASS");
    return 0;
}
