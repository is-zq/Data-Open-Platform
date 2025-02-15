#include "_tools.h"
using namespace idc;

// 程序运行参数的结构体。
struct st_arg
{
    char connstr[101];          // 数据库的连接参数。
    char charset[51];            // 数据库的字符集。
    char inifilename[301];    // 数据入库的参数配置文件。
    char xmlpath[301];         // 待入库xml文件存放的目录。
    char xmlpathbak[301];   // xml文件入库后的备份目录。
    char xmlpatherr[301];    // 入库失败的xml文件存放的目录。
    int  timetvl;                    // 本程序运行的时间间隔，本程序常驻内存。
    int  timeout;                   // 本程序运行时的超时时间。
    char pname[51];            // 本程序运行时的程序名。
} starg;

// 数据入库参数的结构体。
struct st_xmltotable
{
    char filename[101];    // xml文件的匹配规则，用逗号分隔。
    char tname[31];         // 待入库的表名。
    int    uptbz;                // 更新标志：1-更新；2-不更新。
    char execsql[301];     // 处理xml文件之前，执行的SQL语句。
} stxmltotable;

clogfile logfile;         // 本程序运行的日志。
connection conn;     // 数据库连接。
unordered_map<string,ctcols> cols_map;  // 表的字段和主键字段
ctcols* tcols;
vector<struct st_xmltotable> vxmltotable;             // 数据入库的参数的容器。
string strinsertsql;                        // 插入表的SQL语句。
string strupdatesql;                      // 更新表的SQL语句。
vector<string> vcolvalue;            // 存放从xml每一行中解析出来的字段的值，将用于插入和更新表的SQL语句绑定变量。
sqlstatement stmtins,stmtupt;     // 插入和更新表的sqlstatement语句。
ctimer timer;                                    // 记录处理每个xml文件消耗的时间。
int totalcount,inscount,uptcount;    // xml文件的总记录数、插入记录数和更新记录数。
cpactive pactive;    // 进程的心跳。

// 程序退出的信号处理函数。
void EXIT(int sig);
// 程序的帮助文档
void _help(char *argv[]);
// 把xml解析到参数starg结构中 
bool _xmltoarg(const char *strxmlbuffer);
// 业务处理主函数。
bool _xmltodb();
// 把数据入库的参数配置文件starg.inifilename加载到vxmltotable容器中。
bool loadxmltotable();                         
// 根据文件名，从vxmltotable容器中查找的入库参数，存放在stxmltotable结构体中。                    
bool findxmltotable(const string &xmlfilename);
// 处理xml文件的子函数，返回值：0-成功；1-入库参数不正确；2-数据库错误；3-待入库的表不存在；4-执行入库前的SQL语句失败；5- 打开xml文件失败。
int insertxmltodb(const string &fullfilename,const string &filename);
// 拼接插入和更新表数据的SQL。
void crtsql();                                 
// 准备插入和更新的sql语句，绑定输入变量。
void preparesql();                         
// 在处理xml文件之前，如果stxmltotable.execsql不为空，就执行它。
bool execsql();
// 解析xml，存放在已绑定的输入变量vcolvalue数组中。
void splitbuffer(const string &strBuffer);
// 把xml文件移动到备份目录或错误目录。
bool xmltobakerr(const string &fullfilename,const string &srcpath,const string &dstpath);

int main(int argc,char *argv[])
{
    if (argc!=3)
    {
        _help(argv);
        return -1;
    }

    // 关闭全部的信号和输入输出。
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程。
    // 但请不要用 "kill -9 +进程号" 强行终止。
    closeioandsignal(true); 
    signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    if (logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败（%s）。\n",argv[1]);
        return -1;
    }

    // 把xml解析到参数starg结构中
    if (_xmltoarg(argv[2])==false) return -1;

    pactive.addpinfo(starg.timeout,starg.pname);  // 设置进程的心跳。

    _xmltodb();           // 业务处理主函数。
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d\n\n",sig);

    conn.disconnect();

    exit(0);
}

