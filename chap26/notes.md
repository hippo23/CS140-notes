# Concurrency: An Introduction

- A **thread** is like a separate process, but they share the same address space and thus can access the same address space.
  - _More than one point of execution._
- We need a **Thread Control Block (TCB)** similar to process control blocks when context switching between processes.
- Instead of having a single stack in the address space, we opt to have multiple stacks per thread.

## .2 Thread Creation

```c
void *mythread(void *arg) {
  printf("%s\n",(char *) arg);
  return NULL;
}

int
main (int argc, char *argv[])
{
  pthread_t p1, p2;
  int rc;
  printf("main: begin\n");
  Pthread_create(&p1, NULL, mythread, "A");
  Pthread_create(&p2, NULL, mythread, "B");
  // join waits for the threads to finish
  Pthread_join(p1, NULL);
  Pthread_join(p2, NULL);
  printf("main: end\n");
  return 0;
}
```

## .3 Difficulties in Data Sharing

- The code block we are studying will be as follows:

```c
static volatile int counter;

void
*mythread(void *arg)
{
  printf("%s: begin\n", (char *) arg);
  int i;
  for (i = 0; i < 1e7; i++)
  {
    counter = counter + 1;
  }
  printf("%s: done\n", (char *)arg);
  return NULL;
}

int
main(int argc, char *argv[])
{
  pthread_t p1, p2;
  printf("main: begin (counter = %d)\n", counter);
  Pthread_create(&p1, NULL, mythread, "A");
  Pthread_create(&p2, NULL, mythread, "B");

  Pthread_join(p1, NULL);
  Pthread_join(p2, NULL);
  printf("main: done with both (counter %d)\n", counter);
  return 0;
}
```

- Imagine a scenario where a thread wants to update a global variable.
