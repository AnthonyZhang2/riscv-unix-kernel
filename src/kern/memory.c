// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;
void *mem_start_loc; //Start of free pages (for debugging)

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
};

struct pte {
    uint64_t flags:8;
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))
#define PTE_SIZE 8


// INTERNAL FUNCTION DECLARATIONS
//

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
static inline struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);


static inline int is_leaf(int_fast8_t flags);
static inline int is_valid(int_fast8_t flags);
static inline void sfence_vma(void);

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

// EXPORTED VARIABLE DEFINITIONS
//

// EXPORTED FUNCTION DEFINITIONS
// 

//Inputs:   N/A
//Outputs:  N/A
//Effects:  Initializes virtual memory and sets up the main page tables
//Description: First sets up the main page table mappings for PT2, PT1, and PT0.
//Then it enables paging by setting up the right memory space tags. It then
//Allocates the remaining space in the RAM for user_pages that we will use in the page tables.
void memory_init(void) {
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);
    
    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
        
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);

    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = round_up_ptr(heap_start, PAGE_SIZE);
    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += round_up_size (
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE;

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt);

    mem_start_loc = heap_end;

    // Put free pages on the free page list
    // TODO: FIXME implement this (must work with your implementation of
    // memory_alloc_page and memory_free_page).
    kprintf("Heap End: %p\n", heap_end);
    kprintf("Free List: %p\n", free_list);
    char *current_address = (char *)free_list;     //Char pointer for byte addressability
    for(int i = 0; i < page_cnt; i++){
        page = (union linked_page *)(current_address + PAGE_SIZE);
        union linked_page *current_page = (union linked_page *)current_address;

        current_page->next = page;
        
        current_address = current_address + PAGE_SIZE;
    }

    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

//Inputs:   uint_fast_16_t asid: asid for the new memory tag
//Outputs:  uintptr new_tag, the new tag created and switched to
//Effects:  Sets up root page table for new memory space.
//Description: First allocates a new physical page from the free_list. Then
// creates a new SV39 RISC-V Memory paging tag and sets the SATP register.
uintptr_t memory_space_create(uint_fast16_t asid){
    void *pp = memory_alloc_page();
    uintptr_t new_tag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        ((uintptr_t)asid << RISCV_SATP_ASID_shift) |
        pageptr_to_pagenum(pp);
    csrw_satp((uintptr_t)new_tag);
    sfence_vma();
    return new_tag;
}

//Inputs:   N/A
//Outputs:  N/A
//Effects:  Reclaims the original memory space
//Description: Calls memory_unmap_and_free_user and then changes to main memory space.
void memory_space_reclaim(void){
    memory_unmap_and_free_user();
    csrw_satp(main_mtag);
    sfence_vma();
}

//Inputs:   N/A
//Outputs:  
// void *alloc_page: the allocated page physical address
//Effects:  Takes the first physical address from free_list and returns it.
//Description: Takes the first physical address from free_list and returns it. 
// Makes free_list smaller
void *memory_alloc_page(void){
    union linked_page *alloc_page = free_list;
    if(alloc_page == NULL){
        panic("Memory Paging Free_List is Empty!");
    }
    free_list = free_list->next;
    return (void *)alloc_page;
}

//Inputs:   
// void *pp: physical pointer to free.
//Outputs:  N/A
//Effects:  Adds the current pointer back to the free_list.
//Description: Adds the current pointer back to the free_list
void memory_free_page(void *pp){
    union linked_page *new_head = (union linked_page *)pp;
    new_head->next = free_list;
    free_list = new_head;
}


