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
  // the device number
  uint dev;
  // the block number
  uint blockno;
  // the sleep lock that we use to access it
  struct sleeplock lock;
  // the number of references or programs
  // accessing this block
  uint refcnt;
  // moving to the next block in the LRU cache
  struct buf *prev; // LRU cache list
  struct buf *next;
  // the actual data of the block
  uchar data[BSIZE];
};
```

## `fs.c`

### `static void bzero`

```c
// Zero a block.
static void bzero(int dev, int bno) {
  // a buf structure
  struct buf *bp;

  // we actually load the buf structure and lock onto it
  bp = bread(dev, bno);
  // set all the data to zero
  memset(bp->data, 0, BSIZE);
  // write it to the log buffer
  log_write(bp);
  // and release the lock on the buffer
  brelse(bp);
}
```

### `static uint balloc`

```c
// Allocate a zeroed disk block.
// returns 0 if out of disk space.
static uint balloc(uint dev) {
  int b, bi, m;
  struct buf *bp;

  // bp is the buffer structure, we make it null first
  bp = 0;
  // we have a counter on the number of blocks in
  // the filesystem
  for (b = 0; b < sb.size; b += BPB) {
    // for each block, we try and read the data in it
    // and lock onto the returned buffer structure
    // BPB is the number if bits within a block
    bp = bread(dev, BBLOCK(b, sb));
    // BBLOCK gets the block we need that correspondings
    // to the bitmap block of a specific block
    for (bi = 0; bi < BPB && b + bi < sb.size; bi++) {
      // we go through each bit inside the block
      // making sure that we don't exceed the actual superblock
      m = 1 << (bi % 8);
      // m is the specific bit in the current int we are reading
      if ((bp->data[bi / 8] & m) == 0) { // Is block free?
        // note that this rounds down, so we are getting
        // the proper bit. We divide by 8 because
        // bi is the bit, but bp is in bytes
        bp->data[bi / 8] |= m;           // Mark block in use.
        // mark the block in use
        log_write(bp);
        // then update the data in the actual block
        brelse(bp);
        // release control of the block
        bzero(dev, b + bi);
        // and zero out the value of the block number
        // we have just allocated
        return b + bi;
        // and return the value of the block number
        // note that b is also in bits, its just
        // that it exists for iterating through blocks
        // and bi is for iterating inside each block
      }
    }
    // if we are unable to find any, still need to release control
    // of the current block that we have read
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}
```

### `statuc uint bfree`

```c
// Free a disk block.
static void bfree(int dev, uint b) {
  struct buf *bp;
  int bi, m;

  // access the buffer structure in the bitmap
  // that holds the passed block number
  bp = bread(dev, BBLOCK(b, sb));
  // and also get the specific bit offset
  // based on the block number
  bi = b % BPB;
  // bit position
  m = 1 << (bi % 8);
  if ((bp->data[bi / 8] & m) == 0)
    panic("freeing free block");
  // zero out the bit value in the correct byte
  bp->data[bi / 8] &= ~m;
  // update the block
  log_write(bp);
  // release control of the block
  brelse(bp);
}
```

### `void iinit`

```c
void iinit() {
  int i = 0;

  // initialize the lock on the inode table (how many we can
  // have loaded at a time). We need to limit it so we can, well
  // keep track of references to an inode, writes and reads and all
  // that
  initlock(&itable.lock, "itable");
  for (i = 0; i < NINODE; i++) {
    // also initialize the sleeplock per inode
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}
```

### `struct inode *ialloc`

- **QUESTION: Isn't it inefficient to keep acquiring and releasing the block even `inodes` are stored in batches of `IPB` per block?**
  - Maybe it is done so that `ialloc` doesn't hold a single block for an excessively long time?

```c
struct inode *ialloc(uint dev, short type) {
  int inum;
  struct buf *bp;
  struct dinode *dip;

  // we use the superblock of the filesystem to know where
  // the first inode block is located, and go from there
  for (inum = 1; inum < sb.ninodes; inum++) {
    // we read the block at that point
    bp = bread(dev, IBLOCK(inum, sb));
    // we get the inode at each offset, note that
    // there are IPB indodes per block
    dip = (struct dinode *)bp->data + inum % IPB;
    // if it has no type, then it is a free inode
    if (dip->type == 0) { // a free inode
      // // zero out the data in the inode
      memset(dip, 0, sizeof(*dip));
      // assign the type that was passed
      dip->type = type;
      // and put it into the log buffer to be committed soon after
      log_write(bp); // mark it allocated on the disk
      // and release our use of the block
      brelse(bp);
      // return the inode structure (not the data) (and not locked).
      return iget(dev, inum);
    }

    // in any case, if there's no free ones in this block, release the block
    // and move to the next one
    brelse(bp);
  }

  // if we get to this point, there are just no free inodes
  printf("ialloc: no inodes\n");
  return 0;
}
```

### `iupdate`

```c
// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void iupdate(struct inode *ip) {
  struct buf *bp;
  struct dinode *dip;

  // reading the appropriate block with the inode
  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  // getting the right structure corresponding to the
  // inode in the block
  dip = (struct dinode *)bp->data + ip->inum % IPB;
  // copy alll the data of the in-memory inode
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  // copy also the actual addrs data of the in-memory
  // inode. Note that this really only affects the address
  // of the NDIRECT block ADDRESSES and the NINDIRECT block
  // ADDRESS. Writing those actual blocks are a totally
  // different thing altogether
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  // and write it back onto disk
  log_write(bp);
  // release control of the block containing the inode
  brelse(bp);
}
```

### `static struct inode *iget`

```c
// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode *iget(uint dev, uint inum) {
  struct inode *ip, *empty;

  // acquire the actual inode lock
  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      // if the inode is present in memory, then we increase the
      // ref count
      ip->ref++;
      // and then release the lock
      release(&itable.lock);
      return ip;
    }
    // otherwise, we note free structures
    if (empty == 0 && ip->ref == 0) // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry.
  if (empty == 0)
    panic("iget: no inodes");

  ip = empty;
  // the device number
  ip->dev = dev;
  // set the appropriate inode number
  ip->inum = inum;
  // set the reference to 1
  ip->ref = 1;
  // mark it as invalid since we haven't loaded
  // this one into memory yet
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}
```

### `struct inode *idup`

```c
// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode *idup(struct inode *ip) {
  acquire(&itable.lock);
  // just increase the reference count to an inode
  ip->ref++;
  // and release the lock, useful for duplicating
  // file descriptors
  release(&itable.lock);
  return ip;
}
```

### `void ilock`

```c
// Lock the given inode.
// Reads the inode from disk if necessary.
void ilock(struct inode *ip) {
  // the buffer / block
  struct buf *bp;
  // the disk inode structue
  struct dinode *dip;

  // if the inode we are trying to lock onto
  // is actually invalid, then throw an error
  if (ip == 0 || ip->ref < 1)
    panic("ilock");

  // otherwise acquire the lock
  // because for now we assume that we want to
  // have exclusive access to avoid access
  // until one process is done with whatever
  // it needs the inode for
  acquiresleep(&ip->lock);

  // if it is invaid, we have to load it into memory
  if (ip->valid == 0) {
    // read the block containing the inode
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    // get the appropriate address of the dinode structure
    // (remember that the dinode structure is really in
    // the block, though, it is not a pointer to somewhere
    // else in memory)
    dip = (struct dinode *)bp->data + ip->inum % IPB;
    // copy the data
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    // copying even the address of the blocks of the inode
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    // then release the block
    brelse(bp);
    // and mark the inode as valid
    ip->valid = 1;
    // the inode also needs to have a type
    if (ip->type == 0)
      panic("ilock: no type");
  }
}
```

### `void unlock`

```c
// Unlock the given inode.
void iunlock(struct inode *ip) {
  // unlock, meaning that we are done doing whatever
  // we wanted to do to this inode
  if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    // we throw an error if it is invalid inode, we don't
    // have the lock in the first place, or if it will
    // be erased soon
    panic("iunlock");

  // release the sleeplock on the inode
  releasesleep(&ip->lock);
}
```

### `void iput`

- **QUESTION: What is the use of `acquirsleep` when we choose to try and free the `inode`? We already have `itable.lock`, so the `ref` value cannot change at all.**
  - The danger is not in a concurrent `ilock` (because the reference again must be zero) but instead in a concurrent `ialloc` call. For example, it sees us mark our type as zero (which means the current log buffer content must have already been written). This race doesn't really matter, because we make sure to mark it as invalid BEFORE releasing the lock, so that the incoming `ilock` call will make sure to load the `inode` data.
  - AT THIS POINT I THINK ITS JUST FORMALITY T_T
  - I guess what I mean is that if some thread gets this allocated,
- **QUESTION: Does marking it as invalid even matter? The updated version should already be what the `inode` is, why do we have to mark it as invalid?**
  - I think this is also just formality, I see no reason that this should be marked invalid to be honest.

```c
// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void iput(struct inode *ip) {
  // acuuire the lock to the inode table
  acquire(&itable.lock);

  if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
    // inode has no links and no other references: truncate and free.
    // remember that this effectively means that it has no
    // use to the file system and must be deleted

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);
    // acquiresleep on the inode because we don't any other process
    // suddenly trying to access this inode while we are about to
    // free it (meaning that we don'y want any other call trying to ilock it)

    release(&itable.lock);

    // delete all the blocks attached to it
    itrunc(ip);
    // set its type to 0
    ip->type = 0;
    // write it to the appropriate dinode in memory
    iupdate(ip);
    // and mark the inode as invalid
    ip->valid = 0;

    // release ip->lock
    releasesleep(&ip->lock);

    // and acquire the table lock, because we will again be
    // updating the reference count
    acquire(&itable.lock);
  }

  // we need the itable count because the per inode lock
  // is only used for when reading and writing the data
  // of the inode, not metadata like the ref
  ip->ref--;
  // release the lock on the itable
  release(&itable.lock);
}
```

### `void itrunc`

```c
// Truncate inode (discard contents).
// Caller must hold ip->lock.
void itrunc(struct inode *ip) {
  int i, j;
  // block containing the inode
  struct buf *bp;
  uint *a;

  // need to free th indirect blocks first
  for (i = 0; i < NDIRECT; i++) {
    // if it is not a zero (invalid) address
    if (ip->addrs[i]) {
      // then we run bfree with the given uint block number
      bfree(ip->dev, ip->addrs[i]);
      // then mark it as zero
      ip->addrs[i] = 0;
    }
  }

  // otherwise, we check if there is a block number for
  // the indrect blocks
  if (ip->addrs[NDIRECT]) {
    // we should then read that blockj
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    // we store in a the address to the data
    // casting it to uint so that we increment
    // in that amount
    a = (uint *)bp->data;
    // for each non-zero block value, we have to free it
    for (j = 0; j < NINDIRECT; j++) {
      if (a[j])
        bfree(ip->dev, a[j]);
    }
    // release control of the current block
    brelse(bp);
    // then free the actual NINDIRECT block container
    bfree(ip->dev, ip->addrs[NDIRECT]);
    // mark it as zero or invalid
    ip->addrs[NDIRECT] = 0;
  }

  // set the size of the data in the inode as zero
  ip->size = 0;
  // and update and write it to the in-disk
  // dinode
  iupdate(ip);
}
```

### `void stati`

```c
// Copy stat information from inode.
// Caller must hold ip->lock.
void stati(struct inode *ip, struct stat *st) {
  // common sense no comment lol
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}
```

### `int readi`

```c
// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n) {
  uint tot, m;
  struct buf *bp;

  // if the offset is already  greater that the inode data we want to read, well
  // that's no good is it
  if (off > ip->size || off + n < off)
    return 0;
  // if the offset plus the size of data we want to read is too large, we
  // unfortunately will have to truncate and make do
  if (off + n > ip->size)
    n = ip->size - off;

  // now, we start reading, tot, i imagine, is the amount of data we've read?
  for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    // bmap reads a specific block of an inode. offset divided by blocksize, though i
    // imagine if the data is not divisible, then it will be rounded down?
    uint addr = bmap(ip, off / BSIZE);
    // make sure its a valid block (question, when are the blocks zeroed out
    // anyways?) it should be in itrunc, meaning that something must've called
    // iput (or mknod), everything must've been created in that manner i assume
    if (addr == 0)
      break;
    // we read the block corresponding to the address we we're given. Remember,
    // the first 12 ENTRIES (not blocks) in the addrs array are exactly the
    // block numbers. the rest reside in a separate block. Regardless, bmap
    // takes care of that, and returns to us the block number of the bnth block
    // belonging to the inode
    bp = bread(ip->dev, addr);
    // we get the minimum between the total data we want to read and the data we
    // have read and the amount of data in a block minus the offset (the offset
    // into the inode that we have read) divided by the block. You can think of
    // it this way, the very last block we need data to read from, most likely,
    // n-tot will be similar because n is closer to end than the boundary of the
    // actual block. On the other hand, if we are not reading the last block,
    // there are is more than a blocks distance away from the end roughly, and
    // so we want to just read to the blocks boundary minus how much into the
    // offset we have read.
    m = min(n - tot, BSIZE - off % BSIZE);
    // then we copy the data to the place that we want to (i think, for readi,
    // this should be kernel space)
    if (either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    // we release the currentb lock we are reading
    brelse(bp);
  }
  // we only terminate when we have read all the data we wanted
  return tot;
}
```

### `int writei`

- **QUESTION: Why must `bmap` allocate where necessary automatically?**

```c
// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n) {
  // we will need the offset, the src in either user or kernel space
  // and the amount of data we are writing
  uint tot, m;
  // the buffer / block that we will use for reading
  struct buf *bp;

  // make sure that the offset isn't greater than the size, and make sure
  // that we are not writing negative data
  if (off > ip->size || off + n < off)
    return -1;
  // make sure that we don't exceed the maximum file size
  if (off + n > MAXFILE * BSIZE)
    return -1;

  // then we proced to write the data
  for (tot = 0; tot < n; tot += m, off += m, src += m) {
    // get the appropriate block in the inode
    uint addr = bmap(ip, off / BSIZE);
    // if it not an actual block, then we cannot write to it
    if (addr == 0)
      // if it is not valid, we still may have allocated a block
      // and so the `inode` must be updated
      break;
    // otherwise, read the block
    bp = bread(ip->dev, addr);
    // get the minimum of how much space is left in the block
    // and how much we have let to write
    m = min(n - tot, BSIZE - off % BSIZE);
    // then copy the data into the inode location
    if (either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    // then write that to the block via the log buffer
    log_write(bp);
    // and release the block
    brelse(bp);
  }

  // if the offset is greater than the current inode size
  if (off > ip->size)
    // we have to increase the inode size to the appropriate value (the offset)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}
```

### `static uint bmap`

```c
// we pass in the inode that contains the info
// we want to read, and the block number, block being
// the exact block we want to access
static uint bmap(struct inode *ip, uint bn) {
  uint addr, *a;
  struct buf *bp;

  // note that basically, there is some
  // inode and each block it has has 1s or 0s
  // to tell us what are free and what aren't free

  // if the block number is less than the ndirect
  // then the first four(?) addresses lead directly
  // to the block we want to read
  if (bn < NDIRECT) {
    if ((addr = ip->addrs[bn]) == 0) {
      // balloc actually allocates space
      // on the disk
      addr = balloc(ip->dev);
      if (addr == 0)
        return 0;
      // otherwise, we can assign
      // the address of this ndirect block
      // to go the newly allocated space
      ip->addrs[bn] = addr;
    }
    return addr;
  }

  // otherwise it must be an indirect block
  // so we need to get its offset
  bn -= NDIRECT;

  // NINDIRECT is the maximum size
  if (bn < NINDIRECT) {
    // if the NDIRECT BLOCK does not exist
    // we create one
    if ((addr = ip->addrs[NDIRECT]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;

    // then for the specific block number
    // we also need to allocate if it does not exist
    if ((addr = a[bn]) == 0) {
      addr = balloc(ip->dev);
      if (addr) {
        // here we can see that we write the new address
        a[bn] = addr;
        log_write(bp);
      }
    }

    // and then we reduce the reference
    // count and update the LRU cache (possibly)
    brelse(bp);
    // and return the address of the block
    // of the INODE
    return addr;
  }

  panic("bmap: out of range");
}
```
