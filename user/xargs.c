#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

char *_argv[MAXARG];
char buf[512];
int _argc;

void do_subroutine() {
    int pid = fork();

    if (pid == 0) {
        exec(_argv[0], _argv);
        exit(0);
    }
    wait(0);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(2, "xargs: too few args\n");
        exit(-1);
    }
    memset(_argv, 0, sizeof(_argv));
    _argc = argc - 1;
    for (int i = 1; i < argc; i++) {
        _argv[i - 1] = argv[i];
    }

    struct stat st;

    if(fstat(0, &st) < 0) {
        int pos = 0;
        
        _argv[_argc] = buf;

        memset(buf, 0, sizeof(buf));

        while(read(0, &buf[pos], 1) > 0) {
            if (buf[pos] == '\n') {
                buf[pos] = 0;

                do_subroutine();

                pos = 0;
                memset(buf, 0, sizeof(buf));
            } else {
                pos++;
            }
        }
        if (buf[0]) {
            do_subroutine();
        }
    } else {
        do_subroutine();
    }

    exit(0);
}