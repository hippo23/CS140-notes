# Chapter 37: HDDs

## .1: The Interface

- The disk contains an **array of sectors**, with each sector being 512 bytes in size. In total, there are _n_ sectors in a disk, with the numbering going from 0 to n - 1. We can think of this as the **address space** of the drive.
- Multi-sector operations are possible; We can write or read more than a sector at a time. However, a **SINGLE** write to a sector is atomic. It completes entirely or it doesn't complete at all.
- There are some other assumptions that are usually made, but these are never directly specified in the interface. Some are the following:
  - Accessing two blocks that are near one another in the address space is faster than accessing ones that are far apart.
  - Accessing blocks in contiguous chunks are the fastest access modes.

## .2: Basic Geometry

- **platter** - Data is stored here; stored via inducing magnetic changes to the platter. Each of them has two sides or **surfaces**, all coated with a thin magnetic layer to make sure that data persists even if the drive is off.
- All the platters, of which there can be multiple, form the **spindle**.
- Data encoded on each track in concentric circles is called a **track** per circle.

## .4 I/O Time: Doing the Math

- Common sense, just check the book honestly.

## .5: Disk Scheduling

- Because I/O time can take so long, the OS usually has to make decisions about how to schedule I/O operations to the disk. More specifically, it needs to decide what to schedule next.
- With disk scheduling, we typically know how long a certain task will take, so we opt to follow the principles of **SJF** to avoid the convoy effect.

### SSTF: Shortest Seek Time First

- We order the queue of I/O requests by track, and we pick requests on the nearest track to complete first.
- The only downside to this is that the geometry of the drive is not available to the OS, all that it sees is an array of blocks. Instead of nearest track, then, we do **nearest-block-first**.
- The second problem is one of **starvation**. Again, far disk reads or writes will never execute. This is where the next option comes into play:

### Elevator (aka SCAN or C-SCAN)

- We move back and forth across the disk. If we are moving in one direction, we service the closest write that will keep us going in the same direction until we have no choice but to switch directions.
- **F-SCAN** - We freeze the queue to be serviced while conducting a single pass through the disk (often called a sweep). We avoid starvation of far-away requests by delaying the service of late-arriving (but nearer by) requests by putting them into a later queue.
- **C-SCAN** - Instead of going outer-inner-outer-inner, we only go from outer-inner over and over. This is more fair to inner and outer tracks, as back and forth scans are more biased to the middle tracks (the middle has to wait at worst 1R of the disk, outer track and inner has to wait around 2R).
- The last thing we haven't considered is the time for a track to rotate to the sector that we want. We come to the final scheduling algorithm:

### SPTF: Shortest Positioning Time First

- Let us start with a sample problem:
  - _The head is currently at sector 30 of the inner track, do we schedule sector 16 on the middle track or sector 8 on the outer track?_
- It depends on the disk. If seek times are longer than rotation times, we would want to follow [Elevator](#elevator-aka-scan-or-c-scan). If seek times are faster, then it would make more sense to service the far track first, which has to rotate all the way around before passing under the disk head.
