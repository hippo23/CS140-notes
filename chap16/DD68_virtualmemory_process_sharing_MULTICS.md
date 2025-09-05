# Virtual Memory, Processes, and Sharing in MULTICS

- The main goals of MULTICS were as follows:

1. Provide the user with a large machine-independent virtual memory, placing responsibility on the system software.
2. To permit a degree of programming generality not previously practical.
3. To permit sharing of procedures and data among users subject only to proper authorization.

## Concepts of Process and Address Space

- All this you know, but notice this statement on address spaces:

> "_The size of the address space provided to processes makes it_
> _feasible to dispense with files as a separate mechanism for_
> _addressing information held in the computer system._"

- **QUESTION: What exactly is meant by this statement?**
  - It says that no distinction need be drawn between files and segments. I'd like to believe this is similar to the concept of memory mapping, wherein we can create a segment that is contiguous and say, hey, this is a file. That way, when one segment of the process (via the process descriptor) tries to access another segment, we need to see the permissions that the process has on that segment, and can easily access the data if needed (most likely, files will probably be read-only if we are reading from it).

## Addressing

- **_The Generalized Address_**
  - Split into 18-bit segment and 18-bit word.
  - When we address, we use a base-offset register, add that to our offset that is built into the address, then we can (optionally) add that to a register through a mode bit/s in a process known as **indirect addressing**.
- **_The Descriptor Segment_** - Like was said, each process has a descriptor segment, each segment has an offset as its segment number, and its location in memory returns a sort of vector that stores data about the segment.
  - Additionally, the paper states that all the other addressing stuff is non-location dependent, meaning that the process need not be aware of where it actually wants to go in physical memory, it just needs to pass the address it wants to in virtual memory. The only thing that is in direct correspondence or directly mapped to main-memory is the descriptor base register.
  - **QUESTION: What does the following mean? What is meant by not needing associative hardware?**

    > "_Implementation of a memory access specified by a generalized address calls for an associative mechanism that will yield the main memory lactation of any word within main memory
    > when a segment number/word number combination is supplied. A direct use of associative hardware was impossible to justify in view of the other possibilities available._"
    - If we were to imagine trying to implement virtual memory without the help of the _descriptor segment_, we'd need something in hardware to use i.e. a key (segment number) corresponds to this location in physical memory. We sort of do that, but really through tables rather than through directly mapping a segment number of a physical address.

  - This implementation will likely lead to segments having different segment numbers per process (why?)
    - Imagine loading something into memory, say a file. We choose the lowest free index in the segment table, hence, a file could be segment one in one process but segment twenty in the other.

- **_Paging_** - It is said in the paper that the memory needed by information and descriptor segments will be large enough to merit paging.
  - **Why is paging useful for large segments? What makes it not so useful if all the segments are small?** Paging is useful for only allocating what we need. For example, if we have a file and load it into memory, and its very large, then we can only load page-by-page in that segment instead of the entire file. We only load a new page when the address surpasses the last loaded page (we assume that, in this case, all memory in the segment is contiguous relative to physical memory).
  - It also solves external fragmentation, recall xv6 structure, imagine that, but we 'divide' the available virtual memory into segments.

## Inter-segment Linking and Addressing

- **REQUIREMENTS:** We now tackle the idea of sharing segments between multiple processes. To do so, the following requirements need to be fulfilled.
  - **Procedure (code) segments must be _pure_** -> No modifications will be made to the shared segment.
  - **It must be possible for a process to call a routine by its symbolic name without having made prior arrangements for its use** -> The subroutine must be able to provide space for its data, must be able to reference any needed data object, and must be able to call on further routines that may be unknown to the caller.
  - **Segments of procedure must be invariant to the recompilation of other segments** -> The values of identifiers that denote addresses within a segment may change, and it should not appear in the content of any other segment (Why?)
  - Imagine one segment address depends on another. If we recompile some segment, we need to change that segment, and change whatever segments depends on that. This takes a lot of time and, from it, we can possibly infer that there is also a lack of separation between segments. Additionally, if multiple processes share a segment, each recompiled process will affect the shared segment.
- **MAKING A SEGMENT KNOWN**
  - We need a mechanism for providing or assigning a position in the descriptor segment of a process in the case that a procedure or some other segment we are loading is not initially known.
- **LINKAGE DATA**
  - Before a segment is known it is referenced by a symbolic _path name_ which permanently identifies the segment with the directory structure (basically the file path).
