#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  printf("hello (pid:%d)\n", (int)getpid()); // print the parent PID
  int rc = fork();                           // fork into a child process
  if (rc < 0) {
    fprintf(stderr, "fork failed\n"); // fork has failed
    exit(1);
  } else if (rc == 0) {
    printf("child (pid: %d)\n", (int)getpid()); // print the new child process
    char *myargs[3];
    myargs[0] = strdup("wc");      // program name?
    myargs[1] = strdup("code3.c"); // program file or the input file
    myargs[2] = NULL;              // end of the array
    execvp(myargs[0], myargs);     // runs word count
    printf("this shouldn't print out");
  } else {
    int rc_wait = wait(NULL); // wait for child process to finish
    printf("parent of %d (rc_wait: %d) (pid:%d)\n", rc, rc_wait, (int)getpid());
  }
  return 0;
}