//Inputs: 
// uintptr_t vma: Virtual address to map page to. Always maps at pt0.
// uint_fast8_t rwxug_flags: The flags to give to that newly mapped page.
//Outputs:  
// void * vma: mapped virtual address. 
//Effects:  Gets the current root page table and allocates a new pt0.
//Description: First checks if pt2, pt1 are leafs. If they are, change them
// to non leafs and allocate a page from free_list for new page tables.
// Then finally make the last pt0 pte a leaf.
void *memory_alloc_and_map_page(uintptr_t vma, uint_fast8_t rwxug_flags){
    if(vma >= USER_END_VMA || vma < USER_START_VMA){
        return NULL;
    }
    uintptr_t active_mtag = active_space_mtag();
    struct pte *pte2_final_pp = mtag_to_root(active_mtag) + VPN2(vma);
    struct pte *pt1_pp;

    kprintf("Mapped %p ", vma);

    if(is_leaf(pte2_final_pp->flags) || !is_valid(pte2_final_pp->flags)){ //Need to create new Page Table 1
        void *temp = memory_alloc_page();
        *pte2_final_pp = ptab_pte((const struct pte *)temp, (rwxug_flags & PTE_G));
        kprintf("| With PP1 Allocated at %p ", temp);
    }

    pt1_pp = ((struct pte *)pagenum_to_pageptr(pte2_final_pp->ppn)) + VPN1(vma); //Get pt1 pointer
    
    if(is_leaf(pt1_pp->flags) || !is_valid(pt1_pp->flags)){ //Need to create new Page Table 0
        void *pt0_pp = memory_alloc_page();
        *pt1_pp = ptab_pte((const struct pte *)pt0_pp, (rwxug_flags & PTE_G));
        kprintf("| With PP0 Allocated at %p ", pt0_pp);
    }
    void *pp = memory_alloc_page();
    struct pte *pte1_final_pp = (struct pte *)pagenum_to_pageptr(pte2_final_pp->ppn) + VPN1(vma); //final pointers for pt0 insertion
    struct pte *pte0_final_pp = (struct pte *)pagenum_to_pageptr(pte1_final_pp->ppn) + VPN0(vma);
    *pte0_final_pp = leaf_pte(pp, rwxug_flags); 
    kprintf("\n");
    sfence_vma();
    return (void *)vma;
}

//Inputs: 
// const void *vp: Virtual pointer pointing at start of range to map
// size_t size: number of bytes to map
// uint_fast8_t rwxug_flags: flags to give to the mapped range
//Outputs:  N/A
//Effects:  Loops through the required number of pages and allocates each of them with a incrementing vp.
//Description: First calculates number of pages needed. Calls memory_alloc_and_map_page for each of the pages.
void *memory_alloc_and_map_range(uintptr_t vma, size_t size, uint_fast8_t rwxug_flags){
    int num_pages = (size / PAGE_SIZE);    //If not divisible by 4096, add one more page
    if((size - (num_pages)*PAGE_SIZE) % PAGE_SIZE > 0){
        num_pages++;
    }
    void *current_point = (void *)vma;  //Void for byte addressability
    for(int i = 0; i < num_pages; i++){
        //kprintf("%x,\n", current_point);
        memory_alloc_and_map_page((uintptr_t)current_point, rwxug_flags);
        current_point = current_point + PAGE_SIZE;
        
    }
    //kprintf("%d\n", num_pages);
    return (void *)vma;
}

//Inputs: 
// const void *vp: Virtual pointer that points to a specific PT0 Page
// uint_fast8_t rwxug_flags: Flags to change that page to
//Outputs:  N/A
//Effects:  Traverses page tables and changes PT0 PTE to new flags.
//Description: Traverses page tables and changes PT0 PTE to new flags.
void memory_set_page_flags(const void *vp, uint_fast8_t rwxug_flags){
    uintptr_t active_mtag = active_space_mtag();
    struct pte *pte2_final_pp = mtag_to_root(active_mtag) + VPN2((uintptr_t)vp); //Points to the page table entries
    
    struct pte *pte1_final_pp = (struct pte *)pagenum_to_pageptr(pte2_final_pp->ppn) + VPN1((uintptr_t)vp);
    struct pte *pte0_final_pp = (struct pte *)pagenum_to_pageptr(pte1_final_pp->ppn) + VPN0((uintptr_t)vp);
    pte0_final_pp->flags = rwxug_flags | PTE_A | PTE_D | PTE_V;
}

