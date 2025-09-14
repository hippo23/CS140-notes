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

### `void virtio_disk_init`

- We initialize a lock, I assume to assure atomic access to a single disk.
  - **QUESTION: Is the disk by default not atomic access already?**
- Then we check the value of the MMIO registers, first for the magic value, then that the version (meaning the version of the device) is not legacy. The device ID should be two for the `virtio_blk`, and the vendor ID should also match what we expect.
- We then assign the value of the status register to be zero, this calls for a device reset. We then have the follow the steps for [device initialization](#1-device-initialization).
- We then set the acknowledge register, then the driver status bit.
- We negotiate features, as you can see, here we first check the value to see what the device offers, then we DISABLE the feature bits that we don't want to use.
  - **QUESTION: Isn't this bad if we want to know what we CAN use?**
- Read the status, though, to make sure that the features you request are ok.
- Initialize `QueueSel` (purpose seen in the [MMIO registers](#22-mmio-device-register-layout)).
- Check `QueueReady` by reading which returns the last value written to it. You want to make sure this queue is not in use,
  - If it is already ready, that means its in use, hence an error needs to be thrown.
  - **QUESTION: What actually is the `virtqueue`, the register has a value, but I see no reason to believe the disk is aware of any virtual queues yet?**
  - I think we aren't trying to say this `virtqueue` is already in use, our point is that we don't want some other device readying to write into the disk even if they don't share the same `virtqueue` memory.
  - From what I understand, actual `virtqueues` exist in the device, and these are fixed. We need to create a reflection of that structure in the driver so that the device knows how to handle it properly.
- We do some other initialization stuff, like making sure the maximum queue size isn't zero or that max queue size isn't smaller than what we want. Then we have to configure all the physical addresses and point the registers to the appropriate areas.
- Then we mark the queue as ready, and hope that no other driver will attempt and use this
  - (though if they do, won't it break already since the locations have been changed?). If some other devices uses it, its not guaranteed things will somehow succeed, actually, its more or less guaranteed something has to FAIL.
- After all of that, we iterate throughout each entry in our mirror of the contents of the queue and make sure all elements are zero (everything is unused) (\*\*I wonder if we could do a `memset` for this instead).
- Lastly, tell teh device we're completely ready.

```c
void virtio_disk_init(void) {
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(VIRTIO_MMIO_VERSION) != 2 || *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
      *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    panic("could not find virtio disk");
  }

  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if (!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if (*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0)
    panic("virtio disk has no queue 0");
  if (max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if (!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for (int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}
```

### `static int alloc_desc`

- `alloc_desc`, we just look for any descriptor that has a value of one, then we assign that (its index is equivalent to the one in the descriptor chain, remember).

```c
// find a free descriptor, mark it non-free, return its index.
static int alloc_desc() {
  for (int i = 0; i < NUM; i++) {
    if (disk.free[i]) {
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}
```

### `static void free_desc`

- Common sense. We mark all the values in the descriptor to 0 just for predictable behaviour, I'd like to believe.

```c
// mark a descriptor as free.
static void free_desc(int i) {
  if (i >= NUM)
    panic("free_desc 1");
  if (disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}
```

### `alloc3_desc`

- I assume three descriptor allocation exists due to perhaps message framing that we want to use? But we'll see that in a bit.

```c
// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int alloc3_desc(int *idx) {
  for (int i = 0; i < 3; i++) {
    idx[i] = alloc_desc();
    if (idx[i] < 0) {
      for (int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}
```

### `void virtio_disk_rw`

- **SETTING UP THE DEVICE**
  - First thing you notice is that we acquire the lock. We know that, at the sector level, memory access is atomic (the head pin can't be at two places at once). But at higher levels, both cores could be writing to the same place at the same time, which is obviously bad. Hence, we want one operation to finish after the other.
  - Now, overwriting is a different story. One core could very well overwrite the data that you were using, I assume that's a kernel-level implementation now. But what we want is just that there's no dual-state wherein we have writes from two CPU's turning into some undefined state wherein both think a descriptor is free.
  - If there are no descriptors free, then we are forced to wait:
    - **QUESTION: why is the address the first one? is that just an arbitrary choice? Also, couldn't we have just use the address of free without doing `[0]`? Because we're getting the address of the value of something we dereferenced.**
  - Then we need to format the three descriptors we got into the proper message. These descriptors can also be seen [here](#struct-virtioblkreq)
- **SETTING UP THE DATA**
  - Next step that is up is for us to grab the address (remember, not the structure, the address) of the first buffer that was allocated to us and store it in `ops`. We write what kind of type we are trying to make, whether it is a read or it is a write.
  - We mark reserved as zero and input the sector to be equal to the sector that we want.
  - **ONE THING TO NOTICE:** we have a different storage for the actual data we want to send. The buffer is simply a proxy, but we need to store the actual place in memory. Hence we have the command headers, and hence, we also pass in a buffer ourselves for the actual data.
- **SENDING THE DATA**
  - We mark the disk value of the buffer as one, this is useful when we have to deal with interrupts (meaning to say that while we are writing, this will be one, only when it is zero will we know things are finished).
  - **QUESTION: Can't we just use the status bit of the first `idx` to see whether or not it was a success?**
    - The bit is only written to when the operation is a success. We could technically keep looping over the bit, but why would we when we could just wait for an interrupt and do something else? And also, It's not indicated what the value of the status bit should be while waiting, it might be undefined, in which case, looping won't even work.
  - In any case, we push the head descriptor onto the available ring make sure to synchronize (meaning that everything on the CPU thus far has to execute, we don't want any reorganizing wherein we increment `idx` BEFORE it is in the ring or something similar). Then we notify the queue on the device itself, and wait for the device interrupt to tell us that it is finished.
- **CLEANING UP**
  - Once we know the device has notified us that things are done, we set the value of the head buffer back to NULL and also mark all the other descriptor locations as free in the `free` array.
  - We then release the lock we have on the disk.

```c
void virtio_disk_rw(struct buf *b, int write) {
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while (1) {
    if (alloc3_desc(idx) == 0) {
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if (write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64)buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64)b->data;
  disk.desc[idx[1]].len = BSIZE;
  if (write)
    disk.desc[idx[1]].flags = 0; // device reads b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; // device writes 0 on success
  disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  disk.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  while (b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&disk.vdisk_lock);
}
```

### `void virtio_disk_intr`

```c
void virtio_disk_intr() {
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while (disk.used_idx != disk.used->idx) {
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if (disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->disk = 0; // disk is done with buf
    wakeup(b);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}
```

## `virtio.h`

### MMIO registers

- Just see the [notes](#22-mmio-device-register-layout), honestly.

```c
// virtio mmio control registers, mapped starting at 0x10001000.
// from qemu virtio_mmio.h
#define VIRTIO_MMIO_MAGIC_VALUE  0x000 // 0x74726976
#define VIRTIO_MMIO_VERSION  0x004 // version; should be 2
#define VIRTIO_MMIO_DEVICE_ID  0x008 // device type; 1 is net, 2 is disk
#define VIRTIO_MMIO_VENDOR_ID  0x00c // 0x554d4551
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL  0x030 // select queue, write-only
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034 // max size of current queue, read-only
#define VIRTIO_MMIO_QUEUE_NUM  0x038 // size of current queue, write-only
#define VIRTIO_MMIO_QUEUE_READY  0x044 // ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050 // write-only
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060 // read-only
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064 // write-only
#define VIRTIO_MMIO_STATUS  0x070 // read/write
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080 // physical address for descriptor table, write-only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW 0x090 // physical address for available ring, write-only
#define VIRTIO_MMIO_DRIVER_DESC_HIGH 0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW 0x0a0 // physical address for used ring, write-only
#define VIRTIO_MMIO_DEVICE_DESC_HIGH 0x0a4
```

### status register bits (initialization)

- In specs, for a summary:
  - **ACKNOWLEDGE\*** -> say that we have acknowledged the device
  - **DRIVER** -> indicates that the guest OS knows how to drive the device.
  - **FAILED** -> some error has occured.
  - **FEATURES_OK** -> driver has acknowledged all features it understands.
  - **DRIVER_OK** -> driver is setup and ready to drive the device.
  - **DEVICE_NEEDS_RESET** -> device has experienced an error from which it cannot recover.

```c
// status register bits, from qemu virtio_config.h
#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER  2
#define VIRTIO_CONFIG_S_DRIVER_OK 4
#define VIRTIO_CONFIG_S_FEATURES_OK 8
```

### device feature bits

- Went over these in the specs notes, but to give a short summary:
  - `RO` -> read permissions
  - `SCSI` -> I don't know what this is actually, need to read more about it
  - `WCE` -> Writeback-mode, buffer writes and dispatch in chunks..
  - `MQ` -> Support for multiple virtual queues (I wonder, how does supporting multiple virtual queues work).
  - `ANY_LAYOUT` -> Means the messages or buffers we are sending have no specific format
  - `INDIRECT_DESC` -> Tells us of these descriptor leads to a bunch of indirect descriptors. Helps with having more descriptors than the table supports.
  - `EVENT_IDX` -> notify us about availabe and used buffer events.

```c
// device feature bits
#define VIRTIO_BLK_F_RO              5 /* Disk is read-only */
#define VIRTIO_BLK_F_SCSI            7 /* Supports scsi command passthru */
#define VIRTIO_BLK_F_CONFIG_WCE     11 /* Writeback mode available in config */
#define VIRTIO_BLK_F_MQ             12 /* support more than one vq */
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX     29
```

## structs

### `struct virtq_desc`

- See [notes](#65-the-virtqueue-descriptor-table) in specs for details.

```c
// a single descriptor, from the spec.
struct virtq_desc {
  uint64 addr;
  uint32 len;
  uint16 flags;
  uint16 next;
};
```

### `struct virtq_avail`

- See [notes](#66-the-virtqueue-available-ring) in specs for details.

```c
// the (entire) avail ring, from the spec.
struct virtq_avail {
  uint16 flags; // always zero
  uint16 idx;   // driver will write ring[idx] next
  uint16 ring[NUM]; // descriptor numbers of chain heads
  uint16 unused;
};
```

### `struct virtq_used_elem`

- See [notes](#66-the-virtqueue-used-ring) in specs for details.

```c
// one entry in the "used" ring, with which the
// device tells the driver about completed requests.
struct virtq_used_elem {
  uint32 id;   // index of start of completed descriptor chain
  uint32 len;
};
```

### `struct virtq_used`

- See [notes](#66-the-virtqueue-used-ring) in specs for details.

```c
struct virtq_used {
  uint16 flags; // always zero
  uint16 idx;   // device increments when it adds a ring[] entry
  struct virtq_used_elem ring[NUM];
};
```

### `struct virtio_blk_req`

- Specified in the specs. The comments say enough about it, its the first descriptor in a disk request.
  - **type** tells you what kind of transaction we want, it can either be a read (`VIRTIO_BLK_T_IN`), a write (`VIRTIO_BLK_T_OUT`), a discard (`VIRTIO_BLK_T_DISCARD`), a write zeroes (`VIRTIO_BLK_T_WRITE_ZEROES`), or a flush (`VIRTIO_BLK_T_FLUSH`).
    - **discard** is used to tell the device whether or not a certain sector or block of data is free. This tells the device, hey, we won't use these blocks anymore, you are free to overwrite them(?)
    - **zero** is just to zero out storage. Useful, I think, if for example, we are allocating some data and want to make sure that we don't get any unnecessary data at the end. Although files will typically tell you their size. Perhaps just as a precaution in case we read extra bits?
    - **flush** is just to make sure all the buffered writes are then written into the block storage.
- **reserved** has the effect of,,, idk...
- **sector** tells us what offset or sector / block number we are working on.

```c
// the format of the first descriptor in a disk request.
// to be followed by two more descriptors containing
// the block, and a one-byte status.
struct virtio_blk_req {
  uint32 type; // VIRTIO_BLK_T_IN or ..._OUT
  uint32 reserved;
  uint64 sector;
};
```
