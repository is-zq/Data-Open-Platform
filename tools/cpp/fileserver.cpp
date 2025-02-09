#include "_public.h"
using namespace idc;

// 程序运行的参数结构体。
struct st_arg
{
    int    clienttype;                // 客户端类型，1-上传文件；2-下载文件
    char clientpath[256];       // 本地文件存放的根目录。 /data /data/aaa /data/bbb
    int    ptype;                      // 文件上传成功后本地文件的处理方式：1-删除文件；2-移动到备份目录。
    char srvpathbak[256]; // 文件成功上传后，本地文件备份的根目录，当ptype==2时有效。
    bool andchild;                 // 是否上传clientpath目录下各级子目录的文件，true-是；false-否。
    char matchname[256];    // 待上传文件名的匹配规则，如"*.TXT,*.XML"。
    char srvpath[256];           // 服务端文件存放的根目录。/data1 /data1/aaa /data1/bbb
    int    timetvl;                    // 扫描本地目录文件的时间间隔（执行文件上传任务的时间间隔），单位：秒。 
    int    timeout;                  // 进程心跳的超时时间。
    char pname[51];             // 进程名，建议用"tcpputfiles_后缀"的方式。
} starg;

clogfile logfile;            // 服务程序的运行日志
ctcpserver tcpserver;  // 创建tcp通讯的服务端对象
cpactive pactive;         // 进程心跳
string strsendbuffer;   // 发送报文的buffer
string strrecvbuffer;    // 接收报文的buffer

// 父进程退出函数
void FathEXIT(int sig);
// 子进程退出函数
void ChldEXIT(int sig);
// 处理登录客户端的登录报文。
bool clientlogin();
// 上传文件的处理函数
void putfiles_handler();
// 接收文件的内容
bool recvfile(const string &filename,const string &mtime,int filesize);
// 下载文件的处理函数
void getfiles_handler();
// 发送文件主函数
bool _tcpputfiles(bool &bcontinue);
// 发送文件的内容。
bool sendfile(const string &filename,const int filesize);
// 处理传输文件的响应报文（删除或者转存本地的文件）。
bool ackmessage(const string &strrecvbuffer);
// 心跳
bool activetest();

int main(int argc,char *argv[])
{
    if (argc!=3)
    {
      printf("Using:./fileserver port logfile\n");
      printf("Example:./fileserver 5005 /log/idc/fileserver.log\n"); 
      printf("         /project/tools/bin/procctl 10 /project/tools/bin/fileserver 5005 /log/idc/fileserver.log\n\n\n"); 
      return -1;
    }

    char* arg_port = argv[1], *arg_logfile = argv[2];

    // 关闭全部的信号和输入输出。
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程
    // 但请不要用 "kill -9 +进程号" 强行终止
    closeioandsignal(false); 
    signal(SIGINT,FathEXIT); signal(SIGTERM,FathEXIT);

    if (logfile.open(arg_logfile)==false)
    {
        printf("logfile.open(%s) failed.\n",arg_logfile);
        return -1;
    }

    // 服务端初始化。
    if (tcpserver.initserver(atoi(arg_port))==false)
    {
      logfile.write("tcpserver.initserver(%s) failed.\n",arg_port); return -1;
    }

    while (true)
    {
        // 等待客户端的连接请求。
        if (tcpserver.accept()==false)
        {
            logfile.write("tcpserver.accept() failed.\n"); FathEXIT(-1);
        }

        logfile.write("客户端（%s）已连接。\n",tcpserver.getip());

        if (fork()>0)// 父进程继续回到Accept()。
        {
            tcpserver.closeclient();
            continue;
        }
   
        // 子进程重新设置退出信号。
        signal(SIGINT,ChldEXIT); signal(SIGTERM,ChldEXIT);

        tcpserver.closelisten();

        // 子进程与客户端进行通讯，处理业务。

        // 处理登录客户端的登录报文。
        if (clientlogin()==false) ChldEXIT(-1);

        // 把进程的心跳信息写入共享内存（每个子进程有自己的心跳）。
		pactive.addpinfo(starg.timeout,starg.pname);  

        // 如果starg.clienttype==1，调用上传文件的处理函数。
        if (starg.clienttype==1)  putfiles_handler();  

        // 如果starg.clienttype==2，调用下载文件的处理函数。
        if (starg.clienttype==2)  getfiles_handler();

        ChldEXIT(0);
    }
}

