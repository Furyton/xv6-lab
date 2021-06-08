#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int pid, p[2];
    pipe(p);
    pid = fork();
    

    int data[5] = {1, 2, 3, 4, 5}, buf[5];

    if(pid == 0) {
        close(p[0]);
        if(read(p[0], &buf, sizeof(int)) > 0) {
            printf("1,received %d\n", buf[0]);
        }
        if(read(p[0], &buf, sizeof(int)) > 0) {
            printf("2,received %d\n", buf[0]);
        }
        if(read(p[0], &buf, sizeof(int)) > 0) {
            printf("3,received %d\n", buf[0]);
        }
        exit(0);
    } else {
        write(p[1], &data[0], sizeof(int));
        write(p[1], &data[1], sizeof(int));
        close(p[1]);
        close(p[0]);
        wait(0);

        close(p[0]);
    }

    exit(0);
}