# Chapter 16: Segmentation

- **How can we address the large external fragmentation that occurs when we divide virtual memory into fixed partitions for the OS, as well as the large internal fragmentation for each process?**

## .1: Segmentation: Generalized Base / Bounds

- Instead of having just one base and bounds pair in our MMU, why not have a base and bounds pair per logical **segment** of the address space?
- **Segment** - A contiguous space in memory of a particular length.
- If we take a look at the default code stack, we have (1) code space; (2) dynamic / heap space; and (3) stack space. If we divide memory into these portions as separate **segments**, we can place each of them in different parts of physical memory.
- If we implement this approach, we will need a base and bounds pair per segment that the process uses.
- **QUESTION: What if the size of the program grows as it goes on? Won't it need progressively more and more heap / stack space?**

## .2: Which Segment Are We Referring To?

- How does the hardware know what address corresponds to which segment? The first approach is **explicit**.
  - In an explicit approach, we have some designated number of bits, in this case 2, to tell us which segment we are referring to, and the rest of the remaining bits to be used as displacement bits.
  - If the base and bounds were arrays, we would be doing something like the following:

```python
# get the top 2 bits
Segment = (VA & SEG_MASK) >> SEG_SHIFT
# offset
Offset = VA & OFF_MASK
if (Offset >= Bounds[Segment]):
  RaiseException(PROTECTION_FAULT)
else:
  PA = Base[Segment] + Offset
  Register = AccessMemory(PA)
```

- On the other hand, we have the **implicit** approach
  - The hardware determines the segment by seeing how the address was formed. If it was from the program counter, then we are in the code segment, if it is the stack or base pointer, we are in the stack segment, otherwise, we are in the heap.

## .3: What about the stack?

- We show the diagrams of how we divided physical memory, but stacks are, by convention, supposed to grow downward (or decreasing in address number), not upwards.
- To accommodate this difference, we will need some changes in hardware. On top of BB registers, we need one that says how the segment grows.
- A Sample Calculation:
  - STACK SIZE (MAX 4KB): 2KB
  - Virtual address is (0x3C00) -> 11 1100 0000 0000. We are in segment three (stack). This means that we want to be displaced 3KB.
  - We imagine that the base + MAX_SIZE of the segment is the actual translated base (the top, basically). So we imagine that the translate base is -4K from the 0KB base, and we add the offset to that, giving us a -1KB address.
- The book is confusing about this, but basically, imagine the max segment size, and subtract the offset we 'imagine' from the bottom, that is our virtual address.

## .4: Support for Sharing

- With segmentation, we also have the benefit of **sharing** certain memory fragments. In particular, **code sharing**.
- To do this, we will need some sort of **protection** bit to make sure that registers can only access segments they are permitted to.
  - Similar to **fault domains** in that one paper that can only access memory within its own segment. In that case, though, sharing was implemented through assigning the same mapping in every page table for every fault domain.
- In this case, we really just use an additional protection register that dictates what permissions this segment requires. Some have read-write, others have read-execute, others have read-only (good for code sharing, I assume that this is unique per segment).

## .5: Fine-grained vs Coarse-grained Segmentation

- **Coarse-grained** refers to chopping up memory into rather large pieces, smaller segments will usually refer to **fine-grained** segmentation.
- Supporting many segments needs a **segment table**, which allows a segment to be used to more flexible ways (recall the **segment descriptor** that existed for MULTICS). Each process had its own segment descriptor, noted by a **descriptor base register** that tells the hardware where to find this. This holds all the segments that the process owns, and you'd address each based on the offset in the segment descriptor. Each segment would then have its own page-table, which would be used to translate its virtual address to the physical address.

## .6: OS Support

- We focus on three issues:
  - **What should the OS do in the case of a context switch?**
  - **What should the OS do when the segment (and not just the pointer in memory that we use for stack) has to actually grow?**
  - **How do we manage and actually find free-space in memory?**
