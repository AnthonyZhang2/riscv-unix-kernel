# thrasm.s - Special functions called from thread.c
#

        .macro  restore_sstatus_and_sepc
        # Restores sstatus and sepc from trap frame to which sp points. We use
        # t6 as a temporary, so must be used after this macro, not before.

        ld      t6, 33*8(sp)
        csrw    sepc, t6
        ld      t6, 32*8(sp)
        csrw    sstatus, t6
        .endm

        .macro  restore_gprs_except_t6_and_sp

        ld      x30, 30*8(sp)   # x30 is t5
        ld      x29, 29*8(sp)   # x29 is t4
        ld      x28, 28*8(sp)   # x28 is t3
        ld      x27, 27*8(sp)   # x27 is s11
        ld      x26, 26*8(sp)   # x26 is s10
        ld      x25, 25*8(sp)   # x25 is s9
        ld      x24, 24*8(sp)   # x24 is s8
        ld      x23, 23*8(sp)   # x23 is s7
        ld      x22, 22*8(sp)   # x22 is s6
        ld      x21, 21*8(sp)   # x21 is s5
        ld      x20, 20*8(sp)   # x20 is s4
        ld      x19, 19*8(sp)   # x19 is s3
        ld      x18, 18*8(sp)   # x18 is s2
        ld      x17, 17*8(sp)   # x17 is a7
        ld      x16, 16*8(sp)   # x16 is a6
        ld      x15, 15*8(sp)   # x15 is a5
        ld      x14, 14*8(sp)   # x14 is a4
        ld      x13, 13*8(sp)   # x13 is a3
        ld      x12, 12*8(sp)   # x12 is a2
        ld      x11, 11*8(sp)   # x11 is a1
        ld      x10, 10*8(sp)   # x10 is a0
        ld      x9, 9*8(sp)     # x9 is s1
        ld      x8, 8*8(sp)     # x8 is s0/fp
        ld      x7, 7*8(sp)     # x7 is t2
        ld      x6, 6*8(sp)     # x6 is t1
        ld      x5, 5*8(sp)     # x5 is t0
#        ld      x4, 4*8(sp)     # x4 is tp
        ld      x3, 3*8(sp)     # x3 is gp
        ld      x1, 1*8(sp)     # x1 is ra

        .endm

# struct thread * _thread_swtch(struct thread * resuming_thread)

# Switches from the currently running thread to another thread and returns when
# the current thread is scheduled to run again. Argument /resuming_thread/ is
# the thread to be resumed. Returns a pointer to the previously-scheduled
# thread. This function is called in thread.c. The spelling of swtch is
# historic.

        .text
        .global _thread_swtch
        .type   _thread_swtch, @function

_thread_swtch:

        # We only need to save the ra and s0 - s12 registers. Save them on
        # the stack and then save the stack pointer. Our declaration is:
        # 
        #   struct thread * _thread_swtch(struct thread * resuming_thread);
        #
        # The currently running thread is suspended and resuming_thread is
        # restored to execution. swtch returns when execution is switched back
        # to the calling thread. The return value is the previously executing
        # thread. Interrupts are enabled when swtch returns.
        #
        # tp = pointer to struct thread of current thread (to be suspended)
        # a0 = pointer to struct thread of thread to be resumed
        # 

        sd      s0, 0*8(tp)
        sd      s1, 1*8(tp)
        sd      s2, 2*8(tp)
        sd      s3, 3*8(tp)
        sd      s4, 4*8(tp)
        sd      s5, 5*8(tp)
        sd      s6, 6*8(tp)
        sd      s7, 7*8(tp)
        sd      s8, 8*8(tp)
        sd      s9, 9*8(tp)
        sd      s10, 10*8(tp)
        sd      s11, 11*8(tp)
        sd      ra, 12*8(tp)
        sd      sp, 13*8(tp)

        mv      tp, a0

        ld      sp, 13*8(tp)
        ld      ra, 12*8(tp)
        ld      s11, 11*8(tp)
        ld      s10, 10*8(tp)
        ld      s9, 9*8(tp)
        ld      s8, 8*8(tp)
        ld      s7, 7*8(tp)
        ld      s6, 6*8(tp)
        ld      s5, 5*8(tp)
        ld      s4, 4*8(tp)
        ld      s3, 3*8(tp)
        ld      s2, 2*8(tp)
        ld      s1, 1*8(tp)
        ld      s0, 0*8(tp)
                
        ret

        .global _thread_setup
        .type   _thread_setup, @function

