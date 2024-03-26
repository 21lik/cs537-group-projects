#define MAXMUTEXLOCKSHELD 16

#include "spinlock.h"

// User-space sleep lock
typedef struct {
  // Lock state, ownership, etc.
  uint locked;        // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  int pid;            // Process holding lock
} mutex;