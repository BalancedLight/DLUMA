#ifndef DLUMA_ORDERLY_CLOSE_CORE_H
#define DLUMA_ORDERLY_CLOSE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_ORDERLY_CLOSE_REASON_CAPACITY 192U

typedef enum DlumaOrderlyCloseOrigin
{
    DLUMA_CLOSE_ORIGIN_UNKNOWN = 0,
    DLUMA_CLOSE_ORIGIN_USER_ALT_F4,
    DLUMA_CLOSE_ORIGIN_SYSTEM_MENU,
    DLUMA_CLOSE_ORIGIN_SYSTEM_SESSION_END,
    DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED,
    DLUMA_CLOSE_ORIGIN_FIRST_LAUNCH_DECLINED
} DlumaOrderlyCloseOrigin;

typedef struct DlumaOrderlyCloseToken
{
    DlumaOrderlyCloseOrigin origin;
    unsigned long long generation;
    unsigned long long epoch;
    unsigned long long post_time_ms;
    unsigned long long delivery_time_ms;
    unsigned long requester_thread;
    unsigned long delivered_thread;
    unsigned int delivered;
    unsigned int consumed;
    char reason[DLUMA_ORDERLY_CLOSE_REASON_CAPACITY];
} DlumaOrderlyCloseToken;

typedef struct DlumaOrderlyCloseSnapshot
{
    DlumaOrderlyCloseOrigin origin;
    unsigned long long generation;
    unsigned long long epoch;
    unsigned long long post_time_ms;
    unsigned long long delivery_time_ms;
    unsigned long requested_thread;
    unsigned long delivered_thread;
    unsigned int delivered;
    unsigned int internal_token_consumed;
    char reason[DLUMA_ORDERLY_CLOSE_REASON_CAPACITY];
} DlumaOrderlyCloseSnapshot;

void dluma_orderly_close_token_initialize(DlumaOrderlyCloseToken *token);
int dluma_orderly_close_token_publish(
    DlumaOrderlyCloseToken *token,
    DlumaOrderlyCloseOrigin origin,
    unsigned long long generation,
    const char *reason,
    unsigned long requester_thread,
    unsigned long long epoch,
    unsigned long long post_time_ms);
int dluma_orderly_close_token_mark_delivered(
    DlumaOrderlyCloseToken *token,
    unsigned long long generation,
    unsigned long delivered_thread,
    unsigned long long delivery_time_ms,
    unsigned long long maximum_age_ms);
int dluma_orderly_close_token_consume(
    DlumaOrderlyCloseToken *token,
    unsigned long long generation,
    unsigned long delivered_thread,
    unsigned long long now_ms,
    unsigned long long maximum_age_ms);
int dluma_orderly_close_token_snapshot(
    const DlumaOrderlyCloseToken *token,
    DlumaOrderlyCloseSnapshot *snapshot);
const char *dluma_orderly_close_origin_name(DlumaOrderlyCloseOrigin origin);

#ifdef __cplusplus
}
#endif

#endif
