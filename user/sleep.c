#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// argc 是命令行总的参数个数  
// argv[] 是 argc 个参数，其中第 0 个参数是程序的全名，之后的参数是命令行后面跟的用户输入的参数
int main(int argc, char *argv[]) {
    // 参数个数不为2，说明命令格式错误
    if (argc != 2) {
        fprintf(2, "usage: sleep <ticks>\n");
        exit(1);
    }

    int ticks = atoi(argv[1]);
    
    // 不能睡眠小于0的时间
    if (ticks < 0) {
        fprintf(2, "sleep: invalid time\n");
        exit(1);
    }

    sleep(ticks);
    exit(0);
}