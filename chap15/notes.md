# Chapter 15: Mechanism - Address Translation

- We focused on something known as **limited direct execution**
- The idea is that we let the program run directly on the hardware, however, at certain points due to interrupts or system calls, we make it so that the OS gets involved and makes sure the right thing happens.
- The idea with virtual memory is similar, we only let the OS get involved when it needs to.
- We use a process called **hardware-based address trnaslation**, wherein the CPU, via a register that points to some table, is in charge of transforming each memory access that uses a virtual address into a physical address.
- One important thing to remember is the concept of **interposition**
  - In virtualising memory, hardware **interposes**memory access and translates it accordingly. But this works with other things too i.e. the **Nooks** technology for isolation between drivers / kernel extensions.

## .1: Assumptions

- Firstly, we assume that the user's address space must be placed _contiguously_ in physical memory, and we also assume that all the address space is of the same size.

## .2: Example

- Observe the following example:

```c
void func() {
  int x = 3000;
  x = x + 3;
  ...
}
```

- We add the variable `x` into the stack and assign the memory value 3000 to it, then increment it by three. In assembly, this would be following:

```asm
  movl 0x0(%ebx), %eax ;load 0+ebx into eax
  addl $0x03, %eax ;add 3 to eax register
  movl %eax, 0x0(%ebx) ;store eax back to mem
```

- The program code imagines that the instructions are at address zero, but we virtualize the memory and place it someplace else in physical memory.

## .3: Dynamic (Hardware-based) Relocation

- This takes root in a 1950s idea calleed **base and bounds**, otherwise known as **dynamic relocation**. We will need two registers, the base register in the cpu, and the bounds or limit register.
  - Before the creation of this technique, software used **static relocation**. A software called the **loader** would ake in some program about to be run and rewrite addresses to the desired offset in phyiscall memory.
- The hardware, for each process, sets the base register and afterwards only uses that to offset all memory accesses.
- The **limit** register is used to make sure nothing goes beyond the program's address space.
- These are all part of the CPU, and the component that contains it is called the **Memory Management Unit**

## .4: Hardware Support - A Summary