//Inputs: 
// const void *vp: Virtual pointer pointing at start of range to map
// size_t size: number of bytes to map
// uint_fast8_t rwxug_flags: flags to give to the mapped range
//Outputs:  N/A
//Effects:  Loops through the required number of pages and maps each of them with a incrementing vp.
//Description: First calculates number of pages needed. Calls memory_set_page_flags for each of the pages.
void memory_set_range_flags(const void *vp, size_t size, uint_fast8_t rwxug_flags){
    int num_pages = (size / PAGE_SIZE);    //If not divisible by 4096, add one more page
    if((size - (num_pages)*PAGE_SIZE) % PAGE_SIZE > 0){
        num_pages++;
    }
    kprintf("num_pages: %d\n", num_pages);
    void *current_point = (void *)vp;  //Void for byte addressability
    for(int i = 0; i < num_pages; i++){
        memory_set_page_flags((const void *)current_point, rwxug_flags);
        current_point = current_point + PAGE_SIZE;
    }
}

//Inputs:   N/A
//Outputs:  N/A
//Effects:  Unmaps an frees all user memory in the current active memory space
//Description: Loops through every single page entry table and checks if it's user. If it is, set to 0 and return
// page to free list.
void memory_unmap_and_free_user(void){
    uintptr_t active_mtag = active_space_mtag();
   
    void *pp;
    for(int i = 0; i < PTE_CNT; i++){
        struct pte *pte2_final_pp = mtag_to_root(active_mtag) + i;
        if(!is_leaf(main_pt2[i].flags) && is_valid(main_pt2[i].flags) && ((main_pt2[i].flags & PTE_G) != PTE_G)){ //Checks if there is pt1
            struct pte *pte1_final_pp = (struct pte *)pagenum_to_pageptr(pte2_final_pp->ppn); //Points to the page table entries
            for(int j = 0; j < PTE_CNT; j++){   //loop through the page table 1 entries
                if(!is_leaf(pte1_final_pp->flags) && is_valid(pte1_final_pp->flags) && ((pte1_final_pp->flags & PTE_G) != PTE_G)){ //Check if there is pt0
                    struct pte *pte0_final_pp = (struct pte *)pagenum_to_pageptr(pte1_final_pp->ppn);
                    for(int k = 0; k < PTE_CNT; k++){ //loop through the page table 0 entries
                        if(is_leaf(pte0_final_pp->flags) && is_valid(pte0_final_pp->flags) && ((pte0_final_pp->flags & PTE_U) == PTE_U)){
                            pp = pagenum_to_pageptr(pte0_final_pp->ppn);
                            *pte0_final_pp = null_pte(); 
                            memory_free_page(pp);
                        }
                        pte0_final_pp += 1;
                    }
                    pp = pagenum_to_pageptr(pte1_final_pp->ppn);
                    *pte1_final_pp = null_pte(); 
                    memory_free_page(pp);
                }
                if(is_leaf(pte1_final_pp->flags) && is_valid(pte1_final_pp->flags) && ((pte1_final_pp->flags & PTE_U) == PTE_U)){ //Check if this is leaf, remove this one
                    pp = pagenum_to_pageptr(pte1_final_pp->ppn);
                    *pte1_final_pp = null_pte(); 
                    memory_free_page(pp);
                }
                pte1_final_pp += 1;
            }
            pp = pagenum_to_pageptr(pte2_final_pp->ppn);
            *pte2_final_pp = null_pte(); 
            memory_free_page(pp);
        }
        if(is_leaf(main_pt2[i].flags) && is_valid(main_pt2[i].flags) && ((main_pt2[i].flags & PTE_U) == PTE_U)){ //Check if this is leaf, remove this one
            pp = pagenum_to_pageptr(main_pt2[i].flags);
            *pte2_final_pp = null_pte(); 
            memory_free_page(pp);
        }
    }
    sfence_vma();
}

