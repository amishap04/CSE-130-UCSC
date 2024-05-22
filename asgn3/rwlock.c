#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include "rwlock.h"


// structs
typedef struct rwlock {
    pthread_mutex_t lock;
    pthread_cond_t readers_ok;
    pthread_cond_t writers_ok;
    int readers;
    int writers;
    int waiting_readers;
    int waiting_writers;
    PRIORITY priority;
    uint32_t n;
} rwlock_t;

// start of functions
rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *rw = malloc(sizeof(rwlock_t));
    if (rw == NULL) return NULL;
    pthread_mutex_init(&rw->lock, NULL);
    pthread_cond_init(&rw->readers_ok, NULL);
    pthread_cond_init(&rw->writers_ok, NULL);
    rw->readers = 0;
    rw->writers = 0;
    rw->waiting_readers = 0;
    rw->waiting_writers = 0;
    rw->priority = p;
    rw->n = n;
    return rw;
}

void rwlock_delete(rwlock_t **l) {
    if (l == NULL || *l == NULL) return;
    pthread_mutex_destroy(&(*l)->lock);
    pthread_cond_destroy(&(*l)->readers_ok);
    pthread_cond_destroy(&(*l)->writers_ok);
    free(*l);
    *l = NULL;
}

void reader_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->waiting_readers++;
    while (rw->writers > 0 || (rw->priority == WRITERS && rw->waiting_writers > 0)) {
        pthread_cond_wait(&rw->readers_ok, &rw->lock);
    }
    rw->waiting_readers--;
    rw->readers++;
    pthread_mutex_unlock(&rw->lock);
}

void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->readers--;
    if (rw->readers == 0 && rw->waiting_writers > 0) {
        pthread_cond_signal(&rw->writers_ok);
    }
    pthread_mutex_unlock(&rw->lock);
}

void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->waiting_writers++;
    while (rw->readers > 0 || rw->writers > 0) {
        pthread_cond_wait(&rw->writers_ok, &rw->lock);
    }
    rw->waiting_writers--;
    rw->writers++;
    pthread_mutex_unlock(&rw->lock);
}

void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->writers--;
    if (rw->priority == READERS && rw->waiting_readers > 0) {
        pthread_cond_broadcast(&rw->readers_ok);
    } else if (rw->waiting_writers > 0) {
        pthread_cond_signal(&rw->writers_ok);
    } else {
        pthread_cond_broadcast(&rw->readers_ok);
    }
    pthread_mutex_unlock(&rw->lock);
}


