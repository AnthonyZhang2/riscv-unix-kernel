// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 2

// INTERNAL FUNCTION DECLARATIONS
//
#define PLIC_INTPND (PLIC_IOBASE + 0x001000) //interrupt pending memory map offset
#define PLIC_INTENA (PLIC_IOBASE + 0x002000) //interrupt enable memory map offset
#define PLIC_THRESH (PLIC_IOBASE + 0x200000) //interrupt threshold memory map offset
#define PLIC_CLAIM (PLIC_IOBASE + 0x200004) //interrupt claim memory map offset
#define PLIC_REGSIZE 32

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 
// INPUTS: N/A
// OUTPUTS: N/A
// EFFECTS: This function configures the PLIC chip by setting all priorities 0-1023 to 0 & enabling sources
// DESCRIPTION: This function runs through a loop and runs the corresponding plic functions to 
// initialize the chip.
void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 0 (M mode on hart 0).
    // context 1 (S mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(1, i);
    }
}
// INPUTS: 
// int irqno : Interrupt Request number aka where it came from
// int prio : The priority to set for that IRQ number.
// OUTPUTS: N/A
// EFFECTS: This function configures the priority for a corresponding IRQ number.
// DESCRIPTION: This function just calls the corresponding PLIC function and passes
// in the priority and irqno. See plic_set_source_priority for more in depth info.
extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

// INPUTS: int irqno : Interrupt request number
// OUTPUTS: N/A
// EFFECTS: This function disables interupts for a specific interrupt request number
// DESCRIPTION: This function checks if irqno is a valid source. Then it calls the 
// corresponding plic functnio. see plic_set_source_priority for more in depth info.
extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

// INPUTS: N/A
// OUTPUTS: int claim_context_interrupt
// EFFECTS: This function claims an interrupt while being hardwired to context 0.
// DESCRIPTION: This function just calls and returns the corresponding function.
// See plic_claim_context_interrupt for more information.
extern int plic_claim_irq(void) {
    // Hardwired context 0 (M mode on hart 0)
    // Hardwired context 1 (S mode on hart 0).
    trace("%s()", __func__);
    return plic_claim_context_interrupt(1);
}

// INPUTS: int irqno : interrupt request number
// OUTPUTS: N/A
// EFFECTS: This function completes an interrupt in hardwired context 0.
// DESCRIPTION: This function calls the corresponding function. 
// See plic_complete_context_interrupt for more information.
extern void plic_close_irq(int irqno) {
    // Hardwired context 0 (M mode on hart 0)
    
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(1, irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

// INPUTS: 
// uint32_t srcno : The source of the interrupt to set level for.
// uint32_t level : The level to set that source priorty to.
// OUTPUTS: N/A
// EFFECTS: This function sets the srcno interrupt to level specified.
// DESCRIPTION: This function calculates the memory offset of the PLIC
// source registers. It then just sets the register there to level.
void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    if(srcno > PLIC_SRCCNT){
        return;
    }
    if(level > PLIC_PRIO_MAX){
        level = PLIC_PRIO_MAX;
    }
    *((uint32_t *)PLIC_IOBASE + srcno) = level; //0X1000 for context 1
}

// INPUTS: uint32_t srcno : The source of the interrupt read priority for.
// OUTPUTS: int pending : if source is pending, 1. 0 otherwise.
// EFFECTS: This function reads the corresponding pending register for a interrupt source
// DESCRIPTION: This function calculates the memory offset of the PLIC
// pending registers. It then just checks if it's zero or not, returning 1 or 0.
int plic_source_pending(uint32_t srcno) {
    if(srcno == 0){
        return 0;
    }
    uint32_t bitval = srcno % 32; //calculate which reg
    uint32_t wordnum = srcno / 32; //calculate which bit inside reg
    
    uint32_t regval = *((uint32_t *)PLIC_INTPND + wordnum);
    bitval = regval & bitval;

    if(bitval > 0){
        return 1;
    }
    else{
        return 0;
    }
}

// INPUTS: 
// uint32_t ctxno : The context of the interrupt to enable source for.
// uint32_t srcno : The source to enable interrupts for.
// OUTPUTS: N/A
// EFFECTS: This function sets the bit for the ctxno 
// DESCRIPTION: This function calculates the memory offset of the PLIC
// interrupt enable registers. It then calculates the bit mask to enable and 
// set the corresponding bit to 1. Then it OR's it to make it 1.
void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    if(ctxno >= PLIC_CTXCNT){ 
        return;
    }
    if(srcno >= PLIC_SRCCNT){
        return;
    }
    uint32_t bitmask = (1<<(srcno/PLIC_REGSIZE));
    *((uint32_t *)PLIC_INTENA + ((ctxno*0X80)/sizeof(uint32_t)) + srcno%PLIC_REGSIZE) |= bitmask; //set bit to 1
}

// INPUTS: 
// uint32_t ctxno : The context of the interrupt to disable source for.
// uint32_t srcno : The source to disable interrupts for.
// OUTPUTS: N/A
// EFFECTS: This function sets the bit for the ctxno 
// DESCRIPTION: This function calculates the memory offset of the PLIC
// interrupt enable registers. It then calculates the bit mask to disable and 
// set the corresponding bit to 1. Then it OR's it to make it 1.
void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    if(ctxno >= PLIC_CTXCNT){
        return;
    }
    if(srcid >= PLIC_SRCCNT){
        return;
    }
    uint32_t bitmask = ~(1<<(srcid/PLIC_REGSIZE)); //get correct bit and flip it to zero
    *((uint32_t *)PLIC_INTENA + ((ctxno*0X80)/sizeof(uint32_t)) + srcid%PLIC_REGSIZE) &= bitmask;
}

// INPUTS: 
// uint32_t ctxno : The context of the interrupt to set context threshold for.
// uint32_t level : The context threshold to set.
// OUTPUTS: N/A
// EFFECTS: This function sets context threshold for the given context.
// DESCRIPTION: This function checks if the context threshold is valid. Then it
// calculates the needed memory offset with the context level. It sets this register to level.
void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    if(ctxno >= PLIC_CTXCNT){
        return;
    }
    *((uint32_t *)PLIC_THRESH + ((ctxno*0x1000)/sizeof(uint32_t))) = level; //0x1000 because each context is located at a 0x1000 offset
}

// INPUTS: uint32_t ctxno : The context to claim.
// OUTPUTS: uint32_t context_reg : The register located at the claim location.
// EFFECTS: This function claims a context interrupt and returns the register at that location.
// DESCRIPTION: This function checks if the context is valid. If not, it will return 0. 
// Then, it will calculate the memory address and then return that context's register.
uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    if(ctxno >= PLIC_CTXCNT){
        return 0;
    }
    return *((uint32_t *)PLIC_CLAIM + ((ctxno*0x1000)/sizeof(uint32_t))); //0x1000 for the context offset, threshold address
}

// INPUTS: 
// uint32_t ctxno : The context of the interrupt to complete a context interrupt for.
// uint32_t srcno : The interrupt source number to insert into the register.
// OUTPUTS: N/A
// EFFECTS: This function completes a context interrupt and inserts the srcno there.
// DESCRIPTION: This function checks if the context is valid. If not, the function will return.
// Then, it will calculate the memory address and then set that context's register to the interrupt source.
void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    if(ctxno >= PLIC_CTXCNT){
        return;
    }
    *((uint32_t *)PLIC_CLAIM + ((ctxno*0x1000)/sizeof(uint32_t))) = srcno; //0x1000 for the context offset, 0x4 for offset from threshold address
}