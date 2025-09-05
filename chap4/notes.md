# Chapter 4: Processes
## .0 Important Terms
- **Time Sharing** -> Technique used by an OS to share a resource.
- **Space Sharing** -> A resource is divided in space among those who use to wish it.
- **Context Switch** -> Ability to stop running one program and start running another.
- **Policies** -> The philosophy behind certain decisions made by the computer. What program should go first? What can wait ’till later? What can be masked by the computer? What error needs attention immediately?
- **Lazy Loading**
  - Only loading the bytes of memory that are needed by the process.
  - Involves the mechanisms of *paging* and *swapping*.
- **File Descriptors**
  - Used for I/O, helps read input from terminal or from whatever file the process is accessing.
- The **context**, as seen above, is where the data that the process is using is stored. In a context switch, the CPU can go back to this if needed.
- Some other states are visible, such as zombie if a process is done running but has not yet been cleaned up.
- **Trap frame** -> Maybe what we are waiting for if the process is blocked?
## .1: What is a Process?
- A process is understood by it s machine state, what can a program do when it is running. Likewise, what parts are important to its execution?
- The memory, for example, that the process can access is called its *address space*.
- Another important part is *registers*, and how a program reads and/or writes to them.
  - **Program Counter**
  - **Stack Pointer** & **Frame Pointer**
- I/O information, the storage devices that the program is accessing. We need a list of files (*the file descriptor*) to keep track of this.
## .2: API
- The interface of an operating system, how languages and applications interact with the computer via the OS. It typically includes these commands
  - **Create** -> Create a new process, in C, it is `c fork()`
  - **Destroy** -> Kills a process, `c kill()`
  - **Wait** -> A process needs to stop running, perhaps while waiting for I/O
  - Miscellaneous Control -> Additional commands such as a `c wait()` according to a timer.
  - **Status** -> Status information about a process.
## .3: Process Creation
- First, the OS loads code into memory. This will typically follow the **ELF** format in UNIX systems, wherein code is translated into a format that allows it to be loaded into any address space, and then into an executable.
- Modern OS’s use the concept of *lazy loading* (see terms above).
- Once process is in memory, space is allocated for the run-time stack (think the stack in assembly, each function call having its own portion of the stack when we need to preserve data upon moving to another part of the code).
  - Initialised with `c argc` and `c argv` from c.
- Additional memory is also given to the **heap**
  - Memory is given to the programmer, i.e. `c malloc()` and its opposite, `c free()`. This is where we store the various variables in assembly if they are not in registers.
- File descriptors are initialised, *three* of them to be exact.
## .4: Process States
- **Running** -> The process is running and executing instructions
- **Ready** -> Ready to run but the OS has chosen not to run it.
- **Blocked** -> Some kind of operation has performed that renders the process unready to run until something else happens i.e. waiting for I/O.
## .5: Data Structures
- A *process list* is kept by the OS for all that are ready to run and some additional info to know which one is running.
  - Also known as a *Process Control Block* or *process descriptor*.
```c
// the registers xv6 will save and restore
// to stop and subsequently restart a process
struct context {
  int eip;
  int esp;
  int ebx;
  int ecx;
  int edx;
  int esi;
  int edi;
  int ebp;
};
// the different states a process can be in
enum proc_state { UNUSED, EMBRYO, SLEEPING,
RUNNABLE, RUNNING, ZOMBIE };
// the information xv6 tracks about each process
// including its register context and state
struct proc {
  char *mem;
  uint sz;
  char *kstack; 

  enum proc_state state; // Process state
  int pid; // Process ID
  struct proc *parent; // Parent process
  void *chan; // If !zero, sleeping on chan
  int killed; // If !zero, has been killed
  struct file *ofile[NOFILE]; // Open files
  struct inode *cwd; // Current directory
  struct context context; // Switch here to run process
  struct trapframe *tf; // Trap frame for the
  						// current interrupt
};
```
- The **context**, as seen above, is where the data that the process is using is stored. In a context switch, the CPU can go back to this if needed.
- Some other states are visible, such as zombie if a process is done running but has not yet been cleaned up.
- **Trap frame** -> Maybe what we are waiting for if the process is blocked?
## Unclear Parts:
- What is a frame pointer???

