#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "wmap.h"

int
sys_fork(void)
{
  // TODO: copy the mappings from the parent process to the child, here or in proc.c
  return fork();
}

int
sys_exit(void)
{
  // TODO: remove all the mappings from the current process address space, here or in proc.c
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
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
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

uint
sys_wmap(void)
{
  // System call arguments
  uint addr;
  int length;
  int flags;
  int fd;

  // TODO: fix below as necessary
  // Get system call arguments, ensure they are valid
  if (argint(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &flags) < 0 || argint(3, &fd) < 0)
    return FAILED;
  // Check flags are valid
  if (((flags & MAP_SHARED) == 0) == ((flags & MAP_PRIVATE) == 0))
    return FAILED;
  // TODO: implement wmap

  return SUCCESS;
}

int
sys_wunmap(void)
{
  // System call arguments
  uint addr;

  // TODO: fix below as necessary
  // Get system call arguments, ensure they are valid
  if (argint(0, &addr) < 0)
    return FAILED;
  // TODO: implement wunmap

  return SUCCESS;
}

uint
sys_wremap(void)
{
  // System call arguments
  uint oldaddr;
  int oldsize;
  int newsize;
  int flags;

  // TODO: fix below as necessary
  // Get system call arguments, ensure they are valid
  if (argint(0, &oldaddr) < 0 || argint(1, &oldsize) < 0 || argint(2, &newsize) < 0 || argint(3, &flags) < 0)
    return FAILED;
  // TODO: implement wremap

  return SUCCESS;
}

int
sys_getpgdirinfo(void)
{
  // System call arguments
  struct pgdirinfo *pdinfo;

  // TODO: fix below as necessary
  // Get system call arguments, ensure they are valid
  if (argptr(0, &pdinfo, sizeof(struct pgdirinfo)) < 0)
    return FAILED;
  // TODO: implement getpgdirinfo

  return SUCCESS;
}

int
sys_getwmapinfo(void)
{
  // System call arguments
  struct wmapinfo *wminfo;

  // TODO: fix below as necessary
  // Get system call arguments, ensure they are valid
  if (argptr(0, &wminfo, sizeof(struct wmapinfo)) < 0)
    return FAILED;
  // TODO: implement getwmapinfo

  return SUCCESS;
}