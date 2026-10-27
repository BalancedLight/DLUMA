#include "dlw_orderly_close_core.h"

static void dluma_orderly_close_copy_reason(
    char *output, const char *reason)
{
    unsigned int index = 0;
    if (!output) return;
    if (reason) {
        while (reason[index] &&
               index + 1U < DLUMA_ORDERLY_CLOSE_REASON_CAPACITY) {
            output[index] = reason[index];
            ++index;
        }
    }
    output[index] = 0;
}

static int dluma_orderly_close_not_expired(
    const DlumaOrderlyCloseToken *token,
    unsigned long long now_ms,
    unsigned long long maximum_age_ms)
{
    return token && token->generation && maximum_age_ms &&
        now_ms >= token->post_time_ms &&
        now_ms - token->post_time_ms <= maximum_age_ms;
}

void dluma_orderly_close_token_initialize(DlumaOrderlyCloseToken *token)
{
    unsigned int index;
    unsigned char *bytes;
    if (!token) return;
    bytes = (unsigned char *)token;
    for (index = 0; index < (unsigned int)sizeof(*token); ++index)
        bytes[index] = 0;
}

int dluma_orderly_close_token_publish(
    DlumaOrderlyCloseToken *token,
    DlumaOrderlyCloseOrigin origin,
    unsigned long long generation,
    const char *reason,
    unsigned long requester_thread,
    unsigned long long epoch,
    unsigned long long post_time_ms)
{
    if (!token || !generation || !reason || !reason[0] ||
        !requester_thread ||
        (origin != DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED &&
         origin != DLUMA_CLOSE_ORIGIN_FIRST_LAUNCH_DECLINED) ||
        (token->generation && !token->consumed))
        return 0;
    dluma_orderly_close_token_initialize(token);
    token->origin = origin;
    token->generation = generation;
    token->epoch = epoch;
    token->post_time_ms = post_time_ms;
    token->requester_thread = requester_thread;
    dluma_orderly_close_copy_reason(token->reason, reason);
    return token->reason[0] != 0;
}

int dluma_orderly_close_token_mark_delivered(
    DlumaOrderlyCloseToken *token,
    unsigned long long generation,
    unsigned long delivered_thread,
    unsigned long long delivery_time_ms,
    unsigned long long maximum_age_ms)
{
    if (!token || !generation ||
        token->generation != generation ||
        token->delivered || token->consumed ||
        !delivered_thread ||
        !dluma_orderly_close_not_expired(
            token, delivery_time_ms, maximum_age_ms))
        return 0;
    token->delivered_thread = delivered_thread;
    token->delivery_time_ms = delivery_time_ms;
    token->delivered = 1;
    return 1;
}

int dluma_orderly_close_token_consume(
    DlumaOrderlyCloseToken *token,
    unsigned long long generation,
    unsigned long delivered_thread,
    unsigned long long now_ms,
    unsigned long long maximum_age_ms)
{
    if (!token || !generation ||
        token->generation != generation ||
        !token->delivered || token->consumed ||
        token->delivered_thread != delivered_thread ||
        !dluma_orderly_close_not_expired(
            token, now_ms, maximum_age_ms))
        return 0;
    token->consumed = 1;
    return 1;
}

int dluma_orderly_close_token_snapshot(
    const DlumaOrderlyCloseToken *token,
    DlumaOrderlyCloseSnapshot *snapshot)
{
    if (!token || !snapshot) return 0;
    snapshot->origin = token->origin;
    snapshot->generation = token->generation;
    snapshot->epoch = token->epoch;
    snapshot->post_time_ms = token->post_time_ms;
    snapshot->delivery_time_ms = token->delivery_time_ms;
    snapshot->requested_thread = token->requester_thread;
    snapshot->delivered_thread = token->delivered_thread;
    snapshot->delivered = token->delivered;
    snapshot->internal_token_consumed = token->consumed;
    dluma_orderly_close_copy_reason(snapshot->reason, token->reason);
    return 1;
}

const char *dluma_orderly_close_origin_name(DlumaOrderlyCloseOrigin origin)
{
    switch (origin) {
    case DLUMA_CLOSE_ORIGIN_UNKNOWN:
        return "unknown";
    case DLUMA_CLOSE_ORIGIN_USER_ALT_F4:
        return "user_alt_f4";
    case DLUMA_CLOSE_ORIGIN_SYSTEM_MENU:
        return "system_menu_close";
    case DLUMA_CLOSE_ORIGIN_SYSTEM_SESSION_END:
        return "system_session_end";
    case DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED:
        return "asset_isolation_fail_closed";
    case DLUMA_CLOSE_ORIGIN_FIRST_LAUNCH_DECLINED:
        return "first_launch_declined";
    default:
        return "invalid";
    }
}
