                //test functions for the vioblk device

#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"
// #include "elf.h"
// #include "fs.h"
#include "string.h"
#include "memory.h"
#include "process.h"
#include "config.h"

//           end of kernel image (defined in kernel.ld)
extern char _kimg_end[];

// #define RAM_SIZE (8*1024*1024)
// #define RAM_START 0x80000000UL
// #define KERN_START RAM_START
// #define USER_START 0x80100000UL

// #define UART0_IOBASE 0x10000000
// #define UART1_IOBASE 0x10000100
// #define UART0_IRQNO 10

// #define VIRT0_IOBASE 0x10001000
// #define VIRT1_IOBASE 0x10002000
// #define VIRT0_IRQNO 1

// static void shell_main(struct io_intf * termio);
#define read_buf_size 4096
static char read_buf[read_buf_size  + 1];
static char write_buf[read_buf_size];
static char copy_buf[4] = "abcd";
static char write_test_buf[read_buf_size];

void main(void) {
    // struct io_intf * termio;
    struct io_intf * blkio;
    void * mmio_base;
    int result;
    int i;
    // int read_buf_size = 4096;
    // uint8_t read_buf[read_buf_size  + 1];
    // char write_buf[read_buf_size];
    // char copy_buf[4] = "abcd";
    int copy_buf_size = sizeof(copy_buf);
    int write_buf_size = sizeof(write_buf);
    uint64_t read_pos = 0;
    uint64_t write_pos = 0;
    uint64_t cur_pos;
    uint64_t blksz;
    // char write_test_buf[write_buf_size];
    uint64_t len;

    console_init();
    memory_init();
    intr_init();
    devmgr_init();
    thread_init();
    procmgr_init();

    // heap_init(_kimg_end, (void*)USER_START);

    //           Attach NS16550a serial devices

    for (i = 0; i < 2; i++) {
        mmio_base = (void*)UART0_IOBASE;
        mmio_base += (UART1_IOBASE-UART0_IOBASE)*i;
        uart_attach(mmio_base, UART0_IRQNO+i);
    }
    
    //           Attach virtio devices

    for (i = 0; i < 8; i++) {
        mmio_base = (void*)VIRT0_IOBASE;
        mmio_base += (VIRT1_IOBASE-VIRT0_IOBASE)*i;
        virtio_attach(mmio_base, VIRT0_IRQNO+i);
    }

    intr_enable();
    // timer_start();

   result = device_open(&blkio, "blk", 0);

    if (result != 0)
        panic("device_open failed");
    else
        kprintf("block device open\n");

    result = blkio->ops->ctl(blkio, IOCTL_SETPOS, &read_pos);

    if (result != 0)
        panic("read set_pos failed");
    else
        kprintf("read set_pos success\n");
    
    result = blkio->ops->read(blkio, read_buf, read_buf_size);

    if(result != read_buf_size)
        panic("device read failed");
    else
        kprintf("device read success\n");

    kprintf("read start\n");
    // read_buf[read_buf_size] = 0;
    // kprintf(read_buf);
    for(int i=0; i < read_buf_size; i++){
        kprintf("%x", read_buf[i]);
    }
    kprintf("\nread end\n");

    // //hello world write and read
    // result = blkio->ops->ctl(blkio, IOCTL_SETPOS, &write_pos);

    // if (result != 0)
    //     panic("write set_pos failed");
    // else
    //     kprintf("write set_pos success\n");

    // result = blkio->ops->write(blkio, write_buf, write_buf_size);

    // if(result != write_buf_size)
    //     panic("device write failed");
    // else
    //     kprintf("device write success\n");

    // result = blkio->ops->ctl(blkio, IOCTL_SETPOS, &read_pos);

    // if (result != 0)
    //     panic("read set_pos failed");
    // else
    //     kprintf("read set_pos success\n");
    
    // result = blkio->ops->read(blkio, write_test_buf, write_buf_size);

    // if(result == write_buf_size){
    //     if(strcmp(write_buf, write_test_buf) == 0)
    //         kprintf("device read success\n");
    // }
    // else
    //     panic("device read failed");

    //4096 byte write and read
    for (i=0; i < write_buf_size / copy_buf_size; i++)
        strncat(write_buf + i * copy_buf_size, copy_buf, copy_buf_size);

    result = blkio->ops->ctl(blkio, IOCTL_SETPOS, &write_pos);

    if (result != 0)
        panic("write set_pos failed");
    else
        kprintf("write set_pos success\n");

    result = blkio->ops->write(blkio, write_buf, write_buf_size);

    if(result != write_buf_size)
        panic("device write failed");
    else
        kprintf("device write success\n");

    result = blkio->ops->ctl(blkio, IOCTL_SETPOS, &read_pos);

    if (result != 0)
        panic("read set_pos failed");
    else
        kprintf("read set_pos success\n");
    
    result = blkio->ops->read(blkio, write_test_buf, write_buf_size);

    if(result == write_buf_size){
        if(strncmp(write_buf, write_test_buf, write_buf_size) == 0)
            kprintf("device read success\n");
        else
            panic("device read failed");
    }
    else
        panic("device read failed");

    com0_putc(write_test_buf[0]);
    com0_putc(write_test_buf[1]);
    com0_putc(write_test_buf[2]);
    com0_putc(write_test_buf[3]);

    result = blkio->ops->ctl(blkio, IOCTL_GETLEN, &len);

    if(result != 0)
        panic("get len failed");
    else
        kprintf("get len success:%d\n", len);
    
    result = blkio->ops->ctl(blkio, IOCTL_GETPOS, &cur_pos);

    if(result != 0)
        panic("get pos failed");
    else
        kprintf("get pos success:%d\n", cur_pos);

    result = blkio->ops->ctl(blkio, IOCTL_GETBLKSZ, &blksz);

    if(result != 0)
        panic("get blksz failed");
    else
        kprintf("get blksz success:%d\n", blksz);

    blkio->ops->close(blkio);

    kprintf("device close success\n");


}