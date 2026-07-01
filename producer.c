/* ------------------------------------------------------------------ *
 *  Producer thread: connects to the Bluesky Jetstream firehose over a
 *  secure WebSocket, reassembles each JSON frame, and pushes it into the
 *  shared ring buffer. Event-driven via libwebsockets; never blocks the
 *  network loop (the ring buffer drops-newest on overflow).
 *
 *  Resilience (required by the brief):
 *    - automatic reconnection with exponential backoff + jitter
 *    - keepalive PING after 30 s idle, hang up + reconnect if dead 45 s
 *  Built against libwebsockets 4.3.x, following the official
 *  minimal-ws-client example.
 * ------------------------------------------------------------------ */
#include "producer.h"
#include "config.h"

#include <libwebsockets.h>
#include <string.h>

static struct lws_context *g_context;     /* the lws context              */
static ring_buffer_t      *g_rb;          /* where received frames go      */
static volatile int        g_interrupted; /* set by producer_stop()        */

/* Per-connection state. Single connection, so one static instance is fine. */
static struct my_conn {
    lws_sorted_usec_list_t sul;             /* schedules the next retry      */
    struct lws            *wsi;             /* the live connection, or NULL  */
    uint16_t               retry_count;     /* consecutive failed attempts   */
    char                   rx[MAX_MSG_SIZE];/* frame reassembly buffer       */
    size_t                 rx_len;
} mco;

/* Backoff schedule between reconnection attempts (ms). The last entry is
 * reused for all further attempts, and conceal_count is set high so the
 * client effectively never gives up during a 24 h run. */
static const uint32_t backoff_ms[] = { 1000, 2000, 3000, 5000, 5000 };

static const lws_retry_bo_t retry = {
    .retry_ms_table          = backoff_ms,
    .retry_ms_table_count    = LWS_ARRAY_SIZE(backoff_ms),
    .conceal_count           = 10000,   /* keep retrying ~forever          */
    .secs_since_valid_ping   = 30,      /* send PING after 30 s idle        */
    .secs_since_valid_hangup = 45,      /* drop dead link after 45 s        */
    .jitter_percent          = 20,
};

/* Scheduled callback that (re)starts a connection attempt. */
static void
connect_client(lws_sorted_usec_list_t *sul)
{
    struct my_conn *m = lws_container_of(sul, struct my_conn, sul);
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof i);
    i.context               = g_context;
    i.address               = WS_SERVER_ADDRESS;
    i.port                  = WS_SERVER_PORT;
    i.path                  = WS_SERVER_PATH;
    i.host                  = i.address;
    i.origin                = i.address;
    i.ssl_connection        = LCCSCF_USE_SSL;   /* wss:// over TLS          */
    i.protocol              = NULL;             /* server uses no subproto  */
    i.local_protocol_name   = "jetstream";      /* binds our callback       */
    i.pwsi                  = &m->wsi;
    i.retry_and_idle_policy = &retry;

    if (!lws_client_connect_via_info(&i))
        /* couldn't even start the attempt — schedule another with backoff */
        lws_retry_sul_schedule(g_context, 0, sul, &retry,
                               connect_client, &m->retry_count);
}

static int
callback_jetstream(struct lws *wsi, enum lws_callback_reasons reason,
                   void *user, void *in, size_t len)
{
    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_user("jetstream: connected\n");
        mco.retry_count = 0;     /* reset backoff after a good connection */
        mco.rx_len      = 0;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        /* A logical message may arrive in several fragments — reassemble. */
        if (lws_is_first_fragment(wsi))
            mco.rx_len = 0;

        if (mco.rx_len + len <= MAX_MSG_SIZE) {
            memcpy(mco.rx + mco.rx_len, in, len);
            mco.rx_len += len;
        } else {                              /* oversize: keep what fits */
            size_t space = MAX_MSG_SIZE - mco.rx_len;
            memcpy(mco.rx + mco.rx_len, in, space);
            mco.rx_len = MAX_MSG_SIZE;
        }

        /* Whole message in hand → hand it to the consumer via the buffer. */
        if (lws_is_final_fragment(wsi) && !lws_remaining_packet_payload(wsi)) {
            rb_try_push(g_rb, mco.rx, mco.rx_len);
            mco.rx_len = 0;
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        lwsl_err("jetstream: connection error: %s\n",
                 in ? (char *)in : "(none)");
        goto do_retry;

    case LWS_CALLBACK_CLIENT_CLOSED:
        lwsl_warn("jetstream: connection closed\n");
        goto do_retry;

    default:
        break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);

do_retry:
    mco.wsi = NULL;
    if (g_interrupted)
        return 0;                    /* shutting down: do not reconnect */
    /* schedule a reconnection with backoff; returns nonzero only if the
     * (very high) conceal_count is finally exhausted */
    if (lws_retry_sul_schedule_retry_wsi(wsi, &mco.sul, connect_client,
                                         &mco.retry_count))
        lwsl_err("jetstream: reconnection attempts exhausted\n");

    return 0;
}

static const struct lws_protocols protocols[] = {
    { .name = "jetstream", .callback = callback_jetstream },
    { 0 } /* terminator */
};

void *
producer_thread(void *arg)
{
    g_rb = (ring_buffer_t *)arg;

    /* keep errors/warnings/our own messages; silence verbose lws notices */
    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_USER, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof info);
    info.options             = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port                = CONTEXT_PORT_NO_LISTEN; /* client only      */
    info.protocols           = protocols;
    info.fd_limit_per_thread = 1 + 1 + 1;

    g_context = lws_create_context(&info);
    if (!g_context) {
        lwsl_err("jetstream: lws_create_context failed\n");
        return NULL;
    }

    /* kick off the first connection through the scheduler */
    connect_client(&mco.sul);

    while (!g_interrupted)
        lws_service(g_context, 0);   /* blocks on poll, driven by events  */

    lws_context_destroy(g_context);
    g_context = NULL;
    return NULL;
}

void
producer_stop(void)
{
    g_interrupted = 1;
    if (g_context)
        lws_cancel_service(g_context);   /* wake the blocked service loop */
}
