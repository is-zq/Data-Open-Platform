//FTP文件下载程序：使用FTP协议下载文件。
#include "_public.h"
#include "_ftp.h"
using namespace idc;

// 程序运行参数的结构体。
struct st_arg
{
    char host[31];                        // 远程服务端的IP和端口。
    int  mode;                           // 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。
    char username[31];               // 远程服务端ftp的用户名。
    char password[31];                // 远程服务端ftp的密码。
    char remotepath[256];          // 远程服务端存放文件的目录。
    char localpath[256];              // 本地文件存放的目录。
    char matchname[256];          // 待下载文件匹配的规则。
    int  ptype;                            // 下载后服务端文件的处理方式：1-什么也不做；2-删除；3-备份。
    char remotepathbak[256];   // 下载后服务端文件的备份目录。
    char okfilename[256];          // 已下载成功文件信息存放的文件。
    bool checkmtime;                // 是否需要检查服务端文件的时间，true-需要，false-不需要，缺省为false。
    int  timeout;                         // 进程心跳超时的时间。
    char pname[51];                  // 进程名，建议用"ftpgetfiles_后缀"的方式。
} starg;

struct st_fileinfo              // 文件信息的结构体。
{
    string filename;           // 文件名。
    string mtime;              // 文件时间。
    st_fileinfo()=default;
    st_fileinfo(const string &in_filename,const string &in_mtime):filename(in_filename),mtime(in_mtime) {}
    void clear() { filename.clear(); mtime.clear(); }
}; 

clogfile logfile;   //日志文件
cftpclient ftp;     //ftp客户端对象
cpactive pactive;  // 进程心跳的对象
unordered_map<string,string> ok_files;   //已下载的文件，从okfile文件中加载
list<st_fileinfo> filelist; //下载之前列出服务端文件名的容器，从nlist文件中加载。
list<st_fileinfo> unchanged_files;   //本次未修改不需要下载的文件
list<st_fileinfo> download_files;    //本次要下载的文件

// 程序退出和信号2、15的处理函数。
void EXIT(int sig);
//输出帮助文档
void _help();
//解析xml参数
bool _xmltoarg(char* xmlbuffer);
//加载nlist文件获取文件名
bool loadlistfile();
// 加载okfile中的数据到容器ok_files中。
void loadokfile();
// 比较filelist和ok_files，得到unchanged_files和download_files.
void compmap();
// 把容器unchanged_files中的数据写入okfile，覆盖之前的旧okfile.
bool writetookfile();
// 把下载成功的文件记录追加到okfile中。
bool appendtookfile(st_fileinfo &stfileinfo);