//Inputs: 
// void *vp: VALID virtual pointer to convert to physical
//Outputs: 
// void *pp: physical pointer converted from virtual
//Effects:  Traverses down the page table and converts the VALID virtual pointer into a physical pointer
//Description: Traverses down the page table and converts the VALID virtual pointer into a physical pointer. 
// This function only works for pt0.
void *convert_virt_to_phys(void *vp){
    uintptr_t active_mtag = active_space_mtag();
    struct pte *pte2_final_pp = mtag_to_root(active_mtag) + VPN2((uintptr_t)vp);      //Points to the page table entries
    if(pte2_final_pp->ppn == 0){  //Not initialized, return NULL
        return NULL;
    }
    struct pte *pte1_final_pp = (struct pte *)((void *)pagenum_to_pageptr(pte2_final_pp->ppn) + VPN1((uintptr_t)vp)*PTE_SIZE);
    if(pte1_final_pp->ppn == 0){ //Not initialized, return NULL
        return NULL;
    }
    struct pte *pte0_final_pp = (struct pte *)((void *)pagenum_to_pageptr(pte1_final_pp->ppn) + VPN0((uintptr_t)vp)*PTE_SIZE);
    if(pte0_final_pp->ppn == 0){ //Not initialized, return NULL
        return NULL;
    }
    return (void *)(pte0_final_pp->ppn << PAGE_ORDER | ((uint64_t)vp & 0xFFF));
}

//Inputs: 
// const void * vptr: virtual pointer to handle page fault
//Outputs:  N/A
//Effects:  If it's in the range of the user_space, we can allocate another page. If not, panic.
//Description: If it's in the range of the user_space, we can allocate another page. If not, panic.
void memory_handle_page_fault(const void * vptr){
    if((uintptr_t)vptr >= USER_END_VMA || (uintptr_t)vptr < USER_START_VMA){
        panic("Virtual Pointer out of Range!");
    }
    memory_alloc_and_map_page((uintptr_t)vptr, (int_fast8_t)(PTE_X | PTE_W | PTE_R | PTE_U));
}

//Inputs: 
// const void * vptr: virtual pointer to handle page fault
//Outputs:  N/A
//Effects:  If it's in the range of the user_space, we can allocate another page and return 0. If not, return 1. 
//Description: If it's in the range of the user_space, we can allocate another page and return 0. If not, return 1.
// This function is used for debugging since we can't just have code calling panic.
int memory_handle_page_fault_simulated(const void * vptr){
    if((uintptr_t)vptr >= USER_END_VMA || (uintptr_t)vptr < USER_START_VMA){
        return 1;
    }
    memory_alloc_and_map_page((uintptr_t)vptr, (int_fast8_t)(PTE_X | PTE_W | PTE_R | PTE_U));
    return 0;
}

//Inputs: 
// const void * vptr: virtual pointer to handle page fault
//Outputs:  N/A
//Effects:  If it's in the range of the user_space, we can allocate another page and return 0. If not, return 1. 
//Description: If it's in the range of the user_space, we can allocate another page and return 0. If not, return 1.
// This function is used for debugging since we can't just have code calling panic.
int write_to_mem_simulated(void * vptr, int_fast8_t flags){
    if((uintptr_t)vptr >= USER_END_VMA || (uintptr_t)vptr < USER_START_VMA){
        return 1;
    }
    void *pp = convert_virt_to_phys(vptr);
    if(pp == NULL){
        return 1;
    }
    if((mem_get_flags((uintptr_t)vptr) & flags) != flags){
        return 1;
    }
    return 0;
}


