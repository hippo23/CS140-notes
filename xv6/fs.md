# The xv6 Filesystem

## xv6 Book

### Introduction

- The filesystem structure of xv6 can be divided into the following levels:
  - **File descriptor** -
  - **Pathname** - We resolve pathnames with recursive lookup, so each part of the path would be an Inode and we keep looking through its blocks until we end up at an Inode with the file's name(?) I know ELF files have names, not sure about this.
  - **Directory** - This is a special Inode whose content is a sequence of directory entries (so I'm guessing each block could be another directory or it could be an actual file Inode).
  - **Inode** - Individual files, each with a unique i-number and some blocks holding the file's data.
  - **Logging** - We wrap updates to several blocks into a **transaction**, this is done for 1) efficiency reasons (less requests have to be sent to the disk); and 2) this is how we ensure that transactions are atomic. It's either we complete one, or we don't at all. If its only atomic at the sector level, well, you could've changed 99% of all the other sectors.
  - **Buffer cache** - Two functions: 1) To cache disk blocks (or sectors, like we mentioned); and 2) To synchronize access to them. We only want one kernel process writing to this specific block at a time.
    - **QUESTION: Is this different from, say, an `amoswap` instruction? Because this is only block level whilst more than mutiple blocks (which some info may be) can be written to and loaded from simultaneously?**
  - **Disk** - This is what we read and write to. You'll see many of our interactions with the disk device in the source code within `virtio_disk.c`.

### .2: Buffer cache layer

## `header files`

### `fs.h`

#### `struct superblock`

- You can think of this as the equivalent of the `ELF` header for `ELF` files. This tells us metadata about the file-system, how large it is in blocks, how many data blocks we have, how many Inode blocks, and how many free-map blocks.
- It is ordered in the order you see it, `log`, `inode`, `free`. We can use the number per kind of block to find the start of each.

```c
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};
```

#### `struct dinode`

```c
// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};
```

### `buf.h`

#### `struct buf`

- _valid_ is used when, for example, we are reading a buffer from the disk but it actually hasn't finished yet.
- _disk_ is for which disk the buffer is currently reading from (although right now I think there is only one disk, as can be seen in [virtiodisk](/xv6/virtiodisk.md)).
- _blockno_, just the displacement within the disk I'd like to think. For the filesystem in qemu (and hence xv6), we just see an array of blocks (is this index the same as the numbering on the disk itself). (Correct, lol, this is what we are acting on if you look at [virtiodisk](/xv6/virtiodisk.md)).
- _lock_ is for dealing with concurrent memory uses to a certain block. A block refers to a specific part of memory, and we don
- _refcnt_
- _buf_
- _next_

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};
```

## `fs.c`
