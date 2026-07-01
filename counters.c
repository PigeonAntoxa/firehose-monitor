#include "counters.h"
#include <string.h>

void counters_init(counters_t *m)
{
    memset(&m->c, 0, sizeof m->c);
    pthread_mutex_init(&m->lock, NULL);
}

void counters_destroy(counters_t *m)
{
    pthread_mutex_destroy(&m->lock);
}

void counters_inc(counters_t *m, msg_kind_t k)
{
    pthread_mutex_lock(&m->lock);
    switch (k) {
    case KIND_COMMIT:   m->c.commit++;   break;
    case KIND_IDENTITY: m->c.identity++; break;
    case KIND_ACCOUNT:  m->c.account++;  break;
    case KIND_INFO:     m->c.info++;     break;
    default:            m->c.other++;    break;
    }
    pthread_mutex_unlock(&m->lock);
}

void counters_snapshot_reset(counters_t *m, counts_t *out)
{
    pthread_mutex_lock(&m->lock);
    *out = m->c;
    memset(&m->c, 0, sizeof m->c);
    pthread_mutex_unlock(&m->lock);
}
