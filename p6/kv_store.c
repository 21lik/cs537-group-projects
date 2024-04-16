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
#include "common.h"

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
};

struct kv_store hashtable;
int num_threads = 0;
//pthread_t threads[MAX_THREADS];
char shm_file[] = "shmem_file";

/**
 * Initialize the hashtable structure.
 * @param size the number of indeces of the hashtable.
 * @return 0 on success.
*/
int init_kv_store(int size) {
    hashtable.size = size;
    hashtable.v_head = malloc(sizeof(struct value_node*) * size);
    hashtable.v_locks = malloc(sizeof(pthread_mutex_t*) * size);
    for (int i = 0; i < size; i++) {
        hashtable.v_head[i] = NULL; // No nodes at start
        hashtable.v_locks[i] = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(hashtable.v_locks[i], NULL);
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
 * Free the elements in the hashtable structure.
 * @return 0 on success.
*/
int free_kv_store() {
    for (int i = 0; i < hashtable.size; i++) {
        free_linked_list(hashtable.v_head[i]);
        hashtable.v_head[i] = NULL;
        pthread_mutex_destroy(hashtable.v_locks[i]);
        free(hashtable.v_locks[i]);
        hashtable.v_locks[i] = NULL;
    }
    free(hashtable.v_head);
    hashtable.v_head = NULL;
    free(hashtable.v_locks);
    hashtable.v_locks = NULL;
    return 0;
}

/**
 * Put the key-value pair into the hashtable, or replace the value if the key
 * is already present. Since chaining with linked lists is used, resizing is
 * unnecessary.
 * @param k the key.
 * @param v the value.
*/
void put(key_type k, value_type v) {
    int index = hash_function(k, hashtable.size);
    bool found_key = false;
    pthread_mutex_lock(hashtable.v_locks[index]);
    for (struct keyvalue_node *this_node = hashtable.v_head[index]; this_node != NULL; this_node = this_node->next) {
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
        new_node->next = hashtable.v_head[index];
        hashtable.v_head[index] = new_node; // Functions like a stack
    }
    pthread_mutex_unlock(hashtable.v_locks[index]);
    return;
}

/**
 * Get the value with the given key from the hashtable.
 * The key-value pair is NOT deleted.
 * @param k the key
 * @return the corresponding value
*/
value_type get(key_type k) {
    int index = hash_function(k, hashtable.size);
    value_type output = 0;
    pthread_mutex_lock(hashtable.v_locks[index]);
    for (struct keyvalue_node *this_node = hashtable.v_head[index]; this_node != NULL; this_node = this_node->next) {
        if (this_node->k == k) {
            output = this_node->v;
            break;
        }
    }
    pthread_mutex_unlock(hashtable.v_locks[index]);
    return output;
}

void *thread_function(void *arg) {
    struct ring *r = (struct ring*) arg;
    struct buffer_descriptor bd;
    while (true) {
        ring_get(r, &bd);
        struct buffer_descriptor *result = (struct buffer_descriptor*) (arg + bd.res_off);
        memcpy(result, &bd, sizeof(struct buffer_descriptor));
        if (bd.req_type == PUT) {
            put(bd.k, bd.v);
        }
        else if (bd.req_type == GET) {
            result->v = get(bd.k);
        }
        else {
            printf("ERROR: invalid request type detected by server.\n");
            return (void*) -1;
        }
        result->ready = 1;
    }
}

int main(int argc, char *argv[]) {
    int n = 0, s = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            n = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-s") == 0) {
            s = atoi(argv[++i]);
        }
    }

    init_kv_store(s);

	int fd = open(shm_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0)
		perror("open");

    // Get the file size
    struct stat statbuf;
    fstat(fd, &statbuf);

    // Get a pointer to the shared mmap memory
	char *mem = mmap(NULL, statbuf.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (mem == (void*) -1)
		perror("mmap");

	// mmap dups the fd, no longer needed
	close(fd);

	struct ring *r = (struct ring*) mem;

    // Create threads, fetch requests from ring buffer, update client request completion status
    pthread_t threads[n];
    for (int i = 0; i < n; ++i) {
        pthread_create(&threads[i], NULL, &thread_function, (void*) r);
    }
    for (int i = 0; i < n; ++i) {
        pthread_join(threads[i], NULL); // Prevent main thread from freeing the hashtable early
    }

    // Free memory at the end (unused)
    free_kv_store();
    return 0;
}
