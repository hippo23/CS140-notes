# Files in xv6

## `file.c`

## `file.h`

### structs

#### `struct file`

- **TO BE ADDED**

```c
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};
```

#### `struct inode`

- An [inode](https://www.reddit.com/r/linux4noobs/comments/13g6h1m/what_exactly_are_inodes/) seems to refer to anything that is a file, with subcategories being a directory (which has a list of actual `inode` files that are part of the directory).
- Here, we can see that there is a device number, I assume to tell us what disk this node is supposed to mirror. Its Inode number, reference count, and a `sleeplock`.
- The `sleeplock` protects the following attributes:
  - `valid` bit - similar to the valid bit of a `buf` used at the [virtual disk level](/xv6/virtiodisk.md).
  - `type` - I assume this is also like the three descriptors that are used to add data into the virtual disk. This should tell us whether the `inode` is read or write.
  - `major` - **TO BE ADDED**
  - `minor` - **TO BE ADDED**
  - `nlink` - **TO BE ADDED**
  - `size`, self-explanatory (although, I wonder if we need to know a max size for this? For example, if it is a awrite inode? I'm not sure how limited it is by block size in the disk).
  - `addrs` - **TO BE ADDED**

```c
// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};
```

#### `struct devsw`

```c
// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

```

### global variables

#### `devsw[]`

```c
extern struct devsw devsw[];
```

### function definitions

#### `void fileinit`

- All we do here is initialize the `ftable.lock`. This lock will be used later on to enforce serial access looking for an available file descriptor that we can use (sort of like looking for a process that we can run).

```c
void fileinit(void) { initlock(&ftable.lock, "ftable"); }
```

#### `struct file *filealloc`

- Here, we just look for an available file descriptor inside `ftable.lock`. Once we have found one, we increase the `ref` count and return the object.
- **QUESTION** Unlike with processes, there doesn't seem to be any `wfi` instruction here. Hence, wouldn't it be wiser to just use a per-file lock instead.

```c
// Allocate a file structure.
struct file *filealloc(void) {
  struct file *f;

  acquire(&ftable.lock);
  for (f = ftable.file; f < ftable.file + NFILE; f++) {
    if (f->ref == 0) {
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}
```

#### `struct file *filedup`

- If there is already a file loaded here, and we also want to refer to it, then we use `filedup`. [filealloc](#struct-file-filealloc) is exclusively for loading a new file into memory.

```c
// Increment ref count for file f.
struct file *filedup(struct file *f) {
  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}
```

#### `void fileclose`

- We acquire the lock since we will be changing the state of the file. If the `ref` is already zero or less, throw an error, we shouldn't be closing that file. If not, then we decrease the `ftable->lock` if it is still not zero after decreasing.
- If it is zero, then we need to do a cleanup. Get the address of the descriptor, set its `ref` to zero, and set its type to `FD_NONE`. Then we release the lock.
- If the file descriptor was used for an `FD_PIPE`, we also need to close the pipe via [pipeclose](/xv6/pipe.md). Otherwise, if it is an `inode` or `device` type, we need to do some logging stuff **TO BE EXPLAINED IN LOG.md**.

```c
// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f) {
  struct file ff;

  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("fileclose");
  if (--f->ref > 0) {
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if (ff.type == FD_PIPE) {
    pipeclose(ff.pipe, ff.writable);
  } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
    begin_op();
    iput(ff.ip);
    end_op();
  }
}
```

### `void fileclose`

```c
// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f) {
  struct file ff;

  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("fileclose");
  if (--f->ref > 0) {
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if (ff.type == FD_PIPE) {
    pipeclose(ff.pipe, ff.writable);
  } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
    begin_op();
    iput(ff.ip);
    end_op();
  }
}
```

### `int filestat`

```c
// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr) {
  struct proc *p = myproc();
  struct stat st;

  if (f->type == FD_INODE || f->type == FD_DEVICE) {
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}
```

### `int fileread`

```c
// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64 addr, int n) {
  int r = 0;

  if (f->readable == 0)
    return -1;

  if (f->type == FD_PIPE) {
    r = piperead(f->pipe, addr, n);
  } else if (f->type == FD_DEVICE) {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if (f->type == FD_INODE) {
    ilock(f->ip);
    if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}
```

### `int filewrite`

```c
// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n) {
  int r, ret = 0;

  if (f->writable == 0)
    return -1;

  if (f->type == FD_PIPE) {
    ret = pipewrite(f->pipe, addr, n);
  } else if (f->type == FD_DEVICE) {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if (f->type == FD_INODE) {
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    int i = 0;
    while (i < n) {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if (r != n1) {
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}
```
