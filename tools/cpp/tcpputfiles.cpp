//TCP文件上传程序：自定义TCP文件上传。
#include "_public.h"
using namespace idc;

// 程序运行的参数结构体。
struct st_arg
{
    int    clienttype;                // 客户端类型，1-上传文件；2-下载文件，本程序固定填1。
    char ip[31];                       // 服务端的IP地址。
    int    port;                        // 服务端的端口。
    char clientpath[256];       // 本地文件存放的根目录。 /data /data/aaa /data/bbb
    int    ptype;                      // 文件上传成功后本地文件的处理方式：1-删除文件；2-移动到备份目录。
    char clientpathbak[256]; // 文件成功上传后，本地文件备份的根目录，当ptype==2时有效。
    bool andchild;                 // 是否上传clientpath目录下各级子目录的文件，true-是；false-否。
    char matchname[256];    // 待上传文件名的匹配规则，如"*.TXT,*.XML"。
    char srvpath[256];           // 服务端文件存放的根目录。/data1 /data1/aaa /data1/bbb
    int    timetvl;                    // 扫描本地目录文件的时间间隔（执行文件上传任务的时间间隔），单位：秒。 
    int    timeout;                  // 进程心跳的超时时间。
    char pname[51];             // 进程名，建议用"tcpputfiles_后缀"的方式。
} starg;

clogfile logfile;            // 日志对象
ctcpclient tcpclient;     // 创建tcp通讯的客户端对象
cpactive pactive;        // 进程心跳
string strsendbuffer;   // 发送报文的buffer
string strrecvbuffer;    // 接收报文的buffer

// 帮助文档。
void _help();
// 把xml解析到参数starg结构中。
bool _xmltoarg(const char *strxmlbuffer);
// 程序退出和信号2、15的处理函数。
void EXIT(int sig);
// 心跳。
bool activetest();
// 向服务端发送登录报文，把客户端程序的参数传递给服务端。
bool login(const char *argv);
// 上传文件主函数
bool _tcpputfiles(bool &bcontinue);
// 把文件的内容发送给对端。
bool sendfile(const string &filename,const int filesize);
// 处理传输文件的响应报文（删除或者转存本地的文件）。
bool ackmessage(const string &strrecvbuffer);

int main(int argc,char *argv[])
{
    if (argc!=3) { _help(); return -1; }

    char* logfilename = argv[1], *xmlbuffer = argv[2];

    // 关闭全部的信号和输入输出。
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程。
    // 但请不要用 "kill -9 +进程号" 强行终止。
    // 在网络通讯程序中，一般不关IO，因为某些函数可能会往1和2中输出信息
    // 如果关了1和2，那么1和2会被socket重用，向1和2输出的信息会发送到网络中。
    closeioandsignal(false); 
    signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    // 打开日志文件。
    if (logfile.open(logfilename)==false)
    {
        printf("打开日志文件失败（%s）。\n",argv[1]);
        return -1;
    }

    // 解析xml，得到程序运行的参数。
    if (_xmltoarg(xmlbuffer)==false) return -1;
    
    pactive.addpinfo(starg.timeout,starg.pname);  // 把进程的心跳信息写入共享内存

    // 向服务端发起连接请求。
    if (tcpclient.connect(starg.ip,starg.port)==false)
    {
        logfile.write("tcpclient.connect(%s,%d) failed.\n",starg.ip,starg.port);
        EXIT(-1);
    }

    // 向服务端发送登录报文，把客户端程序的参数传递给服务端。
    if (login(xmlbuffer)==false)
    {
        logfile.write("login() failed.\n");
        EXIT(-1);
    }

    bool bcontinue=false;   // 如果调用_tcpputfiles()发送了文件，bcontinue为true，否则为false。

    while (true)
    {
        // 上传文件
        if (_tcpputfiles(bcontinue) == false)
        {
            logfile.write("_tcpputfiles() failed.\n");
            EXIT(-1);
        }

        // 如果刚才执行文件上传任务的时候上传了文件，在上传的过程中，可能有新的文件陆续已生成，
        // 那么，为保证文件被尽快上传，进程不体眠。（只有在刚才执行文件上传任务的时候没有上传文件的情况下才休眠）
        if (bcontinue==false)
        {
            sleep(starg.timetvl);

            // 发送心跳报文。
            if (activetest()==false) break;
        }

        pactive.uptatime();
    }
   
    EXIT(0);
}

bool activetest()
{
    strsendbuffer="<activetest>ok</activetest>";
    //  xxxxxxxxxx  logfile.write("发送：%s\n",strsendbuffer.c_str());
    if (tcpclient.write(strsendbuffer)==false) return false; // 向服务端发送请求报文。

    if (tcpclient.read(strrecvbuffer,10)==false) return false; // 接收服务端的回应报文。
    //  xxxxxxxxxx  logfile.write("接收：%s\n",strrecvbuffer.c_str());

    return true;
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d\n\n",sig);

    exit(0);
}

