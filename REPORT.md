# Lab util

## sleep

学习使用 sleep 系统调用， 命令行第一个参数作为sleep的时间。

```c
// in user/sleep.c

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc < 2) exit(-1);

    exit(sleep(atoi(argv[1])));
}

```

## pingpong

学习使用 pipe 系统调用。要求建立一对 pipe，父子进程互相收发消息。

pipe 有两端，一端写入消息后，另一端可以读出。当一端没有数据时，另一端读入会阻塞。使用完 pipe 后需要 close。close 也会等待所有的读写操作。

实验流程：父进程在一端写入数据并wait(0)，子进程读取，输出成功信息，写数据，父进程读取，关闭管道。

```c
// user/pingpong.c

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int p[2], pid;
    pipe(p);

    pid = fork();
    char data[] = "d", buf[2];

    if(pid == 0) {
        if(read(p[0], &buf, 1) > 0) 
            printf("%d: received ping\n", getpid());

        write(p[1], &data, 1);
 
        exit(0);
    } else {
        write(p[1], &data, 1);

        wait(0);

        if(read(p[0], &buf, 1) > 0)
            printf("%d: received pong\n", getpid());
        close(p[0]);
        close(p[1]);
    }

    exit(0);
}

```

## primes

利用 pipe 和 fork 系统调用来实现并发版本的素数筛(CSP Thread)，原理详见[Bell Labs and CSP Threads](https://swtch.com/~rsc/thread/)。

```c
// prime.c
// function main()

int pid, pfd[2];
pipe(pfd);

pid = fork();

if(pid == 0) {
    pipeline(pfd);
} else {
    for(int i = 2; i <= 35; i++) {
        write(pfd[1], &i, sizeof(int));
    }
    
    close(pfd[1]);

    wait(0);

    close(pfd[0]);
}
```

进程一负责创建子进程并且把数字 2~35 依次写入管道 pfd 的写端。子进程则执行 pipeline 函数递归的进行素数筛。

```c
//prime.c

void pipeline(int fd[]) {
    int p, n, cfd[2] = {-1};
    close(fd[1]);
    if (read(fd[0], &p, sizeof(int)) > 0) {
        printf("prime %d\n", p);

        while(read(fd[0], &n, sizeof(int)) > 0) {
            if (n % p != 0) {
                if (cfd[0] < 0) {
                    pipe(cfd);
                    int pid = fork();

                    if (pid == 0) {
                        pipeline(cfd);
                        exit(0);
                    }
                }

                write(cfd[1], &n, sizeof(int));
            }
        }

        close(cfd[1]);
        wait(0);
        close(cfd[0]);
        close(cfd[1]);
    }
    exit(0);
}
```

每个子进程被分配了一对管道 cfd，用来将当前进程获得的需要继续筛的数字传给子进程。

当前进程首先从管道中读出的数字为素数，记录并打印，并据此筛掉后读入的数字。传递数字前首先会建立新的管道用来通信，之后会fork新进程并递归的去执行 pipeline 函数。

## find

利用系统调用 fstat 等递归地在某一文件将内查找给定的文件。fstat 系统调用可以获得文件描述符对应文件的信息，通过它我们可以知道对应的文件类型是目录还是普通的文件。目录文件中存放的是目录下的子目录和子文件。通过检查文件名是否匹配很容易实现给定文件的查找。

```c
// in find.c

void explore_dir(char *path) {
    char *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);

        return;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (st.type == T_FILE) {
        fprintf(2, "find: %s not a dir\n", path);
        return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");

        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        if (stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }

        if (st.type == T_FILE) {
            check_file(buf);
        } else if (st.type == T_DIR) {
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;

            explore_dir(buf);
        }
    }
    close(fd);
}
```

void explore(char* path) 函数用于遍历path目录下的子文件，若是文件则检查是否匹配，若是目录，则递归的调用explore进行搜索。

相关代码见 附录 1.3

## xargs

要求利用系统调用 fork 和 exec 实现简化版的 linux 中的 xargs 命令。xargs 会接受一个执行程序的命令，如 `echo hi`，同时shell可能会通过 pipe 机制向 xargs 传输一系列的参数，我们需要将这些可能的参数追加到执行命令的参数列表中。若遇到换行符，表示执行新的一行命令而不是追加。

实现时需要在 0 号文件描述符中读取可能的追加参数 (若没有数据，则直接执行)，遇到换行符时或没有数据的时候，利用 fork 和 exec 执行给定的程序。执行程序的命令被保存到了 _argv 中。

```c
// user/xargs.c

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
```
