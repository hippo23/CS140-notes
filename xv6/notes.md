# XV6 File Breakdown

## User Files

## Kernel Files

### `spinlock.c` & `spinlock.h`

- **Spinlock**
  - A spinlock is a lock that causes a thread trying to acquire it to simply wait in a loop ("spin") while repeatedly checking whether the lock is available.
  - Since the thread remains active but is not performing a useful task, the use of such a lock is a kind of busy waiting.
  - This is typically used for a shared resource i.e. some space in memory or an I/O device that a specific CPU has locked onto.

#### `spinlock` structs

##### `struct spinlock`

```c
struct spinlock {
  uint locked; // determines if the lock is currently held
  // the other parts are meant for debugging
  char *name; // the name of the spinlock
  struct cpu *cpu; // the actual CPU that is currently holding the lock
}
```

#### `spinlock` Definitions

##### `void initlock`

- Simply initializes a lock with the following arguments. I assume that `cpu = 0` means that there is no actual CPU attached yet?

```c
void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}
```

##### `void acquire`

- This is how we assign an actual lock to a specific CPU.
- We use [push_off()](#####`void-push_off`) to disable all interrupts.
  - **QUESTION: What is the risk if we don't disable interrupts while acquiring a spinlock?** If we interrupt, but we acquire a lock, but then we get interrupted since the interrupt handler needs the lock that we have, that could cause a deadlock. The one holding the lock is waiting for the interrupt to terminate, and the interrupt is waiting for the lock (if I understand correctly, we can't somehow put an interrupt to sleep?).
- Following this, check if some other CPU is holding the resource, in which case we execute `panic()`
  - Note that `panic` does to seem to be defined in the code.
- We assign the value 1 to a5, and then the register s1 gets the current state of the lock.
- The instruction `amoswap.w.aq a5, a5, (s1)` works as follows:
  - `amo-` means that we are executing an atomic memory operation.
  - `swap` means that we are swapping the value of some register with some place in memory. `rs2` gets the value in memory, `rs1 is the location`, `rd` is the original value of `rs2`.
  - `.w` means that we are working in 32-bit sizes.
  - `.aq` refers to the 'memory-ordering' semantics.
    - `aq` is used for: This is defined as 'ensuring that all memory writes on other cores are visible to this core before the instruction completes'
    - `rl` is used for: ensuring that all writes from this core are visible to other cores before the instruction completes.
  - Because we are using 'acquire' semantics, we are essentially saying that before we swap this value into memory, MAKE SURE that any other writes from other cores happens first.
  - For our use case, we are saying that before we put this new value into the memory address, we make sure that any loads that are pending execute first (i.e. the lock has already been removed by the other CPU, but it is still in cache / a buffer).
