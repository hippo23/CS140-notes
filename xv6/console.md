# `console` files

## `console.c`

### `struct` definitions

### `func` definitions

#### `void consputc`

- Depending on the character that was typed, we need to output a certain value to the console. If `BACKSPACE` was passed, it moves the cursor back once space, allowing the previous character to be overwritten
- Otherwise, we just go ahead and print the character that was passed.

```c
void consputc(int c) {
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}
```

#### `int consolewrite`

- We have the source of the text we want to write to the console, and we go through all of them using `uartputc`

```c
//
// user write()s to the console go here.
//
int consolewrite(int user_src, uint64 src, int n) {
  int i;

  for (i = 0; i < n; i++) {
    char c;
    if (either_copyin(&c, user_src, src + i, 1) == -1)
      break;
    uartputc(c);
  }

  return i;
}
```

#### `int consoleread`

- We pass the destination of the text (either in kernel space or user space) and the size of what we want to read.
- Acquire the `cons.lock` to make sure we are the only one reading from the console.
- if `con.r == cons.w`, that means we have read all there is to read, and must sleep until there is something to read.
- Otherwise, we get the character to read in the circular buffer via `c = cons.buf[cons.r++ % INPUT_BUF_SIZE]`
- What is `dst`? I think it corresponds to the actual destination, `user_dst` is just whether or not it actually is a user or kernel address.
- We then have to increment `dst` to get the next character, and decrement `n` to say we have this much left to read.

```c
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int consoleread(int user_dst, uint64 dst, int n) {
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while (n > 0) {
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while (cons.r == cons.w) {
      if (killed(myproc())) {
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if (c == C('D')) { // end-of-file
      if (n < target) {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if (c == '\n') {
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

```

#### `void consoleintr`

- So if I understand correctly, `uartintr` happens when there is input to the `uart`, but does that mean keyboard inputs go to the `uartintr`, go to the `consoleintr`, which prints back into the `uart`?
  - I think a unique thing to see here is that what you see in the console being written is not the same as that which `consoleread` read is reading. Yes, it is mirrored, but `consoleread` uses the `cons.buf` buffer, the `uart` services as the visual mirror but cannot read from the `cons.buf` in an online manner.
- Note the special case of the `BACKSPACE` and `DELETE` key, we can see that with delete, we first move the cursor appropriately and also decrease the edit index (saying we are overwriting the last character).
- When we actually end up writing a character, we need to check that 1) it is valid; 2) the edited index (that could be far further that how mnay we have read) is less than what is the max amount of characters in the buffer.
  - In the case that they are actually greater, then that means that we are adding characters to a buffer that is already full (because we haven't read `INPUT_BUF_SIZE` characters).
- In any case, once it satisfies these conditions, we can feel free to actually to write the character to the console AND to the buffer. We also truncate the written characters starting from the edited index (I think we could do this from the `DELETE` case itself, but it only really matters when are trying to read the line, which is when `\n` is pressed).

```c
//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void consoleintr(int c) {
  acquire(&cons.lock);

  switch (c) {
  case C('P'): // Print process list.
    procdump();
    break;
  case C('U'): // Kill line.
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (cons.e != cons.w) {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }

  release(&cons.lock);
}
```

#### `void consoleinit`

```c
void consoleinit(void) {
  initlock(&cons.lock, "cons");

  uartinit();



  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
```
