# Chapter 30: Condition Variables

```c
void
*child
(void *arg)
{
  printf("child\n");
  return NULL;
}

int
main
(int argc, char *argv[])
{
  printf("parent: begin\n");
  pthread_t c;
  Pthread_create(&c, NULL, child, NULL);
  printf("parent: end\n");
  return 0;
}
```

- Ideally, we should see the following output from the code above:

```txt
parent: begin
child
parent: end
```

- We would use `pthread_join`, but how exactly does one implement a function like this?
- We cold opt to use a global variable, as seen below:

```c
volatile int done = 0;

void
*child(void *arg)
{
  printf("child\n");
  done = 1;
  return NULL;
}

int
main
(int argc, char *argv[])
{
  printf("parent: begin\n");
  pthread_t c;
  Pthread_create(&c, NULL, child, NULL); // child
  while (done == 0)
    ;
  printf("parent: end\n");
  return 0;
}
```

- The problem with this is that it is inefficient and wastes CPU time, so what else can we do to put the parent to sleep until the condition we are waiting for comes true?

## 1 Definition and Routines

- To wait for a condition to become true, we can wait for what is known as a **condition variable**
  - This is an explicit queue that threads can put themselves on when some state of execution is not as desired.
- To declare such a thing, we need to just write a `pthread_cond_t c`, which declares `c` as a condition variable.
- There are two operations associated with a condition variable:
  - `wait()` -> executed when a thread wishes to put itself to sleep
  - `signal()` -> executed when a thread has changed something in the program and thus wants to wake a sleeping thread waiting on this condition.

  ```c
  pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
  pthread_cond_signal(pthread_cond_t *c);
  ```

- To understand this code, let's take a look at the solution to the join problem.

```c
int done = 0;
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c = PTHREAD_COND_INITIALIZER;

void thr_exit()
{
  // lock
  Pthread_mutex_lock(&m);
  // set done
  done = 1;
  // signal to sleeping threads
  Pthread_cond_signal(&c);
  // and unlock
  Pthread_mutex_unlock(&m);
}

void *child(void *arg)
{
  // the child prints
  printf("child\n");
  thr_exit();
  return NULL;
}

void thr_join()
{
  // acquire the lock (used to serialize wakeups)
  Pthread_mutex_lock(&m);
  // if done has not yet been set
  while (done == 0)
    // go to sleep and wait for hte process
    Pthread_cond_wait(&c, &m);
  // when done has been set, we are done and we unlock (we acquired the lock
  // when we woke up (why do we need to acquire the lock again when we wakeup?)
  // it just assumes we are in some kind of critical section, does the work for us
  // because a lot of the times we will need the lock
  Pthread_mutex_unlock(&m);
}

int main(int argc, char *argv[])
{
  // parent begins executing
  printf("parent: begin\n");
  // we create a trhead object
  pthread_t p;
  // properly initialize it or start the thread, I mean,
  Pthread_create(&p, NULL, child, NULL);
  // the parent calls join
  thr_join();
  prinf("parent: end\n");
  return 0;
}
```

```c
// this doesn't work, main process could just sleep forever
void thr_exit()
{
  Pthread_mutex_lock(&m);
  Pthread_cond_signal(&m);
  Pthread_mutex_unlock(&m);
}

void thr_join()
{
  Pthread_mutex_lock(&m);
  Pthread_cond_wait(&c, &m);
  Pthread_mutex_unlock(&m);
}

```

```c
// if the child process is in the middle of setting done and executes
// right as main thread reads done == 0 as true, then main thread will sleep
// forever
void thr_exit()
{
  done = 1;
  Pthread_cond_signal(&c);
}

void thr_join()
{
  // even while could not solve this, because, again, the wakeup
  // signal has been issued before the parent sleeps
  if (done == 0)
    Pthread_cond_wait(&c);
}
```

## 2 The Producer / Consumer (Bounded Buffer) Problem

