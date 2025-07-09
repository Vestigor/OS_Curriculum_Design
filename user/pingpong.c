#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main() {
    int p2c[2]; // pipe: parent to child
    int c2p[2]; // pipe: child to parent

    pipe(p2c);
    pipe(c2p);

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // 子进程
        close(p2c[1]); // 子进程不从 p2c 写
        close(c2p[0]); // 子进程不从 c2p 读

        char byte;
        read(p2c[0], &byte, 1); // 从父进程读取一个字节
        printf("%d: received ping\n", getpid());

        write(c2p[1], &byte, 1); // 写回给父进程
        
        // 管道使用完毕后，释放剩余端口
        close(p2c[0]);
        close(c2p[1]);

    } else {
        // 父进程
        close(p2c[0]); // 父进程不从 p2c 读
        close(c2p[1]); // 父进程不从 c2p 写

        char byte = 'A';
        write(p2c[1], &byte, 1); // 发一个字节给子进程

        read(c2p[0], &byte, 1); // 读取回来的字节
        printf("%d: received pong\n", getpid());
        
        // 管道使用完毕后，释放剩余端口
        close(p2c[1]);
        close(c2p[0]);

        wait(0); // 等待子进程退出
    }

    exit(0);
}