int main(int argc,char *argv[])
{
    // 程序的帮助。
    if (argc != 3)
    {
        _help();
        return -1;
	}

    char *logfilename = argv[1],*xmlbuffer = argv[2];

    // 忽略全部的信号和关闭I/O，设置信号处理函数。
    closeioandsignal(true);
    signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    //打开日志文件
    if(logfile.open(logfilename) == false)
    {
        printf("打开日志文件(%s)失败。\n",logfilename);
        return -1;
    }

    //解析xml获取参数
    if(_xmltoarg(xmlbuffer) == false)
    {
        logfile.write("参数解析错误。\n");
        return -1;
    }

    pactive.addpinfo(starg.timeout,starg.pname);  // 把进程的心跳信息写入共享内存

    //登录ftp服务器
    if(ftp.login(starg.host,starg.username,starg.password,starg.mode) == false)
    {
        logfile.write("ftp.login(%s,%s,%s) failed.\n%s\n",starg.host,starg.username,starg.password,ftp.response());
        return -1;
    }

    //进入ftp服务器存放文件的目录
    if(ftp.chdir(starg.remotepath) == false)
    {
        logfile.write("ftp.chdir(%s) failed.\n%s\n",starg.remotepath,ftp.response());
        return -1;
    }

    //获取文件名，保存在本地nlist文件中
    if(ftp.nlist(".",sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid())) == false)
    {
        logfile.write("ftp.nlist(%s) failed.\n%s\n",starg.remotepath,ftp.response());
        return -1;
    }

    pactive.uptatime();   // 更新进程的心跳

    //加载nlist文件并将文件名保存到容器filelist中
    if (loadlistfile() == false)
    {
        logfile.write("loadlistfile() failed.\n");
        return -1;
    }

    if (starg.ptype==1)
    {
        loadokfile();
        compmap();
        if(writetookfile() == false)
            return -1;
    }
    else
        filelist.swap(download_files);   // 为了统一文件下载的代码，把二者交换。

    pactive.uptatime();   // 更新进程的心跳

    string strremotefilename,strlocalfilename;
    //遍历filelist下载文件
    for(auto &fileinfo:download_files)
    {
        sformat(strremotefilename,"%s/%s",starg.remotepath,fileinfo.filename.c_str());         // 拼接服务端全路径的文件名。
        sformat(strlocalfilename,"%s/%s",starg.localpath,fileinfo.filename.c_str());                 // 拼接本地全路径的文件名。

        logfile.write("get %s ...",strremotefilename.c_str());
        // 调用ftpclient.get()方法下载文件。
        if (ftp.get(strremotefilename,strlocalfilename,starg.checkmtime)==false) 
        {
            logfile << "failed.\n" << ftp.response() << "\n";
            return -1;
        }

        logfile << "ok.\n"; 

        pactive.uptatime();   // 更新进程的心跳

        // ptype==1，增量下载
        if(starg.ptype == 1)
        {
            if(appendtookfile(fileinfo) == false)
                return -1;
        }
        // ptype==2，删除服务端的文件。
        else if (starg.ptype == 2)
        {
            if (ftp.ftpdelete(strremotefilename)==false)
            {
                logfile.write("ftp.ftpdelete(%s) failed.\n%s\n",strremotefilename.c_str(),ftp.response());
                return -1;
            }
        }
        // ptype==3，把服务端的文件移动到备份目录。
        else if (starg.ptype == 3)
        {
            string strremotefilenamebak=sformat("%s/%s",starg.remotepathbak,fileinfo.filename.c_str());  // 生成全路径的备份文件名。
            if (ftp.ftprename(strremotefilename,strremotefilenamebak)==false)
            {
                logfile.write("ftp.ftprename(%s,%s) failed.\n%s\n",strremotefilename.c_str(),strremotefilenamebak.c_str(),ftp.response());
                return -1;
            }
        }
    }

    return 0;
}

void EXIT(int sig)
{
    printf("程序退出，sig=%d\n\n",sig);

    exit(0);
}

