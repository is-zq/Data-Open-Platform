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

struct st_surfdata
{
    char obtid[11]; //站号
    char ddatatime[15]; //数据时间，格式：yyyymmddhh24miss，精确到分钟，秒固定填00
    int t;      //气温，单位：0.1摄氏度
    int p;      //气压，单位：0.1百帕        
    int u;      //相对湿度，0-100
    int wd;     //风向，0-360
    int wf;     //风速，单位：0.1 m/s
    int r;      //降雨量，单位：0.1mm
    int vis;    //能见度，单位：0.1m
};

clogfile logfile;   //本程序运行的日志
list<st_stcode> stlist; //存放参数
list<st_surfdata> datalist; //存放观测数据
char strddatetime[15];  //获取数据的时间

/* 信号2、15的处理函数 */
void EXIT(int sig);

/* 从参数文件中加载参数到stlist */
bool loadstcode(const string &inifile);

/* 根据站点参数生成观测数据存到datalist中 */
void crtsurfdata();

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
    memset(strddatetime,0,sizeof(strddatetime));
    ltime(strddatetime,"yyyymmddhh24miss"); //获取系统时间
    strncpy(strddatetime+12,"00",2);    //秒固定为00
    crtsurfdata();

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

void crtsurfdata()
{
    srand(time(0));

    for(auto &st:stlist)
    {
        st_surfdata stsurfdata;
        strcpy(stsurfdata.obtid,st.obtid);
        strcpy(stsurfdata.ddatatime,strddatetime);
        stsurfdata.t = rand()%350;
        stsurfdata.p = rand()%265 + 10000;
        stsurfdata.u = rand()%101;
        stsurfdata.wd = rand()%360;
        stsurfdata.wf = rand()%150;
        stsurfdata.r = rand()%16;
        stsurfdata.vis = rand()%5001 + 100000;
        datalist.emplace_back(stsurfdata);
    }
}