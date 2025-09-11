# `virtio_disk.c` and `virtio.h`

## qemu documentation

### basic facilities of a `virtio` device

#### .1: device status field

- The _device status_ provides a low-level indication of the completed steps of the following sequebce. The following bits are defined (in the order that they would typically be set):
  - **ACKNOWLEDGE (1)** - The guest OS house found and recognized it as a valid device.
  - **DRIVER(2)** - The device knows how to drive the device.
  - **FAILED(128)** - Indicates that something went wrong with the OS, and that it has given up on the device.
  - **FEATURES_OK (8)** - Indicates that the driver has acknowledged all the features it understands, and feature negotiation is complete.
  - **DRIVER_OK (4)** - Indicates that the driver is set up and ready to drive the device.
  - **DEVICE_NEEDS_RESET (64)** - Indicates that the device has experienced an error from which it can't recover.

##### .1.1: driver requirements

- The driver must accomplish the following:
  - update _device status_ to indicate the completed steps of the sequence specified in 3.1
  - The driver cannot clear any device status field. If one is set i.e. **FAILED**, the driver has to reset device before attempting to re-initialize.
  - driver should not rely on completion of operations if **DEVICE_NEEDS_RESET** is set.

##### .1.2: device requirements

- device must initialize _device status_ to zero 0 upon reset.
- _must not_ consume buffers or send any used buffer notifications to the driver before **DRIVER_OK** is set.
- the rest is common sense.

#### .2: feature bits

- device offers all the features it understands, the driver will read this and tell the device the subset that it accepts.
- the bits sent are divided into the following:
  - 0 - 23 -> feature bits for the specific device type
  - 24 - 37 -> feature bits reserved for extensions to the queue (**what is the queue?**) and feature negotiation mechanisms.
  - 38+ -> feature bits reserved for future extensions
- are these under some standard dictated by IEEE? Nope, it really depends on who implemented the virtual hardware. But most follow this site, OASIS.

##### .2.1: driver requirements: feature bits

- driver must not accept non-existent features, and must not accept a feature which requires another feature which was not supported.
- driver should go into backwards compatability mode if it finds a feature it does not understand

##### .2.2: device requirements

- device must not offer a feature which requires another feature which was not offered.
- device SHOULD accept any valid subset of features the driver accepts, otherwise it MUST fail to set the **FEATURES_OK** _device status_ bit.

##### .2.3: legacy interface: a note on feature bits

- transitional drivers MUST detect legacy devices by detecting that the feature bit `VIRTIO_VERSION_F_1` is not offered (i assume that means that bit is the newest version).
- transitional devices must detect legacy devices by detecting that `VIRTIO_VERSION_F_1` has not been acknowledged by the driver.

#### .3: notifications

- there are three types of notifications.
  - _configuration change notifications_
    - sent by **devices**, with the recipient being the driver.
    - a configuration change indicates that the device configuration space has changed
  - _available buffer notification_ (buffer for the device to consume)
    - these are sent by the driver, the recipient is the device. This type of notification indicates that a buffer may have been made available on the virtqueue designated by the notification.
  - _used buffer notification_
    - sent by **devices**, with the recipient being the driver.
    - used buffer notification indicates that a buffer may have been used on the virtqueue designated by the notification.

#### .4: device configuration specs

- used for rarely-changing or initialization-time parameters. Where configuration fields are optional, there existence is indicated by feature bits.
- The device configuration uses the litt-enedian format for multi-byte fields.
- Each transport also provides a generation count for the device configuration space, which will change whenever there is a possibility that two accesses to the device configuration space can see different versions of that space.

##### .4.1 device requirements: device configuration space

- the device must allow reading of any device-specific configuration field before **FEATURES_OK** is set by the driver.

##### .4.2 driver requirements: device configuration space

- drivers must not assume reads from fields greater than 32 bits wide are atomic, nor are reads from multiple fields: drivers SHOULD read device configuration space fields like so:

```c
u32 before, after;
do {
  // this is a counter on the device that is incremented each time a driver
  // access its configuration space
  before = get_config_generation(device);
  // read config entry/entries.
  after = get_config_generation(device);

  // if they are not equal, then we have to redo the reading of congiuration space.
} while (after != before);
```

