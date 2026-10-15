// syscall.c - System call handing
// 

#include "trap.h"
#include "process.h"
#include "console.h"
#include "kfs.h"
#include "memory.h"
#include "string.h"
#include "error.h"
#include "scnum.h"
#include "device.h"
#include "thread.h"
#include "timer.h"
#include "heap.h"

/**
 * handles process exit system call
 * 
 * @return -1 (should never return)
 */
static int sysexit(void) {
    process_exit();
    // should never return
    return -1;
}

/**
 * outputs a message to the console
 * 
 * @param msg   null-terminated string to output
 * 
 * @return 0 on success, -EINVAL if msg is NULL
 */
static int sysmsgout(const char * msg) {
    if (msg == NULL) {
        return -EINVAL;
    }
    console_puts(msg);
    return 0;
}

/**
 * opens a device and assigns it to a file descriptor
 * 
 * @param fd      file descriptor to use
 * @param name    name of device to open
 * @param instno  instance number of device
 * 
 * @return 0 on success, negative error code on failure
 */
static int sysdevopen(int fd, const char *name, int instno) {
    struct process *proc;
    struct io_intf *io;
    int ret;
    
    if (fd < 0 || fd >= PROCESS_IOMAX || name == NULL) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] != NULL) {
        return -EBUSY;  // file descriptor already in use
    }

    // use the device_open function from device.h
    ret = device_open(&io, name, instno);
    if (ret < 0) {
        return ret;
    }

    // store the device interface in the process's io table
    proc->iotab[fd] = io;
    return 0;
}

/**
 * opens a file and assigns it to a file descriptor
 * 
 * @param fd     file descriptor to use
 * @param name   name of file to open
 * 
 * @return 0 on success, negative error code on failure
 */
static int sysfsopen(int fd, const char *name) {
    struct process *proc;
    struct io_intf *io;
    int ret;

    if (fd < 0 || fd >= PROCESS_IOMAX || name == NULL) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] != NULL) {
        return -EBUSY;  // file descriptor already in use
    }

    ret = fs_open(name, &io);
    if (ret < 0) {
        return ret;
    }

    proc->iotab[fd] = io;
    return 0;
}

/**
 * closes an open file descriptor
 * 
 * @param fd    file descriptor to close
 * 
 * @return 0 on success, negative error code on failure
 */
static int sysclose(int fd) {
    struct process *proc;

    if (fd < 0 || fd >= PROCESS_IOMAX) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] == NULL || proc->iotab[fd]->ops == NULL) {
        return -EBADFD;  // Invalid file descriptor
    }

    proc->iotab[fd]->ops->close(proc->iotab[fd]);
    proc->iotab[fd] = NULL;
    return 0;
}

/**
 * reads data from an open file descriptor
 * 
 * @param fd     file descriptor to read from
 * @param buf    buffer to store read data
 * @param bufsz  size of buffer
 * 
 * @return number of bytes read on success, negative error code on failure
 */
static long sysread(int fd, void *buf, size_t bufsz) {
    struct process *proc;

    if (fd < 0 || fd >= PROCESS_IOMAX || buf == NULL) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] == NULL || proc->iotab[fd]->ops == NULL) {
        return -EBADFD;
    }

    return proc->iotab[fd]->ops->read(proc->iotab[fd], buf, bufsz);
}

/**
 * writes data to an open file descriptor
 * 
 * @param fd    file descriptor to write to
 * @param buf   buffer containing data to write
 * @param len   number of bytes to write
 * 
 * @return number of bytes written on success, negative error code on failure
 */
static long syswrite(int fd, const void *buf, size_t len) {
    struct process *proc;

    if (fd < 0 || fd >= PROCESS_IOMAX || buf == NULL) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] == NULL || proc->iotab[fd]->ops == NULL) {
        return -EBADFD;
    }
    return proc->iotab[fd]->ops->write(proc->iotab[fd], buf, len);
}

/**
 * performs control operations on an open file descriptor
 * 
 * @param fd    file descriptor to control
 * @param cmd   control command
 * @param arg   argument for command
 * 
 * @return 0 on success, negative error code on failure
 */
static int sysioctl(int fd, const int cmd, void *arg) {
    struct process *proc;

    if (fd < 0 || fd >= PROCESS_IOMAX) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] == NULL) {
        return -EBADFD;
    }

    return proc->iotab[fd]->ops->ctl(proc->iotab[fd], cmd, arg);
}

/**
 * executes a program from an open file descriptor
 * 
 * @param fd    file descriptor containing program to execute
 * 
 * @return negative error code on failure (does not return on success)
 */
static int sysexec(int fd) {
    struct process *proc;

    if (fd < 0 || fd >= PROCESS_IOMAX) {
        return -EINVAL;
    }

    proc = current_process();
    if (proc->iotab[fd] == NULL) {
        return -EBADFD;
    }

    return process_exec(proc->iotab[fd]);
}

//inputs: current trap_frame pointer
//output: pid to parent process, 0 to child process
//side-effects: forks the current process and creates a duplicate child process
//description: forks the current process and jumps to child in user mode

