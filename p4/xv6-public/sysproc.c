#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "wmap.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#define min(a, b) ((a) < (b) ? (a) : (b))
int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  // Exit the process
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
    // Retrieve arguments
    if (argint(1, &length) < 0 || argint(2, &flags) < 0 || argint(0, (int *)&addr) < 0)
        return FAILED;

    // Retrieve the file descriptor if the mapping is not anonymous
    if (!(flags & MAP_ANONYMOUS) && argint(3, &fd) < 0)
        return FAILED;

    // Ensure mapping is private or shared, but not both
    if (((flags & MAP_PRIVATE) != 0) == ((flags & MAP_SHARED) != 0))
        return FAILED;

    struct proc *curproc = myproc();
    struct file *f = 0;

    // If not anonymous, validate and retrieve the file structure
    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0 || fd >= NOFILE || (f = curproc->ofile[fd]) == 0)
            return FAILED;
        filedup(f);  // Increase the file's reference count
    }

    // Handle non-fixed mappings by searching for a suitable address
    if (!(flags & MAP_FIXED)) {
        addr = 0x60000000; // Start from this address
        while (1) {
            int fit = 1;
            for (struct mmap_entry *me = curproc->mmaps; me != 0; me = me->next) {
                if ((addr < me->addr + me->length) && (addr + length > me->addr)) {
                    addr = PGROUNDUP(me->addr + me->length);
                    fit = 0;
                    break;
                }
            }
            if (fit || addr + length > 0x80000000) {
                break;  // Suitable space found or reached address space limit
            }
        }

        if (addr + length > 0x80000000) {
            if (f) {
                fileclose(f);
            }
            return FAILED;  // No suitable address found
        }
    } else {
        // Ensure the requested address doesn't overlap existing mappings (for MAP_FIXED)
        for (struct mmap_entry *me = curproc->mmaps; me != 0; me = me->next) {
            if ((addr < me->addr + me->length) && (addr + length > me->addr)) {
                if (f) {
                    fileclose(f);
                }
                return FAILED;
            }
        }
    }

    // Create a new memory map entry
    struct mmap_entry *me = (struct mmap_entry *)kalloc();
    if (me == 0) {
        if (f) {
            fileclose(f);
        }
        return FAILED;
    }

    // Initialize the memory map entry
    memset(me, 0, sizeof(struct mmap_entry));
    me->addr = addr;
    me->length = length;
    me->flags = flags;
    me->mapping_process = curproc;  // Indicates that this process mapped the virtual memory first
    me->n_loaded_pages = 0;
    me->file = f;             // Associate the file with the mapping
    me->rc = 1;

    // If MAP_PRIVATE, mark the pages as copy-on-write
    if (flags & MAP_PRIVATE) {
        char *vaddr = (char *)me->addr;
        for (uint i = 0; i < me->length; i += PGSIZE) {
            pte_t *pte = walkpgdir(curproc->pgdir, vaddr + i, 0);
            *pte &= ~PTE_W; // Mark the page as read-only
        }
    }

    // Add the new memory map entry to the process's list
    me->next = curproc->mmaps;
    curproc->mmaps = me;

    return addr;
}

int
sys_wunmap(void) {
    uint addr;
    if (argint(0, (int *)&addr) < 0) return FAILED;

    struct proc *curproc = myproc();
    struct mmap_entry **pme = &curproc->mmaps;

    while (*pme != 0) {
        struct mmap_entry *me = *pme;
        if (me->addr == addr) {
            if (me->flags & MAP_PRIVATE) {
                // For MAP_PRIVATE, simply invalidate PTEs
                for (uint i = 0; i < me->length; i += PGSIZE) {
                    char *vaddr = (char *)(me->addr + i);
                    pte_t *pte = walkpgdir(curproc->pgdir, vaddr, 0);
                    if (pte && (*pte & PTE_P)) {
                        // Clear the PTE
                        *pte = 0;
                    }
                }
            } else if (me->flags & MAP_SHARED) {
                // For shared mappings, invalidate PTEs and close the file
                for (uint i = 0; i < me->length; i += PGSIZE) {
                    char *vaddr = (char *)(me->addr + i);
                    pte_t *pte = walkpgdir(curproc->pgdir, vaddr, 0);
                    if (pte && (*pte & PTE_P)) {
                        if ((*pte & PTE_W)) {
                            // Page is dirty, write back to file
                            me->file->off = i;
                            filewrite(me->file, vaddr, PGSIZE);
                            *pte &= ~PTE_W; // Clear the writable bit
                        }
                        // Clear the PTE
                        uint physical_address = PTE_ADDR(*pte); // TODO: does this work?
                        kfree(P2V(physical_address)); // TODO: does this work?
                        *pte = 0;
                    }
                }
                // Close the file descriptor associated with the mapping
                if (me->file) {
                    fileclose(me->file);
                }
            }

            // Remove the mapping from the process's list
            *pme = me->next;
            kfree((char *)me);

            // Invalidate the TLB
            lcr3(V2P(curproc->pgdir));

            return SUCCESS;
        }
        pme = &me->next;
    }

    // Mapping not found
    return FAILED;
}

