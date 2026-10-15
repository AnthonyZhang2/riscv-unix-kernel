//           vioblk.c - VirtIO serial port (console)
//          

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "lock.h"

//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1
#define VIOBLK_UNIT_SIZE 512    //default sector size used in read/write
#define DESC_LEN 16             //descriptor length in bytes
#define DESC_ELEM 3             //number of descriptors in the indirect descriptor table
#define AVAIL_Q_SIZE 1          //max descriptors in available queue
#define USED_Q_SIZE 1           //max descriptors in used queue
#define VIRTIO_INTR_USED    (1 << 0)    //interrupt status reg used bit mask
#define VIRTIO_INTR_CONFIG  (1 << 1)    //interrupt status reg config bit mask
#define VIRTQ_INDIRECT_DESC_TABLE_LEN DESC_ELEM * DESC_LEN

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct indirect_descriptor_table { 
        /* The actual descriptors (16 bytes each) */ 
        struct virtq_desc desc[VIRTQ_INDIRECT_DESC_TABLE_LEN / 16]; 
};

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[1];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;
};

static struct lock vioblk_lock;

//           INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.
//inputs : pointer to memory mapped device regs, interrupt source no
//outputs: none
//effects: sets ops and feature bits for device, initializes descriptors, 
//attaches virtq to device, registers isr and device 
//description: attaches a virtio block device to the system and creates 
//the vioblk device struct used by driver

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed

    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;
    int qid = 0;                                  //as first queue is at idx 0
    uint_fast16_t vqlen = 1;

    static const struct io_ops ops = {            //create ops interface for device
        .close = vioblk_close,
        .read = vioblk_read,
        .write = vioblk_write,
        .ctl = vioblk_ioctl
    };
    struct indirect_descriptor_table * desc_tab;
    

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    //           Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();

    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    //           If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //           Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    //           FIXME Finish initialization of vioblk device here
 
    dev->regs = regs;
    dev->irqno = irqno;
    dev->io_intf.ops = &ops;                            //set ops function pointers
    dev->size = regs->config.blk.capacity * VIOBLK_UNIT_SIZE;       //capacity is in terms of 512 B sectors
    dev->blksz = blksz;
    dev->blkcnt = dev->size / (uint64_t)(dev->blksz);               //get block count from size/ block size
    dev->blkbuf = (char *)((void *)dev + sizeof(struct vioblk_device));     //get address of blkbuf by adding offset
    
    desc_tab = kmalloc(sizeof(struct indirect_descriptor_table));   //allocate indirect descriptor table
    memset(desc_tab, 0, sizeof(struct indirect_descriptor_table));

    // struct virtq_desc desc_tab_desc = {                             //descriptor for indirect descriptor table
    //     .addr = (uint64_t) &dev->vq.desc[1],
    //     .len = DESC_ELEM * DESC_LEN,
    //     .flags = VIRTQ_DESC_F_INDIRECT
    // };
    // struct virtq_desc desc_tab_desc = {                             //descriptor for indirect descriptor table
    //     .addr = (uint64_t) desc_tab,
    //     .len = VIRTQ_INDIRECT_DESC_TABLE_LEN,
    //     .flags = VIRTQ_DESC_F_INDIRECT
    // };
    struct virtq_desc header_desc = {                               //descriptor for request header
        .addr = (uint64_t) &dev->vq.req_header,
        .len = sizeof(struct vioblk_request_header),
        .flags = VIRTQ_DESC_F_NEXT,
        .next = 1                                                   //next descriptor index in array
    };
    struct virtq_desc data_desc = {                                 //descriptor for request data
        .addr = (uint64_t) dev->blkbuf,
        .len = dev->blksz,
        .flags = VIRTQ_DESC_F_NEXT,
        .next = 2                                                   //next descriptor index in array
    };
    struct virtq_desc status_desc = {                               //descriptor for request status
        .addr = (uint64_t) &dev->vq.req_status,
        .len = sizeof(dev->vq.req_status),
        .flags = VIRTQ_DESC_F_WRITE
    };

    desc_tab->desc[0] = header_desc;        //put request descriptors into indirect descriptor table
    desc_tab->desc[1] = data_desc;
    desc_tab->desc[2] = status_desc;

    struct virtq_desc desc_tab_desc = {                             //descriptor for indirect descriptor table
        .addr = (uint64_t) desc_tab,
        .len = VIRTQ_INDIRECT_DESC_TABLE_LEN,
        .flags = VIRTQ_DESC_F_INDIRECT
    };

    dev->vq.desc[0] = desc_tab_desc;
    // dev->vq.desc[1] = header_desc;
    // dev->vq.desc[2] = data_desc;
    // dev->vq.desc[3] = status_desc;

    // desc_tab->desc[0] = header_desc;
    // desc_tab->desc[1] = data_desc;
    // desc_tab->desc[2] = status_desc;

    virtio_attach_virtq(regs, qid, vqlen, (uint64_t) &dev->vq.desc, (uint64_t) &dev->vq.used, (uint64_t) &dev->vq.avail);

    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev);
    dev->instno = device_register("blk", vioblk_open, dev);

    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    //           fence o,oi
    __sync_synchronize();
}