//Inputs: 
// const void * vp: virtual pointer to validate
// size_t len: number of bytes to validate
// uint_fast8_t rwxug_flags: flags to check for validation
//Outputs:  
// int valid: 1 if valid and null terminated, 0 if not valid
//Effects:  Checks each page in the range to see if has the correct flags.
//Description: Gets the current root table and loops through each possible page in
// the range given. Checks if they all have the valid flag
int memory_validate_vptr_len(const void * vp, size_t len, uint_fast8_t rwxug_flags){
    int num_pages = (len / PAGE_SIZE);    //If not divisible by 4096, add one more page
    if((len - (num_pages)*PAGE_SIZE) % PAGE_SIZE > 0){  //Calculate if we need extra page
        num_pages++;
    }
    uintptr_t active_mtag = active_space_mtag();
    struct pte *pte2_final_pp = mtag_to_root(active_mtag) + VPN2((uintptr_t)vp);    //Get ptr to first page table entry
    if(pte2_final_pp->ppn == 0){    //Not initialized
        return 0;
    }
    struct pte *pte1_final_pp = (struct pte *)pagenum_to_pageptr(pte2_final_pp->ppn) + VPN1((uintptr_t)vp);
    if(pte1_final_pp->ppn == 0){    //Not initialized
        return 0;
    }
    
    kprintf("num_pages: %d\n", num_pages);
    void *current_point = (void *)vp;  //Void for byte addressability

    for(int i = 0; i < num_pages; i++){

        if((mem_get_flags((uintptr_t)current_point) & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G)) != rwxug_flags){ //rwxuf flags not valid
            kprintf("%x\n", (mem_get_flags((uintptr_t)current_point) & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G)) != rwxug_flags);
            return 0;
        }
        current_point = current_point + PAGE_SIZE;  //increment by 4kb
    }
    return 1;
}

//Inputs: 
// const char * vs: Virtual character string that should be valid. 
// uint_fast8_t ug_flags: The flags to check if it's correct
//Outputs:  
// int valid: 1 if valid and null terminated, 0 if not valid
//Effects:  Converts virtual to physical address and checks for null termination.
//Description: Converts virtual to physical address and checks for null termination.
// Also checks if each page in the middle has the correct flags. 
int memory_validate_vstr (const char * vs, uint_fast8_t ug_flags){
    char *current_vp = (char *)vs;
    char *current_char;
    while((uintptr_t)current_vp < USER_END_VMA){    //Make sure we don't keep going past 0xD0000000
        current_char = (char *)convert_virt_to_phys(current_vp); //dereference physical address
        if((mem_get_flags((uintptr_t)vs) & ug_flags) != ug_flags){  //check for invalid flags
            return 0;
        }
        if(*current_char == '\0'){  
            return 1;
        }
        current_vp++;
    }
    return 0;
}

