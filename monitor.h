#ifndef MONITOR_H
#define MONITOR_H

#include "ring_buffer.h"
#include "counters.h"

/* What the monitor thread needs: the buffer (for occupancy), the counters
 * (to snapshot), and a run flag it polls each second to know when to stop. */
typedef struct {
    ring_buffer_t *rb;
    counters_t    *counters;
    volatile int  *running;     /* loops until *running becomes 0 */
} monitor_ctx_t;

/* Monitor pthread entry. arg must be a (monitor_ctx_t *). */
void *monitor_thread(void *arg);

#endif /* MONITOR_H */
