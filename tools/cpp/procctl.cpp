//调度程序：定期调度程序或脚本。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc,char *argv[])
{
    if(argc < 3)
    {
        printf("Using: ./procctl timetvl program argv...\n");
        printf("Example: /project/tools/bin/procctl 10 /usr/bin/tar zcvf /tmp/tmp.tgz /usr/include\n");
        printf("Example: /project/tools/bin/procctl 60 /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log csv,xml,json\n\n\n");

        printf("# 本程序是服务程序的调度程序，周期性启动服务程序或shell脚本。\n");
        printf("#   timetvl: 运行周期，单位：秒。\n");
        printf("#       被调度的程序运行结束后，在timeval秒后会被procctl重新启动\n");
        printf("#   program: 被调度的程序名，必须使用全路径。\n");
        printf("#   argv...: 被调度程序的参数。\n#\n");
        printf("# 注意：本程序不会被kill杀死，但可以用kill -9强行杀死。\n");

        return -1;
    }

    // 关闭信号和I/O，本程序不希望被打扰。
    // 注意：被调度程序的I/O也会被影响
    for(int i=0;i<64;i++)
    {
        signal(i,SIG_IGN);
        close(i);
    }

    // 让该程序运行在后台由init进程托管，使其不受shell控制。
    if(fork() != 0) exit(0);

    // 启用SIGCHILD信号，让父进程可以wait子进程获取退出状态。
    signal(SIGCHLD,SIG_DFL);

    // exec()的参数
    char *pargv[argc];
    for(int i=2;i<argc;i++)
        pargv[i-2] = argv[i];
    pargv[argc-2] = nullptr;

    while(true)
    {
        if(fork() == 0) //子进程加载被调度程序
        {
            execv(argv[2],pargv);
            exit(0);    //exec运行失败退出
        }
        else    //父进程等待子进程
        {
            int status;
            wait(&status);
            sleep(atoi(argv[1]));   //休眠timetvl秒再次调度
        }
    }

    return 0;
}