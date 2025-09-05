# Chapter 8: Multi-Level Feedback Queue

## .1: MLFQ: Basic Rules

- Multiple queues have differing priority levels, with the scheduler using said priority to decide which job to run first.
- For jobs that are on the same queue and are ready to run, **RR** scheduling will be used.

```sh
  1. If Priority(A) > Priority(B), A runs (B doesn't).
  2. If Priority(A) = Priority(B), A & B run in RR.
```

- **MLFQ varies the priority of queues based on their perceived or observed behaviour**.
  - For example, voluntarily relinquishing hold of CPU to wait for another process or an I/O input means that the priority will remain high.
  - If a process uses a CPU intensively for long periods of time, then the job won't be high priority.
- Of course, static priority would mean that low priority jobs would basically never run. This is where _dynamic priorities_ comes into play.

## .2: How To Change Priority

- Use the concept of **allotment**, wherein there is a maximum amount of time that a job can spend at a certain priority level.

```sh
  3. When a job enters the system, it is placed at the highest level priority.
  4. See (4.1) and (4.2) below:
    4(a). If a job uses up its allotment while running, its priority is **reduced**.
    4(b). If a job gives up the CPU (for example, by performing an I/O operation) before the allotment is up, it stays at the same priority level i.e. its allotment is also reset.
```

- This is good already and is able to roughly model **SJF**, however, there are some problems.

1. **Starvation** -> Again, too many high priority jobs could still mean that low priority jobs will never run.
2. **Game the scheduler** -> If a malicious program so wanted to, they could make it so that the program asks for an I/O each time it was about to use up allotment time. That way, it never reliquishes its high priority.
3. **Change in behaviour** -> A CPU-intensive process may suddenly need I/O, but at that point, it is already low priority. What to do in that case?

## .3: The Priority Boost

```sh
  5. After some time period S, move all the jobs in the system to the topmost queue.
```

- This new rule solves the issue of **starvation** as well as the issue of changes in behaviour.
- **What time period should _S_** be set to? These are often called **voo-doo constants**, being that they are very hard to get correctly. In this case, the length of **_S_** is typically left to the system administrator or to machine learning.

## .4: Better Accounting

- Instead of allowing the CPU to retain its time, we simply do not reset allotment periods. This can be done because of the new reset that we introduced in rule (5).

```
  4. Once a job uses up its alloatment at a given level (regardless of how many times it has given up the CPU) its priority is reduced (i.e., it moves down one queue).
```

# Additional Readings

## An analysis of decay-usage scheduling in multiprocessors

- The main idea is that we decrease the priority of a job when it acquires CPU time, and increase its priority when it is unable to use the CPU.
- The priority consists of the following components:

```sh
  1. A base priority for each job.
  2. A time-dependent part based on processor usage.
```

- The abstract notes that it would be impossible to calculate a mean response time. **_Why is that?_**
- We aim to analyse the effect of varying the base priorities, values of the parameters of the schedulers, and their **steady-state shares**. **_Will there always be a steady-state for all CPU schedulers given a constant set of jobs?_**

### Introduction

- The time dependent component of priority is the _accumulated and decayed CPU usage_.
  - This increases in a linear-proportional fashion with CPU time obtained.
  - At the end of every **decay cycle**, it is divided by a _decay factor_ in order to account for change in program behaviour.

### The Model

#### Scheduling in UNIX Systems

- Distributions of UNIX (at the time) had schedulers follow a similar pattern, that is:

```sh
  1. Round-robin scheduling
  2. Multi-level priority queuing
  3. Priority ageing
```

- Time is tracked via ticks of the CPU clock, which were 10ms.
- Each job / process in UNIX had a job descriptor, which had the following fields
  - `p_nice` -> The nice-value, which has a range 0 through 19. These are used as hints of the CPU to say that, if the nice value is higher, priority can be lower as it is not urgent.
  - `p_cpu` -> The _accumulated and decayed CPU usage_, which is incremented by 1 for every clock tick received. This is periodically decrement (_priority deaging_).
  - `p_usrpri` -> The actual priority of a job, which depends on the previous two fields. High value corresponds to low precedence.

- The priority of a process is equal to:

```python
p_userpri := PUSER + R * p_cpu + lambda * p_nice
```

- Here, _PUSER_ is a constant separating kernel-mode priorities from user-mode priorities.
- Then, for each **_decay cycle_**, the following recomputation is performed for every process.

```python
p_cpu := p_cpu/D + (lambda) * p_nice

# D > 1 is the decay factor
# lambda >= 0
```

- The priority of the job will then be recalculated using this new value.
- To give more insight, we look at the exact values for **Mach** and **UNIX**

```python
# 4.3BSD
## load is equal to the number of jobs in the run queue divided by the number of
## processors as sampled by the system.
R = 0.25
D = (2 * load + 1) / (2 * load)
lambda = 1
gamma = 2

# Mach
PUSER = 12
## R' is the increment of the priority due to one second of received CPU time when l=1
## l, the load factor, defined by:
## The point of the load factor is such that priorities are in the same range
## regardless of the number of processes and pro-cessors
## T is the number of clock ticks per second.
R = lR'/T
### N is the number of processes, and P is the number of processors
l = max(1, N/P)

```

#### The Decay-Usage Scheduling Model

- To read further

## Principled Schedulability Analysis for Distributed Storage Systems using Thread Architecture Models

## Multilevel Feedback Queue Schedulers in Solaris

### Kinds of Scheduling in Solaris

- Scheduling is performed at two levels:
  - **_Class Independent Routines_** -> Those that are patching and preempting processes.
  - **_Class-dependent routines_** -> Routines responsible for setting the priority of each of its classes.
- For solaris in particular, there are three kinds of scheduling classes:

```sh
  1. Real-time -> Priorities range from 100-159
  2. System -> Priorities range from 60-99
  3. Time-sharing -> Priorities range from 0-59
```

### Class-independent functionality

- These are what arbitrate across the scheduling classes. They have the following scheduling responsibilities:
  - **_The process with the highest priority must be dispatched, and the state of the pre-empted process saved._**
  - **_The class-independent functions must notify the class-dependent routines when the state of its processes changes i.e. creation and termination, if a process is blocked, or runnable to blocked, or when a time quantum expires_**.
  - **_Processes must be moved between priority queues in the class-independent data structures, and must be moved between blocked and ready queues._**

### Time-sharing scheduling class

#### Dispatch Table Implementation

- For each job in the Time-schedular class, the following data structure is maintained

```c
typedef struct tsproc {
  long ts_timeleft; // time remaining in quantum
  long ts_dispwait; // number of seconds since start of quantum (not reset upon preemption)
  pri_t ts_pri; // priority (0 - 59)
  kthread_t *ts_tp; // pointer to thread
  struct tsproc *ts_next; // link to next tsproc on list
  struct tsproc *ts_prev; // link to previous tsproc
} tsproc_t;
```

- The TS class has the following routines:
  - **ts_enterclass(thread T<pointer>)** -> called when a new thread is added to the TS class. It initializes a `tsproc_t` structure for the process.
  - **ts_exitclass(thread T)**
  - **ts_tick(thread T)** -> Called once every 10ms with a pointer to the currently running thread. The `ts_timeleft` variable is decremented by one. If it reaches zero, it goes to an assigned lower-level based on its current level.
  - **ts_update()** -> Called once a second to check the starvation qualities of each process. Increases waiting time by one and checks if we need to push any to a higher priority.