```c
int buffer;
int count = 0;

void put(int value)
{
  assert(count == 0);
  count = 1;
  buffer = value;
}

int get()
{
  assert(count == 1);
  count = 0;
  return buffer;
}
```

```c
void *producer(void *arg)
{
  while (1)
  {
    int tmp = get();
    printf("%d\n", tmp);
  }
}

void *consumer(void *arg)
{
  while (1)
  {
    int tmp = get();
    printf("%d\n", tmp);
  }
}
```

```c
int loops;
cond_t cond;
mutex_t mutex;

void *producer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    Pthread_mutex_lock(&mutex);
    if (count == 1)
      Pthread_cond_wait(&cond, &mutex);
    put(i);
    Pthread_cond_signal(&cond);
    Pthread_mutex_unlock(&mutex);
  }
}

void *consumer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    Pthread_mutex_lock(&mutex);
    if (count == 0)
      Pthread_cond_wait(&cond, &mutex);
    int tmp = get();
    Pthread_cond_signal(&cond);
    Pthread_mutex_unlock(&mutex);
    printf("%d\n", tmp);
  }
}
```

```c
int loops;
cond_t cond;
mutex_t mutex;

void *producer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    Pthread_mutex_lock(&mutex);
    while (count == 1)
      Pthread_cond_wait(&cond, &mutex);
    put(i);
    Pthread_cond_signal(&cond);
    Pthread_mutex_unlock(&mutex);
  }
}

void *consumer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    Pthread_mutex_lock(&mutex);
    while (count == 0)
      Pthread_cond_wait(&cond, &mutex);
    int tmp = get();
    Pthread_cond_signal(&cond);
    Pthread_mutex_unlock(&mutex);
    printf("%d\n", tmp);
  }
}
```

```c
int buffer[MAX];
in fill_ptr = 0;
int use_ptr = 0;
int count = 0;

void put(int value)
{
  buffer[fill_ptr] = value;
  fill_ptr = (fill_ptr + 1) % MAX;
  count++;
}

int get()
{
  int tmp = buffer[use_ptr];
  use_ptr = (use_ptr + 1) & MAX;
  count --;
  return tmp;
}
```

```c
void *producer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    Pthread_mutex_lock(&mutex);
    while (count == MAX)
      Pthread_cond_wait(&empty, &mutex);
    put(i);
    // same thing here
    Pthread_cond_signal(&fill);
    Pthread_mutex_unlock(&mutex);
    // another error that we tried to prevent
    // is that one consumer steals the data
    // from another consumer. This is prevent by using
    // the while loop
  }
}

void *consumer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    Pthread_mutex_lock(&mutex);
    while (count == 0)
      Pthread_cond_wait(&fill, &mutex);
    int tmp = get();
    // use this so that the consumer only wakes up producers
    Pthread_cond_signal(&empty);
    Pthread_mutex_unlock(&mutex);
    printf("%d\n", tmp);
  }
}
```

## 3 Covering Conditions

```c
int bytesLeft = MAX_HEAP_SIZE;
cond_t c;
mutex_t m;

void *
allocate(int size)
{
// allocating space on the heap
  Pthread_mutex_lock(&m);
  // if bytes left is not enough to fit, we sleep
  while (bytesLeft < size)
    Pthread_cond_wait(&c, &m);
  // once we are woken up, we reacquire the lock and check
  // the condition
  void *ptr = ...; // get the memory from the heap
  // subtract the bytes left appropriately
  bytesLeft -= size;
  // release the lock
  Pthread_mutex_unlock(&m);
  return ptr;
}

void free(void *ptr, int size)
{
  Pthread_mutex_lock(&m);
  bytesLeft += size;
  Pthread_cond_signal(&c);
  Pthread_mutex_unlock(&m);
}

// the main problem is that the free'r doesn't actually know
// which allocating process to wake up. Unless size is always fixed
// this could vary, right?

// this can be solved by waking up all processes and using mesa
// semantics to guarantee correctness, at the expense of performance
```
