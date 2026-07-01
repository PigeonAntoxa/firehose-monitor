/* ------------------------------------------------------------------ *
 *  Monitor thread: fires once per second on a drift-free schedule and writes
 *  one CSV line per tick to metrics_log.txt.
 *
 *  Timing: we sleep to ABSOLUTE deadlines on CLOCK_MONOTONIC
 *  (clock_nanosleep + TIMER_ABSTIME). Each deadline is start + N seconds, so
 *  the work done inside a tick (snapshot, CPU read, disk write) eats into the
 *  slack before the next deadline but never pushes it later — no accumulated
 *  drift over 24 h. MONOTONIC is immune to NTP adjustments.
 *
 *  The logged timestamp uses CLOCK_REALTIME (as the brief requires) so the
 *  post-processing can compute jitter as (t[n] - t[n-1] - 1.0). It is taken
 *  right after wake-up, before any I/O, so it reflects the wake instant.
 * ------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "monitor.h"
#include "config.h"

#include <stdio.h>
#include <time.h>
#include <errno.h>

/* Read aggregate CPU jiffies from the first line of /proc/stat.
 *   total = sum of all fields, idle = idle + iowait. */
static int
read_cpu_jiffies(unsigned long long *total, unsigned long long *idle)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return -1;

    char label[8];
    unsigned long long u = 0, n = 0, s = 0, i = 0, io = 0,
                       irq = 0, sirq = 0, st = 0;
    int got = fscanf(f, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
                     label, &u, &n, &s, &i, &io, &irq, &sirq, &st);
    fclose(f);
    if (got < 5)
        return -1;

    *idle  = i + io;
    *total = u + n + s + i + io + irq + sirq + st;
    return 0;
}

void *
monitor_thread(void *arg)
{
    monitor_ctx_t *ctx = (monitor_ctx_t *)arg;

    FILE *log = fopen(LOG_FILE_PATH, "a");
    if (!log) {
        perror("monitor: fopen(metrics_log.txt)");
        return NULL;
    }

#if WRITE_CSV_HEADER
    fseek(log, 0, SEEK_END);
    if (ftell(log) == 0)
        fprintf(log, "Seconds,Nanoseconds,Commit_Count,Identity_Count,"
                     "Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct\n");
#endif

    /* seed the CPU delta so the very first logged value is meaningful */
    unsigned long long prev_total = 0, prev_idle = 0;
    read_cpu_jiffies(&prev_total, &prev_idle);

    /* anchor the periodic grid to "now" on the monotonic clock */
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (*ctx->running) {
        /* advance to the next absolute 1-second deadline */
        next.tv_sec += 1;

        /* sleep to the absolute deadline; if interrupted, re-sleep to the
         * SAME deadline so the period stays exact */
        int rc;
        while ((rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                     &next, NULL)) == EINTR && *ctx->running)
            ;
        if (!*ctx->running)
            break;

        /* wall-clock timestamp at the wake instant (before any I/O) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        /* counts accumulated during the last 1-second window */
        counts_t c;
        counters_snapshot_reset(ctx->counters, &c);

        /* current buffer fill level */
        double occ = rb_occupancy_pct(ctx->rb);

        /* CPU% over the last second from /proc/stat jiffie deltas */
        double cpu = 0.0;
        unsigned long long total, idle;
        if (read_cpu_jiffies(&total, &idle) == 0) {
            unsigned long long dt = total - prev_total;
            unsigned long long di = idle  - prev_idle;
            if (dt > 0)
                cpu = 100.0 * (double)(dt - di) / (double)dt;
            prev_total = total;
            prev_idle  = idle;
        }

        fprintf(log, "%lld,%ld,%llu,%llu,%llu,%llu,%.2f,%.2f\n",
                (long long)ts.tv_sec, ts.tv_nsec,
                (unsigned long long)c.commit,
                (unsigned long long)c.identity,
                (unsigned long long)c.account,
                (unsigned long long)c.info,
                occ, cpu);
        fflush(log);   /* push each second's row out so a power blip at hour
                          18 still leaves us 18 h of data */
    }

    fclose(log);
    return NULL;
}