//Inputs:   
// uint_fast16_t asid: new asid to create the new memory space
//Outputs:  
// uintptr_t pointer: pointer that points to the new memory tag created
//Effects:  Clones the original memory space at root and then copies it over to the asid.
//Description: Loops through each page table and memory allocated table and copies it to a new page table.
uintptr_t memory_space_clone(uint_fast16_t asid){
    //uintptr_t old_mem_tag = active_space_mtag();
    // struct pte *old_pt2 = (struct pte *)active_space_root();
    // uintptr_t new_mem_tag = memory_space_create(asid);
    // struct pte *new_pt2 = (struct pte *)active_space_root();
    
    struct pte *old_pt2 = (struct pte *)active_space_root();

    void *new_root_pp = memory_alloc_page();
    uintptr_t new_tag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        ((uintptr_t)asid << RISCV_SATP_ASID_shift) |
        pageptr_to_pagenum(new_root_pp);

    struct pte *new_pt2 = (struct pte *)new_root_pp;

    void *new_page;
    for(int i = 0; i < PTE_CNT; i++){
        //kprintf("I: %d\n", i);
        if(is_valid(old_pt2[i].flags) && is_leaf(old_pt2[i].flags)){
            if((old_pt2[i].flags & PTE_G) == PTE_G){
                new_pt2[i] = leaf_pte(pagenum_to_pageptr(old_pt2[i].ppn), old_pt2[i].flags);
            }
            else{
                new_pt2[i] = leaf_pte(pagenum_to_pageptr(old_pt2[i].ppn), old_pt2[i].flags);
                kprintf("%x %x\n", pagenum_to_pageptr(new_pt2[i].ppn), pagenum_to_pageptr(old_pt2[i].ppn));
                kprintf("fdaagd");
                memcpy(pagenum_to_pageptr(new_pt2[i].ppn), pagenum_to_pageptr(old_pt2[i].ppn), PAGE_SIZE*PTE_CNT*PTE_CNT);
                kprintf("aifewjiofeawij");
            }
            
        }
        else if(is_valid(old_pt2[i].flags)){
            struct pte *old_pt1 = (struct pte *)pagenum_to_pageptr(old_pt2[i].ppn);
            struct pte *new_pt1 = memory_alloc_page();
            for(int j = 0; j < PTE_CNT; j++){
                //kprintf("J: %d\n", j);
                if(is_valid(old_pt1[j].flags) && is_leaf(old_pt1[j].flags)){
                    if((old_pt1[j].flags & PTE_G) == PTE_G){
                        new_pt1[j] = leaf_pte(pagenum_to_pageptr(old_pt1[j].ppn), old_pt1[j].flags);
                    }
                    else{
                        new_page = memory_alloc_page();
                        //kprintf("STARTED HERE\n");
                        new_pt1[j] = leaf_pte(new_page, old_pt1[j].flags);
                        memcpy(new_page, pagenum_to_pageptr(old_pt1[j].ppn), PAGE_SIZE);
                        //kprintf("ENDED HERE\n");
                    }
                }
                else if(is_valid(old_pt1[j].flags)){
                    struct pte *old_pt0 = (struct pte *)pagenum_to_pageptr(old_pt1[j].ppn);
                    struct pte *new_pt0 = memory_alloc_page();
                    for(int k = 0; k < PTE_CNT; k++){
                        //kprintf("K: %d\n", k);
                        //kprintf("PT0: %p\n", old_pt0);
                        if(is_valid(old_pt0[k].flags)){
                            if((old_pt0[j].flags & PTE_G) == PTE_G){
                                new_pt0[k] = leaf_pte(pagenum_to_pageptr(old_pt0[k].ppn), old_pt0[k].flags);
                               // kprintf("%x %x\n", pagenum_to_pageptr(new_pt0[k].ppn), pagenum_to_pageptr(old_pt0[k].ppn));
                            }
                            else{
                                new_page = memory_alloc_page();
                                new_pt0[k] = leaf_pte(new_page, old_pt0[k].flags);
                               // kprintf("HEFWFE");
                                memcpy(new_page, pagenum_to_pageptr(old_pt0[k].ppn), PAGE_SIZE);
                                //kprintf("agoegjoa");
                            }
                        }
                    }
                    new_pt1[j] = ptab_pte(new_pt0, old_pt1[j].flags);
                }
            }
            //kprintf("here?");
            new_pt2[i] = ptab_pte(new_pt1, old_pt2[i].flags);
           // kprintf("here");
        }
    }
    //kprintf("here??");
    //csrw_satp(new_tag);
    sfence_vma();
    return new_tag;
}


//Inputs:   N/A
//Outputs: 
// void* mem_start: start of the end_heap.
//Effects:  Gets the end_heap address. Used for testing & debugging
//Description: Gets the end_heap address. Used for testing & debugging
void *get_mem_start(void){
    return mem_start_loc;
}
// INTERNAL FUNCTION DEFINITIONS
//