//inputs: address of io interface pointer, vioblk device struct pointer
//outputs: 0 on successful open, negative error code on error
//effects: enables the virtqueues for use, sets flags in vioblk_device struct, 
//returns io ops pointer to ioptr and enables interrupts from device
//description: opens the device for use and sets flags for device

int vioblk_open(struct io_intf ** ioptr, void * aux) {
    //           FIXME your code here
    int qid = 0;                                    //as first queue is at idx 0

    struct vioblk_device *  const dev = aux;        //pointer to device struct

    assert(ioptr != NULL);

    if(dev->opened)                                 //return error if device open
        return -EBUSY;

    virtio_enable_virtq(dev->regs, qid);
    dev->vq.avail.idx = 0;      //initialize counters in avail and used queues
    dev->vq.used.idx = 0;
    dev->vq.avail.flags = 0;    //initialize flags in avail and used queues
    dev->vq.used.flags = 0;

    //reset flags and buffer
    dev->readonly = 0;              //device can read and write
    dev->pos = 0;                   //init pos to device start
    dev->bufblkno = 0;              //init blkbufno to first block
    memset(dev->blkbuf, 0xff, dev->blksz);  //init data and status buffer to check if written later
    dev->vq.req_status = 0xff;

    condition_init(&dev->vq.used_updated, "used_updated");
    
    //dev->io_intf.refcnt += 1;
    ioref(&(dev->io_intf));

    intr_enable_irq(dev->irqno);

    *ioptr =  &dev->io_intf;
    dev->opened = 1;
    
    lock_init(&vioblk_lock, "vioblk_lock");

    return 0;
}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).

//inputs: pointer to io interface
//outputs: none
//effects: resets queues and disables interrupts from device
//description: closes the device and disables interrupts

void vioblk_close(struct io_intf * io) {
    //           FIXME your code here
    int qid = 0;                                  //as first queue is at idx 0

    assert(io != NULL);

    struct vioblk_device * const dev = (void *)io - offsetof(struct vioblk_device, io_intf);  //device pointer

    assert(dev->opened);               //error as device not opened
    
    virtio_reset_virtq(dev->regs, qid);

    intr_disable_irq(dev->irqno);

    dev->opened = 0;  
}

//inputs: io interface pointer, data buf, buf size
//outputs: bytes read on success, negative error code on error
//effects: sets read request in the indirect descriptor, updates avail ring, 
//notifies device,waits for response and reads repsonse bytes until buf size
//description: reads buf size bytes from device into data buf