// Resize an existing mapping.
uint
sys_wremap(void) {
  uint oldaddr;
  int oldsize, newsize, flags;

  // Fetch system call arguments
  if (argint(0, (int *)&oldaddr) < 0 ||
      argint(1, &oldsize) < 0 ||
      argint(2, &newsize) < 0 ||
      argint(3, &flags) < 0) {
    return FAILED;
  }

  // Validate the new size
  if (newsize <= 0) {
    return FAILED;
  }

  struct proc *curproc = myproc();
  struct mmap_entry *me = 0, *pme = 0;

  // Locate the corresponding memory mapping
  for (me = curproc->mmaps; me != 0; pme = me, me = me->next) {
    if (me->addr == oldaddr && me->length == oldsize) {
      break;
    }
  }

  // If no corresponding mapping is found, return an error
  if (me == 0) {
    return FAILED;
  }

  // If new size overlaps with another map entry, must move (will fail if cannot move)
  int mustmove = 0;
  if (newsize > oldsize) {
    for (struct mmap_entry *otherme = curproc->mmaps; otherme != 0; otherme = otherme->next) {
      if (me != otherme && otherme->addr <= me->addr + newsize) {
        // Overlap found, must move (or fail if cannot)
        if (!(flags & MREMAP_MAYMOVE))
          return FAILED;
        mustmove = 1;
        break;
      }
    }
  }
//   cprintf("mustmove=%d\n", mustmove); // TODO: debug
//   cprintf("flags=%d\n", flags); // TODO: debug

  // Resize the mapping in place if there's enough space and no movement is required
  if (!mustmove || !(flags & MREMAP_MAYMOVE)) {
    me->length = newsize;
    return oldaddr;
  } 
    // Attempt to create a new mapping
    // uint newaddr = wmap(0, newsize, me->flags, -1);  // Anonymous mapping, so fd is -1
    
    // TODO: revise below

    uint newaddr = 0x60000000; // Start from this address
    while (1) {
      int fit = 1;
      for (struct mmap_entry *otherme = curproc->mmaps; otherme != 0; otherme = otherme->next) {
        if ((newaddr < otherme->addr + otherme->length) && (newaddr + newsize > otherme->addr)) {
          newaddr = PGROUNDUP(otherme->addr + otherme->length);
          fit = 0;
          break;
        }
      }
      if (fit || newaddr + newsize > 0x80000000) {
        break;  // Suitable space found or reached address space limit
      }
    }

    if (newaddr + newsize > 0x80000000) {
      return oldaddr;  // Return old address if the new mapping creation fails
    }

    // cprintf("newaddr=%x\n", newaddr); // TODO: debug

    // Create a new memory map entry
    struct mmap_entry *newme = (struct mmap_entry *)kalloc();
    if (newme == 0) {
      return oldaddr;  // Return old address if the new mapping creation fails
    }

    // Initialize the memory map entry
    memset(newme, 0, sizeof(struct mmap_entry));
    newme->addr = newaddr;
    newme->length = newsize;
    newme->flags = me->flags;
    newme->mapping_process = curproc;  // Indicates that this process mapped the virtual memory first
    newme->n_loaded_pages = me->n_loaded_pages; // TODO: either this or 0
    newme->file = me->file;             // Associate the file with the mapping
    newme->rc = 1;

    // cprintf("constructed newme\n"); // TODO: debug

    // If MAP_PRIVATE, mark the pages as copy-on-write
    // if (flags & MAP_PRIVATE) {
    //   char *vaddr = (char *)me->addr;
    //   for (uint i = 0; i < newsize; i += PGSIZE) {
    //     pte_t *pte = walkpgdir(curproc->pgdir, vaddr + i, 0);
    //     *pte &= ~PTE_W; // Mark the page as read-only
    //   }
    // }

    // Add the new memory map entry to the process's list
    newme->next = curproc->mmaps;
    curproc->mmaps = newme;
    
    // TODO: revise above

    // if (newaddr == (uint)-1) {
    //   return oldaddr;  // Return old address if the new mapping creation fails
    // }

    // Copy existing data to the new mapping location
    memmove((void *)newaddr, (void *)oldaddr, oldsize);

    // Remove the old mapping


    // TODO: revise below

    // cprintf("copied data\n"); // TODO: debug

    if (me->flags & MAP_PRIVATE) {
        // For MAP_PRIVATE, simply invalidate PTEs
        for (uint i = 0; i < newsize; i += PGSIZE) {
            char *vaddr = (char *)(me->addr + i);
            pte_t *pte = walkpgdir(curproc->pgdir, vaddr, 0);
            if (pte && (*pte & PTE_P)) {
                // Clear the PTE
                // uint physical_address = PTE_ADDR(*pte); // TODO: does this work?
                // kfree(P2V(physical_address)); // TODO: does this work?
                // *pte = 0;
            }
        }
    } else if (me->flags & MAP_SHARED) {
        // For shared mappings, invalidate PTEs and close the file
        for (uint i = 0; i < newsize; i += PGSIZE) {
            char *vaddr = (char *)(newsize + i);
            pte_t *pte = walkpgdir(curproc->pgdir, vaddr, 0);
            if (pte && (*pte & PTE_P)) {
                if ((*pte & PTE_W)) {
                    // Page is dirty, write back to file
                    me->file->off = i;
                    filewrite(me->file, vaddr, PGSIZE);
                    // *pte &= ~PTE_W; // Clear the writable bit
                }
                // Clear the PTE
                // uint physical_address = PTE_ADDR(*pte); // TODO: does this work? (shared, so...)
                // kfree(P2V(physical_address)); // TODO: does this work? (shared...)
                // *pte = 0; // TODO: if shared, though... (also see above in wunmap)
            }
        }

        // Remove the mapping from the process's list
        // *pme = me->next;
        // kfree((char *)me);

        // Invalidate the TLB
        lcr3(V2P(curproc->pgdir));
    }
    // cprintf("invalidated PTEs\n"); // TODO: debug
    // TODO: revise above

    // Remove the mapping from the process's list
    if (pme)
      pme->next = me->next;
    else
      newme->next = me->next;
    kfree((char *)me);

    // cprintf("wremap done\n"); // TODO: debug


    return newaddr;
  
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
    wminfo->n_loaded_pages[count] = me->n_loaded_pages;  // Reflect actual loaded pages
    count++;
  }

  wminfo->total_mmaps = count;
  return SUCCESS;
}

