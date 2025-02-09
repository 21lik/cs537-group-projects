#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int sys_clone(void)
{
  int fn, stack, arg;
  argint(0, &fn);
  argint(1, &stack);
  argint(2, &arg);
  return clone((void (*)(void*))fn, (void*)stack, (void*)arg);
}

// Acquire the mutex lock, putting the thread to sleep if it's not available.
int sys_macquire(void) {
  mutex *m;
  if (argptr(0, (char**) &m, sizeof(mutex)))
    return -1;

  struct proc *curproc = myproc();
  pte_t *pte = walkpgdir(curproc->pgdir, m, 0); // Get PTE corresponding to the mutex
  if (!pte)
    return -1; // Invalid mutex pointer
  void* m_phys = (void*) V2P(*pte); // Calculate the physical address of the mutex

  acquire(&m->lk);
  while (m->locked) {
    sleep(m_phys, &m->lk); // Use physical address of m as argument to sleep
  }
  m->locked = 1;
  m->pid = curproc->pid;
  curproc->locks_held[curproc->num_locks_held++] = m_phys;
  release(&m->lk);
  return 0;
}

// Release the lock and wake up any threads waiting for the lock.
int sys_mrelease(void) {
  mutex *m;
  if (argptr(0, (char**) &m, sizeof(mutex)))
    return -1;

  struct proc *curproc = myproc();
  pte_t *pte = walkpgdir(curproc->pgdir, m, 0); // Get PTE corresponding to the mutex
  if (!pte)
    return -1; // Invalid mutex pointer
  void* m_phys = (void*) V2P(*pte); // Calculate the physical address of the mutex

  acquire(&m->lk);
  for (int i = 0; i < curproc->num_locks_held; i++) {
    if (curproc->locks_held[i] == m_phys) {
      curproc->locks_held[i] = curproc->locks_held[--(curproc->num_locks_held)];
      curproc->locks_held[curproc->num_locks_held] = 0;
    }
  }
  m->locked = 0;
  m->pid = 0;
  wakeup(m_phys); // Use physical address of m as argument to wakeup
  release(&m->lk);
  return 0;
}

// Adjust the priority of the thread.
int sys_nice(void) {
  int inc;

  if (argint(0, &inc) < 0)
    return -1;

  inc += myproc()->nice;
  if (inc > 19)
    inc = 19;
  else if (inc < -20)
    inc = -20;
  myproc()->nice = inc;
  return 0;
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  if (n == 0) {
    yield();
    return 0;
  }
  acquire(&tickslock);
  ticks0 = ticks;
  myproc()->sleepticks = n;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  myproc()->sleepticks = -1;
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
