#include <pthread.h>
#include <sys/mman.h>
#include <string.h>

#include "ring_buffer.h"

int init_ring(struct ring *r) {
    if (r == NULL) {
        return -1;
    }
    memset(r, 0, sizeof(struct ring));
    r->p_head = 0;
    r->p_tail = 0;
    r->c_head = 0;
    r->c_tail = 0;
    pthread_mutex_init(&r->p_mutex, NULL);
    pthread_mutex_init(&r->c_mutex, NULL);
    pthread_cond_init(&r->not_full, NULL);
    pthread_cond_init(&r->not_empty, NULL);
    return 0;
}

void ring_submit(struct ring *r, struct buffer_descriptor *bd) {
    if (r == NULL || bd == NULL) return;
    pthread_mutex_lock(&r->p_mutex);
    while (((r->p_head + 1) % RING_SIZE) == r->p_tail) {
        pthread_cond_wait(&r->not_full, &r->p_mutex);
    }
    memcpy(&r->buffer[r->p_head], bd, sizeof(struct buffer_descriptor));
    r->p_head = (r->p_head + 1) % RING_SIZE;

    pthread_cond_signal(&r->not_empty);
    pthread_mutex_unlock(&r->p_mutex);
}

void ring_get(struct ring *r, struct buffer_descriptor *bd) {
    if (r == NULL || bd == NULL) return;
    pthread_mutex_lock(&r->c_mutex);
    while (r->c_head == r->c_tail) {
        pthread_cond_wait(&r->not_empty, &r->c_mutex);
    }
    memcpy(bd, &r->buffer[r->c_head], sizeof(struct buffer_descriptor));
    r->c_head = (r->c_head + 1) % RING_SIZE;

    pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->c_mutex);
}

