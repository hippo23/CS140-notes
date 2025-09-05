#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  printf("Hello, world!\n"); // goes into stdio buffer, not yet written
  pid_t pid = fork();
  if (pid == 0) {
    exit(0);
  } else {
    wait(NULL);
  }
}
