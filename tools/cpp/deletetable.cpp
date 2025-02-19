//表清理程序：清理表中满足条件的数据。
#include "_public.h"
#include "_ooci.h"
using namespace idc;

struct st_arg
{
    char connstr[101];     // 数据库的连接参数。
    char tname[31];        // 待清理的表名。
    char keycol[31];        // 待清理的表的唯一键字段名。
    char where[1001];    // 待清理的数据需要满足的条件。
    int    maxcount;        // 执行一次SQL删除的记录数。
    char starttime[31];   // 程序运行的时间区间。
    int  timeout;             // 本程序运行时的超时时间。
    char pname[51];      // 本程序运行时的程序名。
} starg;



clogfile logfile;   // 日志文件
connection conn;    // 数据库连接对象
cpactive pactive;   // 进程心跳

// 程序退出和信号2、15的处理函数。
void EXIT(int sig);
// 帮助文档
void _help();
// 解析xml参数
bool _xmltoarg(const char *strxmlbuffer);
// 判断当前时间是否在程序运行的时间区间内。
bool instarttime();
// 业务处理主函数。先select出符合条件的记录再分批delete，避免大事务。
bool _deletetable();

int main(int argc,char *argv[])
{
    if (argc!=3) { _help(); return -1; }

    // 关闭全部的信号和输入输出
    // 处理程序退出的信号
    closeioandsignal(); signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    if (logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败（%s）。\n",argv[1]); return -1;
    }

    // 把xml解析到参数starg结构中
    if (_xmltoarg(argv[2])==false) return -1;

    // 判断当前时间是否在程序运行的时间区间内。
    if (instarttime()==false) return 0;

    pactive.addpinfo(starg.timeout,starg.pname);

    if (conn.connecttodb(starg.connstr,"Simplified Chinese_China.AL32UTF8",true) != 0)  // 清理数据不用回滚，开启自动提交。
    {
        logfile.write("connect database(%s) failed.\n%s\n",starg.connstr,conn.message());
        EXIT(-1);
    }

    // 业务处理主函数。
    _deletetable();
}

void _help()
{
    printf("Using:/project/tools/bin/deletetable logfilename xmlbuffer\n\n");

    printf("Sample:/project/tools/bin/procctl 3600 /project/tools/bin/deletetable /log/idc/deletetable_ZHOBTMIND1.log "\
                         "\"<connstr>idc/idcpwd@snorcl11g_124</connstr><tname>T_ZHOBTMIND1</tname>"\
                         "<keycol>rowid</keycol><where>where ddatetime<sysdate-0.03</where>"\
                         "<maxcount>10</maxcount><starttime>22,23,00,01,02,03,04,05,06,13</starttime>"\
                         "<timeout>120</timeout><pname>deletetable_ZHOBTMIND1</pname>\"\n\n");

    printf("本程序是共享平台的公共功能模块，用于清理表中的数据。\n");

    printf("logfilename 本程序运行的日志文件。\n");
    printf("xmlbuffer   本程序运行的参数，用xml表示，具体如下：\n\n");

    printf("connstr     数据库的连接参数，格式：username/passwd@tnsname。\n");
    printf("tname       待清理数据表的表名。\n");
    printf("keycol      待清理数据表的唯一键字段名，可以用记录编号，如keyid，建议用rowid，效率最高。\n");
    printf("where       待清理的数据需要满足的条件，即SQL语句中的where部分。\n");
    printf("maxcount    执行一次SQL语句删除的记录数，建议在100-500之间。\n");
    printf("starttime   程序运行的时间区间，例如02,13表示：程序启动时，只在02时和13时运行，其它时间不运行。"\
                        "如果starttime为空，表示不限制，只要本程序启动，就会执行数据抽取任务，"\
                        "为了减少对数据库的压力，数据清理一般在业务最闲的时候时进行。\n");
    printf("timeout     本程序的超时时间，单位：秒，建议设置120以上。\n");
    printf("pname       进程名，尽可能采用易懂的、与其它进程不同的名称，方便故障排查。\n\n");
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d\n\n",sig);

    conn.disconnect();

    exit(0);
}