void _help()
{
    printf("\n");
    printf("Using:/project/tools/bin/ftpgetfiles logfilename xmlbuffer\n\n");

    printf("Sample:/project/tools/bin/procctl 30 /project/tools/bin/ftpgetfiles /log/idc/ftpgetfiles_surfdata.log " \
             "\"<host>192.168.182.124:21</host><mode>1</mode>"\
             "<username>mysql</username><password>zqMYSQL20030313</password>"\
             "<remotepath>/tmp/idc/surfdata</remotepath><localpath>/idcdata/surfdata</localpath>"\
             "<matchname>SURF_ZH*.XML,SURF_ZH*.CSV</matchname>"\
             "<ptype>3</ptype><remotepathbak>/tmp/idc/surfdatabak</remotepathbak>"\
             "<timeout>30</timeout><pname>ftpgetfiles_test</pname>\"\n\n");
    printf("Sample:/project/tools/bin/procctl 30 /project/tools/bin/ftpgetfiles /log/idc/ftpgetfiles_test.log " \
              "\"<host>192.168.182.124:21</host><mode>1</mode>"\
              "<username>mysql</username><password>zqMYSQL20030313</password>"\
              "<remotepath>/tmp/ftp/server</remotepath><localpath>/tmp/ftp/client</localpath>"\
              "<matchname>*.TXT</matchname>"\
              "<ptype>1</ptype><okfilename>/idcdata/ftplist/ftpgetfiles_test.xml</okfilename>"\
              "<checkmtime>true</checkmtime>"\
              "<timeout>30</timeout><pname>ftpgetfiles_test</pname>\"\n\n\n");

    printf("本程序是通用的功能模块，用于把远程ftp服务端的文件下载到本地目录。\n");
    printf("logfilename是本程序运行的日志文件。\n");
    printf("xmlbuffer为文件下载的参数，如下：\n");
    printf("    <host>192.168.182.124:21</host> 远程服务端的IP和端口。\n");
    printf("    <mode>1</mode> 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。\n");
    printf("    <username>mysql</username> 远程服务端ftp的用户名。\n");
    printf("    <password>zqMYSQL20030313</password> 远程服务端ftp的密码。\n");
    printf("    <remotepath>/tmp/idc/surfdata</remotepath> 远程服务端存放文件的目录。\n");
    printf("    <localpath>/idcdata/surfdata</localpath> 本地文件存放的目录。\n");
    printf("    <matchname>SURF_ZH*.XML,SURF_ZH*.CSV</matchname> 待下载文件匹配的规则。"\
                  "不匹配的文件不会被下载，本字段尽可能设置精确，不建议用*匹配全部的文件。\n");
    printf("    <ptype>1</ptype> 文件下载成功后，远程服务端文件的处理方式："\
                  "1-什么也不做；2-删除；3-删除并备份，如果为3，还要指定备份的目录。\n");
    printf("    <remotepathbak>/tmp/idc/surfdatabak</remotepathbak> 文件下载成功后，服务端文件的备份目录，"\
                  "此参数只有当ptype=3时才有效。\n");
    printf("    <okfilename>/idcdata/ftplist/ftpgetfiles_test.xml</okfilename> 已下载成功文件名清单，"\
                  "此参数只有当ptype=1时才有效。\n");
    printf("    <checkmtime>true</checkmtime> 是否需要检查服务端文件的时间，true-需要，false-不需要，"\
                  "此参数只有当ptype=1时才有效，缺省为false。\n");
    printf("    <timeout>30</timeout> 下载文件超时时间，单位：秒，视文件大小和网络带宽而定。\n");
    printf("    <pname>ftpgetfiles_test</pname> 进程名(可选)。\n\n\n");
}

bool _xmltoarg(char* xmlbuffer)
{
    memset(&starg,0,sizeof(struct st_arg));

    getxmlbuffer(xmlbuffer,"host",starg.host,30);   // 远程服务端的IP和端口。
    if (strlen(starg.host)==0)
    { logfile.write("host is null.\n");  return false; }

    getxmlbuffer(xmlbuffer,"mode",starg.mode);   // 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。
    if (starg.mode!=2)  starg.mode=1;

    getxmlbuffer(xmlbuffer,"username",starg.username,30);   // 远程服务端ftp的用户名。
    if (strlen(starg.username)==0)
    { logfile.write("username is null.\n");  return false; }

    getxmlbuffer(xmlbuffer,"password",starg.password,30);   // 远程服务端ftp的密码。
    if (strlen(starg.password)==0)
    { logfile.write("password is null.\n");  return false; }

    getxmlbuffer(xmlbuffer,"remotepath",starg.remotepath,255);   // 远程服务端存放文件的目录。
    if (strlen(starg.remotepath)==0)
    { logfile.write("remotepath is null.\n");  return false; }

    getxmlbuffer(xmlbuffer,"localpath",starg.localpath,255);   // 本地文件存放的目录。
    if (strlen(starg.localpath)==0)
    { logfile.write("localpath is null.\n");  return false; }

    getxmlbuffer(xmlbuffer,"matchname",starg.matchname,100);   // 待下载文件匹配的规则。
    if (strlen(starg.matchname)==0)
    { logfile.write("matchname is null.\n");  return false; }  

    // 下载后服务端文件的处理方式：1-什么也不做；2-删除；3-备份。
    getxmlbuffer(xmlbuffer,"ptype",starg.ptype);   
    if ( (starg.ptype!=1) && (starg.ptype!=2) && (starg.ptype!=3) )
    { logfile.write("ptype is error.\n"); return false; }

    // 下载后服务端文件的备份目录。
    if (starg.ptype==3) 
    {
        getxmlbuffer(xmlbuffer,"remotepathbak",starg.remotepathbak,255); 
        if (strlen(starg.remotepathbak)==0) { logfile.write("remotepathbak is null.\n");  return false; }
    }

    //增量下载文件。
    if (starg.ptype==1) 
    {
        getxmlbuffer(xmlbuffer,"okfilename",starg.okfilename,255); // 已下载成功文件名清单。
        if ( strlen(starg.okfilename)==0 ) { logfile.write("okfilename is null.\n");  return false; }

        // 是否需要检查服务端文件的时间，true-需要，false-不需要，此参数只有当ptype=1时才有效，缺省为false。
        getxmlbuffer(xmlbuffer,"checkmtime",starg.checkmtime);
    }

    getxmlbuffer(xmlbuffer,"timeout",starg.timeout);   // 进程心跳的超时时间。
    if (starg.timeout==0) { logfile.write("timeout is null.\n");  return false; }

    getxmlbuffer(xmlbuffer,"pname",starg.pname,50);     // 进程名(可选)。

    return true;
}

