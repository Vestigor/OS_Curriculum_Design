#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void subProcess(int pfd[2]) {
    close(pfd[1]);  // 只读

    int prime;
    if (read(pfd[0], &prime, sizeof(int)) == 0) {
        close(pfd[0]);
        exit(0);
    }

    printf("prime %d\n", prime);

    int n;
    int newp[2];
    pipe(newp);

    if (fork() == 0) {
        // 子进程递归处理
        close(newp[1]);
        subProcess(newp);
    } else {
        close(newp[0]); // 父只写
        while (read(pfd[0], &n, sizeof(int)) > 0) {
        if (n % prime != 0) {
            write(newp[1], &n, sizeof(int));
        }
        }
        close(pfd[0]);
        close(newp[1]);
        wait(0);
        exit(0);
    }
}

int main() {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        // 子进程处理筛选
        subProcess(p);
        exit(0);
    } 
    else {
        // 父进程写入 2~35
        close(p[0]); // 父写
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);
        wait(0);
        exit(0);
    }
}
