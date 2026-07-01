#ifndef CONSUMER_H
#define CONSUMER_H

#include "ring_buffer.h"
#include "counters.h"

/* What the consumer thread needs: where to read frames, where to count them. */
typedef struct {
    ring_buffer_t *rb;
    counters_t    *counters;
} consumer_ctx_t;

/* Parse one JSON frame and return its "kind" (KIND_OTHER on parse failure or
 * an unknown kind). Pure function, exposed so it can be unit-tested. */
msg_kind_t consumer_classify(const char *json, size_t len);

/* Consumer pthread entry. arg must be a (consumer_ctx_t *). */
void *consumer_thread(void *arg);

#endif /* CONSUMER_H */
