#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/mman.h>
#include "ring_buffer.h"

#define MAX_THREADS 128

/**
 * A linked list node representing a key-value pair.
*/
struct keyvalue_node {
    key_type k;
    value_type v;
    struct keyvalue_node *next;
};

/**
 * A hashtable structure that uses chaining to handle collisions.
 * Each bucket is protected by a mutex lock.
*/
struct kv_store {
    int size;

	struct keyvalue_node **v_head; // Key-value pairs, using a linked list/stack for each index
    pthread_mutex_t **v_locks; // Locks, one for each index
}; // TODO: we may want to switch to stack-allocated memory

struct kv_store *hashtable = NULL;
int num_threads = 0;
pthread_t threads[MAX_THREADS];
char shm_file[] = "shmem_file";

/**
 * Initialize the hashtable structure.
 * @param size the number of indeces of the hashtable.
 * @return 0 on success.
*/
int init_kv_store(int size) {
	// TODO: test
    hashtable = malloc(sizeof(struct kv_store));
    hashtable->size = size;
    hashtable->v_head = malloc(sizeof(struct value_node*) * size);
    hashtable->v_locks = malloc(sizeof(pthread_mutex_t*) * size);
    for (int i = 0; i < size; i++) {
        hashtable->v_head[i] = NULL; // No nodes at start
        hashtable->v_locks[i] = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(hashtable->v_locks[i], NULL);
    }
    return 0;
}

/**
 * Free the linked list of key-value pair nodes.
 * @param list a pointer to the head node of the linked list.
 * @return 0 on success.
*/
int free_linked_list(struct keyvalue_node *list) {
    while (list != NULL) {
        struct keyvalue_node *temp = list;
        list = list->next;
        temp->k = 0;
        temp->v = 0;
        temp->next = NULL;
        free(temp);
    }
    return 0;
}

/**
 * Free the hashtable structure.
 * @return 0 on success.
*/
int free_kv_store() {
    for (int i = 0; i < hashtable->size; i++) {
        free_linked_list(hashtable->v_head[i]);
        hashtable->v_head[i] = NULL;
        pthread_mutex_destroy(hashtable->v_locks[i]);
        free(hashtable->v_locks[i]);
        hashtable->v_locks[i] = NULL;
    }
    free(hashtable->v_head);
    hashtable->v_head = NULL;
    free(hashtable->v_locks);
    hashtable->v_locks = NULL;
    free(hashtable);
    hashtable = NULL;
    return 0;
}

/**
 * Put the key-value pair into the hashtable, or replace the value if the key
 * is already present. Since chaining with linked lists is used, resizing is
 * unnecessary.
 * @param k the key.
 * @param v the value.
 * @return 0 on success.
*/
int put(key_type k, value_type v) {
	// TODO: test
    int index = hash_function(k, hashtable->size);
    bool found_key = false;
    pthread_mutex_lock(hashtable->v_locks[index]);
    for (struct keyvalue_node *this_node = hashtable->v_head; this_node != NULL; this_node = this_node->next) {
        if (this_node->k == k) {
            this_node->v = v;
            found_key = true;
            break;
        }
    }
    if (!found_key) {
        struct keyvalue_node *new_node = malloc(sizeof(struct keyvalue_node));
        new_node->k = k;
        new_node->v = v;
        new_node->next = hashtable->v_head;
        hashtable->v_head = new_node; // Functions like a stack
    }
    pthread_mutex_unlock(hashtable->v_locks[index]);
    return 0;
}

/**
 * Get the value with the given key from the hashtable.
 * The key-value pair is NOT deleted.
 * @param k the key
 * @return the corresponding value
*/
int get(key_type k) {
	// TODO: test
    int index = hash_function(k, hashtable->size);
    int output = 0;
    pthread_mutex_lock(hashtable->v_locks[index]);
    for (struct keyvalue_node *this_node = hashtable->v_head; this_node != NULL; this_node = this_node->next) {
        if (this_node->k == k) {
            output = this_node->v;
            break;
        }
    }
    pthread_mutex_unlock(hashtable->v_locks[index]);
    return output;
}

void *thread_function(struct ring *r) {
    struct buffer_descriptor bd;
    while (true) {
        // TODO: test, make sure this works
        ring_get(r, &bd);
        int output;
        if (bd.req_type == PUT) {
            output = put(bd.k, bd.v);
        }
        else if (bd.req_type == GET) {
            output = get(bd.k);
        }
        else {
            printf("ERROR: invalid request type detected by server.\n");
            return -1;
        }
        struct buffer_descriptor *result = (struct buffer_descriptor*) (((void*) r) + bd.res_off); // TODO: get shared memory region start
        memcpy(result, output, sizeof(struct buffer_descriptor));
        result->ready = 1;
    }
}

int main(int argc, int argv[]) {
    if (argc != 5) {
        printf("Usage: ./server -n <number of server threads> -s <initial hashtable size>\n");
        return -1;
    }
    int n = 0, s = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)
            n = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0)
            s = atoi(argv[++i]);
    }

    init_kv_store(s);

    // TODO: fix below

    
    int shm_size = sizeof(struct ring) + 
		num_threads * win_size * sizeof(struct buffer_descriptor);
	
	int fd = open(shm_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0)
		perror("open");

	char *mem = mmap(NULL, shm_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (mem == (void*) -1) 
		perror("mmap");
    

	// mmap dups the fd, no longer needed
	close(fd);

	struct ring *r = (struct ring*) mem;
    // TODO: fix above

    // TODO: create threads, fetch requests from ring buffer, update client request completion status
    
    for (int i = 0; i < n; i++) {
        pthread_create(&threads[i], NULL, &thread_function, r);
    }
    for (int i = 0; i < n; i++) {
        pthread_join(&threads[i], NULL); // TODO: do we want this (wait for threads to finish), have main thread go to sleep, or have main thread return?
    }


    // Free memory at the end // TODO: since function will run indefinitely, should we use stack allocated instead?
    free_kv_store();
    return 0;
}