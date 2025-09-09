# Chapter 10: Multi-processor Scheduling

## .1: Multi-processor Architecture

- There are a hierarchy of **hardware** caches (think L1, L2, etc.) in a single CPU.
- When we have multiple active processes in the systems, we need to deal with the idea of **cache coherency**
  - When a processes issues a write to memory, that invalidates the cache in other CPU's, hence, it needs to be flushed.
  - Or, when we have a **write-back** cache, writing something to cache but not main memory, switching processes, and reading that same portion in memory will yield not up-to-date data.
- On a bus-based system, we do something known as **bus-snooping**, each cache pays attention to memory updates by observing the bus that connects them to main memory.
- Write-backs, however, are more difficult, as these writes aren't immediately available to other processes and **bus-snooping** cannot happen.

## .2: Don't Forget Synchronization

- You know this already.

## .3: One Final Issue: Cache Affinity

- A process, when moving around in a scheduler that has multiple CPUs, typically wants to end up on the same CPU as that is where all the cache is held.

## .4: Single-Queue Scheduling

- We can first try and reuse the scheme from the MLFQ for a single CPU. We have a single-queue scheduling algorithm. The only thing we need to ensure here is that when a CPU is checking for a process, we lock onto to queue. Only when that CPU has found a process it can run will we unlock and allow another CPU to go find a new process.
- The greater problem is that processes will most likely just bounce around CPUs with no regards to cache affinity.
- To alleviate this, there are some affinity mechanisms that are used to make sure or make it more likely that the process runs on the same CPU.
- However, this can be difficult, we need to avoid scheduling conflicts through locking, but also make sure that no jobs get starved. If the first CPU wants to keep running A, we need to make sure that some other CPU gets B.

## .5: Multi-Queue Scheduling

- Our framework consists of multiple scheduling queues. Each queue will follow some discipline, such as round robin.
- When a job enters a system, we use some heuristics to decide which queue we want to put it in.
- We avoid moving jobs around. Once it is in that queue, we no longer move it to the queue of another CPU. Nevertheless, one problem is that some CPUs will finish before others and be left idle. Hence, we need to find a way to **migrate** processes by checking if there is a load imbalance.

## .6: Linux Multiprocessor Schedulers

- Just trivia. See the reference notes for more interesting information.
