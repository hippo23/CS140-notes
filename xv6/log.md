# Logging Mechanisms in xv6

## xv6 chapters on the logger

### .4: The logging layer

- When trying to deal with crash recovery, there are two situations that we are trying to avert:
  - _an inode with a reference to a content block that is marked free_
  - _an allocated but unreferenced content block_
- The latter is not that harmful, as it just means that eventually, we will overwrite it? Since I assume that if we ask for it in memory then it's mostly on us if we are using the data that we read.
- The former is ore harmful, as we are essentially referring a location in memory that can be written to by others.
- To fix this, we use **logging**, wherein a system call does ot directly write the on-disk file system data structures. Instead, we place all the writes into the log.
- Once we have logged all of our writes, it writes a _commit_ record to the disk indicating that the log contains a complete operation. Once all of this data is written to the disk, the log is cleared.
- In this way, if we crash before the operation, we just clear the log. If we crash during or while writing, we just write all the data again. This allows us to have an atomic state in memory.

### .5: Log design

- The log resides at a known, fixed location, specified in the superblock. It consists of a header block followed by a sequence of updated block copes ("logged blocks").
  - Basically speaking, if you look at the [logheader](#struct-logheader) struct, you will see what was mentioned there. We have the number of actual blocks, followed by each block in the log header. The count is set to zero after each operation to indicate that the log is empty.
- **QUESTION: Can you relate how this ability makes it so that the previous problems with crashes are averted?**
  - The operatiosn are atomic. At least the log operations are. If we are freeing something, I'm guessing its guaranteed to be free or not free at all? If we crash before it clears, we restart and clear things again. If we crash after it clears, then the code should have appropriately assumed that that block was, indeed, free? Like a that it was in our ball-park to know that the block was actually free.
  - Note that when we talk about free-ing somewhere in memory, we mean its mirror, the struct that we have in the code that reflects that place in memory. That is what we are logging and what we want to make sure is either free or not free at all.
  - With regards to the second situation, if we want to lock a certain place in memory, but we are unable to return that reference back to whoever called, then what we need to log is the fact that we allocated a place in memory, and we need to make sure that whoever called to allocate gets a reference to that place in memory.

### .6: Actual Code of Logging

- This is a typical use of a log in a system call, note that we see [bread](/xv6/virtiodisk.md) on our notes on the virtual disk driver for log, this just gets a free reference to a block and returns the actual data.
  - `install_trans()` actually reads each block from the log and writes it to the proper place in the file system.

```c
begin_op();
...
bp = bread(...);
bp->data[...] = ...;
log_write(bp);
...
end_op();
```

- As we can see, we use `begin_op()`. This waits until he log is not currently committing, and until there is enough space to hold the writes from this call. `begin_op()` also increments a counter that tells us how many active system calls are dealing with memory right now.
- We then use `log_write`, which is a proxy to `bwrite`, wherein we record the block sector's number in memory (the location of what we are writing to), reserve it a slot in the log on disk, and pin the buffer in the block cache to prevent the block cache from evicting it.
- `end_op` then decrements the number of system calls we are adding to the log. When this reaches zero, we commit the current transaction by calling `commit`. The process of `commit` is as follows:
  - `write_log()` is called to copy each block modified in the transaction from the cache to its slot in the log on disk.
  - `write_head()` writes the header block to disk: this is how we actually save the data, the header block is stored on disk, and when we crash, we just look for the header block and see if there's anything stored.

### .7: Block Allocator

- All file and directory content are stored in disk blocks, which must in turn be allocated from a free pool. Xv6's block allocator maintains a free bitmap on disk, with one bit per block. A zero bit indicates that the block is free, otherwise, it is in use. The `mkfs` program sets the bits corresponding to the boot sector, superblock, log blocks, inode blocks, and bitmap blocks.
- There are two functions that we will see present with the block allocator. The first is `balloc`.
  - As we can see, we start with a `buf` struct. We also have the `dev` as a paramter to tell us what disk we are actually allocating something to.
  - We use a loop, starting from zero, going until `sb.size` (which is essentially the entire file system size in blocks), and we add `BPB`, which is just the number of `bitmaps` per block. We go through each BITMAP block, use `bread` to ensure atomic access, then check for any 0 bits. IF we find any, we mark that block as in use, run `log_write` for the purposes we saw earlier, and then release the block so that some other process can go ahead and allocate some disk block. We then zero out the block we want to write to (**why?**), and then return the address of the block we used.
  - The reason why we zero out the block is to make sure that when some other file reads that data, we expect that all data beyond what is useful will be zero. Just helps enforce predictable behaviour I assume.

```c
// Blocks.

// Allocate a zeroed disk block.
// returns 0 if out of disk space.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}
```

- After that, we also have the `bfree` function call:

```c
// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}
```

- First we start with a buffer, then we again read a bitmap block from this specific displacement in the bitmap (using `b`). After that, we go over the bitmap per bit and equate all of that to zero. We then use `log_write` to actually add it to the writes that need to be written. When there are no more things that need to be written, that's when the bit map is updated.

### .6: The Inode Layer

- The term `inode` can actually refer to two things:
  - The on-disk data structure contains containing a file's size and list of data block numbers.
  - That, or `inode` might refer to an in-memory `inode`, which contains a copy of the on-disk `inode` as well as extra information needed within the kernel.
- For the disk one, these are packed into a contiguous area called the `inode` blocks. Each of them are the same size, and are defined by the `struct dinode`:

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

- `type` -> distinguishes between files, directories, and special files. A type of `0` indicates that the `dinode` is free.
- `nlink` -> couns the number of directory entries that refer to this `inode`, in order to recognize when the on-disk `inode` and its data-blocks should be freed.
- `size` -> The number of bytes of content in the file.
- `addrs` -> records the block numbers of the disk blocks holding the file's contents.

- The kernel, on the other hand, keeps a set of active `inode`s in a table called `itable; struct inode` which we see below.

```c
struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;
```

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

- The kernel stores an `inode` in memory only if there is a C pointer referring to that `inode`. The `ref` field counts the number of pointers that refer to it, if that drops to zero, we get rid of that node.
- One of the most confusing points are the sheer amount of locks involved here. We try to outline them and properly delineate their purpose:
  - `itable.lock` - Maintains invariant that an `inode` is present in the `inode` table at most once, and the invariant that an in memory `inode`'s ref counts the number of in-memory pointers to the `inode` (**what?**)
    - Basically, each time we try and allocate or mark an `inode` as free, then have to lock `itable.lock` (like process locks). Hence, access is serialized.
    - Techncially speaking, it is, the question is that whether or not that is a good idea. If one inode is free, well, we have to wait for it before checking al the others. That's kinda dumb, isn't it lol you find an occupied one and wait forever. That really only works for schedulers because they are, well, round robin.
  - `inode.lock` -> The question is whether or not the lock on `inode` themselves are enough to restrict that.
  - `inode.ref` -> Number of references to the `inode`, if it is zero, it is empty.
  - `inode.nlink` -> if the link count is greater than zero, we won't mark it as clear.
- **QUESTION: How is `nlink` different from `ref`? I mean, when is `nlink` used anyway? Is it when a directory refers to it? So its an `inode` referring to another `inode`?**``

```c
// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }
```

- Also, see that `iget` doesn't actually return anything useful all the time. Hence, there is a `valid` bit to tell us whether it has actually gotten the equivalent `dinode` in memory (similar to UART checking to see whether or not the buffer LRU cache checks to see whether or not this buffer). We need to use `ilock` (seen below) to actually fill it with valid data.

```c
// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}
```

- One thing we notice is that we assume we pass in a valid `inode` structure that has already been acquired. This `inode` would have been constructed first, having a fixed structure (\*\*why can't we use an unlimited amount of `inodes`, why not have many instead?) that we get from `iget`, we modify it so that we can get the data-blocks we want?
  - In response to the question, I believe that we want a fixed number of `inodes` so that we can maintain and keep access serial. If we added and removed `inodes` dynamically, we would be forced to know if a certain `inode` mirrors some block in memory that we already reference. That could get messy.
  - Also, this dynamic table as an alternative needs to go somewhere, but we risk overwriting something else if it gets too large.
- Anyways, going back to `ilock`, we acquire the sleep-lock on the `inode` and then check if it is valid. If it is not, we need to read the appropriate `dinode` block into memory. The `IBLOCK` is calculated as the following:

```c
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

```

- The `inode` number is passed in, we divide that by the number of `inodes` per block in IPB, and once we do that, we add it to `sb.inodestart`, which is the block number of the first `inode`.
- Once we have found that `dinode` (in the form of a block), we actually need to convert it to a `dinode` by getting the data address and getting the specific `dinode` in this block (since there are multiple of them inside a single block).
- We then make all the values equivalent, and also move the data of `addrs` (this is because if we just use an equals, we'll end up with a pointer).
- We then release our use of the block, count `ip` as valid.

## `log.c`

### structs

#### `struct logheader`

```c
struct logheader {
  int n;
  int block[LOGSIZE];
};
```

```c
struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
```