void _help(char *argv[])
{
    printf("Using:/project/tools/bin/xmltodb logfilename xmlbuffer\n\n");

    printf("Sample:/project/tools/bin/procctl 10 /project/tools/bin/xmltodb /log/idc/xmltodb_vip.log "\
              "\"<connstr>idc/idcpwd@snorcl11g_124</connstr><charset>Simplified Chinese_China.AL32UTF8</charset>"\
              "<inifilename>/project/idc/ini/xmltodb.xml</inifilename>"\
              "<xmlpath>/idcdata/xmltodb/vip</xmlpath><xmlpathbak>/idcdata/xmltodb/vipbak</xmlpathbak>"\
              "<xmlpatherr>/idcdata/xmltodb/viperr</xmlpatherr>"\
              "<timetvl>5</timetvl><timeout>50</timeout><pname>xmltodb_vip</pname>\"\n\n");

    printf("本程序是共享平台的公共功能模块，用于把xml文件入库到Oracle的表中。\n");
    printf("logfilename   本程序运行的日志文件。\n");
    printf("xmlbuffer     本程序运行的参数，用xml表示，具体如下：\n\n");

    printf("connstr     数据库的连接参数，格式：username/passwd@tnsname。\n");
    printf("charset     数据库的字符集，这个参数要与数据源数据库保持一致，否则会出现中文乱码的情况。\n");
    printf("inifilename 数据入库的参数配置文件。\n");
    printf("xmlpath     待入库xml文件存放的目录。\n");
    printf("xmlpathbak  xml文件入库后的备份目录。\n");
    printf("xmlpatherr  入库失败的xml文件存放的目录。\n");
    printf("timetvl     扫描xmlpath目录的时间间隔（执行入库任务的时间间隔），单位：秒，视业务需求而定，2-30之间。\n");
    printf("timeout     本程序的超时时间，单位：秒，视xml文件大小而定，建议设置30以上。\n");
    printf("pname       进程名，尽可能采用易懂的、与其它进程不同的名称，方便故障排查。\n\n");
}