void FathEXIT(int sig)  
{
    // 以下代码是为了防止信号处理函数在执行的过程中被信号中断。
    signal(SIGINT,SIG_IGN); signal(SIGTERM,SIG_IGN);

    logfile.write("父进程退出，sig=%d。\n",sig);

    tcpserver.closelisten();    // 关闭监听的socket。

    kill(0,15);     // 通知全部的子进程退出。

    exit(0);
}

void ChldEXIT(int sig)  
{
    // 以下代码是为了防止信号处理函数在执行的过程中被信号中断。
    signal(SIGINT,SIG_IGN); signal(SIGTERM,SIG_IGN);

    logfile.write("子进程退出，sig=%d。\n",sig);

    tcpserver.closeclient();    // 关闭客户端的socket。

    exit(0);
}

bool clientlogin()
{
    // 接收客户端的登录报文。
    if (tcpserver.read(strrecvbuffer,10)==false)
    {
        logfile.write("tcpserver.read() failed.\n"); return false;
    }
    // xxxxxxxx logfile.write("strrecvbuffer=%s\n",strrecvbuffer.c_str());

    // 解析客户端登录报文，不需要对参数做合法性判断，客户端已经判断过了。
    memset(&starg,0,sizeof(struct st_arg));
    getxmlbuffer(strrecvbuffer,"clienttype",starg.clienttype);
    getxmlbuffer(strrecvbuffer,"clientpath",starg.clientpath);
    getxmlbuffer(strrecvbuffer,"srvpath",starg.srvpath);
    getxmlbuffer(strrecvbuffer,"srvpathbak",starg.srvpathbak);
    getxmlbuffer(strrecvbuffer,"andchild",starg.andchild);
    getxmlbuffer(strrecvbuffer,"ptype",starg.ptype);
    getxmlbuffer(strrecvbuffer,"matchname",starg.matchname);

    getxmlbuffer(strrecvbuffer,"timetvl",starg.timetvl);        // 执行任务的周期。
    getxmlbuffer(strrecvbuffer,"timeout",starg.timeout);     // 进程超时的时间。
    getxmlbuffer(strrecvbuffer,"pname",starg.pname,50);    // 进程名。

    // 防止非法的连接请求。
    if ( (starg.clienttype!=1) && (starg.clienttype!=2) )
        strsendbuffer="failed";
    else
        strsendbuffer="ok";

    if (tcpserver.write(strsendbuffer)==false)
    {
        logfile.write("tcpserver.write() failed.\n"); return false;
    }

    logfile.write("%s login %s.\n",tcpserver.getip(),strsendbuffer.c_str());

    return true;
}

void putfiles_handler()
{
    while (true)
    {
        pactive.uptatime(); //更新进程心跳

        // 接收客户端的报文。
        if (tcpserver.read(strrecvbuffer,starg.timetvl+10)==false)
        {
            logfile.write("tcpserver.read() failed.\n"); return;
        }
        // xxxxxxxx logfile.write("strrecvbuffer=%s\n",strrecvbuffer.c_str());

        // 处理心跳报文。
        if (strrecvbuffer == "<activetest>ok</activetest>")
        {
            strsendbuffer="ok";
            // xxxxxxxx logfile.write("strsendbuffer=%s\n",strsendbuffer.c_str());
            if (tcpserver.write(strsendbuffer)==false)
            {
                logfile.write("tcpserver.write() failed.\n");
                return;
            }
        }

        // 处理上传文件的请求报文。
        if (strrecvbuffer.find("<filename>") != string::npos) 
        {
            // 解析上传文件请求报文的xml。
            string clientfilename;   // 对端的文件名。
            string mtime;               // 文件的时间。
            int  filesize=0;              // 文件大小。
            getxmlbuffer(strrecvbuffer,"filename",clientfilename);
            getxmlbuffer(strrecvbuffer,"mtime",mtime);
            getxmlbuffer(strrecvbuffer,"size",filesize);

            // 接收文件的内容。
            // starg.clientpath=/tmp/client
            // starg.srvpath=/tmp/server
            // 客户端的文件名是：clientfilename=/tmp/client/aaa/1.txt
            // 服务端的文件名是：serverfilename=/tmp/server/aaa/1.txt
            string serverfilename;  // 服务端的文件名
            serverfilename=clientfilename;
            replacestr(serverfilename,starg.clientpath,starg.srvpath,false);

            logfile.write("recv %s(%d) ...",serverfilename.c_str(),filesize);
            if (recvfile(serverfilename,mtime,filesize)==true)
            {
                logfile << "ok.\n";
                sformat(strsendbuffer,"<filename>%s</filename><result>ok</result>",clientfilename.c_str());
            }
            else
            {
                logfile << "failed.\n";
                sformat(strsendbuffer,"<filename>%s</filename><result>failed</result>",clientfilename.c_str());
            }
            
            // 把确认报文返回给对端。
            // xxxxxxxx logfile.write("strsendbuffer=%s\n",strsendbuffer.c_str());
            if (tcpserver.write(strsendbuffer)==false)
            {
                logfile.write("tcpserver.write() failed.\n");
                return;
            }
        }
    }
}

