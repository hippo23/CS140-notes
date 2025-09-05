# Chapter 36: File System Devices

## .1: A High-Level Overview

- The CPU is the main block we want to connect things to, often, we have **high-performance** and **low-performance** connections.
  - The memory is connected the soonest or the fastest with a **memory bus**.
  - The graphics is next, often through a **general I/O bus**.
  - Last in the hierarchy are slow devices using the peripheral I/O bus i.e. SCSI, SATA, USB
- In actual use-cases, we have this kind of breakdown:
  - There is a special connection for each of the graphics component **(PCIE Graphics)** and memory component **(memory interconnect)**
  - Beneath that, we have a general bus called the **I/O Chip**, for example Intel's Direct Media Interface (DMI). This has ports for eSATA (Disks), Universal Serial Bus (USB), and Peripheral Interconnect Interface Express (PCIe)

## .2: A Canonical Device

- A device has two components, the hardware **interface** it presents to the rest of the system. Similar to software, hardware must present a kind of interface so that the system can control its operation.
- Second is the **internal structure**, which is implementation specific and abstracts all the complicated stuff about I/O devices from the system.
  - Some devices will havea few chips, others will have more complex devices i.e. RAID Controller (which, on its own, has a bunch of software (firmware, in this case)).

## .3 The Canonical Protocol

- For example, in a device interface, we have certain **registers**
  - **status register** -> Holds the current status of the device
  - **command register** -> Used to tell the device to perform a certain task
  - **data register** -> Write to pass data to the device
- Here is a sample interaction that the OS might have:

```c
While (STATUS == BUSY)
  ; // wait until device is not busy
Write data to DATA register
Write command to COMMAND register
  (starts the device and executes the command)
While (STATUS == BUSY)
  ; // wait until device is done with your request
```

- We can break this down into four steps:

1. OS has to wait until the device is free (**QUESTION: Is there where `sleeplocks` become useful?**). We repeatedly read the status register in something we call _polling_.
2. We send data to the data register; If the data is very large, we often do this in repeated writes. If this data is coming from the CPU, we call this **programmed I/O (PIO)**.
3. A command is written to the command register.
4. The OS waits for the command to finish **this is most likely where we need to switch or yield to a new process(?)**

## .4: Lowering CPU Overhead With Interrupts

- Instead of polling the device repeatedly, the OS can issue a request, put that process to sleep, and context switch to another task. A hardware interrupt will be issued (then, most likely, a wakeup call in xv6) once the task is finished. This causes the CPU to jump to a predetermined interrupt service routine (`kernelvec` or `uservec`), and using some `if` statements, we can find out it is from an external device
- Given that it is a disk write or something, we can react accordingly. This allows for **overlap of computation**.
  - This is only useful for longer tasks, overhead will outweigh external interactions with devices that are quick.
    - Sometimes, we can try and use a hybrid version where we wait for a little while before actually going to sleep.
  - Another reason to not use interrupts is **live-lock**, where we only find ourselves processing interrupts and nothing else, the OS just keeps jumping to the interrupt handler over and over.
- One optimization we could use is **coalescing**. In this setup, a device that delivers an interrupt needs to wait for a bit before raising it to the CPU. We essentially **buffer** this with other interrupts, allowing us to reduce the overhead of interrupt processing. Too much waiting time could lead to latency, however.

## .5: More Efficient Data Movement With DMA

- When transferring programmed I/O to the disk (something where the CPU needs to be actively engaged, especially if we are moving multiple blocks that require multiple repeated commands), we spend too much time on a trivial task.
- We need to write the data one word at a time (**QUESTION: Does that mean the DATA register of the device interface is rather large?**) and only when I/O on the disk is initiated can we move to new process.
- Instead, we use **Direct Memory Access (DMA)**, wherein the DMA engine is told where the data lives, how much data to copy, and which device to send it to. After that, the OS is done and works with another process. When the DMA is complete, an interrupt is raised so that the CPU knows the transfer accomplished.

## .6: Methods of Device Interaction

- **How does the OS actually communicate with the device?**
- There are two primary methods that are used:
  - **I/O instructions** -> Specify a way for the OS to send data to specific device registers and allow the construction of the above protocols.
    - An example is the `in` and `out` instructions that we see in x86. The caller specifies a register with the data in it, and specific _port_ (device) that we want to send it to.
  - **Memory-mapped I/O** -> The hardware makes device registers available as if they were memory locations. This is what is done in `xv6`, wherein the PLIC puts the data returned by device interfaces(?) into specific parts of memory, and we can read that for the causes of interrupts. **Is the case the same when sending data?**
    - The OS sends a write or loads something from a specific address, but the hardware routes this to the device instead of main memory.

## .7: Fitting Into The OS: The Device Driver

- **How do we fit devices with very different interfaces to interact with the CPU and hence the OS? The goal is to keep the OS device neutral.**
- Some software in the OS must know how the device works in detail, this is what we call a **device driver**.
- Imagine the file system stack of Linux:
  - **POSIX API <open, read, write, close, etc.>**
    - File System
    - Raw Interface (stuff like **file-system checker, or a disk defragmentation tool** use this to directly read and write blocks).
  - **Generic Block Interface <block read/write>**
    - Generic Block Layer
  - **Specific Block Interface <protocol-specific read/write>**
    - Device Driver <SCSI, ATA, etc.>
- Think of the GBI as a super-type of everything below it. The SBI can do everything the GBI does, but has to be dumbed down to ONLY using those that GBI supports.

## .8: Case Study: A Simple IDE Disk Driver

```asm
Control Register:
  Address 0x3F6 = 0x08 (0000 1RE0): R=reset,
    E=0 means "enable interrupt"
Command Block Registers:
  Address 0x1F0 = Data Port
  Address 0x1F1 = Error
  Address 0x1F2 = Sector Count
  Address 0x1F3 = LBA low byte
  Address 0x1F4 = LBA mid byte
  Address 0x1F5 = LBA hi byte
  Address 0x1F6 = 1B1D TOP4LBA: B=LBA, D=drive
  Address 0x1F7 = Command/status

Status Register (Address 0x1F7):
  7      6     5     4   3    2    1     0
  BUSY READY FAULT SEEK DRQ CORR IDDEX ERROR
Error Register (Address 0x1F1): (check when ERROR==1)
  7    6   5  4    3   2    1    0
  BBK UNC MC IDNF MCR ABRT T0NF AMNF

# BBK = Bad Block
#  UNC = Uncorrectable data error
#  MC = Media Changed
#  IDNF = ID mark Not Found
#  MCR = Media Change Requested
#  ABRT = Command aborted
#  T0NF = Track 0 Not Found
#  AMNF = Address Mark Not Found
```

- The `reset` portion of the control register is known as the **host software reset bit**, this just 'resets' the drive meaning that we abort any commands, restore the registers, etc.
- The rest are rather common sense.
