//           elf_test.c - Main function: Sets up and tests IO_Lit and Elf_Load
//          

#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"
#include "elf.h"
#include "fs.h"
#include "string.h"

//           end of kernel image (defined in kernel.ld)
extern char _kimg_end[];

#define RAM_SIZE (8*1024*1024)
#define RAM_START 0x80000000UL
#define KERN_START RAM_START
#define USER_START 0x80100000UL

#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

#define VIRT0_IOBASE 0x10001000
#define VIRT1_IOBASE 0x10002000
#define VIRT0_IRQNO 1

#define TESTNUM 5
#define TESTBUF1 127
#define TESTBUF2 69

extern char _companion_f_start[];   //Companion.o start object pointer
extern char _companion_f_end[];     //Companion.o end object pointer

//Inputs:   N/A
//Outputs:  N/A
//Effects:  Sets up and tests IO
//Description: Sets up QEMU and virtual devices and UART. Finally sets up IO Int and runs tests on Elf_Load.
void main(void) {    
    struct io_intf * termio;
    struct io_intf * blkio;
    void * mmio_base;
    int result;
    int i;

    console_init();
    intr_init();
    devmgr_init();
    thread_init();
    timer_init();

    heap_init(_kimg_end, (void*)USER_START);

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
    timer_start();

   result = device_open(&blkio, "blk", 0);

    if (result != 0)
        panic("device_open failed");
    
    result = fs_mount(blkio);

    debug("Mounted blk0");

    if (result != 0)
     panic("fs_mount failed");

    result = device_open(&termio, "ser", 1);

    if (result != 0)
        panic("Could not open ser1");
    
    void (*exe_entry)(struct io_intf*);

    kprintf("IOLit Tests\n\n\n");

    long long unsigned int size = _companion_f_end-_companion_f_start;

    kprintf("Start of companion file: %p\n", _companion_f_start);
    kprintf("End of companion file: %p\n", _companion_f_end);
    kprintf("Size of companion file: %llu\n", size);

    char buffer[16];        //Buffer for read/write
    struct io_lit *temp;    
    temp = kmalloc(sizeof(struct io_lit));  //Allocate space for the IO interface
    
    kprintf("Initializing IOLit\n");
    struct io_intf *io = iolit_init(temp, _companion_f_start, size);
    uint64_t position = 0;

    kprintf("SUCCESS initialized IOLit\n");
    kprintf("Control Register Tests \n");

    io->ops->ctl(io, IOCTL_GETPOS, &position);          //Check current position
    kprintf("current ctl position %llu\n", position);
    
    uint64_t pos = TESTNUM;
    result = io->ops->ctl(io, IOCTL_SETPOS, &pos);      //Set current position to arbitrary val
    kprintf("Set return value: %d\n", result);

    io->ops->ctl(io, IOCTL_GETPOS, &position);          //Read current position
    kprintf("current ctl position %llu\n", position);

    if(position == TESTNUM){
        kprintf("SUCCESS Set POS and Get POS\n");
    }
    else{
        panic("FAILED SET POSITION");
    }

    io->ops->ctl(io, IOCTL_GETLEN, &position);      //Get length of current IO Interface
    if(position == _companion_f_end - _companion_f_start){     //Check if matches with companion file
        kprintf("SUCCESS ctl length %llu\n", position);
    }
    else{
        panic("FAILED CONTROL LENGTH");
    }


    result = io->ops->ctl(io, IOCTL_GETBLKSZ, &position);      //Check block size of IO
    if(position == 1){
        kprintf("SUCCESS Block Size %llu\n", position);
    }
    else{
        panic("FAILED BLOCK SIZE");
    }

    pos = 0;
    result = io->ops->ctl(io, IOCTL_SETPOS, &pos);             //Reset position back to beginning

    buffer[0] = TESTBUF1;    //Characters to write into io buffer
    buffer[1] = TESTBUF2;

    io->ops->write(io, buffer, 2);     //We're just checking 2 numbers 

    buffer[0] = ' ';    //Clear the original characters to see if we read right
    buffer[1] = ' ';

    pos = 0;
    result = io->ops->ctl(io, IOCTL_SETPOS, &pos);          //Reset to beginning since we wrote and position changed
    io->ops->read(io, buffer, 2);       //We're just checking 2 numbers 

    if(buffer[0] == TESTBUF1 && buffer[1] == TESTBUF2){
        kprintf("SUCCESS Write and Read\n");
    }

    pos = 0;
    result = io->ops->ctl(io, IOCTL_SETPOS, &pos);          //Reset since we read and position changed

    io->ops->ctl(io, IOCTL_GETPOS, &position);
    kprintf("current ctl position %llu\n", position);

    kprintf("Calling elf_load\n");
    result = elf_load(io, &exe_entry);

    if(result == 0){
        kprintf("SUCCESS elf_load returned %d\n", result);
    }
    else{
        panic("FAILED elf_load");
    }
    

    if(exe_entry != NULL){
        kprintf("exe_entry properly set \n");
    }
    else{
        panic("FAILED: exe_entry properly set \n");
    }
    
}

