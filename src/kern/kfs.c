//           kfs.c 
// 

#include "kfs.h"

#define MAX_FILE_NUM 32
#define BLOCK_SIZE 4096

static file_t file_table[MAX_FILE_NUM];
static uint32_t fs_num_files = 0;
static directory_entry_t dir_entries[MAX_FILE_NUM];
static struct io_intf * dev_io;
static uint8_t boot_blk[BLOCK_SIZE];
static uint8_t inode_blk[BLOCK_SIZE];
static struct lock fs_lock;

/**
 * mounts a filesystem using the provided I/O interface
 * 
 * @param io    pointer to io_intf for filesystem device
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_mount(struct io_intf* io) {
    if (!io) return -EINVAL;

    // read the boot block
    // char boot_blk[BLOCK_SIZE];
    lock_init(&fs_lock, "fs_lock");
    lock_acquire(&fs_lock);

    if (ioseek(io, 0) < 0 || ioread(io, boot_blk, BLOCK_SIZE) != BLOCK_SIZE) {
        return -EIO;
    }

    lock_release(&fs_lock);

    // parse number of files from the boot block
    fs_num_files = *(uint32_t*) boot_blk;
    if (fs_num_files > MAX_FILE_NUM || fs_num_files < 0) return -EINVAL;

    // load directory entries
    memcpy(dir_entries, boot_blk + sizeof(directory_entry_t), fs_num_files * sizeof(directory_entry_t));
    
    // init file table
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        file_table[i].flags = 0; // mark all entries as unused
    }

    dev_io = io;

    return 0;
}

/**
 * opens a file by name
 * 
 * @param name  name of file to open
 * @param io   pointer to location to store file's io_intf
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_open(const char* name, struct io_intf** io) {
    static const struct io_ops ops = {
        .close = fs_close,
        .read = fs_read,
        .write = fs_write,
        .ctl = fs_ioctl
    };

    if (!name || !io) return -EINVAL;

    int dir_idx = -1;
    for (int i = 0; i < fs_num_files; i++) {
        if (strncmp(dir_entries[i].name, name, sizeof(dir_entries[i].name)) == 0) {
            dir_idx = i;
            break;
        }
    }

    if (dir_idx == -1) return -ENOENT; // file not dir_idx

    // find an available slot in the file table
    file_t* file = NULL;
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (file_table[i].flags == 0) {
            file = &file_table[i];
            break;
        }
    }

    if (!file) return -ENODEV; // too many open files

    // initialize the file structure
    file->flags = 1; // Mark as in use
    file->inode = dir_entries[dir_idx].inode;
    file->pos = 0;
    file->io_intf.ops = &ops;

    //file->io_intf.refcnt += 1;
    ioref(&(file->io_intf));
    lock_acquire(&fs_lock);

    // read the inode block
    // char inode_blk[BLOCK_SIZE];
    if (ioseek(dev_io, BLOCK_SIZE * (file->inode + 1)) < 0 || ioread(dev_io, inode_blk, BLOCK_SIZE) != BLOCK_SIZE) {
        file->flags = 0;
        return -EIO; // io error
    }

    lock_release(&fs_lock);
    
    // extract file size from the inode block
    file->size = *(uint32_t*)inode_blk;
    ioseek(dev_io, BLOCK_SIZE * (1 + fs_num_files + *(uint32_t *)(inode_blk + sizeof(uint32_t))));
    // set up the io_intf for the file
    // file->io_intf.ops = (*io)->ops;
    *io = &file->io_intf;

    return 0;
}


/**
 * closes an open file
 * 
 * @param io    pointer to file's io_intf
 */
void fs_close(struct io_intf* io) {
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (&file_table[i].io_intf == io) {
            file_table[i].flags = 0; // mark as unused
            file_table[i].pos = 0;
            file_table[i].size = 0;
            return;
        }
    }
}

/**
 * writes data to a file
 * 
 * @param io    pointer to file's io_intf
 * @param buf   buffer containing data to write
 * @param n     number of bytes to write
 * 
 * @return number of bytes written, negative error code on failure
 */
