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

| type       | name         |
| ---------- | ------------ |
| `spinlock` | uart_tx_lock |
| `char []`  | uart_tx_buf  |
| `uint64`   | uart_tx_w    |
| `uint64`   | uart_tx_r    |

### `uart_tx_r`, `uart_tx_w`, and `uart_tx_buf`

- This is used in tandem with the buffer size (in this case, 32) to simulate a 32 character queue in `uart_tx_buf`. Each time we add something to the buffer, we check the condition `uart_tx_w == uart_tx_r + 32`, meaning that the amount of characters in the buffer is 32 steps ahead of the read (meaning to say, there are 32 characters in the buffer, hence, we need to wait or stop the write).

### `uart_tx_lock`

- Like any other `spinlock`, it is used to guarantee atomicity in operations. See the functions below for where it is implemented.

## function definitions

### `void uartinit`

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

### Operation

### Serial Data Representation

- When we use the word **character**, we mean a word of data (we decide the length) being transferred. In the xv6 driver, we see that we select 8-bit words.
- The serial communication line (**what is the purpose of this?**) has a value of `1` when there is no data being communicated. The data that will be transmitted through the interface has the following format:
  - A start bit, `0`, is always sent first
  - 5 to 8 bits follow, the least significant one being sent first.
  - A parity bit may follow the data bits to provide error checking capability
    - **QUESTION: How does the parity bit work?**
    - This works by putting a `1` or `0` if the number of 1's being sent is either odd or even. The receiver does the same thing, and if the parity bit changes, there must be an error. This doesn't rule out errors, but helps with assuming that things actually work most of the time.

#### Transmission

- some other device -> uart -> main computer
- The data in the `THR` register (short for transmission holding register, you'll see it later) (data to `THR` was written a microprocessor (not the CPU)) is passed to the transmitter shift register wherein we make the bits follow the format described in [serial data representation](#serial-data-representation).

#### Reception

- main computer -> uart -> some other device
- The `RSR` (short for Receiver Shift Register) formats the data into parallel format. It is then transferred into the `RHR` (short for Receiver Holding Register) where it is read from the microprocessor.
- When a character is received, the following errors may occur:
  - **Overrun Error** - A character is assembled but there is no space to it (receiver holding register is occupied?) because the `RHR` wasn't read fast enough.
  - **Parity Error** - Parity bit does not align with the data that was read.
  - **Framing error** - The stop bit is not zero.
  - **Break interrupt** - The `rxd` line that transmits data has been kept at zero for a complete character time (meaning that no data has been sent or we keep sending a bunch of zeros?)

#### FIFOs

- The holding register for b

#### Interrupts
