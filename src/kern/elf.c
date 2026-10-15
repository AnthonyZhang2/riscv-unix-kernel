#include "elf.h"
#include "memory.h"

#define MEMOFFSET 0x80100000
#define MAXMEM    0x81000000

#define EI_NIDENT 16
#define HEADERSIZE 48

//ELF FILE HEADER CONSTANTS
/* These constants define the different elf file types */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3
#define ET_CORE   4
#define ET_LOPROC 0xff00
#define ET_HIPROC 0xffff

#define	EI_MAG0		0		/* e_ident[] indexes */
#define	EI_MAG1		1
#define	EI_MAG2		2
#define	EI_MAG3		3
#define	EI_CLASS	4
#define	EI_DATA		5
#define	EI_VERSION	6
#define	EI_OSABI	7
#define	EI_PAD		8

#define EM_ARM      40      /*Advanced RISC Machines ARM*/

#define	ELFMAG0		0x7f		/* EI_MAG */
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'
#define	ELFMAG		"\177ELF"
#define	SELFMAG		4

#define	ELFCLASSNONE	0		/* EI_CLASS */
#define	ELFCLASS32	1
#define	ELFCLASS64	2
#define	ELFCLASSNUM	3

#define ELFDATANONE	0		/* e_ident[EI_DATA] */
#define ELFDATA2LSB	1
#define ELFDATA2MSB	2

#define EV_NONE		0		/* e_version, EI_VERSION */
#define EV_CURRENT	1
#define EV_NUM		2

#define EM_RISC     0xF3    /*RISC-V Machine Type*/

#define EE_NOENTRY  0       /*No valid entry location*/

// ELF PROGRAM FILE HEADER CONSTANTS

#define PT_LOAD    1


#define PROHEADSIZE 56

//ELF PROGRAM HEADER TABLE FLAG CONSTANTS

#define PF_X 1
#define PF_W 2
#define PF_R 4


// OTHER CONSTANTS

//           arg is pointer to uint64_t
#define IOCTL_GETLEN        1
//           arg is pointer to uint64_t
#define IOCTL_SETLEN        2
//           arg is pointer to uint64_t
#define IOCTL_GETPOS        3
//           arg is pointer to uint64_t
#define IOCTL_SETPOS        4
//           arg is ignored
#define IOCTL_FLUSH         5
//           arg is pointer to uint32_t
#define IOCTL_GETBLKSZ      6

typedef uint64_t	Elf64_Addr;
typedef uint16_t	Elf64_Half;
typedef int16_t	    Elf64_SHalf;
typedef uint64_t	Elf64_Off;
typedef int32_t	    Elf64_Sword;
typedef uint32_t	Elf64_Word;
typedef uint64_t	Elf64_Xword;
typedef int64_t	    Elf64_Sxword;

typedef struct elf_header{
    uint64_t Elf64_Addr;       // Unsigned program address
    uint64_t Elf64_Off;        // Unsigned file offset
    uint16_t Elf64_Section;    // Unsigned section index
    uint16_t Elf64_Versym;     // Unsigned version symbol information 
    unsigned char Elf_Byte;        
    uint16_t Elf64_Half;       
    int32_t Elf64_Sword;      
    uint32_t Elf64_Word;       
    int64_t Elf64_Sxword;     
    uint64_t Elf64_Xword;      
} elf_header;

typedef struct {
    unsigned char e_ident[EI_NIDENT];   //Essentially a checksum, must be correct to be valid elf file
    Elf64_Half    e_type;               //identifies the object file type
    Elf64_Half    e_machine;            //specifies the required architecture
    Elf64_Word    e_version;            //file version
    Elf64_Addr    e_entry;              //virtual address to which the system first transfers control
    Elf64_Off     e_phoff;              //program header table file offset in bytes
    Elf64_Off     e_shoff;              //section header table file offset in bytes
    Elf64_Word    e_flags;              //processor-specific flags
    Elf64_Half    e_ehsize;             //elf header's size in bytes
    Elf64_Half    e_phentsize;          //size in bytes of one entry in the file program's header table
    Elf64_Half    e_phnum;              //number of entries in the program header table
    Elf64_Half    e_shentsize;          //Section header entry size in bytes
    Elf64_Half    e_shnum;              //number of entries in the section header table.
    Elf64_Half    e_shstrndx;           //section header table index of the entry associated with the section name string table
} Elf64_Ehdr;

typedef struct {
    uint32_t   p_type;      //What type the array of file says
    uint32_t   p_flags;     //bit mask of flags relevant to segment
    Elf64_Off  p_offset;    //offset from beginning of file at which first byte of segment resides
    Elf64_Addr p_vaddr;     //virtual address at which the first byte of segment resides in memory
    Elf64_Addr p_paddr;     //segment's physical address, on BSD must be 0
    uint64_t   p_filesz;    //number of bytes in the file image of segment
    uint64_t   p_memsz;     //number of bytes in the memory image of the segment
    uint64_t   p_align;     //the value to which segments are aligned in memory and in the file
} Elf64_Phdr;

