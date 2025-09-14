# `bio.c`

## struct definitions

- We know of the existence of the [buf](/xv6/fs.md) structure, which is used to store copies of blocks that we've read into memory. Here, we see that our entire cache, called `bcache` is put into a single `struct`.
- Is the lock there for concurrency? It's not for access to singular blocks, that would be too slow. Could it be for editing state?
- Lastly, we have the head.

```c
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;
```

## function definitions

### `void binit`

- First the initialization of the buffer cache, which is really a doubly-linked list of various buffer objects (these buffer objects are in the array, we are just sorting them from most recently used to least recently used).
- This is one of THE MOST STUPID ways I have ever seen a doubly-linked list be made, but if you look closely, its actually really cool. It goes as follows:
  - Imagine we only have the _buffer head_ first. You can think of this is as what we use to setup a sort of queue for the most recent and least recent. `b->next` goes from newest to oldest, `b->prev` goes from oldest to newest (in a cyclical fashion).
  - Now, we get a new buffer to add. This, for now, is our newest buffer. We point its `->next` to the second-newest element, which, in this case, is just the buffer head. We also point its `prev` to whatever element is 'newer', which, in this case, is the buffer head, because nothing else is newer.
  - Following that, we need to change our buffer head values, the previous value of the 'previous-newest' element (now second-newest) must now point to the newest element because of the order we mentioned earlier, and the newest element must now become the element we just added.
- You can think of it this way, when we add an element, we first point its next to the second-newest element, and we point its prev to the head (because there is no newest element). Following this, we point the second-newest elements previous element to go from what was head to the newest element, and we mark the newest element as, well, the newest element.
- We don't change the next of the second-newest element, this stays fixed to whatever was older than it.

```c
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}
```

### `static struct buf* bget`

- We check the buffer cache from oldest to newest. In this case, two things have to match up:
  - **device number** - `buf->dev` refers to the device number (to distinguish between disks, I presume)
  - And `buf->blockno` is the block number (think of the block / sector numbers in diagrams seen in [Chapter 37](/chap37/notes.md) of OTSEP).
- If it is not cached, we need to remove the least recently used cached block (which is `head->prev`) and check the following:
  - `buf->refcnt` should be zero (this just means there should be no current process referring to the block), then we use that slot in the cache, and assign the `buf->refcnt` to be one.
- Perhaps the hardest part of this function is the locks that we need to acquire. Let's look at the two and try to deduce the reason for their usage:
  - `bcache.lock` -> A scenario wherein usage would be broken is if 1) One process found a cached process, but before we could increment the `refcnt`, another process removes it as it has not been recently used; Another is if two processes are looking for a cached process, they could increment at the same time; lastly, if two processes are looking for a free slot, they might both end up using the free-slot.
  - `b->lock // sleeplock` -> This is a per-block lock. Although we allow more than one process to have access to a certain block, we only allow one of them to use it at a given time. **Why is that?**
    - The reason is that we are not just returning the value of the buffer, we are returning the actual object (or, well, the pointer). Two processes writing to the same cached buffer would be disastrous, as one process is not aware of what the other is doing. We use sleep-lock because we can't really predict how long one process will use a portion of memory.

```c
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}
```

### `struct buf* bread`

- There is where the valid bit comes in. If we have not retrieved a certain block from memory but need to read it, then we call `virtio_disk_rw` which you can see described in our [driver notes](/xv6/virtiodisk.md).
- **QUESTION: Is there any possible case wherein we have it in cache but we don't have the block in memory**
  - As far as I know, there isn't. Technically speaking, that state only exists if we 1) use `bget` independently (which no function does); or 2) We are in the process of loading the data. Assuming that all the succeeding instructions are gonna read the data from the block, no reordering should occur, hence, I think the valid check is really just for newly allocated blocks.

```c
// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}
```

### `void bwrite`

- Common sense. Just take note of the need to be locked onto the process.

```c
// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}
```

### `void brelse`

- Again, very simple if you understood the previous functions. We need to hold the lock (duh), we release that lock (**QUESTION: Why don't we release that lock AFTER we have actually cleared out all trace of us using it**)
  - It is possible that some other process is about to see this cached block (has incremented `refcnt` and all that) and holds `bcache->lock` while waiting for `b->lock`. On the other hand, we are waiting for `bcache->lock` while hollding `b->lock`, its deadlock 101.
- If all is nice and dandy, and if no process is currently waiting, we decrement `refcnt` and move it to the head of the buffer cache if it is at zero.
  - The element that is older than it should have ITS newer element point to `b`'s newer element (You're basically just taking it out of its slot and joining the left and the right).
  - The element that is newer than it should have its older element point to `b`'s older element.
  - The element older than `b` should be the original newest element.
  - The element newer than `b` should just go back to the cache head.
  - The original newest elements newer element should be `b`
  - And `b` is to be made the newest element.
- Then we release the lock :)

```c
// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock);
}
```

````c

```c
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}
````

```c
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
```