long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz)
{
    //           FIXME your code here

    //function assumes blksz is 512 bytes
    unsigned long bytes_read = 0;
    int qid = 0;                        //first queue is at index 0
    uint32_t len;                       //bytes written to blkbuf by device
    uint64_t offset = 0;                //offset to write data from buf 
    uint64_t sector_pos;            //pos in current sector
    int s;

    assert(io != NULL);

    struct vioblk_device * restrict dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    struct indirect_descriptor_table * desc_tab = (void *) dev->vq.desc[0].addr;

    if(!dev->opened)                        //device not opened
        return -ENODEV;
    
    if(dev->pos + bufsz > dev->size)        //device does not have enough space
        return -EINVAL;

    lock_acquire(&vioblk_lock);

    while(bytes_read < bufsz){    
        // struct virtq_desc header_desc = {                               //descriptor for request header
        //     .addr = (uint64_t) &dev->vq.req_header,
        //     .len = sizeof(struct vioblk_request_header),
        //     .flags = VIRTQ_DESC_F_NEXT,
        //     .next = 1
        // };
        // struct virtq_desc data_desc = {                                 //descriptor for request data
        //     .addr = (uint64_t) dev->blkbuf,
        //     .len = dev->blksz,
        //     .flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE,
        //     .next = 2
        // };
        // struct virtq_desc status_desc = {                               //descriptor for request status
        //     .addr = (uint64_t) &dev->vq.req_status,
        //     .len = sizeof(dev->vq.req_status),
        //     .flags = VIRTQ_DESC_F_WRITE
        // };
        dev->vq.req_header.sector = (uint64_t) dev->pos / VIOBLK_UNIT_SIZE;     //sector corresponding to pos
        dev->vq.req_header.type = VIRTIO_BLK_T_IN;                              //read request
        desc_tab->desc[1].flags |= VIRTQ_DESC_F_WRITE;                            //device can write to blkbuf

        // desc_tab->desc[0] = header_desc;
        // desc_tab->desc[1] = data_desc;
        // desc_tab->desc[2] = status_desc;

        dev->vq.avail.ring[ dev->vq.avail.idx % AVAIL_Q_SIZE] = 0;  //insert request into available queue

        __sync_synchronize();

        dev->vq.avail.idx++;                                        //increment available index

        if(!dev->vq.used.flags)                 //send available notification to device if enabled by device
            virtio_notify_avail(dev->regs, qid);

        s = intr_disable();

        condition_wait(&dev->vq.used_updated);      //wait for device response

        intr_restore(s);

        if(dev->vq.req_status == VIRTIO_BLK_S_UNSUPP)   //request error
            return -EINVAL;

        if(dev->vq.req_status == VIRTIO_BLK_S_IOERR)
            return -EIO;

        if(dev->vq.req_status == VIRTIO_BLK_S_OK){   //request success   
            len = dev->vq.used.ring[dev->vq.used.idx % USED_Q_SIZE].len;          //bytes written by device
            if(!len)                                //len is zero
                continue;
            if(len > VIOBLK_UNIT_SIZE)                                    //ignore extra bytes from device
                len = VIOBLK_UNIT_SIZE;                     
            sector_pos = dev->pos % VIOBLK_UNIT_SIZE;               //pos is not at sector start
            len = len - sector_pos;
            if(bufsz - bytes_read < len)          //less than 512 bytes remaining
                len = bufsz - bytes_read;   

            memcpy(buf + offset, dev->blkbuf + sector_pos, len);
            bytes_read += len;                  //update counters
            offset += len;
            dev->pos += len;
            dev->pos %= dev->size;              
            dev->bufblkno = (uint64_t) dev->pos / VIOBLK_UNIT_SIZE;
        }
    }

    lock_release(&vioblk_lock);

    return bytes_read;
    
}

//inputs: io interface pointer, data buf, bytes to write
//outputs: bytes written on success , negative error code on error
//effects: puts write request in indirect descriptor, updates avail ring, notifies device and waits for response
//description: writes n bytes into device from buf

long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n)
{
    //           FIXME your code here

    //functions assumes blksz is 512 bytes
    unsigned long bytes_wrote = 0;
    uint64_t offset = 0;
    uint64_t len;
    int qid = 0;                        //first queue is at index 0
    uint64_t rp, wp;                    //read position , write position
    uint64_t sector_pos;                //position in current sector
    int s;

    assert(io != NULL);

    struct vioblk_device * restrict dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    struct indirect_descriptor_table * desc_tab = (void *) dev->vq.desc[0].addr;

    if(!dev->opened)            //device not opened
        return -ENODEV;
    
    if(dev->pos + n > dev->size)        //device does not have enough space
        return -EINVAL;

    lock_acquire(&vioblk_lock);
    
    while(bytes_wrote < n){
        // struct virtq_desc header_desc = {                               //descriptor for request header
        //     .addr = (uint64_t) &dev->vq.req_header,
        //     .len = sizeof(struct vioblk_request_header),
        //     .flags = VIRTQ_DESC_F_NEXT,
        //     .next = 1
        // };
        // struct virtq_desc data_desc = {                                 //descriptor for request data
        //     .addr = (uint64_t) dev->blkbuf,
        //     .len = dev->blksz,
        //     .flags = VIRTQ_DESC_F_NEXT,
        //     .next = 2
        // };
        // struct virtq_desc status_desc = {                               //descriptor for request status
        //     .addr = (uint64_t) &dev->vq.req_status,
        //     .len = sizeof(dev->vq.req_status),
        //     .flags = VIRTQ_DESC_F_WRITE
        // };

        dev->vq.req_header.sector = (uint64_t) dev->pos / VIOBLK_UNIT_SIZE;         //set sector
        dev->vq.req_header.type = VIRTIO_BLK_T_OUT;
        desc_tab->desc[1].flags &= ~VIRTQ_DESC_F_WRITE;                             //device can only read blkbuf

        // desc_tab->desc[0] = header_desc;
        // desc_tab->desc[1] = data_desc;
        // desc_tab->desc[2] = status_desc;

        len = dev->blksz;                                   
        if(dev->blksz != VIOBLK_UNIT_SIZE)          //set data buf len to 512  
            len = VIOBLK_UNIT_SIZE;
        sector_pos = dev->pos % VIOBLK_UNIT_SIZE;

        if(sector_pos != 0){                //write not at sector start so we need to read previous vals
            wp = dev->pos;
            rp = dev->pos - sector_pos;             //set pos to beginning of current sector
            dev->pos = rp;
            vioblk_read(io, dev->blkbuf, sector_pos);   //read current sector upto pos
            dev->pos = wp;                              //restore position to write pos
            len = len - sector_pos;
        }

        if(n - bytes_wrote < len)              //less than available space in sector remaining to write
            len = n - bytes_wrote;
        
        memcpy(dev->blkbuf + sector_pos, buf + offset, len);
        
        dev->vq.avail.ring[ dev->vq.avail.idx % AVAIL_Q_SIZE] = 0;  //insert request into available queue

        __sync_synchronize();

        dev->vq.avail.idx++;                                        //increment available index

        if(!dev->vq.used.flags)                 //send available notification to device if enabled by device
            virtio_notify_avail(dev->regs, qid);

         s = intr_disable();

        condition_wait(&dev->vq.used_updated);      //wait for device response

        intr_restore(s);

        if(dev->vq.req_status == VIRTIO_BLK_S_UNSUPP)   //request error
            return -EINVAL;

        if(dev->vq.req_status == VIRTIO_BLK_S_IOERR)
            return -EIO;
        
        if(dev->vq.req_status == VIRTIO_BLK_S_OK){
            bytes_wrote += len;                     //update counters
            offset += len;
            dev->pos += len;
            dev->pos %= dev->size;              
            dev->bufblkno = (uint64_t) dev->pos / VIOBLK_UNIT_SIZE;
        }
    }

    lock_release(&vioblk_lock);
    
    memset(dev->blkbuf, 0, dev->blksz);

    return bytes_wrote;
}

