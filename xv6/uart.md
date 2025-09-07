# `uart.c`

## macros

```c
// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0 // receive holding register (for input bytes)
#define THR 0 // transmit holding register (for output bytes)
#define IER 1 // interrupt enable register
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#define FCR 2 // FIFO control register
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR (3 << 1) // clear the content of the two FIFOs
#define ISR 2                   // interrupt status register
#define LCR 3                   // line control register
#define LCR_EIGHT_BITS (3 << 0)
#define LCR_BAUD_LATCH (1 << 7) // special mode to set baud rate
#define LSR 5                   // line status register
#define LSR_RX_READY (1 << 0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1 << 5)    // THR can accept another character to send
```

- Just check the [documentation notes](#documentation-for-universal-asynchronous-receivertransmitter)

## global variables

| type       | name                 |
| ---------- | -------------------- |
| `spinlock` | uart_tx_lock         |
| `char []`  | uart_tx_buf          |
| `uint64`   | uart_tx_w            |
| `uint64`   | uart_tx_r            |
| `int`      | panicking & panicked |

### `uart_tx_r`, `uart_tx_w`, and `uart_tx_buf`

- This is used in tandem with the buffer size (in this case, 32) to simulate a 32 character queue in `uart_tx_buf`. Each time we add something to the buffer, we check the condition `uart_tx_w == uart_tx_r + 32`, meaning that the amount of characters in the buffer is 32 steps ahead of the read (meaning to say, there are 32 characters in the buffer, hence, we need to wait or stop the write).

### `uart_tx_lock`

- Like any other `spinlock`, it is used to guarantee atomicity in operations. See the functions below for where it is implemented.

### `panicked` & `panicking`

- `panic` is a function in `printf.c` that sets `panicking = 1` and `panicked = 1` (in succeeding steps, because there's a delay while we print an output to the console).
- It is only used in `uart` WHEN we know that an error has been printed. If an error has been printed, we don't want to print anything further to the console (so basically the shell hangs?).

## function definitions

### `void uartinit`

- Common sense, we're just setting up the registers with the bits we defined in the macros.

```c
void uartinit(void) {
  // disable interrupts.
  // we initially disable all interrupts so that
  // we don't receive any
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  // FIFO enable, meaning that we are in FIFO mode (there is a simpler mode)
  // that only allows transfer of 1-word instead of a 16-word buffer
  // Also notice the FCR_FIFO_CLEAR. The first time we connect, this clears the
  // FIFO buffer for both the transmitter and the receiver.
  // including the offset / pointer
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  // this refers to the FIFO buffer, and whether something can be read
  // or removed
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  // and of course give a name to the lock
  initlock(&uart_tx_lock, "uart");
}
```

### `void uartputc`

- We call this if we want to transmit something to the UART connection.
- If we are panicking (basically, if an error is happening) we want to stall writing to the console (basically, if we were trying to write something and some CPU threw an error, we want to stop that write while we can).
- **QUESTION: Why do we allow operation to continue even if we are currently panicking? What if we move pass `panicked` before that is set to one?**
  - Because! If you look at the `void panic` function in the kernel folder file `printf.c`, you'll notice that we are printing something TO the console. If we loop forever, then that panic message will never print.
  - Notice, however, that in that case, we do grab a lock, hence, no other CPU will be printing to the console. The panicking CPU is **GUARANTEED** to be the last printed message.
- Similar to the mechanisms described in the [global variables](#global-variables), we use `uart_tx_w` and `uart_tx_r` to simulate a queue.
  - If it is the case that the transmission FIFO queue is full, we sleep, waiting for a wakeup call to `uart_tx_r` (and, we also assume that we have locked `uart_tx_lock`).

```c
// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void uartputc(int c) {
  if (panicking == 0)
    acquire(&uart_tx_lock);

  // why are we spinning forever?
  if (panicked) {
    for (;;)
      ;
  }
  while (uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE) {
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  if (panicking == 0)
    release(&uart_tx_lock);
}
```

### `void uartputc_sync`

- You might be asking yourself the following two questions:
  - **QUESTION: What is the purpose of a `uartputc` that doesn't use interrupts?** -
    - Remember, UART is used to receive input from and give input to the console. But when we think about a console, we are not really talking about the 'terminal' that we are used to, but what we call a serial console. This, in the olden times, was an external device that read data 1-bit at a time and had the ability to display that. QEMU emulates this, but treats that serial console as our terminal. In reality, though, screens are far more complex and involve things like VGA connections, etc.
    - Nevertheless, we don't use interrupts for the case wherein the kernel (and not the user) is printing something itself (think of the `echo` command) to the 'serial console'. No interrupt will be generated when an `echo` command is executed by the kernel, hence, we need another way to ask xv6 to write it to the serial console.
  - **QUESTION**: Why do we not have (or not try) to acquire the buffer lock like we did in [uart_putc](#void-uartputc)?
    - My initial answer here was that **deadlock** was the reason. However, notice that with the implementation of [locks](/xv6/notes.md), we are guaranteed to turn off interrupts until we release a lock (now, granted, maybe we turn it on somewhere else in the code, but as far as I know we only do that in system calls beforehand or when preparing to return from an interrupt, as well as when scheduling a new process before turning it off immediately).
    - Another possibility is that it could be for immediacy. Notice that we only use [uartgetc](#int-uartgetc) for file writes (technically can also write to console (think standard output in C), but most of the time, it is used for files). `printf`, on the other hand, is something we want to see immediately when outputting something. Hence, we bypass the buffer and write straight ahead.
  - **QUESTION: Since we are bypassing the buffer in [uartput_sync](#void-uartputcsync), is it possible that there are two simultaneous writes going to the serial console? Like if another CPU was executing [uartputc_sync](#void-uartputcsync), what would happen? Would the other be messed up?**
  - There are two scenarios to consider here:
    - 1. The user is writing to the console using `user/printf` (which is actually built upon the write system call).
    - 2. Another CPU is writing from kernel mode using either `echo` or `kernel/printf.c`.
    - With this, I think this is an actual possibility, but I'll bring it up with Sir Wilson at consultation. What I do think is that it's not really that important to be in sync. `kerne/printf.c` is really just used for printing errors, and so a bit of a race doesn't matter, nothing else will break aside from the error given to the user.
- To be clear, this is used by `printf()` and `echo` statements. In either case, interrupts aren't guaranteed to be off. Hence, we need to turn off interrupts. Now, as for why we don't lock after we turn off interrupts? Because, well, there's nothing to interrupt us. No deadlock is gonna happen, so it's unnecessary.

```c
// alternate version of uartputc() that doesn't
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void uartputc_sync(int c) {
  if (panicking == 0)
    push_off();

  if (panicked) {
    for (;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  if (panicking == 0)
    pop_off();
}
```

### `void uartstart`

- A lot easier than the past functions. If we have written all that there is to be received (so basically, if all data has been sent to the kernel), then we return.
- If there is something we want to pass but the `LSR` register is not available yet (meaning that the FIFO is full), then we don't do anything again. It will interrupt (as discussed in the [documentation](#lsr-line-status-register)) again.
- Otherwise, there is something that we can write, so we write that value, increment our position in the queue, and wakeup any `uartputc` that may have been called but was stopped because there was no available space in our buffer (not the global buffer, our buffer).
- Then we actually write the data. No need to worry about race conditions, we always have the lock.

```c
// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void uartstart() {
  while (1) {
    if (uart_tx_w == uart_tx_r) {
      // transmit buffer is empty.
      return;
    }

    if ((ReadReg(LSR) & LSR_TX_IDLE) == 0) {
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }

    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;

    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);

    WriteReg(THR, c);
  }
}
```

### `int uartgetc`

- Straightforward, if a character can be read, then we read it. If not, return `-1` (used to break out of a constant reading loop in the next function).

```c
// read one input character from the UART.
// return -1 if none is waiting.
int uartgetc(void) {
  if (ReadReg(LSR) & LSR_RX_READY) {
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}
```

### `void uartintr`

- This is called from [traps](/xv6/traps.md). When this happens we want to acknowledge the interrupt coming from the UART connection (different from acknowledging the PLIC-level interrupt) so that another interrupt can come in when we are done.
- We try and read all characters using `uartgetc`. After that, we write as much as we can to the serial console using `uartstart()`.

```c
// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void uartintr(void) {
  ReadReg(ISR); // acknowledge the interrupt

  // read and process incoming characters.
  while (1) {
    int c = uartgetc();
    if (c == -1)
      break;
    consoleintr(c);
  }

  // send buffered characters.
  if (panicking == 0)
    acquire(&uart_tx_lock);
  uartstart();
  if (panicking == 0)
    release(&uart_tx_lock);
}
```

## Documentation for Universal Asynchronous Receiver/Transmitter

### Overview

- It has a **FIFO buffer** used for transferring data. Users can opt to either use it or not. In the case that they don't, they have a 1 byte character buffer to work with.
- **DMA** access is possible with two output signals that inform the DMA controller about when is new received data available and when the UART is able to accept new data for transmission.
- **NOTE: This is a SERIAL interface, meaning to say that we transfer information 1 byte at a time, though there can be buffers holding up to 16 bytes**

### DMA Interface

- We have two signals that can be sent in order to manage the operation of an external DMA controller.
- The signal `rxdry_n` is set to active when the UART has new data that can be read by the DMA controller. This may wait until a trigger level has been reached in rhe receiver's FIFO (**what? Is it like when there is a certain amount of characters left?**)
- The signal `txrdy_n` is set to active when UART is ready to receive new characters to be taken from the DMA controller.
- Additionally, we also have the `dma_rxend` and `dma_txend`, which inform via registers that a DMA transfer has finished for transmission or reception respectively.

### Serial Data Representation

- When we use the word **character**, we mean a word of data (we decide the length) being transferred. In the xv6 driver, we see that we select 8-bit words.
- The serial communication line (**what is the purpose of this?**) has a value of `1` when there is no data being communicated. The data that will be transmitted through the interface has the following format:
  - A start bit, `0`, is always sent first
  - 5 to 8 bits follow, the least significant one being sent first.
  - A parity bit may follow the data bits to provide error checking capability
    - **QUESTION: How does the parity bit work?**
    - This works by putting a `1` or `0` if the number of 1's being sent is either odd or even. The receiver does the same thing, and if the parity bit changes, there must be an error. This doesn't rule out errors, but helps with assuming that things actually work most of the time.

### Operation

#### Transmission

- main computer -> uart -> some other device
- The data in the `THR` register (short for transmission holding register, you'll see it later) (data to `THR` was written a microprocessor (not the CPU)) is passed to the transmitter shift register wherein we make the bits follow the format described in [serial data representation](#serial-data-representation).

#### Reception

- some other device -> uart -> main computer
- The `RSR` (short for Receiver Shift Register) formats the data into parallel format. It is then transferred into the `RHR` (short for Receiver Holding Register) where it is read from the microprocessor.
- When a character is received, the following errors may occur:
  - **Overrun Error** - A character is assembled but there is no space to it (receiver holding register is occupied?) because the `RHR` wasn't read fast enough.
  - **Parity Error** - Parity bit does not align with the data that was read.
  - **Framing error** - The stop bit is not zero.
  - **Break interrupt** - The `rxd` line that transmits data has been kept at zero for a complete character time (meaning that no data has been sent or we keep sending a bunch of zeros?)

#### FIFOs

- The holding register for both transmission and reception can either be a 1-word register or a 16-wrod FIFO register. Which one we utilise depends on if we enabled FIFO or not. We can disable FIFO for transmission and reception _separately_.
- Note that with the transmission FIFO, it is only 8-bits wide (we queue and only shift when we are about to send to the external device). The FIFO for the reception is 11-bits wide to accompany the serial format we outline earlier (when we receive new data, we shift it before putting it into the reception queue) **why is that????**.
  - The shift registers we mentioned are always used when we are about to make data LEAVE the UART connection. For example, if we receive data from an external device (that will be in serial format), we queue the serial (11-bit) data first before parallelizing it at the end to send to the main CPU.
  - In a similar manner, when we receive parallel data from the CPU, we keep it in the queue and only serialize data once we are about to send it to the external device.
- **QUESTION: Why are connected devices always serialized? Why don't we keep them in parallel?** The main point is **synchronization**. With serial transmission, it's very easy for us to know when the word starts and when the words end. On the other hand, we need to make sure that the bits of a parallel transmission at the same time (or at a reasonable gap from each other) so that we actually know what the word being transmitted is.

#### Interrupts

- The interrupts have the following sources.

| Level             | Interrupt                      |
| ----------------- | ------------------------------ |
| 1 (High Priority) | Receiver Line Status           |
| 2                 | Received Data Ready            |
| 3                 | Transmitter Holding Reg. Empy  |
| 4                 | Modem Status                   |
| 5                 | DMA Reception End of Transfer  |
| 6 (Low Priority)  | DMA Transmission End of Trans. |

- We can enable or disable these interrupts with the `IER` (short for interrupt enable register), which we will see in a bit.
- We signal the CPU whenever an enabled interrupt condition appears and reset ONLY when the CPU resets the interrupt (or interrupt source), this varies depending on the interrupt source.
- There is also a _global interrupt enable_ in the third bit of the `MCR` (my chemical romance???) register.
- Lastly, the current interrupt status of the UART can be read from the `ISR` (short for interrupt status register). This is how xv6 can determine what interrupt is happening and what to do.

##### Receiver Line Status

- Interrupt is generated when an error is seen in the received data.
- This interrupt is related with bits 1 to 4 of the `LSR` (short for Line Status Register).
- The errors here are related to those we saw at [reception](#reception)

##### Received Data Ready / Reception Timeout

- This means that: **there is data to be read from the receiver's FIFO (meaning that the CPU can read data that is in the `RHR`)**.

##### Transmitter Holding Register Empty

- In non-FIFO mode, this will be generate when the THR is empty. In FIFO mode, the interrupt will apear when the actual transmission FIFO is empty, meaning to say that we can write 16 words again.
- **QUESTION: So does that mean it is 1 if there is only 1 word in the FIFO queue?**
- This is directly related to the value of the `THR` empty bit in the `LSR`.

##### DMA Reception End of Transfer

- This is used in the case that the UART is connected to a DMA controller, and tells the DMA controller that we are done transferring data TO the DMA controller (and thus, to the memory). The DMA can then notice this and also raise an interrupt to the CPU itself, from what I understand. Using that, we can then read the data (i.e. the user input from the console).

##### DMA Transmission End of Transfer

- In a similar manner, this will happen when we are done transmitting data from the DMA controller into whatever place we have connected UART to (i.e. the console).

### Registers

#### `RHR` (Receiver Holding Register)

- Common sense, holds word to be received.

#### `THR` (Transmission Holding Register)

- Common sense, holds word to be transmitted.

#### `ISR` (Interrupt Status Register)

- bits 6 - 7: We can enable FIFO's for transmission and reception.
- bits 4 - 5: tells us the completion status of a DMA transfer (is this not redundant with the interrupt identification codes from bits 3 -1)
  - **NOTE: This does not make the priority levels that talk about DMA completion redudant. If we have more than one interrupt, the priority level will be overwritten with the higer priority interrupt, but we eventually want to service the DMA as well.**
  - Maybe if you are
- bits 3- 1: We can have the 6 different priority levels given that we described earlier.
- bit 0: Tells us if there is any pending interrupt. `0` means that an interrupt is pending.

#### `IER` (Interrupt Enable Register)

- bit 7: enables interrupt when DMA transfer is done
- bit 6: interrupt for when DMA reception is done.
- bit 4-5: not used.
- bit 3: Modem status interrupt
- bit 2: Receiver line status interrupt (so if we can receive some data).
- bit 1: `THR` empty interrupt (meaning we can send some data)
- bit 0: enables the data ready interrupt (there is some overrun, parity, framing, or break interrupt error)

#### `FCR` (FIFO Control Register)

- bits 6 - 7: Trigger level for the receiver's FIFO. An interrupt will be triggered if the number of words suprasseses trigger level (basically some kind of buffering).
- bit 5: No use.
- bit 4: Enables DMA end signaling (different from interrupts).
- bit 3: Selects the DMA mode (did not go over this).
- bit 2: `1` in this bit resets the FIFO of the transmitter (removes all the data inside), where it is then set to zero afterwards.
- bit 1: `1` resets the receivers FIFO. Same operation as the transmitter.
- bit 0: `1` enables both the transmitter and receiver FIFOs.

#### `LCR` (Line Control Register)

- bit 6: `1` forces a break condition in the transmission line (was not discussed, beyond coverage).
- bit 3 - 5: Select the way in which parity control or not i.e. whether we use it, what value is for odd and what is for even, and whether or not we force a value for the parity bit.
- bit 2: Selects the number of stop bits to be transmitted.
- bit 0 - 1: Word length (from 5-bits to 8-bits in parallel).

#### `LSR` (Line Status Register)

- When we look at the interrupt variation in [Interrupt Status](#isr-interrupt-status-register), for all interrupts that are of the kind RECEIVER LINE STATUS, we look here to gain more information. This is where we see the overrun flag, parity error flag, etc. Just see the docs for all the interrupt flags we can find here.
