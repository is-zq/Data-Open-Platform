#include "_public.h"
#include "_ftp.h"
using namespace idc;

// 程序运行参数的结构体。
struct st_arg
{
    char host[31];                    // 远程服务端的IP和端口。
    int  mode;                         // 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。
    char username[31];           // 远程服务端ftp的用户名。
    char password[31];           // 远程服务端ftp的密码。
    char remotepath[256];     // 远程服务端存放文件的目录。
    char localpath[256];         // 本地文件存放的目录。
    char matchname[101];     // 待上传文件匹配的规则。
    int  ptype;                        // 上传后客户端文件的处理方式：1-什么也不做；2-删除；3-备份。
    char localpathbak[256];   // 上传后客户端文件的备份目录。
    char okfilename[256];      // 已上传成功文件名清单。
    int  timeout;                     // 进程心跳的超时时间。
    char pname[51];               // 进程名，建议用"ftpputfiles_后缀"的方式。
} starg;

// 文件信息的结构体。
struct st_fileinfo
{
    string filename;
    string mtime;
    st_fileinfo()=default;
    st_fileinfo(const string &in_filename,const string &in_mtime):filename(in_filename),mtime(in_mtime) {}
    void clear() { filename.clear(); mtime.clear(); }
};

clogfile logfile;   //日志文件
cftpclient ftp;     //ftp客户端对象
cpactive pactive;  // 进程心跳的对象
unordered_map<string,string> ok_files;   //已上传的文件，从okfile文件中加载
list<st_fileinfo> filelist;         //待上传目录中的文件
list<st_fileinfo> unchanged_files;   //本次未修改不需要上传的文件
list<st_fileinfo> upload_files;    //本次要上传的文件