- In the same manner, [`__sync_lock_test_and_set`](https://www.ibm.com/docs/en/xl-c-and-cpp-aix/13.1.0?topic=functions-sync-lock-test-set) decomposes into atomically assigning the value of `__v` to the variable that some address `__p` points to while creating an acquire memory barrier. We continue putting the value one into the `locked` attribute, but to begin with, the value of `locked` must be 0 for it to be valid, otherwise we keep _spinning_ waiting for the lock to be released by whoever was using it.
  - This is the key to avoiding multi-core deadlocks, as the MEMORY SYSTEM will be told that, if we receive two different writes to this location, you can only approve one.
- [`__sync_synchronize`] creates a full memory barrier, in RISC-V this would be a [`FENCE`](https://stackoverflow.com/questions/26374435/what-is-meant-by-the-fence-instruction-in-the-risc-v-instruction-set) instruction. Essentially, all previous instructions (and note that we technically only have one list of instructions that COULD execute out of order) BEFORE the fence must execute before actions AFTER the fence are executed.
  - This is important for our code because we don't want the data that we locked somehow being written to after we've locked (can happen sometimes in race conditions).
- Only once all these are done can we finally say that this lock has been acquired by this CPU.

```c
void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
              // deadlock meaning that its possible for two tasks to lock. Disabling interrupts
              // makes it so that there is no other task that can lock.
  if(holding(lk))
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

```

##### `void release`

- This is used when a certain CPU is done with a resource. The first few steps are similar to the [`acquire`](#void-acquire)
- We can see that `__sync_synchronize` is still present, meaning that we want to make sure that all actions from the CPU (particularly writing) are read before the instruction releases, to make sure that the next instructions get the synchronized data.
- We then execute another atomic instruction, [`__sync_lock_release`](https://www.ibm.com/docs/en/i/7.5.0?topic=functions-sync-lock-release) which assigns the value 0 to the variable that the address points to. Additionally, a release memory barrier is created by this (otherwise known as **release semantics**) wherein we make sure all writes from the current CPU are visible to the other CPUs.

```c
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}
```

##### `int holding`

- Conditional function used to show whether or not a current lock is held by some CPU.

```c
// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}
```

##### `void push_off`

- Used to disable all interrupts from a specific CPU. `intr_off` uses the function [`w_sstatus`](#`void-wsstatus`) to manipulate the control registers which handle whether or not a CPU is able to take more interrupts.
- At the start, [`intr_get`](#`int-intr-get`) returns a boolean (or int) saying whether or not interrupts are enabled
- You will notice that if `noff` was zero (meaning to say that interrupts were currently enabled) we change `intena` for it to say that interrupts were enabled before `push_off()`
- Otherwise, we add one to `noff`. The reason behind this is imagine we have multiple instructions that are locking different resources, each of them wants to avoid an interrupt. Hence, we need to have a counter that says only when this is zero (meaning that there are no instructions that disabled interrupts still around) can we enable interrupts again.

```c
void
push_off(void)
{
  int old = intr_get();

  // disable interrupts to prevent an involuntary context
  // switch while using mycpu().
  intr_off();

  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}
```

##### `void pop_off`

- Here is where the previous details in [`void push_off`](#void-pushoff) come into play. First we check if interrupts were already enabled, in which case, we panic, saying that the CPU is already interruptible.
- If the CPU has no nested `push_off` calls, then we also panic (I'm assuming that the calls afterwards don't execute).
- Besides those two calls, we unnest the last `push_off` call by decrementing `noff`. If there are no more `push_off` calls on the stack and its last state when it was zero was that interrupts were enabled, then we restore it to that state by enabling interrupts once more.

```c
void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}

```

### `proc.c` & `proc.h`

#### `proc.c` structs

##### `struct proc`

- One notable aspect here is the existence of both [trapframe](#`struct-trapframe`) and [context](`struct-context`).
  - The reason for having both is that they serve different purposes. The trap frame of a CPU is updated whenever we go into kernel mode and restored whenever we leave kernel mode. The context of a CPU is updated and restored during a context switch.
  - **QUESTION: When do the two states diverge?** A simple example is during a context switch that occurs DURING a kernel-level process. Let's say, for some reason or another, we switch while in kernel mode. The current state is the kernel-level registers, but we expect to eventually go back to user-level, hence, we will need both `context` and `trapframe` respectively.
    - One example would be the scheduler forcibly dropping a process due to some exception or a system call. Obviously, you do not want to return to a process that is causing an error. Hence, the kernel drops the process itself, never leaving kernel mode.

```c
// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};

```

##### `struct cpu`

- The CPU `struct` handles the state of each CPU. It has a `proc` attribute for the current process, which uses the [proc](#####cpu) struct.

```c
struct cpu {
  struct proc *proc; // the process running on the CPU
  struct context context; // swtch() here to enter scheduler() MY NOTES: Context refers to the current process running on the CPU. Changing
                          // an active process is a context switch, I wonder why they are separate? Apparently it refers to the registers
                          // of the CPU. Hence, it means the CPU state.
  int noff; // depth of push_off() nesting
  int intena; // Were interrupts enabled before push_off()
}
```

#### `proc` Global Variables

- `struct cpu cpus[NCPU]`
  - Just used for tracking the number of CPUs present in the system
- `struct proc proc[NPROC]`
  - A table of all the processes currently running (64 is the maximum number of processes in the system) (is this the case for all systems? What happens if a system wants to make a new process but there's no space. Or does it refer to currently running processes?)
  - This [answer](https://stackoverflow.com/questions/9361816/maximum-number-of-processes-in-linux) seems to say that there is an unlimited amount of processes but that it is limited per user.
  - No new processes will be created until there is space. This is just enforced for performance reasons, naturally, you could just store more and more processes with a larger and larger memory table wherein some map to the disk and others to RAM.
- `struct proc *initproc`
  - ^^Fill this in later
- `int nextpid = 1`
  - All processes' PID start from 0 (process 0 refers to the kernel process I'd believe?).
- `struct spinlock pid_lock`
  - ^^To be filled in later
- `struct spinlock wait_lock`
  - I assume this is used possibly for waiting on another CPU process to finish? I don't know how that would work though since CPUs aren't an exact memory location but we'll see going forward.

#### `proc.c` functions

##### `void procinit`

- Note that `pid_lock` and `wait_lock` are global variables. We initialize them here at the same time as initializing the entire project table.
  - **QUESTION: Why here?** This is the first entry point for any process, and thus it simply makes sense. Note that we are not acquiring the locks, simply initializing them.
- Additionally, for each process that is available, we initialize a lock for each process. Remember, processes are pieces of memory that a CPU can access. Hence, it makes sense that we don't want one process running on different CPUs, hence the lock.
- Lastly, we assign the value of its stack in the same manner we did when creating them.
  - **QUESTION: Does the assignment of `kstack` happen before or after the execution/s of `proc_mapstacks`? Does the order matter at all?**
  - If we take a look at the entry point in [`main.c`](#`main.c`), we see that `kvminit` is ran before `procinit`. However, I theorise that the order in which these two are executed don't really matter, since `procinit` never accesses the values of `kvminit` and vice-versa, they just have the same basis in `KSTACK`.

```c
// initialize the proc table.
void procinit(void) {
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}
```

##### `int cpuid`

- We call [`r_tp()`](#`static-inline-r_tp`), which is a raw assembly instruction that returns the thread pointer register, used by xv6 to hold the current core's number.
- **QUESTION: What file determines where these registers are setup and how?**

```c
// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
  int id = r_tp();
  return id;
}
```

##### `struct cpu *mycpu`

- Relatively straightforward, use the core number to index into the table of core `struct` and return that value.

```c
// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}
```

##### `struct proc *myproc`

- Recall what [`push_off()`](#void-pushoff) does, disable the current CPU being able to take interrupts (in this case it is equal to the return value of [`my_cpu()`](#struct-cpu-mycpu)).
  - **QUESTION: Why is this necessary?** An interrupt can change(?) the currently running process on the CPU i.e. I/O, timer interrupt forcing pre-emption according to the scheduler. More specifically, imagine a timer interrupt wherein we yield the current process. If we are midway through `myproc()`, we could have already retrieved the CPU, but this would be the old one, so when we run `c->proc`, we'd be getting the process of another CPU.
- After retrieving the current process, we then restore interrupts to its original state.

```c
// Return the current struct proc *, or zero if none.
struct proc *myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}
```

##### `void proc_mapstacks`

- When this is run, we first pass in a type of `pagetable_t` (which is really just `uint64 *`, a list of 64-bit integers).
- We create a new process `*p`.
- For each process that is currently in the process table, we allocate a single page using [`kalloc()`](#void-kalloc) which in return gives us the starting address.
- We then get the value `va` using the macro `KSTACK` which essentially is the address right below the [TRAMPOLINE](https://xiayingp.gitbook.io/build_a_os/traps-and-interrupts/untitled-3)
  - The `TRAMPOLINE` can be thought of as the code we need to execute to go INTO kernel mode. It is mapped to the same virtual address in both user and kernel spaces so that switching page tables doesn't effect its usage.
  - It is usually located that the highest possible point in virtual memory.
- Note that we pass in the page number to `KSTACK`, essentially saying, hey, we need the stack that is about `p` pages down (or `p * 2` if there are invalid guard pages).

- Following that, we run [`kvmmap`](#`void-kmmap`)
  - See the actual details for more info, but we essentially are just making a new mapping in the some page table. The `kpgtbl` is the object, `va` is the virtual address (the kernel stack), `pa` is the actual address that we allocated in memory, its size, and some flags.

```c
// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    // kvmmap is used to create a direct-map page-table for the kernel
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}
```

##### `void proc_freepagetable`

- This is just unloading a process and its trap frame (trap frame being the place in physical memory where the 'state' of the process is stored whenever we trap into the kernel).
- For more detailed implementations, see [`uvmunmap`](#void-uvmunmap) and [`uvmfree`](#void-uvmfree). Basically, it just removes the mappings of this page table virtual address to this portion in physical memory (and optionally frees the memory the page was using).

```c
// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}
```

##### `struct proc *allocproc`

- Execution is straightforward, we lock into a process _(but it isn't actually locked, since no CPU has the process yet?)_. If there is a free process entry, then we allocate a PID and set its state to used, allocate a trap frame page using [`kalloc`](#void-kalloc) (this can go anywhere, it is different from the kernel stack. The kernel stack is used WHEN we change into kernel-space, that is where we store the data in the trap frame).

```c
// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *allocproc(void) {
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trap frame page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

##### `void userinit`

- Just initializes the first user process. Note that we have to release the lock on the process (technically it is not held since no CPU is attached but just do this to avoid confusion).

```c
// Set up first user process.
void userinit(void) {
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}
```

##### `int shrinkproc`

- This shrinks the process size by `n` bytes. [`uvmdealloc`](#`uint64-uvmdealloc`) is used to bring the process size to a smaller size. These need not be page-aligned. Note, however, that in the actual function, we just use the non-page aligned entries and get the closest page-aligned estimate version (basically, the extra space of the page is just left blank, but it is technically available to the process if it needs it).

```c
// Shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int shrinkproc(int n) {
  uint64 sz;
  struct proc *p = myproc();

  if (n > p->sz)
    return -1;

  sz = p->sz;
  sz = uvmdealloc(p->pagetable, sz, sz - n);
  p->sz = sz;
  return 0;
}
```

##### `int fork`

- Execution is straightforward, we use [allocproc](`#struct-proc-*allocproc`) to create a new process. We then use [`uvmcopy`](`#int-uvmcopy`) (which copies the parent's page table into the child's page table).
- Setting the `sz`, `trapframe` (the value, not the address), and the open file descriptors
  - Increment the reference count to each file using [filedup](#`int-filedup`)
- We use [safestrcpy](#`char-*safestrcpy`), which is just like the regular `strcpy` but guaranteed to NULL-terminate.
- **NOTE:** You will notice that we release the `np->lock` of the new process twice. As of right now, I can see no real reason to do this, since the possible deadlocks `wait()` and `exit()` are impossible to actually occur.

```c
// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void) {
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}
```

##### `void reparent`

- Straightforward except for the `wakeup` call to the initial process. We need to call wakeup as it is possible that the `initproc` will inherit `ZOMBIE` processes that haven't been harvested by the parent (perhaps the parent was not waiting for any end in execution).

```c
// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p) {
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}
```

##### `void exit`

- Again, rather straightforward. Two things to note:
  - First, we see the function [`begin_op`](#`void-beginop`). We use this before any system call to mark its start and end. It is used for logging transactions with the file-system. (See the heading for more details).
  - You will also see that at some point, we hold both the `wait_lock` and the `process_lock` for the current process. `wait_lock` is necessary for re-parenting the process, making sure that no `wait` or `sleep` call belonging to `initproc` somehow becomes lost (`reparent` requires this.)
    - Additionally, we need to wait until the end to let go of both locks, as we do not want another process to be able to be waiting on this (which, again, requires `wait_lock`) while the process has not yet been tagged as a zombie.

```c
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status) {
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}
```

##### `int wait`

- The first step here beyond declaring variables is to acquire `wait_lock`
  - **REMINDER:** We need wait_lock to avoid lost wake-ups. If we don't acquire wait lock, then we could possibly wait on a child that is in the midst of exiting but has not yet become a zombie.
- Another important thing to note is the use of [copyout](`int-copyout`). Lots of details in the implementation, but we essentially just copy some data from a source (in this case, the exit state) to the physical address corresponding to the given `addr` in virtual memory via the `pagetable`.
- After looping through all the kids, we terminate if (1) there are no kids; or (2) the current process was killed by the kernel (this does not need a `wait_lock`, this is something that the kernel can force at will).
- If we are unable to find any `ZOMBIE` child processes, we call [sleep](#`void-sleep`), which sets the wait channel of the current processes. When the child processes exit, then a wakeup call will be issued.
  - Notice the passing in of `wait_lock`. Again, to avoid any lost waits wherein a process goes to sleep just as a wakeup call is issued, we make sure the sleep call is locked onto wait until we can mark the process as sleeping. See more details at [sleep](#`void-sleep`)

```c
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr) {
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}
```

##### `void scheduler`

- Straightforward enough, refer to [swtch](#`swtch-asm`) for more details on that part.
- **QUESTION: One part that can cause confusion is the use of `wfi` to wait for interrupts. How can we wait for interrupts if we disabled interrupts in the CPU?**
  - This [answer](https://e2e.ti.com/support/processors-group/processors/f/processors-forum/186688/what-happens-if-an-interrupt-occurs-when-it-is-being-disable-will-it-get-serviced-once-we-re-enable-or-we-are-going-to-miss-it) is not in RISC-V, but the philosophy is similar. There is some way that we store data i.e. a flag register. The `wfi` command checks this, so it doesn't matter if interrupts are not enabled.
  -
- **QUESTION: Notice how when we find no available process, we wait for an interrupt. But what if some process goes from SLEEPING to RUNNABLE? Will the CPU not miss the execution of this process?**
  - Firstly, there will never be deadlock of no CPU's being able to run because they are waiting for an interrupt which not all newly `RUNNABLE` processes do. If a CPU turns into a runnable, it is response to some other event which must've been running i.e. from a `wait` that terminates from an `exit`.
  - Secondly, it is not a waste of CPU's to have only one that is not in `wfi`. This is because of the timer interrupt, which ensures that at some point all CPUs will run once again (among its other uses, such as ensuring that we are not deadlocked by any processes).
- **QUESTION: This is a FCFS implementation, what are the disadvantages?**

```c
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.

    // MY NOTES: if a previous processes had disabled interrupts, then processes that are waiting
    // on some interrupt (for example, I/O has finished transferring via DMA, then we want to notify the CPU)
    // so that things can start running again
    intr_on();
    // MY NOTES: after we allow interrupts and the processes waiting on them get notified(?) we turn it back off.
    // allowing interrupts while we are still going through processes could mean that an interrupt that has finished
    // won't reach the CPU because we were still going through processes to run

    // to be more precise, imagine we have some process that was sleeping when we passed it, but then it wakes up
    // to become a runnable process. Since we go into 'wfi', we handled the interrupt already, so its gone, but wfi
    // still thinks there has been no interrupt and that all processes are sleeping. Hence, we tolerate changes to
    // process state only BEFORE we loop through them.
    intr_off();

    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      // MY NOTES: We run the first processes we find as runnable
      if (p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        // MY NOTES: we move context and perform a context switch from the old processes to the new one
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    // only if we find no processes will we wait for interrupts
    if (found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}
```

##### `void sched`

- This is used in [exit](#`void-exit`), [yield](#`void-yield`), and [sleep](#`void-sleep`).
- **QUESTION: What is the purpose of this process in each of the functions?**
- The `sched` function checks if we can switch the current process running. There are four possible conditions when we are unable to do this:

1. _The lock is currently held by some other code_
2. _`cpu->noff` is not equal to one._
3. _The state of the current process is not running_
4. _Interrupts are not enabled according to `instr_get`_

- The reasoning behind condition (1) is obvious, you don't want some other CPU or code using the process to somehow execute at the same time as the process we are intending to get rid of.
- (2) is rather ambiguous, but essentially, `noff` is only incremented whenever we acquire a lock. Because we are switching into a new process, it is integral that we have no other locks held as we don't when this process will be scheduled again (if ever).
- (3) makes sure that it is not a running process, and that there is a reason we are trying to go back into scheduler.
- (4) Make sure that no interrupts are enabled, lest we fall into some sort of deadlock (disabling should be handled prior, as a deadlock could already technically occur here, this is just a second measure).

- Then, we save the original `intena` status as this belongs to the current process and not the CPU. Once we are done with [swtch](#`void-switch`), we go back and restore the original value of `intena` and return to where the process was currently executing in `sched` (some processes never reach this point).
- **QUESTION: Why should we not save both `proc->intena` & `proc->noff` (in the theoretical case that `noff` is not a CPU property)? Are they both not part of the thread state?**
  - The [scheduler](#void-scheduler) is a good example, wherein we acquire a process lock and we don' want to have any interrupts with it, but there is no active process, technically speaking.

```c
// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}
```

##### `void yield`

- This is done if a process voluntarily wants to go back to the [scheduler()](#void-scheduler). We need to acquire the process lock, as prescribed by [sched()](#void-sched), set the state to `RUNNABLE`, and then release when we are ready to go back to wherever the process was running.

```c
// Give up the CPU for one scheduling round.
void yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}
```

##### `void forkret`

- This is ran at the start of every [fork()](#int-fork) process when it is first scheduled (you will notice in [fork](#int-fork) that we set `p->context.ra` to `forkret`, so that when the [scheduler](#void-scheduler) first switches into it).
- You'll notice that there is a special case for the very first process being ran.
  - **QUESTION: Why the special case for the file system? What exactly is [fsinit](#void-fsinit) doing?**

```c
void forkret(void) {
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke exec() now that file system is initialized.
    // Put the return value (argc) of exec into a0.
    p->trapframe->a0 = exec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}
```

##### `void sleep`

- I've already looked at this so much I kind of know it by memory, but if you don't:
  - Naturally, we need to lock onto the process because we will be modifying its state. Now, after setting the channel and state, we just go back into the scheduler. Once there is a call to [wakeup](#void-wakeup), state will be set to `RUNNABLE` and the scheduler will eventually hop back on.
  - One thing to note: We see the use of the conditional `struct spinlock *lk` parameter (which is really just the global `wait_lock`). This is done to make sure, again, that there are no lost waits, and that a process about to sleep doesn't miss a wakeup call executing at the same time.
    - **QUESTION: Should the parameter `lk` not be dependent on channel?** This is actually correct, but I believe for simplicity's sake we just use a global `wait_lock`, meaning that if one process is going to sleep then no wakeup call can be issued. Once we do reach a process lock, however, we can let go of the `wait_lock`, since now each wakeup call has to acquire the `p->lock` before checking the process state and possibly modifying it.

```c
// Sleep on wait channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}
```

##### `void wakeup`

- Straightforward implementation, go through all the processes, acquire the lock, then check if they have the same channel and are sleeping. If they do, release the lock and set their state to `RUNNABLE`.

```c
// Wake up all processes sleeping on wait channel chan.
// Caller should hold the condition lock.
void wakeup(void *chan) {
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}
```

##### `int kill`

- Used to terminate a process forcibly. Because we went into the kernel via some trap i.e. a timer interrupt is common when forcibly killing processes but other exceptions could cause it too, eventually the process will have to return to user space. It is there that its killed state is caught and it calls [exit](#void-exit) to execute the necessary processes and set state to `ZOMBIE`.

```c
// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid) {
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
```

##### `void setkilled`

- Common sense.

```c
void setkilled(struct proc *p) {
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}
```

##### `int killed`

- Also common sense.

```c
int killed(struct proc *p) {
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}
```

##### `int either_copyout`

- An extension of `copyout`, allowing to use copy data from a kernel address to either a user address or kernel address. See [copyout](#int-copyout) for more details.
- We need `copyout` for user address data because we are copying data into physical memory corresponding to the current process's virtual memory, which is different for each process. We only need `memmove` for the kernel since [kernel address space is the same across all processes](https://unix.stackexchange.com/questions/475676/does-the-linux-kernel-have-its-own-page-table).

```c
// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}
```

##### `int either_copyin`

- The same philosophy as [either_copyout](#int-eithercopyout)

```c
// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}
```

##### `void procdump`

```c
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
  static char *states[] = {
      [UNUSED] "unused",   [USED] "used",      [SLEEPING] "sleep ",
      [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
```

### `kalloc.c`

- This is where methods used for physical memory allocation are defined.
- Pages are size **4096** bytes. User processes, kernel stacks, page-table pages, and pipe buffers use this.

#### `kalloc.c` structs

##### struct `run`

```c
struct run {
  struct run *next;
};
```

##### struct `kmem`

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
```

#### `kalloc.c` functions

##### `void *kalloc`

- The basis of all the other calls in this file. As in the comments, it allocates 4096 bytes of memory and returns a pointer to the start of that memory.
- Notice that we are using `kmem` by acquiring then releasing it
  - We first acquire the `kmem` lock, meaning that no one else can be allocating memory at this point in time
    - **NOTE**: It is at this point I noticed that spin-lock doesn't actually refer to any specific address. In this case, I suppose it is up to the implementation to have a spin-lock for each part of memory to avoid having other processes access it.
  - We get the `freelist` (meaning to say the space in memory that is still available) of `kmem` and assign the 'pointer' of the kernel to the next position of `r`.
  - Now that we have moved the pointer (and made it so that all the memory before this portion can no longer be allocated to another process), we release the lock on `kmem`.
- Now, if `r` is successfully defined (meaning to say that `kmem.freelist` did not route to null, which it would if there was no more memory), we use `memset` to fill the space with junk acc. to `PGSIZE = 4096` or whatever size you want to set it to.
- **QUESTION**: We are moving the kernel before allocating memory, which makes sense from a security perspective, but how exactly does the code know to leave more than 4096 bytes of space before the `->next` pointer.
  - The answer is actually in the [`kinit`](#void-kinit) function. See its details for a further explanation. Essentially, all the `r->next` addresses already exist with the proper `PGSIZE`.

```c
// allocate one 4096-byte page of physical memory.
// returns a pointer that the kernel can use.
// returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, pgsize); // fill with junk
  return (void*)r;
}
```

##### `void kfree`

- Used to either (1) free memory allocated by [kalloc](#void-kalloc) or (2) when initializing the [allocator](`#void-kinit`).
- We take some address `pa`. If it is not divisible by page sizes, or if it less than the first address after kernel (why?), or greater than or equal to `PHYSTOP` (`PHYSTOP` being the last address available to the kernel in RAM).
- Otherwise, we continue and fill the address with junk. Then we create a `run` struct from the `pa` address, lock it to make sure that no other operations are manipulating or clearing this part of memory, and move back the `kmem` pointer by one page.

```c
// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

##### `void freerange`

- Given some `pa_start` address, we get the closest address larger than it that is divisible by `PGSIZE`. For each possible page address following that, we clear spaces in memory using [kfree](#`void-kfree`). In doing so, we also setup `r->next` for all the possible addresses.

```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}
```

##### `void kinit`

- Relatively straightforward, we utilise a struct called `kmem` (it was a one time struct as you can see above) and initialize the `lock` attribute and assign it the name `kmem`.
- We also use the [freerange](#void-freerange) function

```c
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}
```