bool _xmltoarg(const char *strxmlbuffer)
{
    memset(&starg,0,sizeof(struct st_arg));

    getxmlbuffer(strxmlbuffer,"connstr",starg.connstr,100);
    if (strlen(starg.connstr)==0) { logfile.write("connstr is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"tname",starg.tname,30);
    if (strlen(starg.tname)==0) { logfile.write("tname is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"keycol",starg.keycol,30);
    if (strlen(starg.keycol)==0) { logfile.write("keycol is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"where",starg.where,1000);
    if (strlen(starg.where)==0) { logfile.write("where is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"starttime",starg.starttime,30);

    getxmlbuffer(strxmlbuffer,"maxcount",starg.maxcount);
    if (starg.maxcount==0) { logfile.write("maxcount is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"timeout",starg.timeout);
    if (starg.timeout==0) { logfile.write("timeout is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"pname",starg.pname,50);
    if (strlen(starg.pname)==0) { logfile.write("pname is null.\n"); return false; }

    return true;
}

bool instarttime()
{
    if (strlen(starg.starttime)!=0)
    {
        if (strstr(starg.starttime,ltime1("hh24").c_str())==0)
        return false;
    }

    return true;
}

bool _deletetable()
{
    ctimer timer;

    char tmpvalue[21];      // 待删除记录的唯一键的值。

    // 1）准备从表中提取数据的SQL语句。
	// select rowid from T_ZHOBTMIND1 where ddatetime<sysdate-1
    sqlstatement stmtsel(&conn);
    stmtsel.prepare("select %s from %s %s",starg.keycol,starg.tname,starg.where);
    stmtsel.bindout(1,tmpvalue,20);

	// 2）准备从表中删除数据的SQL语句，绑定输入参数。
	// delete from T_ZHOBTMIND1 where rowid in (:1,:2,:3,:4,:5,:6,:7,:8,:9,:10);
    string strsql=sformat("delete from %s where %s in (",starg.tname,starg.keycol);    
    for (int i=0;i<starg.maxcount;i++)
    {
        strsql=strsql+sformat(":%lu,",i+1);
    }
    deleterchr(strsql,',');
    strsql=strsql+")";

    char keyvalues[starg.maxcount][21];   // 存放唯一键字段的值的数组。

    sqlstatement stmtdel(&conn);
    stmtdel.prepare(strsql);                       // 准备删除数据的SQL语句。
    for (int ii=0;ii<starg.maxcount;ii++)
        stmtdel.bindin(ii+1,keyvalues[ii],20);

    if (stmtsel.execute()!=0)                      // 执行提取数据的SQL语句。
    {
        logfile.write("stmtsel.execute() failed.\n%s\n%s\n",stmtsel.sql(),stmtsel.message());
        return false;
    }

    int ccount=0;                 // keyvalues数组中有效元素的个数。
    memset(keyvalues,0,sizeof(keyvalues));

	// 3）处理结果集。
	while (true) 
	{
        memset(tmpvalue,0,sizeof(tmpvalue));
        if (stmtsel.next()!=0) break;
        strcpy(keyvalues[ccount],tmpvalue);
        ccount++;

        // 如果数组中的记录数达到了starg.maxcount，执行一次删除数据的SQL语句。
        if (ccount == starg.maxcount)
        {
            if (stmtdel.execute()!=0)       // 执行从表中删除数据的SQL语句。
            {
                logfile.write("stmtdel.execute() failed.\n%s\n%s\n",stmtdel.sql(),stmtdel.message());
                return false;
            }

            ccount=0;
            memset(keyvalues,0,sizeof(keyvalues));

            pactive.uptatime();               // 进程心跳。
        }
	}

    // 4）如果临时数组中还有记录，再执行一次删除数据的SQL语句。
    if (ccount>0)
    {
        if (stmtdel.execute()!=0)
        {
            logfile.write("stmtdel.execute() failed.\n%s\n%s\n",stmtdel.sql(),stmtdel.message());
            return false;
        }
    }

    if (stmtsel.rpc()>0) logfile.write("deleted from %s %d rows in %.02fsec.\n",starg.tname,stmtsel.rpc(),timer.elapsed());

    return true;
}
