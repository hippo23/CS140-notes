# Chapter 31: Semaphores

## 1 Semaphores: A Definition

```c
#include <semaphore.h>
sem_t s;
// 0 in the second argument indicates that the semaphore will
// be shared by threads in the same process
sem_init(&s, 0, 1);
```

```c
int sem_wait(sem_t *s)
{
  // decrement the value of semaphore s by one
  // wait if value of semaphore s is negative
}

int sem_post(sem_t *s)
{
  // increment the value of semaphore s by one
  // if there are one or more threads waiting, wake one
}
```

- I assume that, relating this to the barrier, we only let processes through if the semaphore value has finally breached negative, otherwise, we have to block them.
- Additionally, once we start letting things through, then the very first one should set the semaphore value back to? Joke, I don't think that works, let's see later on.

```c
sem_t m;
sem_init(&m, 0, X);
sem_wait(&m);
sem_post(&m);
```

## 2 Binary Semaphores

- Common sense.

## 3 Semaphores For Ordering

```c
sem_t s;
void *child(void *args)
{
  printf("child\n");
  sem_post(&s);
  return NULL;
}

int main(int argc, char *argv[])
{
  sem_init(&s, 0, X);
  printf("parent: begin\n");
  Pthread_create(&c, NULL, child, NULL);
  sem_wait(&s);
  printf("parent: end\n");
  return 0;
}
```

## 4 The Producer / Consumer (Bounded Buffer) Problem

```c
// first attempt
// put and get routines
int buffer[MAX];
int fill = 0;
int use = 0;

void put(int value)
{
  // just your regular buffer, although I assume
  // that the lock should be hold here
  buffer[fill] = buffer[use];
  fill = (fill + 1) % MAX;
}

int get()
{
  int tmp = buffer[use];
  use = (use + 1) % MAX;
  return tmp;
}

// adding the full and empty conditions
sem_t empty;
sem_t full;

void *producer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    // semaphore for waiting should be set to the buffer space
    sem_wait(&empty);
    put(i);
    // semaphore for full should be set to zero.
    sem_post(&full);
  }
}

void *consumer(void *arg)
{
  int tmp = 0;
  while (tmp != -1)
  {
    // if the semaphore for full is negative, there is nothing
    // in the buffer, so we have to wait
    sem_wait(&full);
    tmp = get();
    // post that there is now extra space in the buffer for something else
    sem_post(&empty);
    printf("&d\n", tmp);
  }
}

// this works, only problem is that mutual exclusion is not maintained

int main()
{
  ...
  sem_init(&empty, 0, MAX);
  sem_init(&full, 0, 0);
  ...
}
```

```c
// to add mutual exclusion, we do the following
void *producer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    // we add one more semaphor
    // that simply implements locks
    sem_wait(&empty);
    sem_wait(&mutex);
    put(i);
    sem_post(&mutex);
    sem_post(&full);
  }
}

void *consumer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    sem_wait(&mutex);
    sem_wait(&full);
    int tmp = get();
    sem_post(&empty);
    sem_post(&mutex);
    printf("%d\n", tmp);
  }
}

// in what case here will deadlock occur?
// ANSWER: if there is no available item
// then consumer will sleep while holding the lock
// which the producer needs to add stuff

// in the same manner, if there is no space, producer will
// sleep with the lock
```

```c
void *producer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    sem_wait(&empty);
    sem_wait(&mutex);
    put(i);
    sem_post(&mutex);
    sem_post(&full);
  }
}

void *consumer(void *arg)
{
  int i;
  for (i = 0; i < loops; i++)
  {
    // we just exchange the ordering to fix the problem
    sem_wait(&full);
    sem_wait(&mutex);
    int tmp = get();
    sem_post(&mutex);
    sem_post(&empty);
    printf("%d\n", tmp);
  }
}
```

## 5 Reader-Writer Locks

## 6 The Dining Philosophies

## 7 Thread Throttling

## 8 How to Implement Semaphores

## 9 Summary
