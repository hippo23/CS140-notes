#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// if all goes well, this opens this file for WC, using that is standard input,
// and reads the words in there.
int main(int argc, char *argv[]) {
  int rc = fork();
  if (rc < 0) {
    exit(1);
  } else if (rc == 0) {
    close(STDIN_FILENO); // this refers to standard input, hence, we are
                         // closing standard input

    // this p4.c becomes the input of the next program we open
    open("./code4.c", O_CREAT | O_RDONLY,
         S_IRWXU); // standard input is the lowest
                   // that is open.
    char *myargs[2];
    myargs[0] = strdup("wc");
    myargs[1] = NULL;
    execvp("wc",
           myargs); // given that there is no file passed in, it relies on
                    // the standard input, which is p4.c
  } else {
    int rc_wait = wait(NULL);
  }
  return 0;
}
