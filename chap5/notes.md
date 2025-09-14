# Chapter 5: Process API

## .0: Important Terms

- **Shell**
  - A prompt wherein you enter a command (the name of an executable) which is then loaded into memory, `fork()` is called to create a new child process in the parent process of the shell and then waits for the child process to finish.
- **Standard Output**
  - Standard output in the shell is the screen.
  - File descriptors are used and kept open across the `exec()` call.

## .1: The `fork()` System Call

- Used to create a new process. But consider the following code:

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  // this is printing the PID of the current process
  printf("hello (pid:%d)\n", (int) getpid());
  int rc = fork() // here, we are creating another process
  if (rc < 0)
  {
    // this means that the fork has failed
    fprintf(stderr, "fork failed\n");
    exit(1);
  }
  else if (rc == 0)
  {
    // a new process has been created
    printf("child (pid:%d)\n", (int) getpid());
  }
  else
  {
    // here, we are now in the parent process
    printf("parent of %d (pid: %d)\n", rc, (int) getpid());
  }
  return 0;
}
```

- We cannot make assumptions about which exact process is going to run first,
  as this depends on the CPU scheduler.

## .2: The `wait()` System Call

- See the code below for an example of the `wait()` call.

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  printf("hello (pid:%d)\n", (int)getpid());
  int rc = fork();
  if (rc < 0) {
    fprintf(stderr, "fork failed\n");
    exit(1);
  } else if (rc == 0) {
    printf("child (pid:%d)\n", (int)getpid());

  } else {
    int rc_wait = wait(NULL);
    printf("parent of %d (rc_wait:%d) (pid:%d)\n", rc, rc_wait, (int)getpid());
  }
  return 0;
}

```

- See that `wait(NULL)` awaits any child process to finish (whenever one finishes). In the case where there is more than one child process and you want to wait for an exact one, use `waitpid()`.

## .3: The `exec()` System Call

- See the code below for an example:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  printf("hello (pid:%d)\n", (int)getpid()); // print the parent PID
  int rc = fork();                           // fork into a child process
  if (rc < 0) {
    fprintf(stderr, "fork failed\n"); // fork has failed
    exit(1);
  } else if (rc == 0) {
    printf("child (pid: %d)\n", (int)getpid()); // print the new child process
    char *myargs[3];
    myargs[0] = strdup("wc");      // program name?
    myargs[1] = strdup("code3.c"); // program file or the input file
    myargs[2] = NULL;              // end of the array
    execvp(myargs[0], myargs);     // runs word count
    printf("this shouldn't print out");
  } else {
    int rc_wait = wait(NULL); // wait for child process to finish
    printf("parent of %d (rc_wait: %d) (pid:%d)\n", rc, rc_wait, (int)getpid());
  }
  return 0;
}
```

- This is used if you want the child process to run a different program.
- What `exec()` does is that it loads code (and static data) from that executable passed in and overwrites the current code segment and static data with it. The child process essentially becomes a new program starting from `exec()`.

## .4: Motivating the API

- The separation of `fork()` and `exec()` allows the shell to run code after the call to fork but before the call to `exec()`.
- For example, consider the following:

```bash
wc code3.c > newfile.txt
```

- This redirects from **standard output** into `newfile.txt` opened using a file descriptor for the following `exec()` call to use.
- Integral to this is the behaviour of file descriptors. _UNIX_ systems immediately start looking for free file descriptors at zero. This essentially will become the **standard file descriptor**.
- See the code below for an example:

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// if all goes well, this opens this file for WC, using that is standard input,
// and reads the words in there.
int main(int argc, char *argv[]) {
  int rc = fork();
  if (rc < 0) {
    exit(1);
  } else if (rc == 0) {
    close(STDIN_FILENO); // this refers to standard output, hence, we are
                         // closing standard output

    // this p4.c becomes the input of the next program we open
    open("./code4.c", O_CREAT | O_RDONLY, // note the change in permissions to read-only, and the
                                          // removal of the truncate when fd is closed.
         S_IRWXU); // standard input is the lowest
                   // that is open.
    char *myargs[2];
    myargs[0] = strdup("wc");
    myargs[1] = NULL;
    execvp("wc",
           myargs); // given that there is no file passed in, it relies on
                    // the standard input, which is p4.c
  } else {
    int rc_wait = wait(NULL);
  }
  return 0;
}
```

## .5: Process Control And Users

- Beyond the essential system calls we discussed, there are also calls like `kill()`, which ends a process or pauses it until another command resumes it. These, for most software, are triggered externally by commands like `CTRL-c`, otherwise known as `SIGINT`. To build code that listens for these signals, use the `signal()` system call.
- This capability force security to be together to make sure that users can't just end any process, only the processes THEY initiated (or if they are root user, any process).

# Additional Reading

