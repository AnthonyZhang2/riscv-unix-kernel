#include "syscall.h"
#include "string.h"

void main(void) {
    int result;
    int tid;
    char read_buf[512];
    char write_buf[512];
    char copy_buf[4] = "abcd";
    int i;

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
        result = _close(0);

        if(result < 0){
            _msgout("parent _close failed");
            _exit();
        }
        _msgout("parent _close success");

        result = _wait(tid);
        if(result < 0){
            _msgout("parent _wait failed");
            _exit();
        }
        _msgout("parent _wait success");

        _exit();

    } else {
        result = _read(0, read_buf, sizeof(read_buf));

        if(result < 0){
            _msgout("child _read failed");
            _exit();
        }
        _msgout("child _read success");

        result = _write(0, write_buf, sizeof(write_buf));

        if(result < 0){
            _msgout("child _write failed");
            _exit();
        }
        _msgout("child _write success");

        result = _close(0);

        if(result < 0){
            _msgout("child _close failed");
            _exit();
        }
        _msgout("child _close success");
        _exit();
    }
}
