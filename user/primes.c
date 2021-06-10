#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

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

int main(int argc, char *argv[])
{
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

    exit(0);
}