#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include "config.h"

/* One slot holds a single raw JSON frame plus its length. Fixed-size slots
 * mean the producer never calls malloc() on the network hot path. */
typedef struct {
    char   data[MAX_MSG_SIZE];
    size_t len;
} slot_t;

/* Bounded circular queue with its own lock + condition variable.
 * The mutex/condvar are encapsulated here so producer and consumer can't
 * disagree about the locking discipline. */
typedef struct {
    slot_t            *slots;        /* heap array of QUEUE_CAPACITY slots   */
    size_t             head;         /* next index to write                  */
    size_t             tail;         /* next index to read                   */
    size_t             count;        /* current number of queued items       */
    unsigned long long dropped;      /* frames dropped because queue was full*/
    bool               shutdown;     /* set true to release a blocked pop()  */
    pthread_mutex_t    lock;
    pthread_cond_t     not_empty;
} ring_buffer_t;

/* Lifecycle */
void rb_init(ring_buffer_t *rb);
void rb_destroy(ring_buffer_t *rb);

/* Producer side — NON-blocking. If the queue is full the frame is dropped and
 * the dropped counter is incremented; the producer thread never blocks, so the
 * libwebsockets event loop keeps servicing the socket. Returns true if stored. */
bool rb_try_push(ring_buffer_t *rb, const char *msg, size_t len);

/* Consumer side — BLOCKING. Copies the next frame into `out` (must be
 * MAX_MSG_SIZE bytes) and sets *out_len. Returns false only when woken for
 * shutdown with the queue already empty. */
bool rb_pop(ring_buffer_t *rb, char *out, size_t *out_len);

/* Monitor side — current fill level as a percentage (0..100). Brief lock. */
double rb_occupancy_pct(ring_buffer_t *rb);

/* Ask a blocked consumer to wake and exit. */
void rb_signal_shutdown(ring_buffer_t *rb);

#endif /* RING_BUFFER_H */
