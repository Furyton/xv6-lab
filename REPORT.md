# Lab util

## sleep

学习使用 sleep 系统调用， 命令行第一个参数作为sleep的时间。

相关代码见 附录 1.1

## pingpong

学习使用 pipe 系统调用。要求建立一对 pipe，父子进程互相收发消息。

pipe 有两端，一端写入消息后，另一端可以读出。当一端没有数据时，另一端读入会阻塞。使用完 pipe 后需要 close。close 也会等待所有的读写操作。

实验流程：父进程在一端写入数据并wait(0)，子进程读取，输出成功信息，写数据，父进程读取，关闭管道。

相关代码见 附件 1.2

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
