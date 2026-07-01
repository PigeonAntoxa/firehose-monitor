#ifndef COUNTERS_H
#define COUNTERS_H

#include <pthread.h>
#include <stdint.h>

/* The four message kinds the brief asks us to count, plus a diagnostic
 * bucket for parse failures / unrecognized kinds (not written to the CSV). */
typedef enum {
    KIND_COMMIT = 0,
    KIND_IDENTITY,
    KIND_ACCOUNT,
    KIND_INFO,
    KIND_OTHER
} msg_kind_t;

typedef struct {
    uint64_t commit;
    uint64_t identity;
    uint64_t account;
    uint64_t info;
    uint64_t other;
} counts_t;

/* Shared counters, protected by their OWN mutex — separate from the ring
 * buffer's lock so the once-per-second monitor snapshot doesn't contend with
 * the high-frequency producer<->consumer handoff. */
typedef struct {
    counts_t        c;
    pthread_mutex_t lock;
} counters_t;

void counters_init(counters_t *m);
void counters_destroy(counters_t *m);

/* Consumer side: bump one kind's counter (brief lock). */
void counters_inc(counters_t *m, msg_kind_t k);

/* Monitor side: atomically copy the current counts into *out and zero them,
 * so each call returns exactly the counts accumulated since the previous call
 * (the per-second window the brief requires). */
void counters_snapshot_reset(counters_t *m, counts_t *out);

#endif /* COUNTERS_H */
