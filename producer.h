#ifndef PRODUCER_H
#define PRODUCER_H

#include "ring_buffer.h"

/* Entry point for the producer pthread.
 * arg must be a (ring_buffer_t *) — every received JSON frame is pushed there. */
void *producer_thread(void *arg);

/* Ask the producer to stop and wake its event loop. Thread-safe: may be called
 * from another thread (it uses lws_cancel_service internally). */
void producer_stop(void);

#endif /* PRODUCER_H */
