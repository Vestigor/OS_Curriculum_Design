#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    char buf[512];
    int i, n;
    char *p = buf;
    char *args[MAXARG];

    // 拷贝初始命令参数
    for (i = 0; i < argc - 1 && i < MAXARG - 1; i++)
    {
        args[i] = argv[i + 1];
    }

    // 每次读取标准输入的一个字符
    while ((n = read(0, p, 1)) > 0)
    {
        // 每次读取一行后开始处理
        if (*p == '\n')
        {
            *p = '\0';     // 将换行符变为字符串结束符
            args[i] = buf; // 添加当前行作为最后一个参数
            args[i + 1] = 0;

            // 创建子进程
            if (fork() == 0)
            {
                exec(args[0], args);
                fprintf(2, "exec failed\n");
                exit(1);
            }
            else
            {
                wait(0);
            }

            // 重置读取 buffer
            p = buf;
        }
        else
        {
            p++;
        }
    }

    exit(0);
}
