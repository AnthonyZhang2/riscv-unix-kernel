// syscall.h - System calls
//

#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#define IOCTL_GETLEN        1   // arg is pointer to uint64_t
#define IOCTL_SETLEN        2   // arg is pointer to uint64_t
#define IOCTL_GETPOS        3   // arg is pointer to uint64_t
#define IOCTL_SETPOS        4   // arg is pointer to uint64_t
#define IOCTL_FLUSH         5   // arg is ignored
#define IOCTL_GETBLKSZ      6   // arg is pointer to uint32_t

#include <stddef.h>

extern void __attribute__ ((noreturn)) _exit(void);
extern void _msgout(const char * msg);
extern int _close(int fd);
extern long _read(int fd, void * buf, size_t bufsz);
extern long _write(int fd, const void * buf, size_t len);
extern int _ioctl(int fd, const int cmd, void * arg);
extern int _devopen(int fd, const char * name, int instno);
extern int _fsopen(int fd, const char * name);
extern int _exec(int fd);
extern int _fork(void);
extern int _wait(int tid);
extern int _usleep(unsigned long us);

#endif // _SYSCALL_H_
