# Chapter 6: Direct Execution

## .1: Limited Direct Execution

| OS                            | Program                        |
| ----------------------------- | ------------------------------ |
| Create entry for process list |                                |
| Allocate Memory for Program   |                                |
| Load program into memory      |                                |
| Set up stack with argc/argv   |                                |
| Clear registers               |                                |
| Execute **call** `main()`     |                                |
|                               | Run `main()`                   |
|                               | Execute _return_ from `main()` |
| Free memory of process        |                                |
| Remove from process list      |                                |

- Two concerns arise from this approach:
  1. How can we make sure that there is program protection?
  2. How does the OS stop it from running and switch to another process as to implement time sharing.

## .2: Problem 1: Restricted Operations

- We introduce the various level of privileges across the kernel, notably, **user mode** and **kernel mode**.
- The rest is common sense.

## .3: Problem 2: Switching Between Processes

### A Cooperative Approach: Wait for System Calls

- Early operating systems use this approach by implementing a `yield()` command wherein the application voluntarily returns control to the kernel.
- Control is also returned in the case of exceptions such as an invalid memory address, divide-by-zero, etc. through a trap generated for the OS to catch the error and terminate the process.

### A Non-Cooperative Approach: The OS Takes Control

- A **timer interrupt** is used, wherein every few ms control is returned back to the kernel should it need to terminate the program.
- Additionally, it is the responsibility of the hardware to make sure that the information or the state word of the program remains should the kernel choose to continue execution of the process.

### Saving and Restoring Context

- This is made by the part of the system known as the **scheduler**.
- When a CPU switches to a different process, this is known as a **context switch**.

## .4: Worried About Concurrency?

## .5: Summary

# Additional Readings

## The Manchester Mark I and Atlas: A Historical Perspective

- The Mark I and Atlas presented innovations in the following categories:
  1. Instruction format
  2. Operand-address generation
  3. Store management
  4. Sympathy with high-level language usage

- Pioneered the creation of _virtual memory_ by using page-address lines (or registers) that stored the actual physical address in memory of data. If no data was found in these page-address lines (of which there were 32), the computer would perform a full-scan of the mapping of page to memory (known as the _page table_) to find the data and if it is in memory.
- The usage of paging was also novel, and allowed being able to separate programs into chunks that belonged to certain programs (usually via lock-out bits).

## `lmbench`: Portable Tools for Performance Analysis

### Some Important Commands

1. `bcopy()` -> Copies memory from one location to another. Typically uses memory bandwidth from twice to thrice the size of what it is transferring, once for reading, twice for reading what will be overwritten, thrice for writing the new data. Note that this operates strictly within process address space.
2. `read()` -> Attempts to read up to a user-defined amount of bytes from a file descriptor into the process buffer starting at another user-defined variable. Can operate anywhere in memory, and is not restricted solely to the process address space.
3. `mmap()` -> Creates a new mapping in the virtual address space of calling process.

### Important Terms Mentioned

- **Memory-mapping** and memory-mapped files / I/O -> When memory mapping files, where are incorporating file data into the process address space. When more than one process maps the same file, its contents are shared among them, providing a low-overhead mechanism by which processes can synchronize and communicate.
  - Under this part, we create mapped-memory regions. In this parts of storage, there is no lock or access control. Thus, there must be some sort of signal or semaphore control method to prevent access conflicts.
  - The key concept here is that processes do not IMMEDIATELY have access to the entirety of memory like some kind of table. We access files by mapping the process-address space to physical memory. In that manner, we are able to access the file on demand.
  - This VS `read()`
    - `read()` tries to retrieve a fixed number of bytes. On the other hand, using `mmap()` means that the OS is free to decide when to load blocks of memory and how many. This is good if you are accessing a varying size of data. If it is fixed, it is faster to just use read and load it all in one big read.

### Memory-Read Latency

- One of the most useful benchmarks, most other measurements can be defined in terms of memory-read latency.
- For example, **context-switching** latency can be measured based on how long it takes to save the state of one process and load the state of another process.

#### Variations of Memory-Read Latency

- Memory chip cycle latency
  - The way the hardware is built i.e. DRAM architecture
- Pin-to-pin latency
  - Time needed for memory requests to travel from the processor's pins to the memory subsystem and back again.
- Load-in-a-vacuum latency
  - The time that the processor will wait for one load that must be fetched from main memory i.e. cache miss.
  - "Vacuum" means that there is no other activity on the system bus, including no other loads.
- Back-to-back-load latency
  - The time that each load takes, assuming that the instructions before and after are also cache-missing loads. These can take longer than loads in a vacuum due to the following policy: \*critical word first\*\*
  - This means that each sub-block of the cache line that contains the word being loaded is delivered to the processor before the entire cache line has been brought into the cache. This just means that the word reaches the processor before the cache, meaning that loading the next word into cache could take some time.

#### Operating System Entry

- Measurement of how expensive it is to perform a nontrivial entry into the operating system.

#### Signal-handing Cost

- This is how we tell a process what way they should handle an event. They are to processes as interrupts are to the CPU.
