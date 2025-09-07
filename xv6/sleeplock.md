# `sleeplock.c`

## function definitions

### `void sleeplock`

- Common sense.

```c
void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}
```

### `void acquiresleep`

- This is a bit tricky, so we need to walk through this one step at a time. We acquire the lock first. Assume that the lock is currently taken, (in sleep lock format). That means that the sleeplock is taken, but, \*\*the `spinlock` inside `sleeplock` is free. Hence, acquire will not fail (this took me so long to figure out).
  - **QUESTION: Why do we need to acquire the lock in the first place? Why not just do away with the spinlock and use the sleeplock `locked` attribute directly?**
  - **ANSWER:** Simple. We don't want to people acquiring sleep at the same time. For example, if the sleeplock is free, we don't go through the `while` loop and immediately assign `pid`. Without the `lock` inside spinlock, we won't achieve that synchronization.
  - In other words, it is built ON TOP of `spinlock`. `spinlock` is the only function that implements the atomic operation, which is necessary for synchronization between multiple cores. `sleeplock` is just extending that functionality to allow sleeping instead of spinning.
- The rest after that step is common sense. We release the lock at the end to say, hey, other people can try and sleep on this. We don't them to spin until they can sleep on a lock. That would be messy at best, redundant and slow at worst.
- See also, that our condition lock is the `spinlock` of the `sleeplock`. **Can you answer why that is?**
- If a process is releasing the `sleeplock`, and we did not acquire the `spinlock` inside, then it is possible that the process releases the lock and issues a wakeup before the process goes to sleep. We are waiting for something that will never happen.
  - Hence, we use the conditional lock. It's either the process releases the lock first, and that acquiring immediately works, or, we go to sleep first, and then the wakeup happens.

```c
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}
```

### `void releasesleep`

- Direct complement to [acquiresleep](#void-acquiresleep).
- **QUESTION: Why do we need to `acquire` here?**
  - **ANSWER:** Locks can only be hold by one process at a time (else it would be a really dumb lock). Nevertheless, I assume it is in a similar vein to avoiding lost wake-ups. For example, one process is about to acquire the `sleeplock`, but the process using it is in the process of releasing it. The process acquiring thinks its locked but has not gone to sleep, the wakeup call is issued by the releasing process. The acquiring process is left waiting for a wakeup call that is never issued.
- At the very end, we, again, release the lock after issuing a wakeup call. This is us saying, hey, people can acquire this lock now.

```c
void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}
```

### `int holdingsleep`

- **QUESTION: Why do we need to acquire the `spinlock` for holding? We are already guaranteed that an acquire will happen after a holding call, so why the need?**
  - Actually, I do believe that's applicable. However, we need the lock to make sure no interrupts happen, we don't want to switch while we are in the middle of seeing whether or not the current CPU holds the lock. If we do, we might be reading the old CPU object get its PID, and think that we don't hold the current sleep-lock when, in actuality, we do!

```c
int
holdingsleep(struct sleeplock *lk)
{
  int r;

  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}
```
