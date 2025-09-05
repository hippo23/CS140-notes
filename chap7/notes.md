# Chapter 7: CPU Scheduling

- These are the policies that the OS follows to decide which processes to run before others, and which interrupts to handle and which to ignore.

## .1: Workload Assumptions

We make the following assumptions about the processes, sometimes called **jobs**.

1. Each job runs for the same amount of time.
2. All jobs arrive at the same time.
3. Once started, each job runs to completion.
4. All jobs only use the CPU (i.e., they perform no I/O).
5. The run-time of each job is known.

## .2: Scheduling Metrics

- The **scheduling metric** is what we use to compare different scheduling policies.
- In this case, we use the metric of **turnaround time**.
  - This is defined as the time at which the job completes minus the time at which the job arrived in the system.
- Beyond **turnaround time**, which is a performance-focused metric, we also note the metric of **fairness**.
  - The time may be completely optimised, but at the expense of some jobs taking the longest to run.

## .3: First In, First Out (FIFO)

- Turnaround time here is just the sum of each job's completion time / the total amount of jobs.
- Relaxing one of the assumptions, let us say that each job no longer runs for the same amount of time.

## .4: Shortest Job First (SJF)

- We put the job with the shortest time needed first. In this way, short jobs won't be bogged down by long jobs.
- This is fine if all jobs arrive at the same time, but the same cannot be said if jobs are arriving dynamically or at random.

## .5: Shortest Time-to-Completion First (STCF)

- In this case, we relax assumption three that all jobs must run to completion.
- We make the scheduler **pre-emptive**, such that it has the ability to drop one process and start another should it please.

## .6: Response Time

- The previous policy is great. However, it breaks down when it comes to users interacting directly with the computer through a terminal.
- We introduce the metric of **response time**, or the time it takes from when the job entered to the first time it is scheduled.
  - Basically how long it takes for each job to generate some kind of response to the user.

## .7: Round Robin

- Instead of running jobs to completion, we run jobs in slices (sometimes known as a **scheduling quantum**).
- This is great for fairness and response time, but the time to completion for all of the tasks will heavily increase.

# Additional Readings

## The effect of context switches on cache performance