# void _thread_setup (
#      struct thread * thr,             in a0
#      void * sp,                       in a1
#      void (*start)(void *, void *),   in a2
#      void * arg)                      in a3
#
# Sets up the initial context for a new thread. The thread will begin execution
# in /start/, receiving the five arguments passed to _thread_set after /start/.

_thread_setup:
        # Write initial register values into struct thread_context, which is the
        # first member of struct thread.
        
        sd      a1, 13*8(a0)    # Initial sp
        sd      a2, 11*8(a0)    # s11 <- start
        sd      a3, 0*8(a0)     # s0 <- arg 0
        sd      a4, 1*8(a0)     # s1 <- arg 1
        sd      a5, 2*8(a0)     # s2 <- arg 2
        sd      a6, 3*8(a0)     # s3 <- arg 3
        sd      a7, 4*8(a0)     # s4 <- arg 4

        # put address of thread entry glue into t1 and continue execution at 1f

        jal     t0, 1f

        # The glue code below is executed when we first switch into the new thread

        la      ra, thread_exit # child will return to thread_exit
        mv      a0, s0          # get arg argument to child from s0
        mv      a1, s1          # get arg argument to child from s0
        mv      fp, sp          # frame pointer = stack pointer
        jr      s11             # jump to child entry point (in s1)

1:      # Execution of _thread_setup continues here

        sd      t0, 12*8(a0)    # put address of above glue code into ra slot

        ret

        .global _thread_finish_jump
        .type   _thread_finish_jump, @function

#inputs: thread stack anchor pointer, user stack pointer, user program counters
#outputs: none 
#side-effects: starts user program at upc in user mode with stack pointer set to usp
#description: completes jump to user program in user mode from supervisor mode

# void __attribute__ ((noreturn)) _thread_finish_jump (
#      struct thread_stack_anchor * stack_anchor,
#      uintptr_t usp, uintptr_t upc, ...);


_thread_finish_jump:
        # While in user mode, sscratch points to a struct thread_stack_anchor
        # located at the base of the stack, which contains the current thread
        # pointer and serves as our starting stack pointer.

        # TODO: FIXME your code here

        la t0, _trap_entry_from_umode
        csrw stvec, t0          #set stvec to umode trap vector                
        csrw sscratch, a0       #set sscratch to kernel sp
        mv sp, a1               #set sp to usp

        sret

#inputs: child thread pointer, parent trap frame pointer
#outputs: 0 to child process, none to parent process
#side-effects: saves current process and restores parent trap frame into child
#description: jump to child process in user mode

#void __attribute__ ((noreturn)) _thread_finish_fork (
#   struct thread * child, const struct trap_frame * parent_tfr);

        .global _thread_finish_fork
        .type   _thread_finish_fork, @function
        