// Retrieve information about the physical pages in the process address space.
int sys_getpgdirinfo(void) {
    struct pgdirinfo *pdinfo;

    // Retrieve the pointer to pgdirinfo struct from the user space
    if (argptr(0, (void *)&pdinfo, sizeof(*pdinfo)) < 0)
        return FAILED;

    struct proc *curproc = myproc();
    pde_t *pgdir = curproc->pgdir;
    uint n = 0;

    // Iterate through page directory entries
    for (uint i = 0; i < NPDENTRIES && n < MAX_UPAGE_INFO; i++) {
        pde_t pde = pgdir[i];
        // Check if page directory entry is present
        if (pde & PTE_P) {
            pte_t *pgtab = (pte_t *)P2V(PTE_ADDR(pde));
            // Iterate through page table entries
            for (uint j = 0; j < NPTENTRIES && n < MAX_UPAGE_INFO; j++) {
                pte_t pte = pgtab[j];
                // Check if page table entry is present and user accessible
                if (pte & PTE_P && pte & PTE_U) {
                    // Calculate virtual address and physical address
                    pdinfo->va[n] = (i << PDXSHIFT) | (j << PTXSHIFT);
                    pdinfo->pa[n] = PTE_ADDR(pte) | (pte & 0xFFF); // Include the offset within the page
                    n++;
                }
            }
        }
    }

    // Store the number of user pages
    pdinfo->n_upages = n;
    return SUCCESS;
}




