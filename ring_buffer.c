#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>

void rb_init(ring_buffer_t *rb)
{
    rb->slots = calloc(QUEUE_CAPACITY, sizeof(slot_t));
    rb->head = rb->tail = rb->count = 0;
    rb->dropped  = 0;
    rb->shutdown = false;
    pthread_mutex_init(&rb->lock, NULL);
    pthread_cond_init(&rb->not_empty, NULL);
}

void rb_destroy(ring_buffer_t *rb)
{
    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->not_empty);
    free(rb->slots);
    rb->slots = NULL;
}

bool rb_try_push(ring_buffer_t *rb, const char *msg, size_t len)
{
    if (len >= MAX_MSG_SIZE)        /* keep one byte for the NUL terminator */
        len = MAX_MSG_SIZE - 1;

    pthread_mutex_lock(&rb->lock);

    if (rb->count == QUEUE_CAPACITY) {
        rb->dropped++;              /* drop-newest: never block the network */
        pthread_mutex_unlock(&rb->lock);
        return false;
    }

    slot_t *s = &rb->slots[rb->head];
    memcpy(s->data, msg, len);
    s->data[len] = '\0';
    s->len = len;

    rb->head = (rb->head + 1) % QUEUE_CAPACITY;
    rb->count++;

    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->lock);
    return true;
}

bool rb_pop(ring_buffer_t *rb, char *out, size_t *out_len)
{
    pthread_mutex_lock(&rb->lock);

    while (rb->count == 0 && !rb->shutdown)
        pthread_cond_wait(&rb->not_empty, &rb->lock);

    if (rb->count == 0 && rb->shutdown) {       /* drained and asked to stop */
        pthread_mutex_unlock(&rb->lock);
        return false;
    }

    slot_t *s = &rb->slots[rb->tail];
    memcpy(out, s->data, s->len + 1);           /* include the NUL */
    *out_len = s->len;

    rb->tail = (rb->tail + 1) % QUEUE_CAPACITY;
    rb->count--;

    pthread_mutex_unlock(&rb->lock);
    return true;
}

double rb_occupancy_pct(ring_buffer_t *rb)
{
    pthread_mutex_lock(&rb->lock);
    double pct = (double)rb->count * 100.0 / (double)QUEUE_CAPACITY;
    pthread_mutex_unlock(&rb->lock);
    return pct;
}

void rb_signal_shutdown(ring_buffer_t *rb)
{
    pthread_mutex_lock(&rb->lock);
    rb->shutdown = true;
    pthread_cond_broadcast(&rb->not_empty);
    pthread_mutex_unlock(&rb->lock);
}
