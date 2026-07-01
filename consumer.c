/* ------------------------------------------------------------------ *
 *  Consumer thread: blocks on the ring buffer, parses each JSON frame with
 *  cJSON, reads the top-level "kind" field, and bumps the matching counter.
 *
 *  Per the brief, this thread does NO printf and NO file I/O — it only parses
 *  and updates the mutex-protected counters, so it can keep up with bursts.
 * ------------------------------------------------------------------ */
#include "consumer.h"

#include <string.h>
#include <cjson/cJSON.h>

msg_kind_t
consumer_classify(const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root)
        return KIND_OTHER;                 /* malformed/truncated frame */

    msg_kind_t k = KIND_OTHER;
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    if (cJSON_IsString(kind) && kind->valuestring) {
        const char *s = kind->valuestring;
        if      (!strcmp(s, "commit"))   k = KIND_COMMIT;
        else if (!strcmp(s, "identity")) k = KIND_IDENTITY;
        else if (!strcmp(s, "account"))  k = KIND_ACCOUNT;
        else if (!strcmp(s, "info"))     k = KIND_INFO;
    }

    cJSON_Delete(root);                    /* free the tree — no leaks */
    return k;
}

void *
consumer_thread(void *arg)
{
    consumer_ctx_t *ctx = (consumer_ctx_t *)arg;
    char   buf[MAX_MSG_SIZE];
    size_t len;

    /* rb_pop blocks until a frame is ready and returns false on shutdown. */
    while (rb_pop(ctx->rb, buf, &len)) {
        msg_kind_t k = consumer_classify(buf, len);
        counters_inc(ctx->counters, k);
    }
    return NULL;
}