static int sysfork(const struct trap_frame * tfr){
    /*
    int io_count;
    int child_pid;
    int child_tid;

    //const struct thread * tp = tfr->x[TFR_ZERO];
    struct process * parent_proc = thread_process(running_thread());
    child_pid = process_create();
    struct process * child_proc = proctab[child_pid];

    io_count = 0;
    while (io_count < PROCESS_IOMAX)
    {   if(parent_proc->iotab[io_count] != NULL){
            child_proc->iotab[io_count] = parent_proc->iotab[io_count];
            child_proc->iotab[io_count]->refcnt++;
        }
        io_count++;
    }

    child_tid = thread_fork_to_user(child_proc, tfr);
    
    if(!child_tid)
        return child_tid;
    return child_pid;
    */
    int child_tid = process_fork(tfr);
    return child_tid;
}

/**
 * waits for a thread to exit
 * 
 * @param tid   thread ID to wait for, 0 to wait for any thread
 * 
 * @return thread ID on success, negative error code on failure
 */
static int syswait(int tid) {
    if (tid < 0) {
        return -EINVAL;
    }

    if (tid == 0) {
        return thread_join_any();   // wait for any thread
    } else {
        return thread_join(tid);    // wait for specific thread
    }
}

/**
 * sleeps for a given number of microseconds
 * 
 * @param us    number of microseconds to sleep
 * 
 * @return 0 on success, negative error code on failure
 */
static int sysusleep(unsigned long us) {
    struct alarm* al = kmalloc(sizeof(struct alarm));
    alarm_init(al, "sys_alarm");
    alarm_sleep_us(al, us);
    return 0;
}

/**
 * handles system calls by dispatching to appropriate handler
 * 
 * @param tfr   pointer to trap frame containing system call arguments
 */
void syscall_handler(struct trap_frame *tfr) {
    long ret;

    switch (tfr->x[TFR_A7]) {  // system call number is in a7
        case SYSCALL_EXIT:
            ret = sysexit();
            break;

        case SYSCALL_MSGOUT:
            ret = sysmsgout((const char *)tfr->x[TFR_A0]);
            break;

        case SYSCALL_DEVOPEN:
            ret = sysdevopen(tfr->x[TFR_A0], (const char *)tfr->x[TFR_A1], tfr->x[TFR_A2]);
            break;

        case SYSCALL_FSOPEN:
            ret = sysfsopen(tfr->x[TFR_A0], (const char *)tfr->x[TFR_A1]);
            break;

        case SYSCALL_CLOSE:
            ret = sysclose(tfr->x[TFR_A0]);
            break;

        case SYSCALL_READ:
            ret = sysread(tfr->x[TFR_A0], (void *)tfr->x[TFR_A1], tfr->x[TFR_A2]);
            break;

        case SYSCALL_WRITE:
            ret = syswrite(tfr->x[TFR_A0], (const void *)tfr->x[TFR_A1], tfr->x[TFR_A2]);
            break;

        case SYSCALL_IOCTL:
            ret = sysioctl(tfr->x[TFR_A0], tfr->x[TFR_A1], (void *)tfr->x[TFR_A2]);
            break;

        case SYSCALL_EXEC:
            ret = sysexec(tfr->x[TFR_A0]);
            break;

        case SYSCALL_FORK:
            int saved_intr_state = intr_disable();
            ret = sysfork(tfr);
            intr_restore(saved_intr_state);
            if(ret == 0){
                //saved_intr_state = intr_disable();
                tfr->x[TFR_ZERO] = (uint64_t)thread_anchor(running_thread());
                tfr->x[TFR_TP] = (uint64_t)current_thread();
                tfr->x[TFR_A0] = 0;
                //intr_restore(saved_intr_state);
            }
            else{
                tfr->x[TFR_A0] = ret;
            }
            break;

        case SYSCALL_WAIT:
            ret = syswait(tfr->x[TFR_A0]);
            break;

        case SYSCALL_USLEEP:
            ret = sysusleep(tfr->x[TFR_A0]);
            break;

        default:
            ret = -EINVAL;  // invalid system call
            break;
    }

    tfr->x[TFR_A0] = ret;  // store return value directly in trap frame
}

/**
 * validates that a user-space memory range is readable
 * 
 * @param ptr    start of memory range
 * @param size   size of memory range
 * 
 * @return 0 on success, negative error code on failure
 */
static int __attribute__((unused)) validate_user_read(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;
    
    // check for overflow
    if (end < start) {
        return -EINVAL;
    }

    // validate memory range has read access
    if (!memory_validate_vptr_len(ptr, size, PTE_R | PTE_U)) {
        return -EBADFMT;
    }
    return 0;
}

/**
 * validates that a user-space memory range is writable
 * 
 * @param ptr    start of memory range
 * @param size   size of memory range
 * 
 * @return 0 on success, negative error code on failure
 */
static int __attribute__((unused)) validate_user_write(void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;
    
    if (end < start) {
        return -EINVAL;
    }

    // validate memory range has write access
    if (!memory_validate_vptr_len(ptr, size, PTE_W | PTE_U)) {
        return -EBADFMT;
    }
    return 0;
}