_thread_finish_fork:
        # Save the current thread's context
        sd      s0, 0*8(tp)
        sd      s1, 1*8(tp)
        sd      s2, 2*8(tp)
        sd      s3, 3*8(tp)
        sd      s4, 4*8(tp)
        sd      s5, 5*8(tp)
        sd      s6, 6*8(tp)
        sd      s7, 7*8(tp)
        sd      s8, 8*8(tp)
        sd      s9, 9*8(tp)
        sd      s10, 10*8(tp)
        sd      s11, 11*8(tp)
        sd      ra, 12*8(tp)
        sd      sp, 13*8(tp)

        # Switch to the child thread
        mv      tp, a0

        ld t6, 13*8(a0)         #load child thread stack anchor 
        csrw sscratch, t6

               # restore trap vector to umode handler before return
        la      t6, _trap_entry_from_umode
        csrw    stvec, t6

        mv sp, a1

        restore_sstatus_and_sepc
        restore_gprs_except_t6_and_sp
        mv      a0, zero                #set child return value to zero
        csrr    t6, sepc                #read sepc
        addi    t6,t6, 4                #set sepc to next instruction
        csrw    sepc, t6
        
        ld      t6, 31*8(sp)            # restore t6
        ld      sp, 2*8(sp)             # restore original user sp    

        sret


#        # Load the parent's trap frame into the child's registers
#        ld      ra, 1*8(a1)
#        ld      sp, 2*8(a1)
#        ld      gp, 3*8(a1)
        # Don't load tp, it should point to the child's thread struct
#        ld      t0, 5*8(a1)
#        ld      t1, 6*8(a1)
#        ld      t2, 7*8(a1)
#        ld      s0, 8*8(a1)
#        ld      s1, 9*8(a1)
        
#        ld      a2, 12*8(a1)
#        ld      a3, 13*8(a1)
#        ld      a4, 14*8(a1)
#        ld      a5, 15*8(a1)
#        ld      a6, 16*8(a1)
#        ld      a7, 17*8(a1)
#        ld      s2, 18*8(a1)
#        ld      s4, 20*8(a1)
#        ld      s5, 21*8(a1)
#        ld      s6, 22*8(a1)
#        ld      s7, 23*8(a1)
#        ld      s8, 24*8(a1)
#        ld      s9, 25*8(a1)
#        ld      s10, 26*8(a1)
#        ld      s11, 27*8(a1)
#        ld      t3, 28*8(a1)
#        ld      t4, 29*8(a1)
#        ld      t5, 30*8(a1)
#        ld      t6, 31*8(a1)

        # Load sepc and sstatus from the parent's trap frame
#        ld      t0, 33*8(a1)
#        csrw    sepc, t0
#        ld      t0, 32*8(a1)  
#        csrw    sstatus, t0

        # Jump back to the U mode interrupt handler
#        ld      tp, 0*8(a1)
#        ld      a0, 132(a0)
#        csrw    sscratch, a0

 #       addi      a0, zero, 0  # Set the return value to 0 for the child
 #       sret
#        j _trap_entry_from_umode

        # Parent process resumes here
        # Restore the parent's context
#        ld      s0, 0*8(tp)   
#        ld      s1, 1*8(tp)
#        ld      s2, 2*8(tp)
#        ld      s3, 3*8(tp) 
#        ld      s4, 4*8(tp)
#        ld      s5, 5*8(tp)
#        ld      s6, 6*8(tp)
#        ld      s7, 7*8(tp)
#        ld      s8, 8*8(tp)
#        ld      s9, 9*8(tp)
#        ld      s10, 10*8(tp)
#        ld      s11, 11*8(tp)
#        ld      ra, 12*8(tp)
#        ld      sp, 13*8(tp)

        # Set the return value to the child's tid for the parent
#        ld      a0, 144(a1)

        # Return to the parent
#        ret
        



# Statically allocated stack for the idle thread.

        .section        .data.stack, "wa", @progbits
        .balign          16
        
        .equ            IDLE_STACK_SIZE, 4096

        .global         _idle_stack_lowest
        .type           _idle_stack_lowest, @object
        .size           _idle_stack_lowest, IDLE_STACK_SIZE

        .global         _idle_stack_anchor
        .type           _idle_stack_anchor, @object
        .size           _idle_stack_anchor, 2*8

_idle_stack_lowest:
        .fill   IDLE_STACK_SIZE, 1, 0xA5

_idle_stack_anchor:
        .global idle_thread # from thread.c
        .dword  idle_thread
        .fill   8
        .end
        