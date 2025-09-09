# CFS Internals

## Translating CFS to Multi-Core Systems

- To load-balance properly, we consider the following variables:
  - **Priority** - We want to run high priority processes more often than low priority processes. Hence, it is not ideal that all cores should have an equal mount.
  - **Weight** - We want processes that are higher in weight to have more time than those lower right, preferably in ratios. For example, the weight of a high-priority thread is equal to nine low-priority threads. But if the high-priority thread, in its own CPU, suddenly goes to sleep, we will need to constantly **work steal**. We don't want this to the common case, as the overhead in addition to it defeating the purpose of per-core CPUs is a detriment to the system.
  - **Load** - To build further upon weight, we balance run-queues based on _load_. It is a combination of the thread weight in addition to average CPU utilization. If a high-priority process constantly relinquishes the CPU, then it needs to have reduced time.
- Another benefit of the load-tracking metric is that it accounts for varying levels of multithreading in different processes.
  - **EXAMPLE:** One process has a lot of threads, and another process with a few threads. The threads of the first process will have far more CPU time than the smaller one, hence, starvation occurs.
  - To fix this, Linux uses **group scheduling**. When a thread belongs to a `cgroup`, its load is further divided by the total number of threads in its `cgroup`.

## The Load-Balancing Algorithm

- A basic algorithm would compare all the load of the cores then transfer tasks from the most loaded to the least loaded core, but this does not account for **cache affinity**. Hence, we need a hierarchical strategy.
  - **Core** - This is at the bottom of each hierarchy.
  - **functional units** - Each pair of core shares a functional unit or FPU
  - **LLC** - 8 CPUs share a single last level cache. This form what we call a **NUMA** node.
  - Numa nodes are then further grouped by level of connectivity. Nodes that are one hop apart from each other will be at the next level, and so on.
- With this hierarchy, what we are essentially doing is going through each level of the hierarchy, with one core of each domain being made responsible for balancing the load.
- Then, the average load is computed for each scheduling group of the scheduling domain, and the busiest group is picked. If the busiest group's load is lower than the local group's load, the load is considered balanced at this level.