#### .5: virtqueues

- This is the mechanism for bulk data transport on `virtio` devices.
- To make a request, we add a buffer to the queue, then we signal to the device in a driver event saying that there is an available buffer.
- To receive a request, the device executes it and adds a used buffer to the queue, triggering a used buffer event.
- We get a report of how many bytes were used, known as the "read length."

- Each of the virtual queues have three parts:
  - **descriptor area** -> used for describing buffers (remember how segment descriptors exist? it's like that. I'd assume that, that there will be some number or displacement that tells us where to find a specific buffer in the driver area, and device area (for when we want to see used buffers)).
  - **driver area** -> extra data supplied by driver to the device
  - **device area** -> extra data supplied by device to the driver

- **QUESTION: So where is the actual data being stored?**

#### .6: split virtqueues

- Each part of the virtual queue is writable either through the device or through the driver, but never both.
- multiple parts will have to be updated when making a buffer available and when marking it as used.
- each queue has a 16-bit queue size parameter, which sets the number of entries and implies the total size.

- each part of the virtual queue that we described [above](#5-virtqueues) are contiguous parts in memory of the OS. Each of them have different alignment requirements, as follows:

| virtqueue part   | alignment | size               |
| ---------------- | --------- | ------------------ |
| descriptor table | 16        | `16*(queue size)`  |
| available ring   | 2         | `6+2*(queue size)` |
| used ring        | 4         | `6+8*(queue size)` |

- Queue size refers to the maximum number of buffers in the queue:
  - **QUESTION: Can you tell me why the sizes aren't always equal?**
    - Each buffer gets a descriptor table entry, each also get an available ring and used ring (but why is it twice the amount of used rings). Also, why is there a +6 for the last two?
- One thing you should notice is that the `MAX_QUEUE_SIZE` is actually 2^15. This is to avoid the case wherein, if the two values are equal, we don't know if we've read all there is in the buffer, or if we haven't read anything at all. As opposed to this, if we leave the last bit to tell us what wrap we are in, we know that if the addresses are equal but the wrap-bit is different, we've lapped the reading bit.
- Of course, if we keep continuing, we'll eventually run into the issue but, well, at that point its your fault for not catching it beforehand.

##### .6.1: driver requirements: virtqueues

- The driver must ensure that the physical address of the first byte of each virtqueue part is a multiple of the specified alignment value in the above table.
  - If i may give my reason, I assume that somewhere in the code, we use modulo to check something related to the buffer i.e. adding a new buffer and making sure that the address we put it at is page-aligned, it'd be harder if to begin with the first address was not divisible.

##### .6.4 message framing

- the framing of messages with descriptors is independent of the contents of the buffers (what?). basically, for example, if we have a network packet with a header of 12 bytes and a body or whatever of 1514 bytes, we can opt to either 1) put it all together; 2) separate it; or 3) divide it arbitrarily. I wonder though, if you found the network header, would it tell you the descriptor index of its actual content?

##### .6.5 the virtqueue descriptor table