# Additional Reading
## The Nucleus of a Multiprogramming System
- See paper (here)[https://dl.acm.org/doi/pdf/10.1145/362258.362278]
### Abstract
- A fundamental set of primitives will be used that allow the dynamic creation and control of a hierarchy of processes as well as the communication among them.
### Introduction
- *An OS should be able to change the mode of operation it controls*
  - A computer should not be limited only to batch processing, priority scheduling, real-time scheduling. In other words, the WAY in which the OS is going to be used.
### System Nucleus
- Idea was the focus on the main parts of controlling an environment of parallel, cooperating processes.
- Needed to define, firmly, what exactly a process was.
- Find a set of primitives for the synchronisation and transfer of information among parallel processes.
- Finally, a rule for the lifecycle of a process.
### Processes
- **Internal Process** vs **External Process**
  - Internal is the execution of 1+ programs in a storage area, with their own PID that other process use to communicate with it.
  - External process is basically I/O. NOT a peripheral device, but the input or output of data from a given document
  	- Document being the external data stored in a physical medium i.e. HDD
    - These also have a unique PID.
- The communication of these processes are coordinated by the system nucleus, an interrupt response program with control of I/O, storage protection, and the interrupt system.
- Notice the analogous relationships of *stores & peripherals, programs & documents, and internal & external processes.*
### Process Communication
- Acc. to Dijkstra, it is possible to construct sufficient primitives solely from a 0 and 1 binary semaphore with indivisible lock (attach or enter a certain mode) and unlock (detach or exit a certain mode).
- As opposed to this, the paper proposes using **message buffers** and a **message queue** for each process as the means of communication. The following are the available primitives:
  1. ***Send Message*** (receiver, message, buffer) -> Copies data into the first available buffer in a pool and into the queue of the receiver
  2. ***Wait message*** (sender, message, buffer) -> Delays the requesting process until a message arrives in its queue, once it has the name of the sender, the contents, and the identity of the buffer, it continues. Buffer is removed from the queue and is prepared for an answer.
  3. ***Send answer*** (result, answer, buffer) -> Copies an answer into a buffer that was received from a message. Sender of message is activated if it is waiting for an answer. The answering process continues immediately. *What does result do here???*
  4. ***Wait answer*** (result, answer, buffer) -> Delays the requesting process until an answer arrives in a given buffer. This answer is to be copied into the process and the buffer returned to the pool. Result specifies whether it is from another process or a dummy response from the system nucleus.
- ***Wait message*** forces a first-come, first-serve behaviour. However, the system has primitives that enable a process to wait for the arrival of the next one or to serve the queue in any order.
- This system is *dynamic* in the sense that a process can be created or be removed at any time. The only way for one process to know of the existence of another is through a mutual buffer.
- In the occasion of erroneous processes, we need make sure that no process can interfere with the conversation of another two processes. This is done by storing the identity of the sender and receiver.
- Through the removal and queuing of a buffer whenever a process is waiting for a message or when an answer is received helps with efficiency.
- To deal with a finite number of buffers, there is a limit on how many buffers a process can have at a single point in time.
### External Processes
- We also use ***send message & wait answer*** here for communication between internal and external processes.
- For each kind of external process, the nucleus has a piece of code (*probably a driver???*) that interprets a message from an international process an initiates I/O using a storage area specified in the message. Once this is terminated by an interrupt, an answer is generated for the internal process with info on block size and possible error conditions.
- External processes are created on request from internal processes. *Creation* is the assignment of a name to particular peripheral device.
- If we think about keyboards, if I understand correctly, the driver is the internal process, asking the nucleus(?) for an I/O with the keyboard. Eventually, when an answer is generated from the external process, the driver receives it?
  - *This is wrong, remember the concept of interrupt routines. When the CPU finds the interrupt from the device, a specific interrupt routine is started. This routine is usually the driver code.*
### Internal Processes
- The set of primitives that allow the *creation*, *control*, and *removal* of internal processes.
- They are created on the request of other internal processes.
- **Creation** -> Involves the assignment of a name of a contiguous storage area selected by the parent process that is under the parent’s own area.
- The parent process can then load a program into the child process and start it.
- On request from a parent process, the nucleus waits from the completion of all I/O in the child process and stops it (it can receive answer and messages in its queue).
- In this philosophy, processes should have complete freedom of program scheduling, the nucleus is only in charge of creating and deleting them, as well as the communication between them.
### Process Hierarchy
- We still need, as part of the system, programs that control strategies of operator communication, scheduling, and resource allocation.
  - It is essential here, that the said programs be implemented as other programs, and that the difference between production programs and OS’s is one of jurisdiction only.
  - To do this, we organise processes into *hierarchies of parent-to-child*.
  > "After initial loading, the internal store contains the sys-
  >  tem nucleus and a basic operating system, S, which can
  >  create parallel processes, A, B, C, etc., on request from
  >  consoles. The processes can in turn create other processes,
  >  D, E, F, etc. Thus while S acts as a primitive operating
  >  system for A, B, and C, these in turn act as operating sys-
  >  tems for their children, D, E, and F."
- 