bool recvfile(const string &filename,const string &mtime,int filesize)
{
    int  totalbytes=0;          // 已接收文件的总字节数。
    int  onread=0;              // 本次打算接收的字节数。
    char buffer[1000];        // 接收文件内容的缓冲区。
    cofile ofile;                   // 写入文件的对象。

    // 必须以二进制的方式操作文件。
    if (ofile.open(filename,true,ios::out|ios::binary)==false) return false;

    while (true)
    {
        memset(buffer,0,sizeof(buffer));

        // 计算本次应该接收的字节数。
        onread = filesize-totalbytes>1000 ? 1000 : filesize-totalbytes;

        // 接收文件内容。
        if (tcpserver.read(buffer,onread)==false)  return false; 

        // 把接收到的内容写入文件。
        ofile.write(buffer,onread);

        // 计算已接收文件的总字节数，如果文件接收完，跳出循环。
        totalbytes=totalbytes+onread;

        if (totalbytes==filesize) break;
    }

    ofile.closeandrename();

    // 文件时间用当前时间没有意义，应该与对端的文件时间保持一致。
    setmtime(filename,mtime);
    return true;
}

void getfiles_handler()
{
    bool bcontinue=false;   // 如果调用发送了文件，bcontinue为true，否则为false。

    while (true)
    {
        // 上传文件
        bcontinue = false;

        cdir dir;
        // 打开starg.clientpath目录。
        if (dir.opendir(starg.srvpath,starg.matchname,10000,starg.andchild)==false)
        {
            logfile.write("dir.opendir(%s) 失败。\n",starg.srvpath);
            return;
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
            if (tcpserver.write(strsendbuffer)==false)
            {
                logfile.write("tcpserver.write() failed.\n");
                return;
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
                tcpserver.closeclient();
                return;
            }

            pactive.uptatime();

            // 接收服务端的确认报文。
            while(delayed > 0)
            {
                //通过I/O复用实现异步通信
                if (tcpserver.read(strrecvbuffer,-1)==false) break;
                // xxxxxxxxxx logfile.write("strrecvbuffer=%s\n",strrecvbuffer.c_str());

                // 处理服务端的确认报文（删除本地文件或把本地文件移动到备份目录）。
                --delayed;
                ackmessage(strrecvbuffer);
            }
        }

        // 继续接收对端的确认报文。
        while (delayed>0)
        {
            if (tcpserver.read(strrecvbuffer,10)==false) break;
            // xxxxxxxxxx logfile.write("strrecvbuffer=%s\n",strrecvbuffer.c_str());

            // 处理传输文件的响应报文（删除或者转存本地的文件）。
            --delayed;
            ackmessage(strrecvbuffer);
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
        if (tcpserver.write(buffer,onread)==false)  { return false; }

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
        replacestr(bakfilename,starg.srvpath,starg.srvpathbak,false);   // 注意，第三个参数一定要填false。
        if (renamefile(filename,bakfilename)==false)
        {
            logfile.write("renamefile(%s,%s) failed.\n",filename.c_str(),bakfilename.c_str());
            return false;
        }
    }

    return true;
}

bool activetest()
{
    strsendbuffer="<activetest>ok</activetest>";
    //  xxxxxxxxxx  logfile.write("发送：%s\n",strsendbuffer.c_str());
    if (tcpserver.write(strsendbuffer)==false) return false; // 向客户端发送请求报文。

    if (tcpserver.read(strrecvbuffer,10)==false) return false; // 接收客户端的回应报文。
    //  xxxxxxxxxx  logfile.write("接收：%s\n",strrecvbuffer.c_str());

    return true;
}