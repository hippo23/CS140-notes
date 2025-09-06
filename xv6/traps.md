# `traps.c`

## global variables

### `struct spinlock tickslock`

- Utilized for managing variables that use the global variable [ticks](#uint-ticks). Makes sure there is no race condition i.e. a sleep is being called at the same time an interrupt is being fired, the sleep might have to wait for the next timer interrupt.

### `uint ticks`

- Mainly used for clock functions such as [sys_sleep](#sys_sleep) or [sys_uptime](#sys_uptime). See their definitions for details.

## function definitions

### `void trapinit`

- Responsible for initializing the global lock [tickslock](#struct-spinlock-tickslock).

```c
void
trapinit(void)
{
  initlock(&tickslock, "time");
}
```

### `void trapinithard`

- Uses the function [w_stvec](#void-w_stvec) to assign the address of the trap-vector base address. Remember, the trap vector is used to supply certain routines or handlers for specific types of traps. Here, we see that we are assigning the symbol in assembly of `kernelvec:` in [kernelvec.S](#kernelvec.S).
- See the notes on that for more details, but essentially, this is THE address we go to when an exception occurs (while in kernel / supervisor mode). You will see that in `kernelvec.S`, we call the function [kerneltrap](#void-kerneltrap), the purpose of which we will discuss in just a moment.
- Note that exceptions are a type of interrupt that can either be:
  - **Faults** - Can be correct and the program can continue
  - **Traps** - Reported immediately after the execution of the trapping instruction
  - **Aborts** - Some severe unrecoverable error

```c
// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}
```

### `uint64 usertrap`

- This is the function called by `uservec` when we are trapping from user mode to supervisor / kernel mode. Its flow goes as follows:

- (1) We first checking if we actually trapped from user space in the `SPP` bit of the `sstatus` or supervisor status register. If not, we throw an error.
- (2) Then, now that we are in kernel space, we must send all following interrupts to the `kernelvec` symbol instead.
- (3) We get the current process and save the `pc` that we trapped from into the trap-frame of the process. `r_sepc` reads the `sepc` register, which is written to by RISC-V when we jump into `stvec` due to some interrupt.
- (4) We now try and find out the cause of the trap. We use the function `r_scause`, which reads the register `scause`, which is set if an exception was caused by some interrupt to the CPU. See the specs [here](https://people.eecs.berkeley.edu/~krste/papers/riscv-privileged-v1.9.pdf) for the breakdown of possible values.
  - (4.1) **SYSTEM CALL** - 8 is the code for an environment call, or `ecall`. These are always implemented by the kernel (the RIPES implementation was just a layer provided to us). In this case, we increment the next PC by four to make sure we move to the next instruction. We turn on interrupt now that we have read all interrupt-related registers (`scause`, `sepc`, and `sstatus`). By default, RISC-V turns this off when we trap into supervisor mode. We then call [syscall](syscall.c). See its implementation for more details, but the gist is the following:
    - The `trap vector` calls `usertrap()` which calls `syscall`. `syscall` has access to an array mapping a certain number of system call. Perhaps the biggest thing you should remember is that the user has a 'proxy' system call defined in [usys.S] to call a system call, that's how we get the unique index to know what to run.
  - (4.2) Otherwise, we check if it is a device interrupt. If !=0 is returned by [devintr()](#`int-devintr`), that means the interrupt was handled appropriately. If it returned 0, it was some unrecognized interrupt.
  - (4.3) If all else fails, we check if a page fault had happened, perhaps through a lazily-allocated page (we returned some virtual address pointer but did not actually assign it a physical address, see [sys_sbrk](sys_sbrk) for more details.)
  - (4.4) If all else fails, we are unable to resolve the cause of the trap, and kill the process.
- If we failed somehow or if the current process got killed (we don't need process lock here as we are not accessing anything private to the process itself i.e. its state).
- [devintr](`int-devintr`) already handled incrementing the ticks that passed due to a clock interrupt, but we still need to yield the current process to the CPU (this is used due to the behaviour of XV6 to be round-robin, with each timeslot being equal to the clock interrupt. If you were implementing another kind of scheduling algorithm, you'd probably jump into the scheduler).
- After all that, we just pass the page-table of the process to `a0` and return to [userret](trampoline.md).
- \*\*QUESTION: Why do we turn on interrupts for system calls right away, but for functions like `devintr()`, we delay it until the very end?
  - Technically, I don't think we have to interrupt immediately for system calls, but I suppose that since these are user calls we try and service them as fast as possible. We turn it back on, actually, because interrupts can't because another system call won't be called from user space (since we are in kernel space), any thread of deadlock in a another system call is eliminated.
  - On the other hand, `devintr()` can be called again, and there are locks present. We don't want any deadlock happen, hence, we delay turning interrupts back on.

```c
//
// handle an interrupt, exception, or system call from user space.
// called from, and returns to, trampoline.S
// return value is user satp for trampoline.S to switch to.
//
uint64
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if((r_scause() == 15 || r_scause() == 13) &&
            vmfault(p->pagetable, r_stval(), (r_scause() == 13)? 1 : 0) != 0) {
    // page fault on lazily-allocated page
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  prepare_return();

  // the user page table to switch to, for trampoline.S
  uint64 satp = MAKE_SATP(p->pagetable);

  // return to trampoline.S; satp value in a0.
  return satp;
}
```

### `void prepare_return`

- Relatively straightforward, first we turn all instructions off because we are about to change the `stvec` to `uservec` WHILE still in kernel mode.
- We then map the `uservec` symbol using the location of the trampoline symbol in memory, with the appropriate offset being generated by `uservec` - `trampoline` and added to the virtual address.
- We store some of the important info i.e. the kernel page table (is this constant per CPU? Or is it global? If there's only one CPU it should be constant). It is constant, its just easier to use the address in the register instead of hard-coding the location of the kernel page table.
- We do the same for the vector that the kernel has to trap into, and the kernel stack (**QUESTION: Why do we reset the kernel stack each time?**). I assume it is because, unlike with user mode, there is no way to interrupt from kernel mode back to user mode. Hence, when we are finally returning from a trap, we can assume we are done with what we are doing, and any other trap will go through the `uservec` process again.
- We then do some bitwise operations to say that (1) the next trap will come from user mode (returning from a trap is not the same is the ORIGIN of a trap, lol), and also to enable interrupts when we get back (not interrupts RIGHT NOW, big difference, change is only made when the `sret` instruction is executed. See [this website](https://five-embeddev.com/riscv-priv-isa-manual/latest-adoc/supervisor.html#sstatus)) for more info.
- Lastly, `sepc` is written to so the CPU knows where we should return to once we execute `sret`.

```c
void
prepare_return(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(). because a trap from kernel
  // code to usertrap would be a disaster, turn off interrupts.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);
}
```

### `void kerneltrap`

- Similar to user-trap, but we need to save the registers into the stack (think just a regular machine code program in RISC-V) using `kernelvec` before moving to the next program. Here, we also need to store registers which are not written to by `kernelvec` explicitly i.e. PC (I don't think this is possible? You'd need a `jal` instruction, but you'd need to overwrite a register to store it (you could use `sscratch`, though why bother, it limits flexibility)), `sstatus` (Will most likely be written to by the interrupt, again, we only have so much space, so just store it here), and `scause` (same reasoning).
- Then we make sure that the interrupt came from supervisor mode, and that we have disabled interrupts (we don't do that for the user, since there is no trap back to user mode, obviously).
- We check the kind of interrupt, (it can't be a system call, we're already in kernel mode, and a page fault cannot technically occur since we've already mapped the whole thing).
- The rest is just like [usertrap](#uint64-usertrap)

```c
// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap() {
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0) {
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(),
           r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}
```

### `void clockintr`

- Obvious, we really mostly use this for system calls and other stuff that needs time. Notice the `r_time` function (returns the number of cycles we gave gone through, that is essentially our clock, so current time + 10ms) though, wherein we right the distance of the next timer interrupt by calculating the next one.

```c
void clockintr() {
  if (cpuid() == 0) {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}
```

### `int devintr`

- More or less common sense, `scause` will be set to that value by the CPU if it is an external interrupt, via [PLIC](plic.md). We try and claim the interrupt, which will be zero if it has already be claimed. Otherwise, we check the device number in the `_IRQ` field to see how we should manage the interrupt. Now, if `irq` was non-zero, then we mark the interrupt as complete first. Else, we just go ahead and return one.
- We increment ticks and return two for a clock interrupt.
- We return 0 if we don't recognize the interrupt / it didn't come from some device (assuming we count the CPU as a device).

```c
// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr() {
  uint64 scause = r_scause();

  if (scause == 0x8000000000000009L) {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ) {
      uartintr();
    } else if (irq == VIRTIO0_IRQ) {
      virtio_disk_intr();
    } else if (irq) {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);

    return 1;
  } else if (scause == 0x8000000000000005L) {
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}
```
