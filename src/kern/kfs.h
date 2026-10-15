// kfs.h  - file system abstractions
//

#ifndef _KFS_H_
#define _KFS_H_

#include "io.h"
#include "fs.h"
#include <string.h>
#include "lock.h"

typedef struct {
    char name[32];
    int inode;
    char pad[28];
} directory_entry_t;

// define file struct
typedef struct file {
    struct io_intf io_intf; 
    size_t pos;
    size_t size;
    int inode;
    int flags;
} file_t;

int fs_mount(struct io_intf* io);
int fs_open(const char* name, struct io_intf** io);
void fs_close(struct io_intf* io);
long fs_write(struct io_intf* io, const void* buf, unsigned long n);
long fs_read(struct io_intf* io, void* buf, unsigned long n);
int fs_ioctl(struct io_intf* io, int cmd, void* arg);

int fs_getlen(file_t* fd, void* arg);
int fs_getpos(file_t* fd, void* arg);
int fs_setpos(file_t* fd, void* arg);
int fs_getblksz(file_t* fd, void* arg);

#endif


