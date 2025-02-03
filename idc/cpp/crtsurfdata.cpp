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

cpactive pactive;   //进程的心跳
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

/**
 * 将datalist中的数据写入到文件中
 * @param outpath 数据文件存放的目录
 * @param datafmt 数据文件的格式，csv，xml或json
 */
bool crtsurffile(const string &outpath,const string &datafmt);

int main(int argc, char const *argv[])
{
    //站点参数文件 生成的测试数据存放的目录 本程序运行的日志
    if(argc != 5)
    {
        cout << "Using: ./crtsurfdata inifile outpath logfile datafmt\n";
        cout << "Examples: /project/tools/bin/procctl 60 /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log csv,xml,json\n\n";

        cout << "本程序用于生成气象站点观测的数据，每分钟运行一次。\n";
        cout << "inifile 气象站点参数文件名。\n";
        cout << "outpath 气象站点数据文件存放的目录。\n";
        cout << "logfile 本程序运行的日志文件名。\n"; 
        cout << "datafmt 输出数据文件的格式，支持csv、xml和json，中间用逗号隔开。\n\n"; 

        return -1;
    }
    const char *arg_inifile = argv[1],*arg_outpath = argv[2],*arg_logfile = argv[3],*arg_datafmt = argv[4];

    closeioandsignal(true);
    signal(SIGINT,EXIT);signal(SIGTERM,EXIT);

    pactive.addpinfo(10,"crtsurfdata");

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
    if(strstr(arg_datafmt,"csv") != NULL) crtsurffile(arg_outpath,"csv");
    if(strstr(arg_datafmt,"xml") != NULL) crtsurffile(arg_outpath,"xml");
    if(strstr(arg_datafmt,"json") != NULL) crtsurffile(arg_outpath,"json");

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

bool crtsurffile(const string &outpath,const string &datafmt)
{
    //拼接生成数据的文件名，如：/tmp/idc/surfdata/SURF_ZH_20250127232200_2254.csv
    string strfilename = outpath+"/"+"SURF_ZH_"+strddatetime+"_"+to_string(getpid())+"."+datafmt;

    cofile ofile;
    if(ofile.open(strfilename) == false)
    {
        logfile.write("ofile.open(%s) failed\n",strfilename.c_str());
        return false;
    }

    if(datafmt == "csv") ofile.writeline("站点代码,数据时间,气温,气压,相对湿度,风向,风速,降雨量,能见度\n");
    else if(datafmt == "xml") ofile.writeline("<data>\n");
    else if(datafmt == "json") ofile.writeline("{\"data\":[\n");
    else return false;
    
    int cnt = 0;
    for(auto &data:datalist)
    {
        if(datafmt == "csv")
        {
            ofile.writeline("%s,%s,%.1f,%.1f,%d,%d,%.1f,%.1f,%.1f\n",
            data.obtid,data.ddatatime,data.t/10.0,data.p/10.0,data.u,data.wd,data.wf/10.0,data.r/10.0,data.vis/10.0);
        }
        else if(datafmt == "xml")
        {
            ofile.writeline("<obtid>%s</obtid><ddatetime>%s</ddatetime><t>%.1f</t><p>%.1f</p><u>%d</u>"
            "<wd>%d</wd><wf>%.1f</wf><r>%.1f</r><vis>%.1f</vis><endl/>\n",
            data.obtid,data.ddatatime,data.t/10.0,data.p/10.0,data.u,data.wd,data.wf/10.0,data.r/10.0,data.vis/10.0);
        }
        else if(datafmt == "json")
        {
            ofile.writeline(R"({"obtid":"%s","ddatetime":"%s","t":"%.1f","p":"%.1f","u":"%d","wd":"%d","wf":"%.1f","r":"%.1f","vis":"%.1f"})",
            data.obtid,data.ddatatime,data.t/10.0,data.p/10.0,data.u,data.wd,data.wf/10.0,data.r/10.0,data.vis/10.0);
            if(cnt++ < datalist.size()-1)
                ofile.writeline(",\n");
            else
                ofile.writeline("\n");
        }
        else
        {
            return false;
        }
    }

    if(datafmt == "xml") ofile.writeline("</data>\n");
    else if(datafmt == "json") ofile.writeline("]}\n");

    ofile.closeandrename();

    logfile.write("生成数据文件%s成功，数据时间%s，记录数%d.\n",strfilename.c_str(),strddatetime,datalist.size());

    return true;
}