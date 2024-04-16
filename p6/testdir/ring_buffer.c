#include <pthread.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

#include "ring_buffer.h"

int init_ring(struct ring *r) {
    if (r == NULL) return -1;
    // memset(r, 0, sizeof(struct ring));
    // Initialize semaphores for shared usage
    sem_init(&r->sem_not_full, 1, RING_SIZE);  // Initial value is the size of the ring
    sem_init(&r->sem_not_empty, 1, 0);         // Initial value is 0
    pthread_mutex_init(&r->s_mutex, NULL);
    pthread_mutex_init(&r->g_mutex, NULL);

    return 0;
}

void ring_submit(struct ring *r, struct buffer_descriptor *bd) {
    if (r == NULL || bd == NULL) return;
    sem_wait(&r->sem_not_full);
    pthread_mutex_lock(&r->s_mutex);
    r->buffer[r->p_head] = *bd;
    r->p_head = (r->p_head + 1) % RING_SIZE;
    pthread_mutex_unlock(&r->s_mutex);
    sem_post(&r->sem_not_empty);
}

void ring_get(struct ring *r, struct buffer_descriptor *bd) {
    if (r == NULL || bd == NULL) return;
    //printf("before wait\n");
    sem_wait(&r->sem_not_empty);
    //printf("ring get\n");
    pthread_mutex_lock(&r->g_mutex);
    *bd = r->buffer[r->c_tail];
    r->c_tail = (r->c_tail + 1) % RING_SIZE;
    pthread_mutex_unlock(&r->g_mutex);
    sem_post(&r->sem_not_full);
}
