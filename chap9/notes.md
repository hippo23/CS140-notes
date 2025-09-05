# Chapter 9: Scheduling a Proportional Share

## Basic Concept: Tickets Represent Your Share

- The amount of tickets a certain job represents the chance it will receive CPU time. By generating a random number, if it falls within the range of the task, then that task gets time. As more tasks are executed, this will tend towards a steady percentage.

## Ticket Mechanisms

- **Ticket Currency** -> Allows a user with a set of tickets to allocate tickets among their own jobs in whatever currency they would like. This is just used to create more minute percentages without the concept of 1/2 being stored.
  - i.e. A and B have 100 tickets. A1 and A2 each get 500 tickets in A's currency. B is running only one job and gets 10/10 tickets.
- **Ticket Transfer** -> A process can temporarily hand off it tickets to another process. This ability is especially useful in a client/server setting.
  - To speed up work, the client passes tickets to the server to help maximize its performance.
- **Ticket Inflation**
  - A process can temporarily raise or lower the number of tickets that it owns.
    - This applies in an environment where a group of processes trust one another. If one process needs more CPU time, it can boost its ticket value without communicating with any other process (differentiating it from ticket transfer, which I assume could have more overhead).

## Implementation

```c
// used to track if we have found the winner
int counter = 0;
// the winner's ticket number
int winner = getrandom(0, totaltickets);
// current: use this to walk through the list of jobs
node_t *current = head;
while (current) {
  counter = counter + current->tickets;
  if (counter > winner)
    break;
  current = current->next;
}
```

## An Example

- To measure the effectiveness of a lottery scheduler, we have the **Fairness Metric**
  - The fairness metric is just the time_first_job / time_second_job. The closer to one, the more fair the scheduler is.

## How To Assign Tickets

- Can usually be done manually, thinking that the user knows best what needs the most and least (and in between) amount of tickets.

## Stride Scheduling

- An alternative to **random scheduling** is called stride scheduling, a deterministic fair-share scheduler. Each job in the system is stride, which is inverse in proportion to the number of tickets it has. The more tickets, the shorter the stride. Each time a process runs, we increment a counter for it(called its **pass** value) by its stride to track its global progress.
- At any given time, pick the process to run that has the lowest pass value so far.

```c
curr = remove_min(queue);
schedule(curr);
curr->pass += curr->stride;
insert(queue, curr);
```

- The main reason you would choose to use a **lottery scheduler** instead of a stride one is the lack of global state. A new job entering into the scheduler would mean that it would run for a very, very, very long time.

## The Linux Completely Fair Scheduler (CFS)

- The current Linux approach achieves all of this in an alternate manner. Known as the **Completely Fair Scheduler**, it implements fair-sharing in an efficient manner.

### Basic Operation

- To fairly divide the CPU among all competing processes, it does so through a counting-based technique known as **virtual runtime**.
- Each time a process runs, it acquires `vruntime`. When a scheduling decision occurs, CFS will pick the process with the lowest `vruntime` possible.
- In order to know when to stop the process and run the next one, we have various parameters. The sooner it stops a process, the more fair it is, but more overhead is incurred as well.
- Here are some of the following parameters:
  - **sched_latency** -> Used to determine how long one process should run before considering a switch (but it is not the exact value)
    - For example, if we have four processes, then a **sched_latency** of 48ms will mean each process will get 12ms.
  - **min_granularity** -> To avoid the time slice becoming too small in the case of too many processes running,
- Note that CFS uses a periodic timer interrupt, which means that it can only make decisions at fixed time intervals. If a job has a time slice that is not a perfect multiple of the interrupt, it's okay since `vruntime` is still precise.

### Weighting (Niceness)

- To handle the concept of **priority**, CFS does not use tickets but instead with the **nice** level of a process. Similar to the formulas seen in the previous chapter, this is used in increasing or decreasing level of priority. In the case of CFS, it maps each process's nice value to a `weight`.

- Using the weight, (wherein negative weights mean higher priority), we can calculate the following:
  - time_slice = weight_k / summation_0_n-1 (weight_i) \* sched_latency.
  - Processes with higher weights will gain a larger percentage of the sched_latency, processes with a lower right, will gain a lower share.
- The way in which we calculate `vruntime` must also be adjusted. We take the actual `runtime_i` accrued and scales it inversely by the weight of the process, by dividing the default weight of the process by its weight.
  - `vruntime` = `vruntime` + `weight_0` / `weight_i` \* `runtime_i`

### Using Red-Black Trees

### Dealing With I/O and Sleeping Processes

### Other CFS Fun