long fs_write(struct io_intf* io, const void* buf, unsigned long n) {
    uint64_t write_pos;
    uint64_t data_blk_no;
    uint64_t data_blk_offset;

    if (!io || !buf) return -EINVAL;

    file_t* file = (file_t*)io;

    lock_acquire(&fs_lock);

    // char inode_blk[BLOCK_SIZE];
    if (ioseek(dev_io, BLOCK_SIZE * (file->inode + 1)) < 0 || ioread(dev_io, inode_blk, BLOCK_SIZE) != BLOCK_SIZE) {
        file->flags = 0;
        return -EIO; // io error
    }

    data_blk_no = (uint64_t) file->pos / BLOCK_SIZE;
    data_blk_offset = (uint64_t) file->pos % BLOCK_SIZE;
    write_pos = BLOCK_SIZE * (1 + fs_num_files + *(uint32_t *)(inode_blk + sizeof(uint32_t)) + data_blk_no) + data_blk_offset;
    ioseek(dev_io, write_pos);
    long bytes_written = iowrite(dev_io, buf, n);
    if (bytes_written > 0) {
        file->pos += bytes_written; // update file position
        if (file->pos > file->size) {
            file->size = file->pos; // update file size
        }
    }

    lock_release(&fs_lock);

    return bytes_written;
}

/**
 * reads data from a file
 * 
 * @param io    pointer to file's io_intf
 * @param buf   buffer to read data into
 * @param n     number of bytes to read
 * 
 * @return number of bytes read, 0 on EOF, negative error code on failure
 */
long fs_read(struct io_intf* io, void* buf, unsigned long n) {
    uint64_t read_pos;
    uint64_t data_blk_no;
    uint64_t data_blk_offset;

    if (!io || !buf) return -EINVAL;

    file_t* file = (file_t*)io;

    // Ensure we don't read past the end of the file
    if (file->pos + n > file->size) {
        n = file->size - file->pos;
        if (n == 0) return 0; // EOF
    }

    lock_acquire(&fs_lock);

    // char inode_blk[BLOCK_SIZE];
    if (ioseek(dev_io, BLOCK_SIZE * (file->inode + 1)) < 0 || ioread(dev_io, inode_blk, BLOCK_SIZE) != BLOCK_SIZE) {
        file->flags = 0;
        return -EIO; // io error
    }

    data_blk_no = (uint64_t) file->pos / BLOCK_SIZE;
    data_blk_offset = (uint64_t) file->pos % BLOCK_SIZE;
    read_pos = BLOCK_SIZE * (1 + fs_num_files + *(uint32_t *)(inode_blk + sizeof(uint32_t)) + data_blk_no) + data_blk_offset;
    ioseek(dev_io, read_pos);
    long bytes_read = ioread(dev_io, buf, n);
    if (bytes_read > 0) {
        file->pos += bytes_read; // Update file position
    }

    lock_release(&fs_lock);

    return bytes_read;
}

/**
 * handles ioctl commands for filesystem
 * 
 * @param io    pointer to file's io_intf
 * @param cmd   IOCTL command
 * @param arg   pointer to argument buffer
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_ioctl(struct io_intf* io, int cmd, void* arg) {
    file_t *file = (file_t*) io;

    switch (cmd)
    {
    case IOCTL_GETLEN:
        return fs_getlen(file, arg);
    case IOCTL_GETPOS:
        return fs_getpos(file, arg);
    case IOCTL_SETPOS:
        return fs_setpos(file, arg);
    case IOCTL_GETBLKSZ:
        return fs_getblksz(file, arg);
    default:
        return -ENOTSUP;
    }
}

/**
 * gets file length
 * 
 * @param fd    pointer to file_t structure
 * @param arg   pointer to uint64_t to store length
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_getlen(file_t* fd, void* arg) {
    if (!fd || !arg) return -EINVAL;
    *(uint64_t*) arg = fd->size;
    return 0;
}

/**
 * gets current file position
 * 
 * @param fd    pointer to file_t structure
 * @param arg   pointer to uint64_t to store position
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_getpos(file_t* fd, void* arg) {
    if (!fd || !arg) return -EINVAL;
    *(uint64_t*) arg = fd->pos;
    return 0;
}

/**
 * sets file position
 * 
 * @param fd    pointer to file_t structure
 * @param arg   pointer to uint64_t containing new position
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_setpos(file_t* fd, void* arg) {
    if (!fd || !arg) return -EINVAL;
    uint64_t new_pos = *(uint64_t*) arg;
    if (new_pos > fd->size) return -EINVAL;
    fd->pos = new_pos;
    return 0;
}

/**
 * gets filesystem block size
 * 
 * @param fd    pointer to file_t structure
 * @param arg   pointer to uint32_t to store block size
 * 
 * @return 0 on success, negative error code on failure
 */
int fs_getblksz(file_t* fd, void* arg) {
    if (!fd || !arg) return -EINVAL;
    *(uint32_t*) arg = BLOCK_SIZE;
    return 0;
}
