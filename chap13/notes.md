# Chapter 13: Address Spaces

# Additional Readings

## Valgrind: A Framework for Heavyweight Dynamic Binary Instrumentation

### .1: Introduction

#### .1: Dynamic Binary Analysis and Instrumentation

- These analyze programs at run-time at the level of the machine code.
- These are implemented using **dynamic binary instrumentation**, whereby the analysis code is added to the original code of the client-program at run-time.

#### .2: Shadow Value Tools and Heavyweight DBA

- These **shadow tools**, purely in software, shadow or mimic every register and memory value with another value that says something about it. As an example, we have the following:

1. `Memcheck` -> Uses shadow values to track which bit values are undefined (i.e. uninitialized) and can detected dangerous uses of undefined values.
2. `TaintCheck` -> Tracks which byte values are tainted via an untrusted source, or derived from tainted values.
3. McCamant and Ernst's secret-tracking tool tracks which bit values are secret and determines how much information about secret inputs is revealed by public outputs.
4. `Hobbes` -> Tracks each value's type determined from operations and can detect following operations that do not suit the values type.
5. `DynCompB` -> Determines abstract types of byte values, for program comprehension and invariant detection purposes.
6. `Annelid` -> Tracks which word values are array pointers and can help track bounds errors.
7. `Redux` -> Creates a _dynamic dataflow graph_ that visualizes the computers computation.

- For all of these tools the shadow value just records an approximation of each value's history. These could be a shadow-bit, shadow-byte, or a shadow word. In any case, a lot of times they detect operations on values like the above and see if it indicates a program defect.

#### .3: Contributions

- I aim to fulfill the following goals in this paper:

1. _Characterize shadow value tools_
2. _Show how to support shadow values in a DBI framework_
3. _Show that DBI frameworks are not all alike_

### .2: Shadow Value Requirements

- There are three characteristics of program execution that are relevant to shadow value tools: (a) programs maintain state, _S_, a finite set of _locations_ that can hold values (think the context of a process), (b) programs execute operations that read and write _S_; and (c) programs execute operations that make memory locations active or inactive.

- With that, we now try and group the requirements of a shadow value:

#### Mimicking _S_

##### R1: Provide Shadow Registers

- A shadow value tool needs to be able manipulate shadow register values like a regular register. It has to juxtapose two sets of register values without breaking the execution of the program.

##### R2: Provide Shadow Memory

- A shadow value must partition the address space between the shadow state and the regular process state, and must access shadow memory safely without interfering with other parts of the process.

#### Read and Write Operations

##### R3: Instrument read/write Instructions

- Most instructions access registers and many access memory. Some tool must instrument some or all of them, and needs to know which locations are being accessed within the program.

##### R4: Instrument read/write system calls

- Similar to the last one, we need to know which locations are being accessed by system calls.

#### Allocation and Deallocation Operations

##### R5: Instrument Start-up Allocations

- All the registers in a system are 'allocated' (perhaps it means when we shift into the process via context switch? Or each time we create a new process with each register being given a value). Statically allocated memory (the stack) also needs to be tracked. It must also have shadow values for memory locations that haven't even been allocated (in case they are later accessed erroneously before being allocated).
  - Memory that hasn't been allocated could be a null pointer, for example.

##### R6: Instrument System Call (de)allocations

- `brk` and `mmap` allocate memory, `munmap` deallocates memory. A shadow value must track these locations in memory and when they are freed.

##### R7: Instrument stack (de)allocations