bool _xmltoarg(const char *strxmlbuffer)
{
    memset(&starg,0,sizeof(struct st_arg));

    getxmlbuffer(strxmlbuffer,"connstr",starg.connstr,100);
    if (strlen(starg.connstr)==0) { logfile.write("connstr is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"charset",starg.charset,50);
    if (strlen(starg.charset)==0) { logfile.write("charset is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"inifilename",starg.inifilename,300);
    if (strlen(starg.inifilename)==0) { logfile.write("inifilename is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"xmlpath",starg.xmlpath,300);
    if (strlen(starg.xmlpath)==0) { logfile.write("xmlpath is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"xmlpathbak",starg.xmlpathbak,300);
    if (strlen(starg.xmlpathbak)==0) { logfile.write("xmlpathbak is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"xmlpatherr",starg.xmlpatherr,300);
    if (strlen(starg.xmlpatherr)==0) { logfile.write("xmlpatherr is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"timetvl",starg.timetvl);
    if (starg.timetvl< 2) starg.timetvl=2;   
    if (starg.timetvl>30) starg.timetvl=30;

    getxmlbuffer(strxmlbuffer,"timeout",starg.timeout);
    if (starg.timeout==0) { logfile.write("timeout is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"pname",starg.pname,50);
    if (strlen(starg.pname)==0) { logfile.write("pname is null.\n"); return false; }

    return true;
}

bool _xmltodb()
{
    cdir dir;

    int icnt=31;    // 计数器，达到一定次数就重新加载配置文件，因为可能修改
    while (true)      // 循环执行入库任务
    {
        // 加载数据入库的参数配置文件
        if (icnt>30)
        {
            if (loadxmltotable()==false)
            {
                return false;
            }
            cols_map.clear();   // 重新加载表字段
            icnt=0;
        }else icnt++;

        // 打开starg.xmlpath目录，为了保证先生成的xml文件先入库，打开目录的时候，应该按文件名排序。
        if (dir.opendir(starg.xmlpath,"*.XML",10000,false,true) == false)
        {
            logfile.write("dir.opendir(%s) failed.\n",starg.xmlpath);
            return false;
        }
        
        if (conn.isopen() == false)
        {
            if (conn.connecttodb(starg.connstr,starg.charset) != 0)
            {
                logfile.write("connect database(%s) failed.\n%s\n",starg.connstr,conn.message());
                return false;
            }
            logfile.write("connect database(%s) ok.\n",starg.connstr);
        }

        while (true)
        {
            if (dir.readdir()==false) break;

            logfile.write("处理文件%s...",dir.m_ffilename.c_str());

            // 处理xml文件的子函数
            int ret = insertxmltodb(dir.m_ffilename,dir.m_filename);

            pactive.uptatime();   // 更新进程的心跳。

            // 0-成功，没有错误。把已入库的xml文件移动到备份目录。
            if (ret==0)
            {
                logfile << "ok(" << stxmltotable.tname << ",总数:" << totalcount << ",插入:" << inscount 
                           << ",更新:" << uptcount << ",耗时:" << timer.elapsed() <<").\n";

                // 把xml文件移动到starg.xmlpathbak参数指定的备份目录
                if (xmltobakerr(dir.m_ffilename,starg.xmlpath,starg.xmlpathbak)==false) return false;
            }

            // 1-入库参数不正确；3-待入库的表不存在；4-执行入库前的SQL语句失败。把xml文件移动到错误目录。
            if ( (ret==1) || (ret==3) || (ret==4))
            {
                if (ret==1) logfile << "failed，入库参数不正确。\n";
                if (ret==3) logfile << "failed，待入库的表（" << stxmltotable.tname << "）不存在。\n";
                if (ret==4) logfile << "failed，执行入库前的SQL语句失败。\n";

                // 把xml文件移动到starg.xmlpatherr参数指定的错误目录
                if (xmltobakerr(dir.m_ffilename,starg.xmlpath,starg.xmlpatherr)==false) return false;
            }

            // 2-数据库错误，函数返回，程序将退出。
            if (ret==2)
            {
                logfile << "failed，数据库错误。\n";
                return false;
            }

            // 5- 打开xml文件失败，函数返回，程序将退出。
            if (ret==5)
            {
                logfile << "failed，打开文件失败。\n";
                return false;
            }
        }

        // 如果刚才处理了文件，表示不空闲，可能不断的有文件需要入库，就不sleep了。
        if (dir.size()==0)  sleep(starg.timetvl);

        pactive.uptatime();   // 更新进程的心跳。
    }

    return true;
}

bool loadxmltotable()
{
    vxmltotable.clear();

    cifile ifile;
    if (ifile.open(starg.inifilename)==false)
    {
        logfile.write("ifile.open(%s) 失败。\n",starg.inifilename);
        return false;
    }

    string strbuffer;

    while (true)
    {
        if (ifile.readline(strbuffer,"<endl/>")==false) break;

        memset(&stxmltotable,0,sizeof(struct st_xmltotable));

        getxmlbuffer(strbuffer,"filename",stxmltotable.filename,100);   // xml文件的匹配规则，用逗号分隔。
        getxmlbuffer(strbuffer,"tname",stxmltotable.tname,30);            // 待入库的表名。
        getxmlbuffer(strbuffer,"uptbz",stxmltotable.uptbz);                   // 更新标志：1-更新；2-不更新。
        getxmlbuffer(strbuffer,"execsql",stxmltotable.execsql,300);       // 处理xml文件之前，执行的SQL语句。
   
        vxmltotable.push_back(stxmltotable);
    }

    return true;
}

bool findxmltotable(const string &xmlfilename)
{
    for ( auto &xmltotable : vxmltotable)
    {
        if (matchstr(xmlfilename,xmltotable.filename)==true)
        {
            stxmltotable = xmltotable;  
            return true;
        }
    }

    return false;
}

int insertxmltodb(const string &fullfilename,const string &filename)
{
    timer.start();          // 开始计时。
    totalcount = inscount = uptcount = 0;

    // 1）根据待入库的文件名，查找入库参数，得到对应的表名。
    if (findxmltotable(filename)==false) return 1; 
    
    // 2）根据表名，读取数据字典，得到表的字段名和主键。
    if (cols_map.find(stxmltotable.tname) != cols_map.end())    // 已获得
    {
        tcols = &cols_map[stxmltotable.tname];
    }
    else    // 未获得，获取字段名和主键
    {
        ctcols tmpcols;
        if (tmpcols.allcols(conn,stxmltotable.tname)==false) return 2;
        if (tmpcols.pkcols(conn,stxmltotable.tname)==false) return 2;

        // 3）根据表的字段名和主键，拼接插入和更新表的SQL语句。
        // 如果表不存在，返回。
        if (tmpcols.m_allcols.size()==0) return 3;
        cols_map.emplace(stxmltotable.tname,std::move(tmpcols));
        tcols = &cols_map[stxmltotable.tname];
    }

    // 拼接插入和更新表的SQL语句。
    crtsql();
    // 提前给输入参数分配足够空间避免扩容导致地址改变
    vcolvalue.resize(tcols->m_allcols.size());
    for(int i=0;i<vcolvalue.size();i++)
    {
        vcolvalue[i].reserve(100);
    }
    // 准备SQL语句，绑定输入变量。
    preparesql();

    // 执行stxmltotable.execsql
    if (execsql()==false) return 4;

    // 4）打开xml文件。
    cifile ifile;
    if (ifile.open(fullfilename)==false)
    {
        conn.rollback();
        return 5;
    }

    string strbuffer;
    while (true)
    {
        // 5）从xml文件中读取一行数据。
        if (ifile.readline(strbuffer,"<endl/>")==false) break;

        totalcount++;

        // 6）根据表的字段名，从读取的一行数据中解析出每个字段值。
        splitbuffer(strbuffer);

        // 7）执行插入或更新的SQL语句。
        if (stmtins.execute()!=0)
        {
            if (stmtins.rc()==1)          // 违反唯一性约束，表示记录已存在。
            {
                if (stxmltotable.uptbz==1)         // 判断入库参数的更新标志。
                {
                    if (stmtupt.execute()!=0)
                    {
                        // update失败，记录错误然后继续处理
                        logfile.write("%s",strbuffer.c_str());
                        logfile.write("stmtupt.execute() failed.\n%s\n%s\n",stmtupt.sql(),stmtupt.message());
                    }
                    else uptcount++;
                }
            }
            else
            {
                // insert失败，记录错误然后继续处理
                logfile.write("%s",strbuffer.c_str());
                logfile.write("stmtins.execute() failed.\n%s\n%s\n",stmtins.sql(),stmtins.message());

                // 数据库系统出了问题，可能有以下原因
                // ORA-03113: 通信通道的文件结尾；ORA-03114: 未连接到ORACLE；ORA-03135: 连接失去联系；ORA-16014：归档失败。
                if ( (stmtins.rc()==3113) || (stmtins.rc()==3114) || (stmtins.rc()==3135) || (stmtins.rc()==16014)) return 2;
            }
        }
        else inscount++;
    }

    // 8）提交事务。
    conn.commit();

    return 0;
}

void crtsql()
{
    // 拼接插入表的SQL语句。 
    // insert into T_ZHOBTMIND1(obtid,ddatetime,t,p,u,wd,wf,r,vis,keyid) 
    //                                   values(:1,to_date(:2,'yyyymmddhh24miss'),:3,:4,:5,:6,:7,:8,:9,SEQ_ZHOBTMIND1.nextval)
    string strinsertcols;    // insert语句的字段列表。
    string strinsertvals;    // insert语句的values。
    int colseq=1;           // values部分字段的序号。

    for (auto &col : tcols->m_vallcols)
    {
        // upttime字段的默认值是sysdate，不需要处理。
        if (strcmp(col.colname,"upttime")==0) continue;

        // 拼接strinsertcols
        strinsertcols = strinsertcols + col.colname+",";

        // 拼接strinsertvals，需要区分keyid字段、date和非date字段。
        if (strcmp(col.colname,"keyid")==0)           // keyid字段需要特殊处理，从与表同名的序列生成器中获取keyid的值。
        {
            strinsertvals = strinsertvals + sformat("SEQ_%s.nextval",stxmltotable.tname+2) + ",";
        }
        else
        {
            if (strcmp(col.datatype,"date")==0)        // 日期时间字段需要特殊处理。
                strinsertvals = strinsertvals + sformat("to_date(:%d,'yyyymmddhh24miss')",colseq) + ",";          // 日期时间字段。
            else
                strinsertvals = strinsertvals + sformat(":%d",colseq) + ",";           // 非日期时间字段。

            colseq++;      // 如果是字段名是keyid，colseq不需要加1，其它字段才加1。
        }
    }

    deleterchr(strinsertcols,',');  deleterchr(strinsertvals,',');   // 把最后一个多余的逗号删除。

    // 拼接出完整的插入语句。
    sformat(strinsertsql,"insert into %s(%s) values(%s)",stxmltotable.tname,strinsertcols.c_str(),strinsertvals.c_str());

    // 参数指定不需要更新
    if (stxmltotable.uptbz!=1) return;

    // 拼接更新表的SQL语句。
    // update T_ZHOBTMIND1 set t=:1,p=:2,u=:3,wd=:4,wf=:5,r=:6,vis=:7 
    //                             where obtid=:8 and ddatetime=to_date(:9,'yyyymmddhh24miss')
    strupdatesql=sformat("update %s set ",stxmltotable.tname);

    // 拼接update语句set后面的部分。
    colseq=1;         // 绑定变量的序号
    for (auto &col : tcols->m_vallcols)
    {
        // 如果是主键字段，不需要拼接在set的后面。
        if (col.pkseq!=0) continue;

        // keyid不需要更新
        if (strcmp(col.colname,"keyid")==0) continue;

        // upttime直接赋值sysdate。
        if (strcmp(col.colname,"upttime")==0)
        {
            strupdatesql = strupdatesql +"upttime=sysdate,";  continue;
        }

        if (strcmp(col.datatype,"date")!=0)
            strupdatesql=strupdatesql+sformat("%s=:%d,",col.colname,colseq);
        else
            strupdatesql=strupdatesql+sformat("%s=to_date(:%d,'yyyymmddhh24miss'),",col.colname,colseq);

        colseq++;
    }

    deleterchr(strupdatesql,',');  // 删除最后一个多余的逗号。

    // 拼接update语句where后面的部分。
    strupdatesql = strupdatesql +  " where 1=1 ";      // 用1=1方便后面统一拼接
    // where 1=1 and obtid=:8 and ddatetime=to_date(:9,'yyyymmddhh24miss')

    for (auto &col : tcols->m_vallcols)
    {
        if (col.pkseq==0) continue;   // 不是主键，跳过。

        if (strcmp(col.datatype,"date")!=0)
             strupdatesql = strupdatesql + sformat(" and %s=:%d",col.colname,colseq);
        else
             strupdatesql = strupdatesql + sformat(" and %s=to_date(:%d,'yyyymmddhh24miss')",col.colname,colseq);

        colseq++;
    }

    return;
}

void preparesql()
{
    // 准备插入表的sql语句
    stmtins.connect(&conn);
    stmtins.prepare(strinsertsql);

    int colseq=1;        // 绑定序号。
    for (int i=0;i<tcols->m_vallcols.size();i++)
    {
        // upttime和keyid不需要绑定参数。
        if ( (strcmp(tcols->m_vallcols[i].colname,"upttime")==0) ||
             (strcmp(tcols->m_vallcols[i].colname,"keyid")==0) ) continue;

        stmtins.bindin(colseq,vcolvalue[i],tcols->m_vallcols[i].collen);
        colseq++;
    }

    // 准备更新表的sql语句
    // 参数中指定了不需要更新，返回。
    if (stxmltotable.uptbz!=1) return;

    stmtupt.connect(&conn);
    stmtupt.prepare(strupdatesql);

    colseq=1;
    // 绑定set部分的输入参数。
    for (int i=0;i<tcols->m_vallcols.size();i++)
    {
        // 主键字段不更新
        if (tcols->m_vallcols[i].pkseq!=0) continue;

        // upttime和keyid不更新
        if ( (strcmp(tcols->m_vallcols[i].colname,"upttime")==0) ||
             (strcmp(tcols->m_vallcols[i].colname,"keyid")==0) ) continue;

        stmtupt.bindin(colseq,vcolvalue[i],tcols->m_vallcols[i].collen);
        colseq++;
    }

    // 绑定where部分的输入参数。
    for (int i=0;i<tcols->m_vallcols.size();i++)
    {
        // 不是主键字段，跳过
        if (tcols->m_vallcols[i].pkseq==0) continue;

        stmtupt.bindin(colseq,vcolvalue[i],tcols->m_vallcols[i].collen);
        colseq++;
    }

    return;
}

bool execsql()
{
    if (strlen(stxmltotable.execsql)==0) return true;

    sqlstatement stmt;
    stmt.connect(&conn);
    stmt.prepare(stxmltotable.execsql);
    if (stmt.execute()!=0)
    {
        logfile.write("stmt.execute() failed.\n%s\n%s\n",stmt.sql(),stmt.message()); return false;
    }

    return true;
}

void splitbuffer(const string &strBuffer)
{
    string strtemp;

    for (int i=0;i<tcols->m_vallcols.size();i++)
    {
        // 用临时变量strtemp是为了防止由于移动赋值改变vcolvalue数组中string的内部地址。
		getxmlbuffer(strBuffer,tcols->m_vallcols[i].colname,strtemp,tcols->m_vallcols[i].collen);

        // 日期时间字段date，提取数字。 
        if (strcmp(tcols->m_vallcols[i].datatype,"date")==0)
        {
            picknumber(strtemp,strtemp,false,false);
        }
        else if (strcmp(tcols->m_vallcols[i].datatype,"number")==0)
        {
            // 数值字段number，提取数字、+-符号和圆点。
            picknumber(strtemp,strtemp,true,true);
        }

        // 字符字段char，不需要任何处理。

        //vcolvalue[i] = strtemp;               // 这样可能会出错，不确定原因？
        vcolvalue[i] = strtemp.c_str();
    }

    return;
}

bool xmltobakerr(const string &fullfilename,const string &srcpath,const string &dstpath)
{
    string dstfilename = fullfilename;

    replacestr(dstfilename,srcpath,dstpath,false);

    if (renamefile(fullfilename,dstfilename.c_str())==false)
    {
        logfile.write("renamefile(%s,%s) failed.\n",fullfilename,dstfilename.c_str());
        return false;
    }

    return true;
}