void _help()
{
    printf("\n");
    printf("Using:/project/tools/bin/tcpputfiles logfilename xmlbuffer\n\n");

    printf("Sample:/project/tools/bin/procctl 20 /project/tools/bin/tcpputfiles /log/idc/tcpputfiles_surfdata.log "\
              "\"<ip>192.168.182.124</ip><port>5005</port>"\
              "<clientpath>/tmp/client</clientpath><ptype>1</ptype>"
              "<srvpath>/tmp/server</srvpath>"\
              "<andchild>true</andchild><matchname>*.xml,*.txt</matchname><timetvl>10</timetvl>"\
              "<timeout>50</timeout><pname>tcpputfiles_surfdata</pname>\"\n\n");

    printf("本程序是数据中心的公共功能模块，采用tcp协议把文件上传给服务端。\n");
    printf("logfilename   本程序运行的日志文件。\n");
    printf("xmlbuffer     本程序运行的参数，如下：\n");
    printf("ip            服务端的IP地址。\n");
    printf("port          服务端的端口。\n");
    printf("ptype         文件上传成功后的处理方式：1-删除文件；2-移动到备份目录。\n");
    printf("clientpath    本地文件存放的根目录。\n");
    printf("clientpathbak 文件成功上传后，本地文件备份的根目录，当ptype==2时有效。\n");
    printf("andchild      是否上传clientpath目录下各级子目录的文件，true-是；false-否，缺省为false。\n");
    printf("matchname     待上传文件名的匹配规则，如\"*.TXT,*.XML\"\n");
    printf("srvpath       服务端文件存放的根目录。\n");
    printf("timetvl       扫描本地目录文件的时间间隔，单位：秒，取值在1-30之间。\n");
    printf("timeout       本程序的超时时间，单位：秒，视文件大小和网络带宽而定，建议设置50以上。\n");
    printf("pname         进程名，尽可能采用易懂的、与其它进程不同的名称，方便故障排查。\n\n");
}