//inputs: io interface pointer, io command, command arg
//outputs: 0 on success , negative error code on error
//effects: calls the corresponding ioctl function
//description: implements io control operations on device

int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    
    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);
    
    switch (cmd) {
    case IOCTL_GETLEN:
        return vioblk_getlen(dev, arg);
    case IOCTL_GETPOS:
        return vioblk_getpos(dev, arg);
    case IOCTL_SETPOS:
        return vioblk_setpos(dev, arg);
    case IOCTL_GETBLKSZ:
        return vioblk_getblksz(dev, arg);
    default:
        return -ENOTSUP;
    }
}

void vioblk_isr(int irqno, void * aux) {
    //           FIXME your code here
    int qid = 0;                                        //first queue is at index 0
    struct vioblk_device * const dev = aux;
    uint32_t intr_status = dev->regs->interrupt_status;

    if(intr_status & VIRTIO_INTR_USED){                 //used ring updated
        condition_broadcast(&dev->vq.used_updated);
        dev->vq.used.idx++;
        dev->regs->interrupt_ack |= VIRTIO_INTR_USED;
    }

    if(intr_status & VIRTIO_INTR_CONFIG){
        if(dev->regs->status & VIRTIO_STAT_DEVICE_NEEDS_RESET){
            dev->regs->status = 0;                      //reset device
            virtio_enable_virtq(dev->regs, qid);        //reenable virtqueue
            dev->regs->interrupt_ack |= VIRTIO_INTR_CONFIG;
        }
        else 
            panic("unknown configuration change error");
    }
}

//inputs: vioblk_device struct pointer, len pointer
//outputs: 0 on success
//effects: sets val in len pointer to device size
//description: copies the device size in bytes to input pointer

int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    //           FIXME your code here
    assert(dev != NULL);

    *lenptr = dev->size;        //copy len 

    return 0;
}

//inputs: vioblk_device struct pointer, pos pointer
//outputs: 0 on success, negative error code on error
//effects: sets val in pos pointer to current position in device
//description: copies the current device position to input pointer

int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    //           FIXME your code here
    assert(dev != NULL);

    if(!dev->opened)                                            //checks for device open or not
        return -ENODEV;
    
    *posptr = dev->pos;     //copy position

    return 0;
}

//inputs: vioblk_device struct pointer, pos pointer
//outputs: 0 on success, negative error code on error
//effects: sets current position in device to val in pos pointer
//description: copies the val in input pointer to pos in device struct

int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    //           FIXME your code here
    assert(dev != NULL);

    if(!dev->opened)                                                //checks for device open or not
        return -ENODEV;
    
    dev->pos = *posptr;     //set position

    return 0;
}

//inputs: vioblk_device struct pointer, blksz pointer
//outputs: 0 on success
//effects: sets val in blksz pointer to device block size in bytes
//description: copies the device block size to input pointer

int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
    //           FIXME your code here
    assert(dev != NULL);

    *blkszptr = dev->blksz;     //copy blksz

    return 0;
}