bool loadlistfile()
{
    filelist.clear();
    cifile ifile;
    if (ifile.open(sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid()))==false)
    {
      logfile.write("ifile.open(%s) 失败。\n",sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid()));
      return false;
    }

    string strfilename;

    while (true)
    {
        if (ifile.readline(strfilename)==false) break;

        if (matchstr(strfilename,starg.matchname)==false) continue;

        if ( (starg.ptype==1) && (starg.checkmtime==true) )
        {
            // 获取ftp服务端文件时间。
            if (ftp.mtime(strfilename)==false)
            {
                logfile.write("ftp.mtime(%s) failed.\n",strfilename.c_str()); return false;
            }
        }

        filelist.emplace_back(strfilename,ftp.m_mtime);
    }

    ifile.closeandremove();

    //调试用
    // for (auto &fileinfo:filelist)
    //    logfile.write("filename=%s,mtime=%s\n",fileinfo.filename.c_str(),fileinfo.mtime.c_str());

    return true;
}

void loadokfile()
{
    ok_files.clear();

    cifile ifile;

    // 注意：如果程序是第一次运行，starg.okfilename是不存在的，直接return就行。
    if ( (ifile.open(starg.okfilename))==false )  return;

    string strbuffer;

    struct st_fileinfo stfileinfo;

    while (true)
    {
        stfileinfo.clear();

        if (ifile.readline(strbuffer)==false) break;

        getxmlbuffer(strbuffer,"filename",stfileinfo.filename);
        getxmlbuffer(strbuffer,"mtime",stfileinfo.mtime);

        ok_files[stfileinfo.filename]=stfileinfo.mtime;
    }
}

void compmap()
{
    unchanged_files.clear(); 
    download_files.clear();

    for (auto &fileinfo:filelist)
    {
        auto it=ok_files.find(fileinfo.filename);           // 在容器一中用文件名查找。
        if (it != ok_files.end())
        {   // 如果找到了，再判断文件时间。
            if (starg.checkmtime==true)
			{
				// 如果时间也相同，不需要下载，否则需要重新下载。
				if (it->second==fileinfo.mtime) unchanged_files.emplace_back(fileinfo);    // 文件时间没有变化，不需要下载。
				else download_files.emplace_back(fileinfo);     // 需要重新下载。
			}
			else
			{
				unchanged_files.emplace_back(fileinfo);   // 不需要重新下载。
			}
        }
        else
        {   // 如果没有找到，把记录放入vdownload容器。
            download_files.emplace_back(fileinfo);
        }
    }
}

bool writetookfile()
{
    cofile ofile;    

    if (ofile.open(starg.okfilename)==false)
    {
      logfile.write("file.open(%s) failed.\n",starg.okfilename);
      return false;
    }

    for (auto &fileinfo:unchanged_files)
        ofile.writeline("<filename>%s</filename><mtime>%s</mtime>\n",fileinfo.filename.c_str(),fileinfo.mtime.c_str());

    ofile.closeandrename();
    return true;
}

bool appendtookfile(st_fileinfo &stfileinfo)
{
    cofile ofile;

    // 以追加的方式打开文件
    if (ofile.open(starg.okfilename,false,ios::app)==false)
    {
      logfile.write("file.open(%s) failed.\n",starg.okfilename);
      return false;
    }

    ofile.writeline("<filename>%s</filename><mtime>%s</mtime>\n",stfileinfo.filename.c_str(),stfileinfo.mtime.c_str());
    return true;
}