bool _xmltoarg(const char *strxmlbuffer)
{
    memset(&starg,0,sizeof(struct st_arg));

    getxmlbuffer(strxmlbuffer,"ip",starg.ip);
    if (strlen(starg.ip)==0) { logfile.write("ip is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"port",starg.port);
    if ( starg.port==0) { logfile.write("port is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"ptype",starg.ptype);
    if ((starg.ptype!=1)&&(starg.ptype!=2)) { logfile.write("ptype not in (1,2).\n"); return false; }

    getxmlbuffer(strxmlbuffer,"clientpath",starg.clientpath);
    if (strlen(starg.clientpath)==0) { logfile.write("clientpath is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"clientpathbak",starg.clientpathbak);
    if ((starg.ptype==2)&&(strlen(starg.clientpathbak)==0)) { logfile.write("clientpathbak is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"andchild",starg.andchild);

    getxmlbuffer(strxmlbuffer,"matchname",starg.matchname);
    if (strlen(starg.matchname)==0) { logfile.write("matchname is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"srvpath",starg.srvpath);
    if (strlen(starg.srvpath)==0) { logfile.write("srvpath is null.\n"); return false; }

    getxmlbuffer(strxmlbuffer,"timetvl",starg.timetvl);
    if (starg.timetvl==0) { logfile.write("timetvl is null.\n"); return false; }

    // 每次上传的时间间隔，单位：秒。
    // starg.timetvl没有必要超过30秒。
    if (starg.timetvl>30) starg.timetvl=30;

    // 进程心跳的超时时间，一定要大于starg.timetvl。
    getxmlbuffer(strxmlbuffer,"timeout",starg.timeout);
    if (starg.timeout==0) { logfile.write("timeout is null.\n"); return false; }
    if (starg.timeout<=starg.timetvl)  { logfile.write("starg.timeout(%d) <= starg.timetvl(%d).\n",starg.timeout,starg.timetvl); return false; }

    getxmlbuffer(strxmlbuffer,"pname",starg.pname,50);

    return true;
}

bool login(const char *argv)
{
    sformat(strsendbuffer,"%s<clienttype>1</clienttype>",argv);
    //  xxxxxxxxxx  logfile.write("发送：%s\n",strsendbuffer.c_str());
    if (tcpclient.write(strsendbuffer)==false) return false; // 向服务端发送请求报文。

    if (tcpclient.read(strrecvbuffer,10)==false) return false; // 接收服务端的回应报文。
    //  xxxxxxxxxx  logfile.write("接收：%s\n",strrecvbuffer.c_str());

    logfile.write("登录(%s:%d)成功。\n",starg.ip,starg.port); 

    return true;
}

bool _tcpputfiles(bool &bcontinue)
{
    bcontinue = false;

    cdir dir;
    // 打开starg.clientpath目录。
    if (dir.opendir(starg.clientpath,starg.matchname,10000,starg.andchild)==false)
    {
        logfile.write("dir.opendir(%s) 失败。\n",starg.clientpath);
        return false;
    }

    int delayed=0;        // 发送但未收到确认的文件数量，发送了一个文件就加1，接收到了一个回应就减1。

    // 遍历目录中的每个文件
    while (dir.readdir())
    {
        bcontinue = true;
        // 把文件名、修改时间、文件大小，发送给对端。
        sformat(strsendbuffer,"<filename>%s</filename><mtime>%s</mtime><size>%d</size>",
                        dir.m_ffilename.c_str(),dir.m_mtime.c_str(),dir.m_filesize);

        // xxxxxxxxxx logfile.write("strsendbuffer=%s\n",strsendbuffer.c_str());
        if (tcpclient.write(strsendbuffer)==false)
        {
            logfile.write("tcpclient.write() failed.\n");
            return false;
        }

        // 发送文件内容。
        logfile.write("send %s(%d) ...",dir.m_ffilename.c_str(),dir.m_filesize);
        if (sendfile(dir.m_ffilename,dir.m_filesize)==true)
        {
            logfile << "ok.\n"; 
            ++delayed;
        }
        else
        {
            logfile << "failed.\n";
            tcpclient.close();
            return false;
        }

        pactive.uptatime();

        // 接收服务端的确认报文。
        while(delayed > 0)
        {
            //通过I/O复用实现异步通信
            if (tcpclient.read(strrecvbuffer,-1)==false) break;
            // xxxxxxxxxx logfile.write("strrecvbuffer=%s\n",strrecvbuffer.c_str());

            // 处理服务端的确认报文（删除本地文件或把本地文件移动到备份目录）。
            --delayed;
            ackmessage(strrecvbuffer);
        }
    }

    // 继续接收对端的确认报文。
    while (delayed>0)
    {
        if (tcpclient.read(strrecvbuffer,10)==false) break;
        // xxxxxxxxxx logfile.write("strrecvbuffer=%s\n",strrecvbuffer.c_str());

        // 处理传输文件的响应报文（删除或者转存本地的文件）。
        --delayed;
        ackmessage(strrecvbuffer);
    }

    return true;
}

bool sendfile(const string &filename,const int filesize)
{
    int  onread=0;         // 每次打算从文件中读取的字节数。 
    char buffer[1000];     // 存放读取数据的buffer，buffer的大小可参考硬盘一次读取数据量（4K为宜）。
    int  totalbytes=0;     // 从文件中已读取的字节总数。
    cifile ifile;          // 读取文件的对象。

    // 必须以二进制的方式操作文件。
    if (ifile.open(filename,ios::in|ios::binary)==false) return false;

    while (true)
    {
        memset(buffer,0,sizeof(buffer));

        // 计算本次应该读取的字节数，如果剩余的数据超过1000字节，就读1000字节。
        onread = filesize-totalbytes>1000 ? 1000 : filesize-totalbytes;

        // 从文件中读取数据。
        ifile.read(buffer,onread);   

        // 把读取到的数据发送给对端。
        if (tcpclient.write(buffer,onread)==false)  { return false; }

        // 计算文件已读取的字节总数，如果文件已读完，跳出循环。
        totalbytes=totalbytes+onread;

        if (totalbytes==filesize) break;
    }

    return true;
}

bool ackmessage(const string &strrecvbuffer)
{
    string filename;   // 本地文件名。
    string result;        // 对端接收文件的结果。
    getxmlbuffer(strrecvbuffer,"filename",filename);
    getxmlbuffer(strrecvbuffer,"result",result);

    // 如果服务端接收文件不成功，直接返回
    if (result!="ok") return true;

    // 如果starg.ptype==1，删除文件。
    if (starg.ptype==1)
    {
        if (remove(filename.c_str())!=0)
        {
            logfile.write("remove(%s) failed.\n",filename.c_str());
            return false;
        }
    }

    // 如果starg.ptype==2，移动到备份目录。  
    if (starg.ptype==2)
    {
        // 生成转存后的备份目录文件名。  例如：/tmp/client/2.xml   /tmp/clientbak/2.xml
        string bakfilename=filename;
        replacestr(bakfilename,starg.clientpath,starg.clientpathbak,false);   // 注意，第三个参数一定要填false。
        if (renamefile(filename,bakfilename)==false) 
        {
            logfile.write("renamefile(%s,%s) failed.\n",filename.c_str(),bakfilename.c_str());
            return false;
        }
    }

    return true;
}