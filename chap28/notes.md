# Chapter 28: Locks

## .4 Evaluating a Lock

- Take note of the three following criterion when building a lock:
  - **Mutual Exclusion**
  - **Fairness**
  - **Performance**
    - No contention, when one thread grabs and releases the lock, what is the overhead?
    - When multiple CPU's are contending for the lock, how does the lock perform?
    - On a single CPU, how does the lock perform?

## .5 Controlling Interrupts

- Disable interrupts only works in the following cases:
  - **Single CPU**
  - We are okay with calling threads to be able to perform privileged instructions.
  - Lost interrupts i.e. missing a read request

## .6 A Failed Attempt: Just Using Loads/Stores

```c
typedef struct lock_t
{ int flag; }
lock_t;

void
init(lock_t *mutex)
{
  // o -> lock is available, 1 -> held
  mutex->flag = 0;
}

void
lock(lock_t *mutex)
{
  while (mutex->flag == 1)
    ;
  mutex->flag = 1;
}

void
unlock(lock_t *mutex)
{
  mutex->flag = 0;
}
```

- Two issues with the code above:
  - **Correctness** -> Two or more threads can both think that the `flag` is zero, and hence, they will both acquire the lock.
  - **Performance** -> Processes trying to wait are stuck spinning; the CPU can't do anything else.

## .7 Building Working Spin Locks with Test-And-Set

```c
int
TestAndSet(int *old_ptr, int new)
{
  int old = *old_ptr; // fetch old value at old_ptr
  *old_ptr = new; // store `new` into old_ptr
  return old; // return the old value
}

typedef struct __lock_
{ int flag; }
_lock_t;

void
init(lock_t *lock)
{
  // 0: lock is available, 1: lock is held
  lock->flag = 0;
}

void
lock(lock_t *lock)
{
  while (TestAndSet(&lock->flag, 1) ==1)
    ;
}

void
unlock(lock_t *lock)
{
  lock->flag = 0;
}
```

## .9 Compare-And-Swap

- The basic idea is for compare-and-swap to test whether or not the value at the address specified by the `ptr` is equal to `expected`--if so, it updates the memory location pointed to by `ptr` with the new value. If not, it does nothing. In both cases, we return the original value.

```c
void
lock(lock_t *lock)
{
  while (CompareAndSwap(&lock->flag, 0, 1))
    ;
}
```

## .10 Load-Linked and Store-Conditional

```c
int
LoadLinked(int *ptr)
{
  return *ptr;
}

int
StoreConditional(int *ptr, int value)
{
  if (no update to *ptr since LL to this addr)
  {
    *ptr = value;
    return 1;
  }
  else
  {
    return 0;
  }
}

void
lock(lock_t *lock)
{
  while (1)
  {
    while (LoadLinked(&lock->flag) == 1)
      ;
    if (StoreConditional(&lock->flag, 1) == 1)
      return;
  }
}

// alternatively, we could also do this
void
lock_short(lock_t *lock)
{
  while (LoadLinked(&lock->flag)) ||
      !StoreConditional(&lock->flag, 1))
    ;
}

void
unlock(lock_t *lock)
{
  lock->flag = 0;
}
```

## .11 Fetch-And-Add

```c
int
FetchAndAdd(int *ptr)
{
  int old = *ptr;
  *ptr = old + 1;
  return old;
}

typedef struct __lock_t
{
  int ticket;
  int turn;
}
lock_t;

void
lock_init(lock_t *lock)
{
  lock->ticket = 0;
  lock->turn = 0;
}

void
lock(lock_t *lock)
{
  int myturn = FetchAndAdd(&lock->ticket);
  while (lock->turn != myturn)
    ;
}

void
unlock(lock_t *lock)
{
  lock->turn = lock->turn + 1;
}
```

## .13 A Simple Approach: Just Yield, Baby

```c
void
init()
{
  flag = 0;
}

void
lock()
{
  while (TestAndSet(&flag, 1) == 1)
    yield();
}

void
unlock()
{
  flag = 0;
}
```

- Problems
  - **Performance** -> Instead of having to wait 99 quanta, it's just 99 context switches and `yield()` calls.
  - **Starvation** -> If all are waiting, some process will always be last and will never get to run until the last minute.

## .14 Using Queues: Sleeping Instead of Spinning

```c
typedef struct __lock_t
{
  int flag;
  int guard;
  queue_t *q;
}
lock_t;

void
lock_init(lock_t *m)
{
  m->flag = 0;
  m->guard = 0;
  queue_init(m->q);
}

void
lock(lock_t *m)
{
  // similar to the sleeplock in
  // xv6, the guard exists so that only
  // one process goes to sleep at a time.
  // WHY?????
  // to allow sychronized access to the sleeping queue

  while (TestAndSet(&m->guard, 1) == 1)
    ;
  if (m->flag == 0)
  // if the lock is available, we take it
  {
    m->flag = 1;
    m->guard = 0;
  }
  else
  // otherwise, add oursevles to the queue
  // you can treat guard the same as a p->lock.
  // We need to hold guard to change the state of the
  // sleeploc, otherwise, a process could issue a wakeup
  // while another process is about to sleep. Lost wakeup.
  {
    queue_add(m->q, gettid());
    // use this to avoid the race condition,
    // I assume there is some modifier or bit
    // to say whether or not setpark has been called
    // in this timeframe
    setpark();
    m->guard = 0;
    // this just means to go to sleep
    // you may see this as a race condition (because interrupts
    // are not disabled)
    park();
  }
}

void
unlock(lock_t *m)
{
  // why do we need an atomic operation here?
  // one process could be about to be added to the queue
  // for example, if the queue was empty otherwise,
  // the flag would be zero but there would then be
  // a process in the queue. This process would never, ever
  // wake up
  while (TestAndSet(&m->guard, 1) == 1)
    ;
  if (queue_empty(m->q))
    m->flag = 0;
  else
    // flag is not set to zero when another thread is woken up (because we asume)
    // that thread will continue executing
    unpark(queue_remove(m->q));

  m->guard = 0;
}
```

## .15 Different OS, Different Support

```c
void
mutex_lock(int *mutex)
{
  int v;
  // if it is zero, it is free, hence
  // just return
  if (atomic_bit_test_set (mutex, 31) == 0)
    return;
  // otherwise, add this two the wait queue
  atomic_increment (mutex);
  // now we keep spinning
  while (1)
  {
    // if the lock is free now, then take it (remember, this should)
    // be atomic
    if (atomic_bit_test_set (mutex, 31) == 0)
    {
      atomic_decrement (mutex);
      return;
    }
    // otherwise, get the current value of the mutex
    v = *mutex;
    // if v is greater than zero (meaning that the lock is free now),
    // we can try again to acquire it
    if (v >= 0)
      continue;

    // if it is negative, then it is still held, and we say that so long as the value
    // of the mutex lock does not change, we just sleep.
    // why is that? because if we go to sleep while the lock is not taken
    // then we will never get woken up, because that just means
    // while changing V, some process released the lock and possibly
    // another lock acquired it, the sleeping process won't sleep properly
    // once that value becomes negative.
    futex_wait (mutex, v);
  }
}

void
mutex_unlock(int *mutex)
{
  // this reuslts in 0 iff there are no other interested threads.
  if (atomic_add_zero (mutex, 0x80000000))
    return;

  // otherwise, we need to wake up threads
  futex_wake (mutex);
}
```
