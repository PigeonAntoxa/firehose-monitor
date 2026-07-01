#ifndef CONFIG_H
#define CONFIG_H

/* ------------------------------------------------------------------ *
 *  Compile-time configuration for the Firehose telemetry monitor.
 *  Everything tunable lives here so the report can justify each choice.
 * ------------------------------------------------------------------ */

/* Bluesky Jetstream endpoint (split into host/port/path for libwebsockets) */
#define WS_SERVER_ADDRESS "jetstream1.us-east.bsky.network"
#define WS_SERVER_PORT    443
#define WS_SERVER_PATH    "/subscribe?wantedCollections=app.bsky.feed.post"

/* Bounded circular buffer geometry.
 *   QUEUE_CAPACITY slots * MAX_MSG_SIZE bytes ~= 16 MB of heap.
 *   Capacity sets how big a network burst we can absorb before the
 *   single ARMv6 core's consumer catches up; MAX_MSG_SIZE must be large
 *   enough that whole post-commit JSON frames fit without truncation. */
#define QUEUE_CAPACITY    1024
#define MAX_MSG_SIZE      16384

/* Output + timing */
#define LOG_FILE_PATH     "metrics_log.txt"
#define MONITOR_PERIOD_NS 1000000000L   /* 1.000000000 s logging period */

/* Write a one-line CSV column header when the log file is first created.
 * Set to 0 if your post-processing expects purely numeric rows. */
#define WRITE_CSV_HEADER  1

#endif /* CONFIG_H */
