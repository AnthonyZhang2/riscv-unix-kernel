//           mem_test.c - Main function: Sets up and tests Virtual Memory functions
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
#include "memory.h"

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
//Description: Sets up QEMU and virtual devices and UART. Finally sets up virtual mem and runs tests.
void main(void) {    
    console_init();
    memory_init();
    intr_init();
    devmgr_init();
    thread_init();
    kprintf("Memory Initialized\n");

    void *pp = memory_alloc_page();
    kprintf("Testing memory_alloc_page\n\n");
    kprintf("First Physical Address: %p\n", pp);
    void *pp2 = memory_alloc_page();
    kprintf("Second Physical Address: %p\n", pp2);
    if(pp2-pp != 0x1000){ //This means we have successfully allocated two 4kb pages
        panic("ERROR: Allocated Wrong Page!\n");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_free_page\n");
    void *temp = pp;
    memory_free_page(pp);
    pp = NULL;
    pp = memory_alloc_page();   //We should allocate the just freed page
    if(pp != temp){ //This means we have successfully allocated previous freed page
        panic("ERROR: Freed Page went Wrong!\n");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_alloc_and_map_page\n");
    memory_free_page(pp2);      //We have to free 2 first to restore original free_list
    memory_free_page(pp);
    
    void *heap_end = get_mem_start();
    kprintf("%p\n", heap_end);
    pp = (void *)0xC0000000;
    pp2 = 0;
    pp = memory_alloc_and_map_page((uintptr_t)pp, (uint_fast8_t)(PTE_R | PTE_X | PTE_U)); 
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != (heap_end + PAGE_SIZE * 2)){
        panic("ERROR: Mapped Incorrect Page!");
    }

    pp = (void *)0xC0001000;
    pp2 = 0;
    pp = memory_alloc_and_map_page((uintptr_t)pp, (uint_fast8_t)(PTE_R | PTE_X | PTE_U)); 
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != (heap_end + PAGE_SIZE * 3)){
        panic("ERROR: Mapped Incorrect Page!");
    }

    pp = (void *)0xC0002000;
    pp2 = 0;
    pp = memory_alloc_and_map_page((uintptr_t)pp, (uint_fast8_t)(PTE_R | PTE_X | PTE_U)); 
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != (heap_end + PAGE_SIZE * 4)){
        panic("ERROR: Mapped Incorrect Page!");
    }

    pp = (void *)0xC00f0000;
    pp2 = 0;
    pp = memory_alloc_and_map_page((uintptr_t)pp, (uint_fast8_t)(PTE_R | PTE_X | PTE_U)); 
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != (heap_end + PAGE_SIZE * 5)){
        panic("ERROR: Mapped Incorrect Page!");
    }

    pp = (void *)0xC0200000;
    pp2 = 0;
    pp = memory_alloc_and_map_page((uintptr_t)pp, (uint_fast8_t)(PTE_R | PTE_X | PTE_U)); 
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != (heap_end + PAGE_SIZE * 7)){
        panic("ERROR: Mapped Incorrect Page!");
    }

    kprintf("Allocating page not in user space\n");
    pp = (void *)0xD0000000;
    pp2 = 0;
    pp = memory_alloc_and_map_page((uintptr_t)pp, (uint_fast8_t)(PTE_R | PTE_X | PTE_U)); 
    if(pp != NULL){
        panic("ERROR: Mapped Incorrect Page!");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_alloc_and_map_range\n");
    pp = (void *)0xC0300000;
    kprintf("Calling memory_alloc_and_map_range with size 8193 (3 pages)\n");
    pp2 = memory_alloc_and_map_range((uintptr_t)pp, PAGE_SIZE*2+1, (uint_fast8_t)(PTE_R | PTE_X | PTE_W | PTE_U));

    pp2 = convert_virt_to_phys(pp);
    if(pp2 != (heap_end + PAGE_SIZE * 8)){
        panic("ERROR: Mapped Incorrect Page!");
    }
    pp2 = convert_virt_to_phys(pp+PAGE_SIZE);
    if(pp2 != (heap_end + PAGE_SIZE * 9)){
        panic("ERROR: Mapped Incorrect Page!");
    }
    pp2 = convert_virt_to_phys(pp+2*PAGE_SIZE);
    if(pp2 != (heap_end + PAGE_SIZE * 10)){
        panic("ERROR: Mapped Incorrect Page!");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_set_page_flags\n");
    int_fast8_t temp_flag = mem_get_flags((const uintptr_t)0xC0300000);
    kprintf("Current Flags: %x\n", temp_flag);
    memory_set_page_flags((const void *)0xC0300000, (int_fast8_t)(PTE_R | PTE_X | PTE_U));
    temp_flag = mem_get_flags((const uintptr_t)0xC0300000);
    kprintf("Changed Flags: %x\n", temp_flag);
    if(temp_flag != (int_fast8_t)(PTE_R | PTE_X | PTE_U | PTE_A | PTE_D | PTE_V)){
        panic("ERROR: Flags don't match expected!");
    }

    temp_flag = mem_get_flags((const uintptr_t)0xC0301000);
    kprintf("Current Flags: %x\n", temp_flag);
    memory_set_page_flags((const void *)0xC0301000, (int_fast8_t)(PTE_R | PTE_X | PTE_U));
    temp_flag = mem_get_flags((const uintptr_t)0xC0301000);
    kprintf("Changed Flags: %x\n", temp_flag);
    if(temp_flag != (int_fast8_t)(PTE_R | PTE_X | PTE_U | PTE_A | PTE_D | PTE_V)){
        panic("ERROR: Flags don't match expected!");
    }
    
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_set_range_flags\n");
    temp_flag = mem_get_flags((const uintptr_t)0xC0300000);
    int_fast8_t temp_flag2 = mem_get_flags((const uintptr_t)0xC0301000);
    int_fast8_t temp_flag3 = mem_get_flags((const uintptr_t)0xC0302000);
    kprintf("Flags in range 0xC0300000-0xC0303000: %x, %x, %x\n", temp_flag, temp_flag2, temp_flag3);
    memory_set_range_flags((const void *)0xC0300000, PAGE_SIZE*3, (int_fast8_t)(PTE_R | PTE_U));
    temp_flag = mem_get_flags((const uintptr_t)0xC0300000);
    temp_flag2 = mem_get_flags((const uintptr_t)0xC0301000);
    temp_flag3 = mem_get_flags((const uintptr_t)0xC0302000);
    kprintf("Flags in range 0xC0300000-0xC0303000: %x, %x, %x\n", temp_flag, temp_flag2, temp_flag3);
    if(temp_flag != (int_fast8_t)(PTE_R | PTE_U | PTE_A | PTE_D | PTE_V)){
        panic("ERROR: Flags don't match expected!");
    }
    if(temp_flag2 != (int_fast8_t)(PTE_R | PTE_U | PTE_A | PTE_D | PTE_V)){
        panic("ERROR: Flags don't match expected!");
    }
    if(temp_flag3 != (int_fast8_t)(PTE_R | PTE_U | PTE_A | PTE_D | PTE_V)){
        panic("ERROR: Flags don't match expected!");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_validate_vptr_len (Extra Credit)\n");

    int mem_val = memory_validate_vptr_len((void *)0xC0300000, PAGE_SIZE*3, (int_fast8_t)(PTE_R | PTE_U));
    if(mem_val == 0){
        panic("Error! Memory Validate Failed!");
    }

    mem_val = memory_validate_vptr_len((const void *)0xC0300000, PAGE_SIZE*4, (int_fast8_t)(PTE_R | PTE_U));
    if(mem_val == 1){
        panic("Memory Validate Failed!");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_validate_vstr (Extra Credit)\n");
    pp = (void *)0xC0300000;
    char *char_ptr = (char *)convert_virt_to_phys(pp);
    kprintf("%p\n", char_ptr);
    for(int i = 0; i < PAGE_SIZE; i++){
        *char_ptr = 'a';
        char_ptr++;
    }
    *char_ptr = '\0';
    int str_validate = memory_validate_vstr(pp, (int_fast8_t)(PTE_U));
    if(str_validate == 0){
        panic("Error! String not detected as null terminated");
    }

    kprintf("SUCCESS\n\n");

    kprintf("Testing memory_space_reclaim && memory_unmap_and_free_user\n");
    
    memory_unmap_and_free_user();
    kprintf("Unmapped User memory\n");

    kprintf("Checking for previously allocated ranges\n");
    pp = (void *)0xC0300000;
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != NULL){
        panic("Did not unmap every User Value!");
    }

    pp = (void *)0xC0001000;
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != NULL){
        panic("Did not unmap every User Value!");
    }

    pp = (void *)0xC0002000;
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != NULL){
        panic("Did not unmap every User Value!");
    }
    kprintf("Did not find any\n");
    kprintf("SUCCESS\n\n");

    kprintf("Simulating User Space\n");
    kprintf("Writing to unmapped virtual memory\n");
    pp = (void *)0xC2000000;
    pp2 = convert_virt_to_phys(pp);
    if(pp2 != NULL){
        panic("Error! Unmapped memory is mapped!");
    }
    if(pp2 == NULL){
        memory_handle_page_fault(pp);
    }
    char_ptr = (char *)convert_virt_to_phys(pp);
    if(char_ptr == NULL){
        panic("Error! Should be mapped now!");
    }
    *char_ptr = 'a';
    if(*char_ptr != 'a'){
        panic("Did not write correctly!");
    }
    kprintf("Successfully demonstrated demand-paging\n\n");
    
    kprintf("Testing Reading/writing a kernel page from user-mode\n");
    pp = (void *)0x80300000; //In kernel space
    if(memory_handle_page_fault_simulated(pp) == 0){
        panic("Error! Page Fault should have triggered panic!");
    }
    kprintf("Panic Triggered! Wrote in incorrect area\n");
    kprintf("SUCCESS \n\n");

    kprintf("Simulating executing instruction without correct flag\n");
    pp = (void *)0xC0000000; //In kernel space
    memory_alloc_and_map_page((uintptr_t)pp, (int_fast8_t)(PTE_W | PTE_R));
    memory_alloc_and_map_page((uintptr_t)pp+PAGE_SIZE, (int_fast8_t)(PTE_X | PTE_W));
    memory_alloc_and_map_page((uintptr_t)pp+PAGE_SIZE*2, (int_fast8_t)(PTE_X | PTE_R));
    int exec = write_to_mem_simulated(pp, (int_fast8_t)(PTE_X));
    if(exec != 1){
        panic("Error! Tried to Execute Non Executable code!!");
    }
    kprintf("Successfully threw an error\n");
    
    kprintf("Simulating writing and reading without correct flag\n");

    int read = write_to_mem_simulated(pp+PAGE_SIZE, (int_fast8_t)(PTE_R));
    if(read != 1){
        panic("Error! Tried to Read Non Readable code!!");
    }

    int write = write_to_mem_simulated(pp+PAGE_SIZE*2, (int_fast8_t)(PTE_W));
    if(write != 1){
        panic("Error! Tried to Write Non Writable code!!");
    }
    kprintf("SUCCESS\n\n");

    kprintf("Succesfully Passed All Tests:\n");
    kprintf("PASSED memory_alloc_page\n");
    kprintf("PASSED memory_free_page\n");
    kprintf("PASSED memory_alloc_and_map_page\n");
    kprintf("PASSED memory_alloc_and_map_range\n");
    kprintf("PASSED memory_set_page_flags\n");
    kprintf("PASSED memory_set_range_flags\n");
    kprintf("PASSED memory_space_reclaim\n");
    kprintf("PASSED memory_unmap_and_free_user\n");
    kprintf("PASSED memory_validate_vptr_len (Extra Credit)\n");
    kprintf("PASSED memory_validate_vstr (Extra Credit)\n");

}