//Inputs: 
// uintptr_t vp: virtual pointer to check flags for.
//Outputs:  
// int_fast8_t flags: The flags returned at the specific memory address.
//Effects:  Traverses the page tables and gets to the final one, reading the flags there.
//Description: Traverses the page tables and gets to the final one, reading the flags there.
int_fast8_t mem_get_flags(uintptr_t vp){
    uintptr_t active_mtag = active_space_mtag();
    struct pte *pte2_final_pp = mtag_to_root(active_mtag) + VPN2((uintptr_t)vp); //Points to the page table entries
    if(pte2_final_pp->ppn == 0){    //Not initialized
        return 0;
    }
    struct pte *pte1_final_pp = (struct pte *)pagenum_to_pageptr(pte2_final_pp->ppn) + VPN1(vp);
    if(pte1_final_pp->ppn == 0){    //Not initialized
        return 0;
    }
    struct pte *pte0_final_pp = (struct pte *)pagenum_to_pageptr(pte1_final_pp->ppn) + VPN0(vp);
    if(pte0_final_pp->ppn == 0){    //Not initialized
        return 0;
    }


    return (int_fast8_t)(pte0_final_pp->flags);
}

//Inputs: 
// uintptr_t vma: virtual memory address. 
//Outputs:  
// int: 1 if well formed. 0 if not.
//Effects:  Checks by seeing if all bits 63:38 are equal.
//Description: Checks by seeing if all bits 63:38 are equal.
static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

//Inputs: 
// const void * vp: virtual memory address. 
//Outputs:  
// int: 1 if well formed. 0 if not.
//Effects:  Checks by seeing if all bits 63:38 are equal.
//Description: Checks by seeing if all bits 63:38 are equal.
static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

//Inputs:  
// uintptr_t vma: Pointer to check alignment
// size_t blksz: Block size
//Outputs:  
// int: 1 if aligned, 0 if not.
//Effects:  Just modulos and check if size / blksz has a remainder.
//Description:  Just modulos and check if size / blksz has a remainder.
static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

//Inputs:  
// void *p: Pointer to check alignment
// size_t blksz: Block size
//Outputs:  
// int: 1 if aligned, 0 if not.
//Effects:  Just modulos and check if size / blksz has a remainder.
//Description:  Just modulos and check if size / blksz has a remainder.
static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

//Inputs: 
// size_t size: The size value to check alignment
// size_t blksz: Block size
//Outputs: 
// int: 1 if alligned, 0 if not.
//Effects:  Just modulos and check if size / blksz has a remainder.
//Description:  Just modulos and check if size / blksz has a remainder.
static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

//Inputs: N/A
//Outputs:  
// uintptr_t mtag: The currently active space mtag
//Effects: Calls a CSR SATP Register function and returns the val
//Description: Calls a CSR SATP Register function and returns the val
static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

//Inputs: 
// uintptr_t mtag: mtag to calculate the pointer for
//Outputs:  
// struct pte *: Pointer to the new root table
//Effects:  Using the mtag, calculates a struct pointer to the root table.
//Description: Using the mtag, calculates a struct pointer to the root table.
static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}

//Inputs: N/A
//Outputs: 
// struct pte *: Pointer to the current active space root table
//Effects: Reads the CATP register and converts that into a pointer to root table.
//Description: Reads the CATP register and converts that into a pointer to root table.
static inline struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

//Inputs:  
// uintptr_t n: The pointer to convert into a pointer for PTE
//Outputs: 
// (void*): new pointer number
//Effects:  Shifts the pointer over by the PAGE_ORDER as specified in PTE.
//Description: Shifts the pointer over by the PAGE_ORDER as specified in PTE.
static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

//Inputs:  
// const void * p: The pointer to convert into a page number for PTE
//Outputs: 
// (uintptr_t): new page number.
//Effects:  Shifts the pointer over by the PAGE_ORDER as specified in PTE.
//Description: Shifts the pointer over by the PAGE_ORDER as specified in PTE.
static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

