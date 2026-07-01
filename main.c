/* Production entry point. Starts the producer, consumer and monitor threads,
 * then waits for SIGINT (Ctrl-C) or SIGTERM and shuts everything down cleanly.
 * Designed to run unattended for the 24 h capture.
 *
 * Signal strategy: main blocks SIGINT/SIGTERM and the worker threads inherit
 * that mask, so the signal is delivered only here (via sigwait). The workers'
 * clock_nanosleep / lws_service are therefore never interrupted by it. */
#include <stdio.h>
#include <signal.h>
#include <pthread.h>

#include "ring_buffer.h"
#include "counters.h"
#include "producer.h"
#include "consumer.h"
#include "monitor.h"

int main(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);   /* threads inherit this mask */

    ring_buffer_t rb;       rb_init(&rb);
    counters_t    counters; counters_init(&counters);
    volatile int  running = 1;

    consumer_ctx_t cctx = { .rb = &rb, .counters = &counters };
    monitor_ctx_t  mctx = { .rb = &rb, .counters = &counters, .running = &running };

    pthread_t prod_tid, cons_tid, mon_tid;
    if (pthread_create(&prod_tid, NULL, producer_thread, &rb)   ||
        pthread_create(&cons_tid, NULL, consumer_thread, &cctx) ||
        pthread_create(&mon_tid,  NULL, monitor_thread,  &mctx)) {
        fprintf(stderr, "thread creation failed\n");
        return 1;
    }

    printf("firehose_monitor running.\n");
    printf("  endpoint : %s%s\n", WS_SERVER_ADDRESS, WS_SERVER_PATH);
    printf("  buffer   : %d slots x %d bytes\n", QUEUE_CAPACITY, MAX_MSG_SIZE);
    printf("  logging  : %s (1 row/s)\n", LOG_FILE_PATH);
    printf("Press Ctrl-C (or send SIGTERM) to stop.\n");
    fflush(stdout);

    /* wait here until a shutdown signal arrives */
    int sig;
    sigwait(&set, &sig);
    printf("\nSignal %d received - shutting down cleanly...\n", sig);

    running = 0;                 /* monitor exits within <= 1 s        */
    producer_stop();             /* breaks the lws service loop        */
    rb_signal_shutdown(&rb);     /* wakes the consumer out of rb_pop   */
    pthread_join(prod_tid, NULL);
    pthread_join(cons_tid, NULL);
    pthread_join(mon_tid,  NULL);

    counters_destroy(&counters);

    /* producer has joined, so this read needs no lock — a headline figure
     * for the report: how many frames the buffer had to drop (target: 0) */
    printf("Total frames dropped (buffer full): %llu\n", rb.dropped);

    rb_destroy(&rb);

    printf("Stopped. Metrics saved to %s\n", LOG_FILE_PATH);
    return 0;
}
