//          fs_test.c - Main functoin: Setup to test file system functionalities
//

#include "kfs.h"
#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"

#define TEST_BLOCK_SIZE 4096
#define TEST_FILE_COUNT 2
#define MAX_FILENAME_LEN 32
#define TEST_DATA_SIZE 100

// storage setup
static char mock_storage[TEST_BLOCK_SIZE * 2];
static struct io_intf mock_io;
static struct io_ops mock_ops;

// mock IO operations
static long mock_read(struct io_intf* io, void* buf, unsigned long count) {
    file_t* file = (file_t*)io;
    if (file->pos + count > TEST_BLOCK_SIZE * 2) {
        count = TEST_BLOCK_SIZE * 2 - file->pos;
    }
    memcpy(buf, mock_storage + file->pos, count);
    return count;
}

static long mock_write(struct io_intf* io, const void* buf, unsigned long count) {
    file_t* file = (file_t*)io;
    if (file->pos + count > TEST_BLOCK_SIZE * 2) {
        return -1;
    }
    memcpy(mock_storage + file->pos, buf, count);
    return count;
}

// test setup
static void setup_mock_fs() {
    // clear storage
    memset(mock_storage, 0, sizeof(mock_storage));

    // setup file count in boot block
    *(int*)mock_storage = TEST_FILE_COUNT;

    // setup directory entries
    directory_entry_t* entries = (directory_entry_t*)(mock_storage + 64);
    strncpy(entries[0].name, "test1.txt", MAX_FILENAME_LEN);
    entries[0].inode = 0;
    strncpy(entries[1].name, "test2.txt", MAX_FILENAME_LEN);
    entries[1].inode = 1;

    // setup inode block with file sizes
    size_t* inode_block = (size_t*)(mock_storage + TEST_BLOCK_SIZE);
    inode_block[0] = TEST_DATA_SIZE;
    inode_block[1] = TEST_DATA_SIZE;

    // setup mock IO interface
    mock_ops.read = mock_read;
    mock_ops.write = mock_write;
    mock_io.ops = &mock_ops;
}

// test functions
static void test_fs_mount() {
    kprintf("Testing fs_mount...\n");
    int result = fs_mount(&mock_io);
    assert(result == 0);
    kprintf("fs_mount test passed\n");
}

static void test_fs_open() {
    kprintf("Testing fs_open...\n");
    struct io_intf* io;
    
    // test opening existing file
    int result = fs_open("test1.txt", &io);
    assert(result == 0);
    assert(io != NULL);
    
    // test opening non-existent file
    result = fs_open("nonexistent.txt", &io);
    assert(result == -ENOENT);
    
    kprintf("fs_open test passed\n");
}

static void test_fs_read_write() {
    kprintf("Testing fs_read and fs_write...\n");
    struct io_intf* io;
    fs_open("test1.txt", &io);
    
    // test write
    char test_data[] = "Hello, World!";
    long written = fs_write(io, test_data, strlen(test_data));
    assert(written == strlen(test_data));
    
    // test read
    char read_buffer[20] = {0};
    file_t* file = (file_t*)io;
    file->pos = 0; // reset position for reading
    
    long read = fs_read(io, read_buffer, strlen(test_data));
    assert(read == strlen(test_data));
    assert(strcmp(test_data, read_buffer) == 0);
    
    kprintf("fs_read and fs_write tests passed\n");
}

static void test_fs_ioctl() {
    kprintf("Testing fs_ioctl...\n");
    struct io_intf* io;
    fs_open("test1.txt", &io);
    
    // test IOCTL_GETLEN
    uint64_t length;
    assert(fs_ioctl(io, IOCTL_GETLEN, &length) == 0);
    
    // test IOCTL_SETPOS
    uint64_t new_pos = 5;
    assert(fs_ioctl(io, IOCTL_SETPOS, &new_pos) == 0);
    
    // test IOCTL_GETPOS
    uint64_t curr_pos;
    assert(fs_ioctl(io, IOCTL_GETPOS, &curr_pos) == 0);
    assert(curr_pos == new_pos);
    
    // test IOCTL_GETBLKSZ
    uint32_t block_size;
    assert(fs_ioctl(io, IOCTL_GETBLKSZ, &block_size) == 0);
    assert(block_size == TEST_BLOCK_SIZE);
    
    kprintf("fs_ioctl tests passed\n");
}

static void test_fs_close() {
    kprintf("Testing fs_close...\n");
    struct io_intf* io;
    fs_open("test1.txt", &io);
    fs_close(io);
    kprintf("fs_close test passed\n");
}

int main() {
    setup_mock_fs();
    
    test_fs_mount();
    test_fs_open();
    test_fs_read_write();
    test_fs_ioctl();
    test_fs_close();
    
    kprintf("All tests completed successfully!\n");
    return 0;
}