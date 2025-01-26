#include "_public.h"
using namespace idc;

clogfile logfile;   //本程序运行的日志

void EXIT(int sig);

int main(int argc, char const *argv[])
{
    //站点参数文件 生成的测试数据存放的目录 本程序运行的日志
    if(argc != 4)
    {
        cout << "Using: ./crtsurfdata inifile outpath logfile\n";
        cout << "Examples: /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log\n\n";

        cout << "inifile 气象站点参数文件名。\n";
        cout << "outpath 气象站点数据文件存放的目录。\n";
        cout << "logfile 本程序运行的日志文件名。\n"; 
    }

    closeioandsignal(true);
    signal(SIGINT,EXIT);signal(SIGTERM,EXIT);

    if(logfile.open(argv[3]) == false)
    {
        cout << "logfile.open(" << argv[3] <<") failed.\n";
        return -1;
    }

    logfile.write("crtsurfdata 开始运行。\n");

    logfile.write("crtsurfdata 运行结束。\n");
    return 0;
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d\n\n",sig);
    exit(0);
}