## The `fork()` in the road

### Introduction

- The potential limitations of `fork()` are the following:
  1. Not thread-safe
  2. Inefficient and unscalable
  3. Security concerns
  4. Fork does _NOT COMPOSE_, every layer of the system from the kernel to the user-mode library has to support it.

### The History of `fork()`

- The original fork in the _Project Genie Time-sharing System_ was the first use of the call, but it allowed the parent to specify the address space and machine context for the new child process.
  > _Notice how this opposes the microkernel paper that believed any process must be a self
  > contained OS for everything except communication between processes._
  - However, there was no function to copy the address space, as was done by Unix.
    - This refers to the `fork()` process taking all the variables from the parent process that they had at the moment of the fork.
    - This sharing of memory between parent and child was hard to implement due to the lack of virtual memory software. I assume, then, that paging and swapping were not a thing, so when some process was going to be transferred, the ENTIRE process would go to the disk instead of just a few parts of memory that can be connected via virtual memory.
      > "Fork" (start new sub-task) was accomplished by simply swapping out the current task,
      > then assigning a new task ID to the task image still in memory and letting it
      > continue running (as the "forked" subtask).
- The **TENEX** OS was a counter-example to the UNIX approach.
  - It either shared the address space between the parent and the child or created the child with an empty address space. There was no actual copying of address space.

### Advantages of the `fork()` API

1. It simplified the UNIX API

- Fork needs no arguments, and provides a simple default for all the state of a new process.
- Creating a process with fork is orthogonal to creating a new program.

2. **Fork eased concurrency** -> This provided an effective form of concurrency before asynchronous I/O. It provided, before libraries, a simple form of code reuse.

### Fork in the Modern Era

#### **Fork is no longer simple**

- The POSIX specification lists 25 special cases in how the parent's state is copied to the child.
  - File locks (whether or not some process is already using an opened file-descriptor?), timers, asynchronous I/O operations, tracing, etc.
  - Other system calls also affect the behaviour with respect to memory mappings i.e. `madvise()`, file descriptors i.e. `(O_CLOXEC)`, and threads i.e. `pthread_atfork()`.
- **Fork doesn't compose**
  - Copied address space makes it hard to create abstractions. For example, with buffered I/O, a user must flush I/O prior to fork, lest output be duplicated.

  ```c
  printf("Hello, world!"); // goes into stdio buffer, not yet written
  pid_t pid = fork();
  if (pid == 0) {
    exit(0);
  } else {
    wait(NULL);
  }

  // This will output Hello, world!Hello, world!
  ```

  - On the other hand, notice what happens if we change the code ever so slightly,

  ```c
  printf("Hello, world!\n"); // goes into stdio buffer, not yet written
  pid_t pid = fork();
  if (pid == 0) {
    exit(0);
  } else {
    wait(NULL);
  }
  // this will output Hello, world!
  ```

  - The reason this code only prints once is because, by default, if there is no newline, the program will buffer the I/O and only print it once it is full. Adding a new line makes it flush immediately, which prevents duplication in the I/O buffer of the child process.

#### **Fork isn't thread-safe**

- A child created by a `fork()` only has a single thread (a copy of the calling thread). If a parent has multiple threads (meaning multiple independent instructions being executed) then the child isn't really a complete snapshot of the parent.
- **Example**:
  - One thread is doing memory allocation and has a heap-lock while another thread begins a `fork()`.
  - Any attempt to allocate memory in the child (acquiring the same lock) will deadlock waiting for an unlock operation that will never happen.
  - _A bit unclear, need to know how exactly lock and unlock is implemented in UNIX_
- A note on `mutex()` and locking in UNIX
  - it is basically waiting for a certain region of code or memory to be unlocked by one thread before being used by others.
  - This is made possible due to the knowledge of the CPU that THIS PROCESS has THESE THREADS and THIS THREAD IS LOCKED.
  - Copying only one thread removes this behaviour.

#### **Fork is insecure**

- A fork inherits everything from its parent, and the programmer is in charge of removing any unwanted state i.e. `fd`, secrets from memory, isolating namespaces. This behaviour violates the principle of least privilege. It also renders address-space layout randomisation (security via not knowing where the executable exactly is and where its buffer) ineffective.

#### **Fork is slow**

#### **Fork doesn't scale**

- Beyond the time needed to setup copy-on-write mappings for pages going from parent to child, the fork API fails to commute with other operations on the process (The order of the operations relative to `fork()` matter).
- Because a forked process inherits everything from the parent, it encourages centralisation of that state in a monolithic kernel where it is cheap to copy and/or reference count. It is then hard to implement kernel compartmentalisation for security or reliability.

#### **Fork encourages memory overcommit**

