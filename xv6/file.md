# Files in xv6

## `file.c`

## `file.h`

### structs

#### `struct file`

-

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