//Inputs: 
//  void * p: The value to be rounded up.
//  size_t blksz: The block size to which the value is rounded up to next block.
//Outputs:  N/A
// void *: rounded up pointer.
//Effects:  Calculates and rounds down a pointer to current block size.
//Description: Divides pointer by blocksz and adds one, multiplying it again. This rounds it up.
static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}
//Inputs: 
//  uintptr_t addr: The addr to be rounded up.
//  size_t blksz: The block size to which the value is rounded up to next block.
//Outputs:  N/A
// void *: rounded up pointer.
//Effects:  Calculates and rounds down a pointer to current block size.
//Description: Divides pointer by blocksz and adds one, multiplying it again. This rounds it up.
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

//Inputs: 
//  size_t n: The value to be rounded up.
//  size_t blksz: The block size to which the value is rounded up.
//Outputs:  N/A
// void *: rounded up pointer.
//Effects:  Calculates and rounds up a pointer to current block size.
//Description: Divides pointer by blocksz and adds one, multiplying it again. This rounds it up.
static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

//Inputs: 
//  void * p: The value to be rounded down.
//  size_t blksz: The block size to which the value is rounded down.
//Outputs:  N/A
// void *: rounded down pointer.
//Effects:  Calculates and rounds down a pointer to current block size.
//Description: Divides pointer by blocksz and multiplies it again. This removes the modulo
static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

//Inputs: 
//  size_t n: The value to be rounded down.
//  size_t blksz: The block size to which the value is rounded down.
//Outputs:  N/A
// size_t: The largest multiple of `blksz` less than or equal to `n`.
//Effects:  Calcs and returns The largest multiple of blksz less than or equal to n.
//Description: This function takes an input value n and a block size blksz. It calculates 
// the largest value less than or equal to n that is an exact multiple of blksz. 
static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

//Inputs: 
// size_t n: The value to be rounded down.
// size_t blksz: The block size to which the value is rounded down.
//Outputs:  
// size_t: The largest multiple of `blksz` less than or equal to `n`.
//Effects: Computes and returns the rounded-down value to the nearest block size.
//Description: This function takes an input value `n` and a block size `blksz`. It calculates 
// the largest value less than or equal to `n` that is an exact multiple of `blksz`. 
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

//Inputs:   
//const void * pptr: Pointer to allocated physical address
//uint_fast8_t g_flag: Global flag
//Outputs:  struct pte: new struct which is a page table with valid flags and ppn set
//Effects:  Creates a new leaf_pte struct.
//Description: Creates a new leaf_pte struct. 
static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}

//Inputs:   
//const struct pte * ptab: New Page Table physical memory location
//uint_fast8_t g_flag: Global flag
//Outputs:  struct pte: new struct which is a page table with valid flags and ppn set
//Effects:  Creates a new ptab_pte struct.
//Description: Creates a new ptab_pte struct.  
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

//Inputs:   N/A
//Outputs:  N/A
//Effects: Checks if the page table entry is a valid leaf
//Description: If it's a leaf then PTE_A = 1 and PTE_D = 1
static inline int is_leaf(int_fast8_t flags){
    if(((flags & PTE_A) == PTE_A) && ((flags & PTE_D) == PTE_D)){
        return 1;
    }
    return 0;
}

//Inputs:   N/A
//Outputs:  N/A
//Effects: Checks if the page table entry is a valid entry
//Description: If it's valid then PTE_V = 1

static inline int is_valid(int_fast8_t flags){
    return ((flags & PTE_V) == PTE_V);
}

//Inputs:   N/A
//Outputs:  N/A
//Effects: Checks if the page table entry is a valid leaf
//Description: If it's a leaf then PTE_A = 1 and PTE_D = 1
static inline struct pte null_pte(void) {
    return (struct pte) { };
}

//Inputs:   N/A
//Outputs:  N/A
//Effects: Checks if the page table entry is a valid leaf
//Description: If it's a leaf then PTE_A = 1 and PTE_D = 1
static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}

