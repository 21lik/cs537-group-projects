#include "types.h"
#include "spinlock.h"
#include "user.h"
#include "param.h"
#include "x86.h"
#include "stat.h"
#include "fcntl.h"

// Initialize the lock and prepares any necessary internal structures.
void minit(mutex *m) {
  struct spinlock *sp = &m->lk;
  sp->name = "mutex";
  sp->locked = 0;
  sp->cpu = 0;
  m->locked = 0;
  m->pid = 0;
}