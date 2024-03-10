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
sys_wmap(void) {
    uint addr;
    int length, flags, fd;

    if (argint(1, &length) < 0 || argint(2, &flags) < 0 || argint(3, &fd) < 0 || argint(0, (int *)&addr) < 0)
        return FAILED;

    if (length <= 0 || (flags & MAP_FIXED && (addr < 0x60000000 || addr >= 0x80000000 || addr % PGSIZE != 0)))
        return FAILED;

    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_FIXED) || !(flags & MAP_SHARED)) {
        return FAILED;
    }

    struct proc *curproc = myproc();
    for (struct mmap_entry *me = curproc->mmaps; me != 0; me = me->next) {
        if (addr >= me->addr && addr < me->addr + me->length) {
            return FAILED;
        }
    }

    struct mmap_entry *me = (struct mmap_entry *)kalloc();
    if (me == 0) {
        return FAILED;
    }

    memset(me, 0, sizeof(struct mmap_entry));
    me->addr = addr;
    me->length = length;
    me->flags = flags;
    me->file = 0;
    me->next = curproc->mmaps;
    curproc->mmaps = me;

    return addr;  // On success, return the starting virtual address
}



int 
sys_wunmap(void) {
    uint addr;

    if (argint(0, (int *)&addr) < 0)
        return FAILED;

    if (addr % PGSIZE != 0)
        return FAILED;

    struct proc *curproc = myproc();
    struct mmap_entry **pme = &curproc->mmaps;

    while (*pme != 0) {
        struct mmap_entry *me = *pme;
        if (me->addr == addr) {
            *pme = me->next;
            kfree((char *)me);
            return SUCCESS;
        }
        pme = &me->next;
    }

    return FAILED;
}


uint
sys_wremap(void)
{
  // // System call arguments
  // uint oldaddr;
  // int oldsize;
  // int newsize;
  // int flags;
// 
  // // TODO: fix below as necessary
  // // Get system call arguments, ensure they are valid
  // if (argint(0, &oldaddr) < 0 || argint(1, &oldsize) < 0 || argint(2, &newsize) < 0 || argint(3, &flags) < 0)
    // return FAILED;
  // // TODO: implement wremap

  return SUCCESS;
}

int 
sys_getwmapinfo(void) {
    struct wmapinfo *wminfo;

    if (argptr(0, (void *)&wminfo, sizeof(*wminfo)) < 0)
        return FAILED;

    struct proc *curproc = myproc();
    struct mmap_entry *me = curproc->mmaps;
    int count = 0;

    for (; me != 0 && count < MAX_WMMAP_INFO; me = me->next) {
        wminfo->addr[count] = me->addr;
        wminfo->length[count] = me->length;
        wminfo->n_loaded_pages[count] = 0;
        count++;
    }

    wminfo->total_mmaps = count;
    return SUCCESS;
}


int 
sys_getpgdirinfo(void) {
    struct pgdirinfo *pdinfo;
    
    if (argptr(0, (void *)&pdinfo, sizeof(*pdinfo)) < 0)
        return FAILED;

    struct proc *curproc = myproc();
    pde_t *pgdir = curproc->pgdir;
    uint n = 0;

    for (uint i = 0; i < NPDENTRIES && n < MAX_UPAGE_INFO; i++) {
        pde_t pde = pgdir[i];
        if (pde & PTE_P) {
            pte_t *pgtab = (pte_t*)P2V(PTE_ADDR(pde));
            for (uint j = 0; j < NPTENTRIES && n < MAX_UPAGE_INFO; j++) {
                pte_t pte = pgtab[j];
                if (pte & PTE_P && pte & PTE_U) {
                    pdinfo->va[n] = (i << PDXSHIFT) | (j << PTXSHIFT);
                    pdinfo->pa[n] = PTE_ADDR(pte);
                    n++;
                }
            }
        }
    }

    pdinfo->n_upages = n;
    return SUCCESS;
}


