#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "common.h"

struct value_node {
    key_type k;
    value_type v;
    struct value_node *next;
};

struct kv_store {
    int size;

	index_t *indeces; // Hashed keys
	struct value_node *v_head; // Values, using a linked list/stack
    pthread_mutex_t **v_locks; // Locks, one for each index
}; // TODO: init a shared struct (name = hashtable), use

int init_kv_store(int size) {
	// TODO: implement
}

int put(key_type k, value_type v) {
	// TODO: implement
    int index = hash_function(k, hashtable->size);
    bool found_key = false;
    pthread_mutex_lock(hashtable->v_locks[index]);
    for (struct value_node *this_node = hashtable->v_head; this_node != NULL; this_node = this_node->next) {
        if (this_node->k == k) {
            this_node->v = v;
            found_key = true;
            break;
        }
    }
    if (!found_key) {
        struct value_node *new_node = mmap(NULL, sizeof(struct value_node), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0); // TODO: fix flags, or use something other than mmap
        new_node->k = k;
        new_node->v = v;
        new_node->next = hashtable->v_head;
        hashtable->v_head = new_node;
    }
    pthread_mutex_unlock(hashtable->v_locks[index]);
    return 0;
}

int get(key_type k) {
	// TODO: implement
    int index = hash_function(k, hashtable->size);
    int output = 0;
    pthread_mutex_lock(hashtable->v_locks[index]);
    for (struct value_node *this_node = hashtable->v_head; this_node != NULL; this_node = this_node->next) {
        if (this_node->k == k) {
            output = this_node->v;
            break;
        }
    }
    pthread_mutex_unlock(hashtable->v_locks[index]);
    return output;
}

int main(int argc, int[] argv) {
    if (argc != 5) {
        printf("Usage: ./server -n <number of server threads> -s <initial hashtable size>\n");
        return -1;
    }
    int n = 0, s = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)
            n = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0)
            s = atoi(argv[++i]);
    }

    // TODO: create threads, hashtable
}