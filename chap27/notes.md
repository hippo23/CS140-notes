# Interlude: Thread API

## .1 Thread Creation

```c
#include <pthread.h>
int
pthread_create(
  pthread_t *thread // the actual thread object,
  const pthread_attr_t *attr // specify the attributes of the thread i.e. stack size,
  void *(*start_routine)(void*) // what function should this thread start running in,
  void *arg // list of arguments that are null terminated i assume?
);
```

```c
typedef struct {
  int a;
  int b;
} myarg_t;

void *mythread(void *arg)
{
  myarg_t *args = (myarg_t *) arg; // simple enough
  // notice how we cast args
  printf("%d %d\n", args->a, args->b); // simple enough
  return NULL; // simple enough
}

int
main(int argc, char *argv[])
{
  pthread_t p; // simple enough
  myarg_t args = { 10, 20 }; // simple enough
  int rc = pthread_create(&p, NULL, mythread, &args); // simple enough
  // we have to recast it back when its received in the function though
}
```

## .2 Thread Completion

### first example

```c
typedef sturct { int a; int b; } myarg_t;
typedef struct { int x; int y; } myret_t;

void
*mythread(void *arg)
{
  // make sure data should be heap allocated, stack allocated data will be deleted
  // at the end of the call frame
  myret_t *rvals = Malloc(sizeof(myret_t));
  rvals-> x = 1;
  rvals->y = 2;
  return (void *) rvals;
}

int
main(int argc, char *argv[])
{
  pthread_t p; // simple enough
  myret_t *rvals; // simple enough
  myarg_t args = { 10, 20 }; // simple enough
  Pthread_create(&p, NULL, mythread, &args); // simple enough
  Pthread_join(p, (void **) &rvals); // why the address of rvals?
                                     // is rvals not already a pointer
  // i believe that it is becuase pthread does not copy a pointer's value
  // it literealy changes the pointer of the data we passed. Hence
  // we pass the location of our pointer (most likely on the stack)
  // and make the pointer that that pointer points to point to the returned value.
  printf("returned %d %d\n", rvals->x, rvals->y);
  free(rvals);
  return 0;
}
```

### second example

```c
void
*mythread(void *arg)
{
  long long int value = (long long int) arg; // only signficant change
  // notice how we have to cast the argument. I assume that if we wanted
  // multiple arguments like char*, it would function like argv too.
  printf("%lld\n", value);
  return (void *) (value + 1);
}

int
main(int argc, char *argv[])
{
  pthread_t p;
  long long int rvalue;
  Pthread_create(&p, NULL, mythread, (void *) 100);
  Pthread_join(p, (void **) &rvalue);
  printf("returned %lld\n", rvalue);
}
```

## .3 Locks

- I imagine this is basically a more sophisticated spinlock?
- And it is a lock for threads only, not between processes.

```c
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
```

- To initialize a POSIX lock, we do the following:

```c
// default manner
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// or we can also do this, for dynamic creation
int rc = pthread_mutex_init(&lock, NULL)
assert(rc == 0);
```

- Make sure to properly handle the case where even acquire fails

```c
void
Pthread_mutex_lock(pthread_mutex_t *mutex)
{
  int rc = pthread_mutex_lock(mutex);
  assert(rc == 0);
}
```

- There are also two other routines besides locking and unlocking

```c
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex, struct timespec *abs_timeout);
```

- `trylock` returns failure if the lock is already held
- `timedlock` returns after a timeout or after acquiring a lock

## .4 Condition Variables

- Useful for one thread waiting for a _signal_ from another thread, for something to finish before it can continue.

```c
// puts the calling thread to sleep, and waits for another thread to signal it
// is this basically just a sleeplock???
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
// this is what we use to actually signal something
int pthread_cond_signal(pthread_cond_t *cond);
```

- An example usage would be this

```c
// thread that goes to sleep
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
Pthread_mutex_lock(&lock);
// notice the loop?
// 1) Ideally, a channel should have a unique attachment
// but if i'm correct, some code in xv6 shares channels between different cases
// so the condition may not always be satisifed
// 2) Additionally, if multiple threads are waiting, and
while (ready == 0)
  Pthread_cond_wait(&cond, &lock);
Pthread_mutex_unlock(&lock);

// code to wake the thread
Pthread_mutex_lock(&lock);
ready = 1;
Pthread_cond_signal(&cond);
Pthread_mutex_unlock(&lock);

// conditionals are global, similar to xv6 (or just accessible somehow, like)
// with a process table, or a specific address with the uart etc etc.
```
