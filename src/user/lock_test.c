#include "syscall.h"
#include "string.h"

void main(void) {
    int result;
    int tid;
    char read_buf[513];
    char write_buf[512];
    char copy_buf[4] = "abcd";
    int i;
    __UINT64_TYPE__ write_pos = 0;
    __UINT64_TYPE__ read_pos = 0;
    
    for (i=0; i < sizeof(write_buf) / sizeof(copy_buf); i++)
        strncat(write_buf + i * sizeof(copy_buf), copy_buf, sizeof(copy_buf));

    result = _fsopen(0, "fib");

    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }
     _msgout("_fsopen success");

    tid = _fork();

    if(tid){
        for (i = 0; i < 3; i++){
            _ioctl(0, IOCTL_SETPOS, &write_pos);
            result = _write(0, write_buf, sizeof(write_buf));
            if(result < 0){
                _msgout("parent _write failed");
                _exit();
            }
            _msgout("parent _write success");
        }

        _wait(tid);

        for (i = 0; i < 3; i++){
            _ioctl(0, IOCTL_SETPOS, &read_pos);
            result = _read(0, read_buf, sizeof(read_buf) - 1);
            if(result < 0){
                _msgout("parent _read failed");
                _exit();
            }
            _msgout("parent _read success");
            _msgout(read_buf);
        }

        _close(0);
        _exit();

    } else {
        for (int i = 0; i < 3; i++){
            _ioctl(0, IOCTL_SETPOS, &write_pos);
            result = _write(0, write_buf, sizeof(write_buf));
            if(result < 0){
                _msgout("child _write failed");
                _exit();
            }
            _msgout("child _write success");
        }

        _close(0);
        _exit();
    }
}
