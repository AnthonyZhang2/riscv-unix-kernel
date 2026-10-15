// process.c - user process
//

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif

#include "process.h"
#include "memory.h"
#include "halt.h"
#include "elf.h"
#include "heap.h"

// COMPILE-TIME PARAMETERS
//

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

#ifndef USP
#define USP 0xD0000000
#endif

// INTERNAL FUNCTION DECLARATIONS
//

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_PID 0

// The main user process struct

static struct process main_proc;

// A table of pointers to all user processes in the system

struct process * proctab[NPROC] = {
    [MAIN_PID] = &main_proc
};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

//inputs: none
//outputs: none
//side-effects: sets the main process struct
//description: initializes the main process struct around the main kernel thread

void procmgr_init(void){
    main_proc.id = MAIN_PID;
    main_proc.tid = running_thread();
    main_proc.mtag = active_memory_space();
    thread_set_process(main_proc.tid, &main_proc);
}

//inputs: io intf from which to load executable 
//outputs: negative error code on error
//side-effects: clears all virtual memory mappings from other user processes,
//              loads the executable starting at user memory space,
//              and jumps to user program in user mode
//description: executes a program referred to by the io intf passed as argument in user mode

int process_exec(struct io_intf * exeio){
    void (*exe_entry) (void);
    int result;

    assert(exeio != NULL);      //check for invaild io_intf

    memory_unmap_and_free_user();

    result = elf_load(exeio, &exe_entry);
    if(result < 0){                             //error in elf load
        kprintf("Elf load failed");
        return -EACCESS;
    }

    thread_jump_to_user(USP, (uintptr_t) exe_entry);
    kprintf("Jump to user failed");             //error in jumping to user program
    return -EACCESS;
}

//inputs: none
//outputs: none
//side-effects: releases all resources associated with the current process
//description: cleans up a finshed process by reclaiming the resources of the process

void process_exit(void){
    int io_count = 0;
    struct io_intf ** const cur_iotab = current_process()->iotab;

    memory_space_reclaim();

    while(io_count < PROCESS_IOMAX){
        if(cur_iotab[io_count]!= NULL){
            if(cur_iotab[io_count]->ops != NULL)
                cur_iotab[io_count]->ops->close(cur_iotab[io_count]);
        }
        io_count++;
    }

    thread_exit(); 
}

void process_terminate(int pid){
    
}

//inputs: none
//outputs: pid
//side-effects: adds a new process to process table
//description: creates a new process struct and adds it to next spot in process table

int process_create(void){
    int pid;
    struct  process * new_proc;    
       
    // Find a free process slot.

    pid = 0;
    while (++pid < NPROC)
        if (proctab[pid] == NULL)
            break;
    
    if (pid == NPROC)
        panic("Too many processes");
    
    new_proc = kmalloc(sizeof(struct process));
    proctab[pid] = new_proc;
    new_proc->id = pid;

    return pid;

}

//inputs: parent trap_frame pointer
//outputs: pid to parent process, 0  to child process
//side-effects: forks the current process and creates a duplicate child process
//description: forks the current process and jumps into the child process in user mode

int process_fork(const struct trap_frame *parent_tfr){
    int child_id = process_create();
    struct process *child_proc = proctab[child_id];
    struct process *parent_proc = current_process();
    
    child_proc->mtag = memory_space_clone((uint_fast16_t)child_id);
    

    for (int i = 0; i < PROCESS_IOMAX; i++) {
        if (parent_proc->iotab[i] != NULL) {
            child_proc->iotab[i] = parent_proc->iotab[i];
            ioref(child_proc->iotab[i]);
        } else {
            child_proc->iotab[i] = NULL;
        }
    }
    
    // Fork the thread
    child_proc->tid = thread_fork_to_user(child_proc, parent_tfr);
    
    // Set the process for the new thread
    thread_set_process(child_proc->tid, child_proc);
    
    return child_proc->tid;

    
//     child_proc->mtag = memory_space_clone(child->id);
}