- Imagine a very small child process forking from a large, complex process. By default, it will take all the pages of the parent process. In the worst-case, this could result in an overflow relative to memory capacity.
- In conservative systems, the fork will fail if there is not enough space in memory to accommodate ALL potential copy-on-write clones.
- To workaround this, Linux overcommits in virtual memory, believing that there is enough space in backing store to accommodate all these copies. However, it does not actually check if there is. This triggers the "out of memory killer" to terminate processes and free up memory.

#### Overall

- Fork is a convenient API for fine-grained control over child processes without the need for strong isolation.
- Why is this good for a shell, but not for other types of applications???

### Difficulties with implementing `fork()`

### Incompatible with a single address space

- Many modern processors, particularly in embedded systems, work in a single address space.
- An example is **Drawbridge libOS** which implements a binary-compatible Windows-environment within an isolated user-mode address space, otherwise known as a _picoprocess_.
  - To create a new child process, all that happens is the allocation of a certain part of the address space to that child process, and then the creation of a new thread for it (meaning that it is still part of only one process).
  - The only form of protection here is the picoprocess itself.
- In Unikernels (wherein everything is in a single address space), it is basically impossible to implement `fork()` as is.

### Incompatible with heterogeneous hardware

- Fork restricts the definition of a process to a single-thread within a single address space.
  With heterogeneous hardware, not all processes look like this
- Some hardware involves having a kernel bypass, wherein hardware directly communicates with software without having to go through the kernel. In this case, forking can become complicated.

### Infects an entire system

- An efficient fork at any layer requires a fork-based implementation at all layers below it. For example, Cygwin is a POSIX compatibility environment for Windows; it implements fork in order to run Linux applications.

### Alternative to `fork()`

#### `posix_spawn()`

- Starts with a completely fresh image of the child program, no address space is cloned from the parent to the child. In most cases, this is better than fork, unless you need something from the parent address space, need to switch to an isolated namespace.

#### `vfork()`

- Similar to the early implementation of fork in Project Genie, wherein address space is shared until the child calls exec. To enable the child to use the parent's stack, it blocks execution of the parent until exec.
- This shared address space, however, comes with the same safety concerns of fork.

## A Multiprocessor System Design: Conway

### Specifying Parallelism

- The paper specifies the five following commands as forming the backbone of parallel computing
  1. `LINE A: FORK A, J, N`
  2. `FORK A, J`
  3. `FORK A`
  4. `JOIN J, B`
  5. `JOIN J`

#### FORK A, J, N

- The first instruction works by starting a fork AND setting the value of the `ctr` variable at the `JOIN` location. In this case, we set it to four because we have four processes we want to await before running the code at `N`.
- After setting that `ctr`, we fork to `LINE A + 1 = LINE B` and `LINE N`.

#### FORK A, J

- Similar to first instruction, except the `J` refers to adding to the `ctr` at location `J` by one. Useful for conditional forks.

#### FORK A

- Basic fork instruction

#### JOIN J, B

- Executed at the end of forks. If the `ctr` at `J` is not yet zero, continue into branch `B`.

#### JOIN J

- Basic join instruction.

### The State Word

- The state word can be the data that needs to be stored into the registers of the CPU for the program to execute. When not in execution, the state word is put into _control memory_, when it needs to actually be executed, the state word is loaded into _state memory_ and the processor communicates with _main memory_ to execute the task.
- This bare-bones arrangement presents some bottlenecks between the two memories (_can you think of one?_).
  - If _control memory_ is only active when `FORK` and `JOIN` instructions are happening, then I/O for long periods of time will stall an instruction.
- Consider the following:
  - `FORK-JOIN` instructions has no distinction between parallelism within a program and parallelism between programs, which can help with simplicity.
  - What are the roles of interrupts in this system?
    - **Internal Interrupts**
      - Triggered by the execution of an instruction and the demand of insertion following the instruction. Overflow, divide check, and invalid address alarms are examples of this.
    - **External Interrupts**
      - Triggered by event not closely timed to the instruction being executed and demands execution of code that isn't always related to the code being executed at interrupt time i.e.I/O operation complete and time clock interrupts.
      - Drawing from that definition, because external interrupts are independent, you can actually just use `FORK` and a `JOIN` such that the I/O can become another parallel process.

### The Storage Subsystem

Three problems related to high-speed storage arises.

1. Complete storage protection must be incorporated in order to isolate the several programs
2. Scavenging and allocation of storage to newly entering programs should not be an expensive process.
3. The effective service rate of the memory should not seriously depreciate the speed increase gained as a result of the addition of processors.

### A System Configuration

- One more fix has to be made--consolidate control and main memory.
- The control memory has the following roles:
  1. Contains logic for queuing state words.
  2. Brief times where it is very busy.
  3. Executive (the kernel routine) should have the facility to allocate modules from the storage pool to the control and main memories as required.
