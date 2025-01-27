#include "_public.h"
using namespace idc;

// 省   站号  站名 纬度   经度  海拔高度
struct st_stcode
{
    char provname[31];  //操作数据库时char[]更方便
    char obtid[11];
    char obtname[31];
    double lat;
    double lon;
    double height;      //单位：米
};

clogfile logfile;   //本程序运行的日志
list<st_stcode> stlist; //存放参数

void EXIT(int sig); //信号2、15的处理函数
bool loadstcode(const string &inifile); //从参数文件中加载参数到stlist

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

        return -1;
    }
    const char *arg_inifile = argv[1],*arg_outpath = argv[2],*arg_logfile = argv[3];

    closeioandsignal(true);
    signal(SIGINT,EXIT);signal(SIGTERM,EXIT);

    if(logfile.open(arg_logfile) == false)
    {
        cout << "logfile.open(" << argv[3] <<") failed.\n";
        return -1;
    }

    logfile.write("crtsurfdata 开始运行。\n");

    // 从站点参数文件中加载站点参数，存放于容器中；
    if(loadstcode(arg_inifile) == false)
    {
        EXIT(-1);
    }

    // 根据站点参数，生成站点观测数据；

    // 把站点观测数据保存到文件中

    logfile.write("crtsurfdata 运行结束。\n");
    return 0;
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d\n\n",sig);
    exit(0);
}

bool loadstcode(const string& inifile)
{
    cifile ifile;
    if(ifile.open(inifile) == false)
    {
        logfile.write("ifile.open(%s) failed\n",inifile.c_str());
        return false;
    }

    string strbuffer;
    ifile.readline(strbuffer);  //先读取标题

    while(ifile.readline(strbuffer))
    {
        ccmdstr cmdstr(strbuffer,",");
        st_stcode stcode;
        cmdstr.getvalue(0,stcode.provname,30);
        cmdstr.getvalue(1,stcode.obtid,30);
        cmdstr.getvalue(2,stcode.obtname,30);
        cmdstr.getvalue(3,stcode.lat);
        cmdstr.getvalue(4,stcode.lon);
        cmdstr.getvalue(5,stcode.height);
        stlist.emplace_back(stcode);
    }

    return true;
}