#include "queue.h"

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

struct queue {
    int size;
    int in;
    int out;
    void **buf;
    sem_t full_slots;
    sem_t empty_slots;
    pthread_mutex_t mutex;
};

queue_t *queue_new(int size) {
    queue_t *q = (queue_t *) malloc(sizeof(queue_t));
    if (q) {
        q->size = size;
        q->in = 0;
        q->out = 0;
        q->buf = (void **) malloc(size * sizeof(void *));
        if (!q->buf) {
            free(q);
            return NULL;
        }
        if (sem_init(&q->full_slots, 0, size) != 0 || sem_init(&q->empty_slots, 0, 0) != 0
            || pthread_mutex_init(&q->mutex, NULL) != 0) {
            free(q->buf);
            free(q);
            return NULL;
        }
    }
    return q;
}

void queue_delete(queue_t **q) {
    if (q && *q) {
        sem_destroy(&(*q)->full_slots);
        sem_destroy(&(*q)->empty_slots);
        pthread_mutex_destroy(&(*q)->mutex);
        free((*q)->buf);
        free(*q);
        *q = NULL;
    }
}

bool queue_push(queue_t *q, void *elem) {
    if (q == NULL) {
        return false;
    }
    sem_wait(&q->full_slots);
    pthread_mutex_lock(&q->mutex);
    q->buf[q->in] = elem;
    q->in = (q->in + 1) % q->size;
    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->empty_slots);
    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    if (q == NULL) {
        return false;
    }
    sem_wait(&q->empty_slots);
    pthread_mutex_lock(&q->mutex);
    *elem = q->buf[q->out];
    q->out = (q->out + 1) % q->size;
    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->full_slots);
    return true;
}