typedef struct {
    uint32_t      st_name;  //name of the associated symbol
    unsigned char st_info;  //symbol type and binding attributes
    unsigned char st_other; //defines the symbol visibility
    uint16_t      st_shndx; //relevant section header table index
    Elf64_Addr    st_value; //value of the associated symbol
    uint64_t      st_size;  //size of the associated symbol
} Elf64_Sym;


// INPUTS: 
// struct io_intf *io   The input io interface to read from the file
// void (**entryptr)(struct io_intf *io)  The entry pointer that will point to entry function of program
// OUTPUTS: int error, Return 0 on success, return negative value on failure
// EFFECTS: Will take input io interface and read the elf file headers and then process the file and load into memory.
// DESCRIPTION: This goes through the given file IO interface and starts reading. It reads and processes the 
// headers approprately until it gets to program header. It then reads program headers down one by one and then
// loads it into memory if the type is PT_Load. THe entry function pointer is placed into io. 
// It then returns with a 0 if successful. 
int elf_load(struct io_intf *io, void (**entryptr)(void)){
    Elf64_Ehdr elf_header;
    Elf64_Phdr pro_header;
    uint64_t start_of_file = 0;
    io->ops->ctl(io, IOCTL_GETPOS, &start_of_file);
    if(io->ops->read(io, elf_header.e_ident, EI_NIDENT) != EI_NIDENT){  //Check the e_ident array checksum
        return -EBADFMT;
    }
    uint64_t temp_pos = 0;
    
    if(elf_header.e_ident[EI_MAG0] != ELFMAG0){       //Check checksum
        return -EBADFMT;
    }
    if(elf_header.e_ident[EI_MAG1] != ELFMAG1){
        return -EBADFMT;
    }
    if(elf_header.e_ident[EI_MAG2] != ELFMAG2){
        return -EBADFMT;
    }
    if(elf_header.e_ident[EI_MAG3] != ELFMAG3){
        return -EBADFMT;
    }
    if(elf_header.e_ident[EI_CLASS] != ELFCLASS64){     //Check if it's 64 bit
        return -EBADFMT;
    }
    if(elf_header.e_ident[EI_DATA] != ELFDATA2LSB){     //Check if it's little endian
        return -EBADFMT;
    }
    if(elf_header.e_ident[EI_VERSION] == EV_NONE){
        return -EBADFMT;
    }
    
    if(io->ops->read(io, &(elf_header.e_type), HEADERSIZE) != HEADERSIZE){      //Check header size
        return -EBADFMT;
    }

    if(elf_header.e_type != ET_EXEC){       //Check if it's an executable
        return -EBADFMT;
    }

    if(elf_header.e_machine != EM_RISC){    //Check if it's RISC-V machine type
        return -EBADFMT;
    }
    
    if(elf_header.e_machine == EV_NONE){    //Check if it's not valid
        return -EBADFMT;
    }

    if(elf_header.e_machine == EE_NOENTRY){    //Check if there's not a valid entry
        return -EBADFMT;
    }


    //Program Header is at file offset e_phoff, and consists of e_phnum entries, each with size e_phentsize.
    
    uint64_t pos = start_of_file + elf_header.e_phoff;
    io->ops->ctl(io, IOCTL_GETPOS, &temp_pos);
    io->ops->ctl(io, IOCTL_SETPOS, &pos);
    for(int i = 1; i <= elf_header.e_phnum; i++){
        if(io->ops->read(io, &pro_header, elf_header.e_phentsize) != elf_header.e_phentsize){
            return -EBADFMT;
        }
        //return pro_header.p_type;
        if(pro_header.p_type == PT_LOAD){   // Only actually load if it's PT_LOAD type
            //initially allocate with read and write in order to load 
            memory_alloc_and_map_range((uintptr_t)pro_header.p_vaddr, (size_t)pro_header.p_filesz, (uint_fast8_t)(PTE_R | PTE_W ));
            void *buf = (void *)pro_header.p_vaddr;
            pos = start_of_file + pro_header.p_offset;
            io->ops->ctl(io, IOCTL_SETPOS, &pos); //Shift to the section in memory
            io->ops->read(io, buf, pro_header.p_filesz); 
            uint_fast8_t temp_flags = PTE_U;
            if((pro_header.p_flags & PF_X) == PF_X){
                temp_flags = temp_flags | PTE_X;
            }
            if((pro_header.p_flags & PF_W) == PF_W){
                temp_flags = temp_flags | PTE_W;
            }
            if((pro_header.p_flags & PF_R) == PF_R){
                temp_flags = temp_flags | PTE_R;
            }
            memory_set_range_flags((void *)pro_header.p_vaddr, (size_t)pro_header.p_filesz, temp_flags);
        }
        pos = start_of_file + elf_header.e_phoff + i*elf_header.e_phentsize;
        io->ops->ctl(io, IOCTL_SETPOS, &pos);
    }
    *entryptr = (void *)elf_header.e_entry;  //Tells system where to transfer control to
    
    return 0;
}