- the buffers the driver us using for the device. its structure is the following:
- notice the macros, the first tells us whether or not we are chaining more buffers together, the read-write permissions, and [indirect descriptors](#653-indirect-descriptors)

```c
struct virtq_desc {
  /* Address (guest-physical). */
  le64 addr;
  /* Length. */
  le32 len;
  /* This marks a buffer as continuing via the next field. */
  #define VIRTQ_DESC_F_NEXT 1
  /* This marks a buffer as device write-only (otherwise device read-only). */
  #define VIRTQ_DESC_F_WRITE 2
  /* This means the buffer contains a list of buffer descriptors. */
  #define VIRTQ_DESC_F_INDIRECT 4
  /* The flags as indicated above. */
  le16 flags;
  /* Next field if flags & NEXT */
  le16 next;
};
```

###### .6.5.2 driver requirements: the virtqueue descriptor table

- drivers MUST NOT add a descriptor chain longer than `2^32` bytes in total; this implies that the loops in the descriptor chains are forbidden
- if `VIRTIO_F_IN_ORDER` has been negotiated, driver must set _next_ to 0 for the last descriptor in the table and to `x+1` for the rest of the descriptors.
  - `x` refers to the offset of a descriptor with `VRING_DESC_F_NEXT` set in _flags_.

##### .6.5.3 indirect descriptors

- some devices benefit by oncurrently dispatching a large numnber of requests. The `VIRTIO_F_INDIRECT_DESC` feature is what allows this.
- If you look at the [structure](#65-the-virtqueue-descriptor-table) of the descriptor table, you'll see that the `addr` attribute will lead to another list of actual buffers should the `VIRTQ_DESC_F_INDIRECT` flag bit be set.
- the structure of the buffers that we find at `addr` is the following:
- each descriptor is 16 bytes each (why 16 bytes?)

```c
struct indirect_descriptor_table {
  /* The actual descriptors (16 bytes each) */
  struct virtq_desc desc[len / 16];
};
```

##### .6.6 the virtqueue available ring

- the available ring has the following structure:

```c
struct virtq_avail {
  #define VIRTQ_AVAIL_F_NO_INTERRUPT 1
  le16 flags;
  le16 idx;
  le16 ring[ /* Queue Size */ ];
  le16 used_event; /* Only if VIRTIO_F_EVENT_IDX */
};
```

##### .6.6 the virtqueue used ring

- the virtqueue used ring has the following structure:
- the `idx` attributre where we would put the next descriptor entry in the ring (modulo the queue size). This starts at 0, and increases.
- `id` is the start of the descriptor chain, and `len` is how many bytes we've written.
- `avail_event`, I assume, is how much we need to make available until the device gets notified.

```c
struct virtq_used {
  #define VIRTQ_USED_F_NO_NOTIFY 1
  le16 flags;
  le16 idx;
  struct virtq_used_elem ring[ /* Queue Size */];
  le16 avail_event; /* Only if VIRTIO_F_EVENT_IDX */
  };
/* le32 is used here for ids for padding reasons. */
struct virtq_used_elem {
  /* Index of start of used descriptor chain. */
  le32 id;
  /* Total length of the descriptor chain which was used (written to) */
  le32 len;
}
```

##### .6.12 virtqueue operation

- At its simplest, we can typically have two virtqueues, the transmission virtqueue and the reception virtqueue. The driver would add utgoing packets to the transmit one, and then would free them after they were used. Similarly, incoming (device-writable) buffers are added to receive virtqueue, and processed after they are used.

##### .6.13 supplying buffers to the device

- the driver offers buffers to one of the device's virtqueues as follows:

1. the driver places the buffer into free descriptor(s) into the descriptor table
2. the driver places the index of the head of the descriptor chain into the next ring of entry of the available ring
3. steps 1 and 2 may be performed repeatedly if batching is possible
4. the driver performs a suitable memory barrier to ensure the device sees the updated descriptor table and available ring before the next step (**how does this ensure that the device will see it?**).
5. the available _idx_ is increased by the number of descriptor chain heads added to the available ring.
6. The driver performs a suitable memory barrier to ensure that it updates the idx field before checking for notification suppression
7. the driver sends an available buffer notification to the device if such notifications are not suppressed.

#### .7 packed virtqueues

- this is an alternative compact virtqueue layout using read-write memory, hat is memory is both read and written to by both host and guest.
- the structure is as follows:
  - **descriptor ring**
  - **driver event suppression**
  - **device event suppression**
- each buffer that is linked into the descriptor ring has the following entries:
  - **buffer ID**
  - **element address**
  - **element length**
  - **flags**
- when we want to send a buffer, we write a buffer to the descriptor ring, notify the device, and the device writes back to the descriptor ring (overwriting a previous one) to send a used event notification.

##### .7.1 driver and device ring wrap counters

- the counter maintained by the driver changes the value each times it makes available the last descriptor in the ring (after making the last descriptor available).
- the counter in the device changes the value each time it uses the last descriptor in the ring.
- they only match when the `device_ctr` and `drive_ctr` are equal.
- to mark a descriptor as available and used, we have the following flags:

```c
#define VIRTQ_DESC_F_AVAIL (1 << 7)
#define VIRTQ_DESC_F_USED (1 << 15)
```

- if we want to mark the descriptor as availabe, we set the `VIRTQ_DESC_F_AVAIL` flag and set the other flag to match the inverse value.
- if we wan to mark something as used, we do the same process but both bits will instead be set.
- they are **different** for an available descriptor and **equal** for a used descriptor.

##### .7.2 polling of available and used descriptors

- writes on both sides can be reordered, but each side are only required to poll and test a single location in memory; the next device descriptor after the one they processed previously, in circular order.

### virtio transport options

#### .2: Virtio over MMIO

- virtual environmens without PCI support might use simple memory mapped device instead of the PCI device.
- the memory ampped behaviour is based on the PCI device specification. most operations including initialization, queues configuration and buffer transfers are identical for the most part. Here are some of the differences:

##### .2.1 MMIO device discovery

- there is no generic device discovery mechanism. Hence, the guest OS will need to know the location of the registers and interrupt(s) used. the suggested binding for systems using flattened device trees is shown in this example:

```c
// EXAMPLE: virtio_block device taking 512 bytes at 0x1e000, interrupt 42.
virtio_block@1e000 {
  compatible = "virtio,mmio";
  // 0 argument is the location, 1 argument is the amount of data stored
  reg = <0x1e000 0x200>;
  interrupts = <42>;
}
```

##### .2.2 MMIO device register layout

- there are memory mapped control registers followed by device specific configuration space (all register values are organized as little endian).

| name of register                     | location                   | function                                                                                                                                                                                                                                                                                       |
| ------------------------------------ | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Name                                 | offset from base direction | description                                                                                                                                                                                                                                                                                    |
| magic value                          | 0x000                      | value: 0x74726976                                                                                                                                                                                                                                                                              |
| version                              | 0x004                      | device version number (should be 1 for legacy)                                                                                                                                                                                                                                                 |
| virtio subsystem device ID           | 0x008                      | varies per device                                                                                                                                                                                                                                                                              |
| virtio subsytem vendor ID            | 0x00c                      | _none given_                                                                                                                                                                                                                                                                                   |
| deviceFeatures                       | 0x010                      | - flags representing features the device supports <br> - Reading from this register depends on the `deviceFeaturesSel` bit, if it is 0, we can get 0 - 31, else, 32 to 63.                                                                                                                     |
| deviceFeatureSel                     | 0x014                      | device (host) features word selection.                                                                                                                                                                                                                                                         |
| driverFeatures                       | 0x020                      | features activated by driver. again, governed by the same mechanism as `deviceFeatures`.                                                                                                                                                                                                       |
| `driverFeaturesSel`                  | 0x024                      | used for selecting that driver feathres that were understood by the device that we want to see                                                                                                                                                                                                 |
| `QueueSel`                           | 0x030                      | **virtual queue index**. writing to this register selets which virtual queue that operations _QueueNumMax_, _QueueNum_, _QueueReady_, etc. operate on.                                                                                                                                         |
| `QueueNumMax`                        | 0x034                      | **maximum virtual queue size**                                                                                                                                                                                                                                                                 |
| `QueueNum`                           | 0x038                      | Queue size is the numnber of elements that are in the queue. writing to this tells us what size of the queue the driver will use                                                                                                                                                               |
| `QueueReady`                         | 0x044                      | **virtual queue ready bit** Writing one (0x1) to this register notifies the device that it can execute requests from the virtual queue, reading from this register returns the last value written to it                                                                                        |
| `QueueNotify`                        | 0x50                       | **queue notifier**, writing a value to this register notifies the device that there are new buffers to process in a queue                                                                                                                                                                      |
| `InterruptStatus`                    | 0x60                       | Reading from this register returns a bit mask of events that caused the device interrupt to be asserted                                                                                                                                                                                        |
| `InterruptACK`                       | 0x064                      | **interrupt acknowledge**, writing a value with bits set as defined in _InterruptStatus_ to this register notifies the device that events causing the interrupt have been handled                                                                                                              |
| `Status`                             | 0x070                      | Reading from this register returns the current device status flags. Writing sets the status flags, indicating the driver progress. Writing zero triggers a device reset                                                                                                                        |
| `QueueDescLow` & `QueueDescHigh`     | 0x080 & 0x084              | Writing to these registers notifies the device about loation of the descriptor area of the queue selected by `QueueSel`                                                                                                                                                                        |
| `QueueDriverLow` & `QueueDriverHigh` | 0x090 & 0x094              | Tells us the location of the driver area of the queue selected by `QueueSel`                                                                                                                                                                                                                   |
| `ConfigGeneration`                   | 0x0fc                      | returns a value describing a version of the device specific configuration space. If no part of the configGeneration has changed, then the returned values are identical. If they are not, the configuration space accesses we're not atomic and the driver has to perform the operations again |
| `Config`                             | 0x100+                     | Device-specific configuration space starts at 0x100 and is accessed with byte alignment                                                                                                                                                                                                        |

### general initialization and device operation

#### .1: device initialization

- The OS must do the following things:

1. Reset the device.
2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
3. Set the DRIVER status bit: the guest OS knows how to drive the device.
4. Read device feature bits, and write the subset of feature bits understood by the OS and driver to the device.
5. Set the FEATURES_OK status bit. The driver MUST NOT accept new feature bits after this step.
6. Re-read _device status_ to ensure the FEATURES_OK bit is still set: otherwise, the device does not support our subset of features and the device is unusable.
7. Perform device-specific setup, including discovery of virtqueues for the device, optional per-bus setup, reading and possibly writing the device's virtio configuration space, and population of virtqueues.
8. Set the DRIVER_OK status bit. At this point the device is "live".

### device types

#### .2 block device

- a simple virtual block device, we read and write requests that we place in the queue, to be then serviced by the device in (probably) some unspecified order.
- **DEVICE_ID** = 2
- **Virtqueues** = 0 requestq

##### .2.3 feature bits

1. `VIRTIO_BLK_F_SIZE_MAX (1)` -> maximum size of any single segment (what is a segment in this context?) is in _size max_.
2. `VIRTIO_BLK_F_SEG_MAX (2)` -> max number of segments in a request is in _seg max_
3. `VIRTIO_BLK_F_GEOMETRY (4)` -> Disk-style geometry specified in _geometry_
4. `VIRTIO_BLK_F_RO (5)` -> device is read-only.
5. `VIRTIO_BLK_F_BLK_SIZE (6)` -> Block size of disk is in _blk size_
6. `VIRTIO_BLK_F_FLUSH (9)` -> cache flush command support.
7. `VIRTIO_BLK_F_TOPOLOGY (10)` -> device exports information on optimal I/O alignment.
8. `VIRTIO_BLK_F_CONFIG_WCE (11)` -> Device can toggle its cache between writeback and writethrough nodes.
9. `VIRTIO_BLK_F_DISCARD (13)` -> device can support discard command, maximum discard sectors size in `max_discord_sectors` and maximum discard segment number in `max_discard_seg`.
10. `VIRTIO_BLK_F_WRITE_ZEROES (14)` -> device can support write zeroes command, maximum write zeroes sectors size in `max_write_zeroes_sectors` and maximum write zeroes segment number in `max_write_zeroes_seg`

##### .2.4 device configuration layout

```c
struct virtio_blk_config {
  le64 capacity;
  le32 size_max;
  le32 seg_max;

  struct virtio_blk_geometry {
    le16 cylinders;
      u8 heads;
      u8 sectors;
    } geometry;
  le32 blk_size;

  struct virtio_blk_topology {
    // # of logical blocks per physical block (log2)
    u8 physical_block_exp;
    // offset of first aligned logical block
    u8 alignment_offset;
    // suggested minimum I/O size in blocks
    le16 min_io_size;
    // optimal (suggested maximum) I/O size in blocks
    le32 opt_io_size;
  } topology;

  u8 writeback;
  u8 unused0[3];
  le32 max_discard_sectors;
  le32 max_discard_seg;
  le32 discard_sector_alignment;
  le32 max_write_zeroes_sectors;
  le32 max_write_zeroes_seg;
  u8 write_zeroes_may_unmap;
  u8 unused1[3];
};
```

## `virtio_disk.c`

## `virtio.h`