- Stack pointer updates in the machine code (statically allocated memory being freed or when we go into another scope, for example) must also be tracked. Some programs even switch between multiple stacks (can they, don't all processes have only one single process stack? Or could it be referring to when a program forks).

##### R8: Instrument heap (de)allocations

- Calls like `brk` and/or `mmap` take memory from the heap and are dynamically allocated. Each block will have metadata attached to it.
- A shadow value tool will need to track the allocations, treat heap operations as atomic (meaning to say that, you know how heaps allocate pages of certain size, and maybe the kernel could allocate a bunch at once, the shadow tool should only care about when it is handed to the pointer as active)--**ATOMIC** in the sense that it becomes active here, and until that point, it is not.

#### Transparent Execution, But With Extra Output

##### R9: Extra Output

- A shadow-value tool needs a side-channel for its output (shadow value memory?), or a little used file descriptor i.e. `stderr`.

### .3: How Valgrind Works

#### .1: Basic Architectue

## The Motivation For Time-sharing

- Batch-processing slows down development and isolates the user from elementary cause-and-effect relationships. The solution to this problem was called **time-sharing**.
- The goal was to time-share computers so that we could have not just one, but multiple users all using a computer at once.

### System Requirements

- We notice the following phenomena when it comes to large, _powerful_ computer installations.

1. There are incentives to have the biggest computer that people can afford.
2. The capacity of a computer installation must be able to grow to meet the demand it serves. It was seen at the time that multiple-access systems for only a few hundred simultaneous users can surpass any single-processor system.
3. Computers must continue to evolve into a primary working tool, and thus, needs as little interruptions as possible.
4. Traffic between the user and secondary storage and terminals provides a need to avoid wasting main-processor time during an input/output request.
5. A major goal of the present effort is to provide multiple access to a growing and
   potentially vast structure of shared data and shared program procedures.

## The MULTICS System

### Hardware

- It was first made for the GE 645 computer, as its ancestor already had support for multi-processors (multiple CPUs), memory modules (think expandable memory), and multiple I/O controllers (disks, keyboards, mice).
- There is no physical path between the processors and the I/O equipment. It only works through **_mailboxes_** in the memory modules and by corresponding interrupts.
- Major modules communicate with each other on an asynchronous basis.
- Perhaps the most novel feature was **instruction addressing**
  - A two-dimensional addressing system was incorporated, similar to that of **Atlas**. This is the earliest form of **virtual memory**.
  - The system is organized into program segments, each of which contains an ordered sequence of words with a conventional linear address. These are paged at the discretion of the supervisor program with either 64 or 1024-word pages.
  - **Only segments are visible to the user, pages are invisible**

- The main benefits of this two-level addressing approach is the following:

1. Any single segment can grow or shrink during execution
2. By specifying a starting point in the segment, the user can operate a program without prior planning of the segments needed or of the storage requirements.
3. The largest amount of code that is bound together is only a single segment.
4. Is the only way to permit pure procedures and data bases to be shared among several users simultaneously. Pure procedure programs do not modify themselves. Hence, programs that use this only need one copy of a jointly used procedure.

- Paging, on the other hand, provides the following

1. Flexible techniques for dynamic storage management (think about using `kalloc()` in xv6. We can easily allocate and free memory purely by moving the allocation pointer around).
2. The mechanism of paging can allow the operation of incompletely loaded programs, The processor need only retain in main memory the more active pages (the program address space is divided into pages, We only really check a page at a time, the rest do not matter).

- One critical feature to both of these features is the **descriptor bit** mechanism which controls the access of processors to the memory. One descriptor bit allows declaring some segments to be 'execute-only', 'read-only', 'data-only'.
- Additionally, descriptors allow:
  - Supervisor modules can be written with the same descriptors as user programs.
  - **QUESTION: What is meant by this sentence?** Refer to [this section](#understanding-descriptor-tables).
  - What I understand is that we can have a descriptor for all, and access is managed via that descriptor.

### System Design of a Computer for Time-Sharing Applications

#### Scope of Extension of GE 635

1. A new I/O control unit was designed to integrate the control of standard peripheral devices.
2. A faster drum system.
3. Addressing logic with paging and segmentation was introduced. With this, interrupt logic and related portions of the machine.

### The Segments and Pages

#### Pages

- We make the allocation of physical memory easier by dividing virtual address space into pages. Each time we need a new space in memory, we use `kalloc()` in RISC-V and return the pointer. All access thus forward can be made using the page-table and a proper offset.

#### Segments

- Not for the allocation of memory, but for the allocation of address space. A segment defines some object such as a data area, a procedure or the like. Each segment CORRESPONDS the virtual memory.
  - Although a large number of segments may be defined, each one having a large number of words, only the currently referenced pages of pertinent segments need to be in memory at any time.
  - **TASK: Can you explain this in XV6 terms?**
    - If I understand correctly, each segment is a portion in virtual memory allocated to a process. In this sense, there is only one virtual memory pointer (unlike in the actual XV6 implementation, wherein each would have its own 'copy' of virtual memory).
    - Additionally, each segment can have pages, and these have their own page-table for translation of the addresses used by the segment.

### Descriptors

- Furthermore, each descriptor (both of the page and segment kind) contains certain access control information, and help with protection. For example, there is the **Write-Permit-Bit**.
  - Another example, imagine that we have a segment that is read-and-write, but a page that it refers (its page descriptor) is read-only. Hence, any process using that segment can only read the data in the physical address that the page translates to.

### The Descriptor Segment and Base Registers

- All the segment descriptors associated with a given process are contained in a single segment known as the descriptor segment.
- Its location in memory is defined by a **descriptor base register**, that tells the processor where exactly in memory this descriptor segment can be found (I assume that its page-table could be similar to the kernel page table in XV6, wherein it is the only directly-mapped one, or just the first one allocated during boot-up).
- When addressing a location in virtual memory, the first 18 bits are dedicated towards the segment number relative to the offset in the descriptor segment, the last 18 bits are the displacement within that segment.
- When addressing in memory, the three MSB were used to select **address base registers** that the process could immediately access. If they wanted to access more, they would have load them into the **address base registers** i.e. for accessing a specific routine.
- Each of the 8 base registers in the 645 had two modes, **paired** and **unpaired**. Paired base registers held a segment number, unpaired held an offset. You would typically use them together when trying to address some location in memory.

      "Multics needed the ability to have indirect words that contained segment numbers. This was accomplished by taking an unused value in the IT tag space. The tag was named "ITS"; this stands for "indirect thru segment". It was heavily used. Everyone who worked on Multics knew about "ITS pointers", and everyone who programmed eventually learned the octal value of the ITS tag: 43. When the ITS tag was present in an indirect word, it meant that this indirect word contained a segment number and the following indirect word contained the word address. There was also an ITB ("Indirect Thru Base") tag that took the segment number from a base register instead of memory. It was rarely used (I found a use for it in the code generator I wrote for my undergraduate thesis, but that is another story). On the 6180 the ITB tag was renamed ITP but the function stayed the same."

- Refer to [Figure 6](https://multicians.org/pg/mvm.html) in this resource to see how the address-base pairs are used to decode the segment and displacement within that segment.

## Understanding Descriptor Tables

- Helps with **interrupt service routines** and **memory management**.
- Each entry in the table contains a descriptor, which contains information about a single object i.e. a service routine, a task, a chunk of code or data. For example, loading a **segment register**.
- These tables are only placed once during boot time, and are then edited later when needed.
- When referring to a descriptor table, we use a **descriptor selector**.
- The idea behind a descriptor table is such that for each segment, we create a descriptor with specific bits specifying stuff like access privilege.
  - Each entry should have a **base** and a **limit** that only exposes that region of memory to the CPU. The user addressing relative to the current selector base.
