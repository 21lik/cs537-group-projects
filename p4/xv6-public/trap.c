#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "wmap.h"
#define min(a, b) ((a) < (b) ? (a) : (b))

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  // TODO: implement case T_PGFLT... (below)
  case T_PGFLT:
      uint pgflt_addr = rcr2();  // Get the faulting address
      struct proc *curproc = myproc();
      int handled = 0;
  
      for (struct mmap_entry *me = curproc->mmaps; me != 0; me = me->next) {
          if (pgflt_addr >= me->addr && pgflt_addr < me->addr + me->length) {
              // The faulting address is within a memory-mapped region
              handled = 1;
  
              if (me->flags & MAP_ANONYMOUS || me->file == 0) {
                  // For anonymous mapping or if there's no associated file
                  char *mem = kalloc();
                  if (!mem) {
                      cprintf("kalloc failed: out of memory\n");
                      curproc->killed = 1;
                      break;
                  }
                  memset(mem, 0, PGSIZE);
                  if (mappages(curproc->pgdir, (char *)PGROUNDDOWN(pgflt_addr), PGSIZE, V2P(mem), PTE_W|PTE_U) < 0) {
                      cprintf("mappages failed\n");
                      kfree(mem);
                      curproc->killed = 1;
                      break;
                  }
                  me->n_loaded_pages++;
              } else if (me->file != 0) {
                  // For file-backed mapping, handle reading from the file
                  // This section needs careful attention to ensure it aligns with your file system's capabilities
                  // In this simplified version, we'll just allocate a page and mark it without actually reading from the file
                  char *mem = kalloc();
                  if (!mem) {
                      cprintf("kalloc failed: out of memory\n");
                      curproc->killed = 1;
                      break;
                  }
                  memset(mem, 0, PGSIZE); // You would replace this with actual file reading logic
                  if (mappages(curproc->pgdir, (char *)PGROUNDDOWN(pgflt_addr), PGSIZE, V2P(mem), PTE_W|PTE_U) < 0) {
                      cprintf("mappages failed\n");
                      kfree(mem);
                      curproc->killed = 1;
                      break;
                  }
                  me->n_loaded_pages++;
              }
              break;  // The page fault has been handled
          }
      }
  
      if (!handled) {
          // The page fault was not handled; it's a segmentation fault
          cprintf("pid %d %s: segmentation fault at 0x%x\n", curproc->pid, curproc->name, pgflt_addr);
          curproc->killed = 1;
      }
      break;
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
