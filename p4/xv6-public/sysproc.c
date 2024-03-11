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

// Map memory in the process's virtual address space to some physical memory,
// similar to mmap.
uint 
sys_wmap(void) {
    uint addr;
    int length, flags, fd;

    if (argint(1, &length) < 0 || argint(2, &flags) < 0 || argint(0, (int *)&addr) < 0)
        return FAILED;

    if (!(flags & MAP_ANONYMOUS) && argint(3, &fd) < 0)
        return FAILED;

    struct proc *curproc = myproc();
    struct file *f = 0;

    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0 || fd >= NOFILE || (f = curproc->ofile[fd]) == 0)
            return FAILED;
        filedup(f);
    }

    if (!(flags & MAP_FIXED)) {
        // For non-fixed mappings, find a suitable address
        addr = 0x60000000; // Start the search from this address
        while (1) {
            int fit = 1;
            for (struct mmap_entry *me = curproc->mmaps; me != 0; me = me->next) {
                if ((addr < me->addr + me->length) && (addr + length > me->addr)) {
                    // Found an overlap, adjust addr and recheck
                    addr = PGROUNDUP(me->addr + me->length);
                    fit = 0;
                    break;
                }
            }
            if (fit || addr + length > 0x80000000) {
                break; // Found a fit or reached address space limit
            }
        }

        if (addr + length > 0x80000000) {
            // No suitable address found within the address space
            if (f) {
                fileclose(f);
            }
            return FAILED;
        }
    } else {
        // For MAP_FIXED, ensure the requested address doesn't overlap existing mappings
        for (struct mmap_entry *me = curproc->mmaps; me != 0; me = me->next) {
            if ((addr < me->addr + me->length) && (addr + length > me->addr)) {
                // Found an overlap with MAP_FIXED
                if (f) {
                    fileclose(f);
                }
                return FAILED;
            }
        }
    }

    struct mmap_entry *me = (struct mmap_entry *)kalloc();
    if (me == 0) {
        if (f) {
            fileclose(f);
        }
        return FAILED;
    }

    memset(me, 0, sizeof(struct mmap_entry));
    me->addr = addr;
    me->length = length;
    me->flags = flags;
    me->file = f;
    me->next = curproc->mmaps;
    curproc->mmaps = me;

    return addr;
}

// Unmap memory from the process's virtual address space.
int
sys_wunmap(void) {
  uint addr;

  // Get system call argument, ensure it is valid
  if (argint(0, (int *)&addr) < 0)
    return FAILED;

  // Check address is page aligned
  if (addr % PGSIZE != 0)
    return FAILED;

  // Unmap the physical pages
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

// Resize an existing mapping.
int
sys_wremap(void)
{
  uint oldaddr;
  int oldsize, newsize, flags;

  // TODO: fix below as necessary
  // Get system call arguments, ensure they are valid
  if (argint(1, &oldsize) < 0 || argint(2, &newsize) < 0 || argint(3, &flags) < 0 || argint(0, (int *)&oldaddr) < 0)
    return FAILED;
  // TODO: implement wremap

  return SUCCESS;
}

// Retrieve information about the memory maps in the process address space.
int
sys_getwmapinfo(void) {
  struct wmapinfo *wminfo;

  // Get system call argument, ensure it is valid
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

// Retrieve information about the physical pages in the process address space.
int
sys_getpgdirinfo(void) {
  struct pgdirinfo *pdinfo;

  // Get system call argument, ensure it is valid
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