// 程序退出和信号2、15的处理函数。
void EXIT(int sig);
//输出帮助文档
void _help();
//解析xml参数
bool _xmltoarg(char* xmlbuffer);
//加载nlist文件获取文件名
bool loadlocalfile();
// 加载okfile中的数据到容器ok_files中。
void loadokfile();
// 比较filelist和ok_files，得到unchanged_files和upload_files.
void compmap();
// 把容器unchanged_files中的数据写入okfile，覆盖之前的旧okfile.
bool writetookfile();
// 把上传成功的文件记录追加到okfile中。
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

    // 把localpath目录下的文件列表加载到容器中
    if (loadlocalfile()==false)
    {
        logfile.write("loadlocalfile() failed.\n");
        return -1;
    }

    pactive.uptatime();   // 更新进程的心跳

    if (starg.ptype==1)
    {
        loadokfile();
        compmap();
        if(writetookfile() == false)
            return -1;
    }
    else
        filelist.swap(upload_files);   // 为了统一文件下载的代码，把二者交换。

    pactive.uptatime();   // 更新进程的心跳

    string strremotefilename,strlocalfilename;

    for(auto &fileinfo:upload_files)
    {
        sformat(strremotefilename,"%s/%s",starg.remotepath,fileinfo.filename.c_str());         // 拼接服务端全路径的文件名。
        sformat(strlocalfilename,"%s/%s",starg.localpath,fileinfo.filename.c_str());                 // 拼接本地全路径的文件名。

        logfile.write("put %s ...",strlocalfilename.c_str());
        // 把文件上传到服务端，第三个参数填true的目的是确保文件上传成功
        if (ftp.put(strlocalfilename,strremotefilename,true)==false) 
        {
            logfile << "failed.\n" << ftp.response() << "\n";
            return -1;
        }

        logfile << "ok.\n"; 

        pactive.uptatime();   // 更新进程的心跳

        // ptype==1，增量上传
        if(starg.ptype == 1)
        {
            if(appendtookfile(fileinfo) == false)
                return -1;
        }
        // ptype==2，删除文件。
        else if (starg.ptype == 2)
        {
            if (remove(strlocalfilename.c_str()) != 0)
            {
                logfile.write("remove(%s) failed.\n",strlocalfilename.c_str());
                return -1;
            }
        }
        // ptype==3，把文件移动到备份目录。
        else if (starg.ptype == 3)
        {
            string strlocalfilenamebak = sformat("%s/%s",starg.localpathbak,fileinfo.filename.c_str());  // 生成全路径的备份文件名。
            if (renamefile(strlocalfilename,strlocalfilenamebak) == false)
            {
                logfile.write("renamefile(%s,%s) failed.\n",strlocalfilename.c_str(),strlocalfilenamebak.c_str());
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
    printf("Using:/project/tools/bin/ftpputfiles logfilename xmlbuffer\n\n");

    printf("Sample:/project/tools/bin/procctl 30 /project/tools/bin/ftpputfiles /log/idc/ftpputfiles_surfdata.log "\
              "\"<host>127.0.0.1:21</host><mode>1</mode><username>mysql</username><password>zqMYSQL20030313</password>"\
              "<localpath>/tmp/idc/surfdata</localpath><remotepath>/idcdata/surfdata</remotepath>"\
              "<matchname>SURF_ZH*.JSON</matchname>"\
              "<ptype>1</ptype><localpathbak>/tmp/idc/surfdatabak</localpathbak>"\
              "<okfilename>/idcdata/ftplist/ftpputfiles_surfdata.xml</okfilename>"\
              "<timeout>80</timeout><pname>ftpputfiles_surfdata</pname>\"\n\n\n");

    printf("本程序是通用的功能模块，用于把本地目录中的文件上传到远程的ftp服务器。\n");
    printf("logfilename是本程序运行的日志文件。\n");
    printf("xmlbuffer为文件上传的参数，如下：\n");
    printf("<host>127.0.0.1:21</host> 远程服务端的IP和端口。\n");
    printf("<mode>1</mode> 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。\n");
    printf("<username>mysql</username> 远程服务端ftp的用户名。\n");
    printf("<password>zqMYSQL20030313</password> 远程服务端ftp的密码。\n");
    printf("<remotepath>/tmp/ftpputest</remotepath> 远程服务端存放文件的目录。\n");
    printf("<localpath>/tmp/idc/surfdata</localpath> 本地文件存放的目录。\n");
    printf("<matchname>SURF_ZH*.JSON</matchname> 待上传文件匹配的规则。"\
           "不匹配的文件不会被上传，本字段尽可能设置精确，不建议用*匹配全部的文件。\n");
    printf("<ptype>1</ptype> 文件上传成功后，本地文件的处理方式：1-什么也不做；2-删除；3-备份，如果为3，还要指定备份的目录。\n");
    printf("<localpathbak>/tmp/idc/surfdatabak</localpathbak> 文件上传成功后，本地文件的备份目录，此参数只有当ptype=3时才有效。\n");
    printf("<okfilename>/idcdata/ftplist/ftpputfiles_surfdata.xml</okfilename> 已上传成功文件名清单，此参数只有当ptype=1时才有效。\n");
    printf("<timeout>80</timeout> 上传文件超时时间，单位：秒，视文件大小和网络带宽而定。\n");
    printf("<pname>ftpputfiles_surfdata</pname> 进程名，尽可能采用易懂的、与其它进程不同的名称，方便故障排查。\n\n\n");
}

// 把xml解析到参数starg结构中。
bool _xmltoarg(char *strxmlbuffer)
{
    memset(&starg,0,sizeof(struct st_arg));

    getxmlbuffer(strxmlbuffer,"host",starg.host,30);   // 远程服务端的IP和端口。
    if (strlen(starg.host)==0)
    { logfile.write("host is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"mode",starg.mode);   // 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。
    if (starg.mode!=2)  starg.mode=1;

    getxmlbuffer(strxmlbuffer,"username",starg.username,30);   // 远程服务端ftp的用户名。
    if (strlen(starg.username)==0)
    { logfile.write("username is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"password",starg.password,30);   // 远程服务端ftp的密码。
    if (strlen(starg.password)==0)
    { logfile.write("password is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"remotepath",starg.remotepath,255);   // 远程服务端存放文件的目录。
    if (strlen(starg.remotepath)==0)
    { logfile.write("remotepath is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"localpath",starg.localpath,255);   // 本地文件存放的目录。
    if (strlen(starg.localpath)==0)
    { logfile.write("localpath is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"matchname",starg.matchname,100);   // 待上传文件匹配的规则。
    if (strlen(starg.matchname)==0)
    { logfile.write("matchname is null.\n");  return false; }

    // 上传后客户端文件的处理方式：1-什么也不做；2-删除；3-备份。
    getxmlbuffer(strxmlbuffer,"ptype",starg.ptype);   
    if ( (starg.ptype!=1) && (starg.ptype!=2) && (starg.ptype!=3) )
    { logfile.write("ptype is error.\n"); return false; }

    if (starg.ptype==3)
    {
        getxmlbuffer(strxmlbuffer,"localpathbak",starg.localpathbak,255); // 上传后客户端文件的备份目录。
        if (strlen(starg.localpathbak)==0) { logfile.write("localpathbak is null.\n");  return false; }
    }

    if (starg.ptype==1)
    {
        getxmlbuffer(strxmlbuffer,"okfilename",starg.okfilename,255); // 已上传成功文件名清单。
        if (strlen(starg.okfilename)==0) { logfile.write("okfilename is null.\n");  return false; }
    }

    getxmlbuffer(strxmlbuffer,"timeout",starg.timeout);   // 进程心跳的超时时间。
    if (starg.timeout==0) { logfile.write("timeout is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"pname",starg.pname,50);     // 进程名(可选)。

    return true;
}

// 把starg.localpath目录下的文件列表加载到filelist容器中。
bool loadlocalfile()
{
    filelist.clear();

    cdir dir;

    // 不包括子目录。
    if (dir.opendir(starg.localpath,starg.matchname)==false)
    {
      logfile.write("dir.opendir(%s) 失败。\n",starg.localpath); return false;
    }

    while (true)
    {
        if (dir.readdir()==false) break;

        filelist.emplace_back(dir.m_filename,dir.m_mtime);
    }

    return true;
}

// 加载starg.okfilename文件中的内容到容器ok_files中。
void loadokfile()
{
    ok_files.clear();

    cifile ifile;

    // 注意：如果程序是第一次上传，okfile不存在，直接返回。
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
    upload_files.clear();

    for (auto &fileinfo:filelist)
    {
        auto it = ok_files.find(fileinfo.filename);
        if (it != ok_files.end())
        {
            // 找到了，如果时间也相同，不需要上传，否则需要重新上传。
            if (it->second==fileinfo.mtime)
                unchanged_files.emplace_back(fileinfo); 
            else
                upload_files.emplace_back(fileinfo);
        }
        else
        {  // 如果没有找到，把记录放入upload容器。
            upload_files.emplace_back(fileinfo);
        }
    }
}

// 把容器unchanged_files中的内容写入okfile，覆盖之前的旧okfile
bool writetookfile()
{
    cofile ofile;

    if (ofile.open(starg.okfilename)==false)
    {
      logfile.write("file.open(%s) failed.\n",starg.okfilename); return false;
    }

    for (auto &fileinfo:unchanged_files)
        ofile.writeline("<filename>%s</filename><mtime>%s</mtime>\n",fileinfo.filename.c_str(),fileinfo.mtime.c_str());

    ofile.closeandrename();

    return true;
}

bool appendtookfile(struct st_fileinfo &stfileinfo)
{
    cofile ofile;

    // 以追加的方式打开文件，注意第二个参数一定要填false。
    if (ofile.open(starg.okfilename,false,ios::app)==false)
    {
      logfile.write("file.open(%s) failed.\n",starg.okfilename); return false;
    }

    ofile.writeline("<filename>%s</filename><mtime>%s</mtime>\n",stfileinfo.filename.c_str(),stfileinfo.mtime.c_str());

    return true;
}