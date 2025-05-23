//反向代理程序外网模块：反向代理的外网服务器。
#include "_public.h"
using namespace idc;

#define MAXSOCK  1024

// 代理路由参数的结构体。
struct st_route
{
    int    srcport;           // 源端口。
    char dstip[31];        // 目标主机的地址。
    int    dstport;          // 目标主机的端口。
    int    listensock;      // 监听源端口的socket。
}stroute;

clogfile logfile;
cpactive pactive;                        // 进程心跳。
vector<struct st_route> vroute;       // 代理路由的容器。
int clientsocks[MAXSOCK];       // 存放每个socket连接对端的socket的值。
int clientatime[MAXSOCK];       // 存放每个socket连接最后一次收发报文的时间。
string clientbuffer[MAXSOCK]; // 存放每个socket发送内容的buffer。
unordered_map<int,int> islistensock;    //socket到vroute下标的映射。
int epollfd=0;                  // epoll的句柄。
int tfd=0;                         // 定时器的句柄。
int cmdlistensock=0;                // 命令通道监听的socket。
int cmdconnsock=0;                 // 命令通道连接的socket。
int clienmaxsock=-1;     // 客户端最大的socket。

// 进程退出函数。
void EXIT(int sig);                      
// 把代理路由参数加载到vroute容器。
bool loadroute(const char *inifile);
// 初始化服务端的监听端口。
int initserver(int port);

int main(int argc,char *argv[])
{
    if (argc != 4)
    {
        printf("\n");
        printf("Using :./rinetd logfile inifile cmdport\n\n");
        printf("Sample:./rinetd /tmp/rinetd.log /etc/rinetd.conf 5001\n\n");
        printf("        /project/tools/bin/procctl 5 /project/tools/bin/rinetd /tmp/rinetd.log /etc/rinetd.conf 5001\n\n");
        printf("logfile 本程序运行的日志文件名。\n");
        printf("inifile 代理路由参数配置文件。\n");
        printf("cmdport 用于与内网代理程序的通讯端口。\n\n");
        return -1;
    }

    // 关闭全部的信号和输入输出。
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程。
    // 但请不要用 "kill -9 +进程号" 强行终止。
    closeioandsignal(); signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    // 打开日志文件。
    if (logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败（%s）。\n",argv[1]);
        return -1;
    }

    pactive.addpinfo(30,"rinetd");       // 设置进程的心跳超时间为30秒。

    // 把代理路由参数加载到vroute容器。
    if (loadroute(argv[2])==false) return -1;

    logfile.write("加载代理路由参数成功(%d)。\n",vroute.size());

    // 初始化命令通道的监听端口。
    if ( (cmdlistensock = initserver(atoi(argv[3]))) < 0 )
    {
        logfile.write("initserver(%s) failed.\n",argv[3]);
        EXIT(-1);
    }

    // 等待内网程序的连接请求，cmdlistensock是阻塞的，并且不交给epoll.
    struct sockaddr_in client;
    socklen_t len = sizeof(client);
    cmdconnsock = accept(cmdlistensock,(struct sockaddr*)&client,&len);
    if (cmdconnsock < 0)
    {
        logfile.write("accept() failed.\n");
        EXIT(-1);
    }
    logfile.write("与内部代理服务器的命令通道已建立(cmdconnsock=%d)。\n",cmdconnsock);

    // 初始化服务端用于监听外网的socket。
    for (int i=0;i<vroute.size();i++)
    {
        if ( (vroute[i].listensock=initserver(vroute[i].srcport)) < 0 )
        {
            logfile.write("initserver(%d) failed.\n",vroute[i].srcport);
            EXIT(-1);
        }

        // 把监听socket设置成非阻塞。
        fcntl(vroute[i].listensock,F_SETFL,fcntl(vroute[i].listensock,F_GETFD,0)|O_NONBLOCK);
        // 记录socket到下标的映射
        islistensock.emplace(vroute[i].listensock,i);
    }

    epollfd=epoll_create(1);
    struct epoll_event ev;
    // 为监听外网的socket准备可读事件。
    for (int i=0;i<vroute.size();i++)
    {
        ev.events=EPOLLIN;
        ev.data.fd=vroute[i].listensock;  
        epoll_ctl(epollfd,EPOLL_CTL_ADD,vroute[i].listensock,&ev);   // 把监听外网的socket的读事件加入epollfd中。
    }

    // 创建定时器。
    tfd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);  // 创建timerfd。
    struct itimerspec timeout;
    memset(&timeout,0,sizeof(struct itimerspec));
    timeout.it_value.tv_sec = 20;                  // 超时时间为20秒。
    timeout.it_value.tv_nsec = 0;
    timerfd_settime(tfd,0,&timeout,NULL);  // 开始计时。
  
    // 为定时器准备事件。
    ev.events=EPOLLIN;   
    ev.data.fd=tfd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,tfd,&ev);       // 把定时器的读事件加入epollfd中。

    struct epoll_event evs[10];     // 存放epoll返回的事件。

    while (true)
    {
        // 等待监视的socket有事件发生。
        int infds=epoll_wait(epollfd,evs,10,-1);

        // 返回失败。
        if (infds < 0)
        {
            logfile.write("epoll() failed。");
            EXIT(-1);
        }

        // 遍历epoll返回的已发生事件的数组evs。
        for (int i=0;i<infds;i++)
        {
            ////////////////////////////////////////////////////////
            // 如果定时器的时间已到，1）更新进程的心跳；2）向命令通道发送心跳报文；3）清理空闲的客户端socket。
            if (evs[i].data.fd==tfd)
            {
                timerfd_settime(tfd,0,&timeout,0);  // 重新开始计时。

                pactive.uptatime();        // 1）更新进程心跳；

                // 2）向命令通道发送心跳报文；
                char buffer[256];
                strcpy(buffer,"<activetest>");
                if (send(cmdconnsock,buffer,strlen(buffer),0)<=0)
                {
                    logfile.write("与内网程序的命令通道已断开。\n");
                    EXIT(-1);
                }

                // 3）清理空闲的客户端socket。
                for (int j=0;j<=clienmaxsock;j++)
                {
                    // 如果客户端socket空闲的时间超过80秒就关掉它。
                    if ( (clientsocks[j]>0) && ((time(0)-clientatime[j])>80) )
                    {
                        logfile.write("client(%d,%d) timeout。\n",clientsocks[j],clientsocks[clientsocks[j]]);
                        close(clientsocks[j]);
                        close(clientsocks[clientsocks[j]]);
                        // 把数组中对端的socket置空
                        clientsocks[clientsocks[j]]=0;
                        // 把数组中本端的socket置空
                        clientsocks[j]=0;
                        //更新最大socket
                       while(clienmaxsock>0 && clientsocks[clienmaxsock] == 0)
                        --clienmaxsock;
                    }
                }

                continue;
            }
            ////////////////////////////////////////////////////////
      
            ////////////////////////////////////////////////////////
            // 如果发生事件的是监听的listensock，表示外网有新的客户端连上来。
            if (islistensock.find(evs[i].data.fd) != islistensock.end())
            {
                int j = islistensock[evs[i].data.fd];
                // 处理连接
                struct sockaddr_in client;
                socklen_t len = sizeof(client);
                int srcsock = accept(vroute[j].listensock,(struct sockaddr*)&client,&len);
                if (srcsock<0) continue;
                if (srcsock>=MAXSOCK) 
                {
                    logfile.write("连接数已超过最大值%d。\n",MAXSOCK);
                    close(srcsock);
                    continue;
                }

                // 通过命令通道向内网程序发送命令，把路由参数传给它。
                char buffer[256];
                memset(buffer,0,sizeof(buffer));
                sprintf(buffer,"<dstip>%s</dstip><dstport>%d</dstport>",vroute[j].dstip,vroute[j].dstport);
                if (send(cmdconnsock,buffer,strlen(buffer),0)<=0)
                {
                    logfile.write("与内网的命令通道已断开。\n");
                    EXIT(-1);
                }

                // 接受内网程序的连接，这里的accept()是阻塞的。
                int dstsock=accept(cmdlistensock,(struct sockaddr*)&client,&len);
                if (dstsock<0)
                {
                    close(srcsock);
                    continue;
                }
                if (dstsock>=MAXSOCK)
                {
                    logfile.write("连接数已超过最大值%d。\n",MAXSOCK);
                    close(srcsock);
                    close(dstsock);
                    continue;
                }

                // 把内网和外网客户端的socket对接在一起。

                // 为新连接的两个socket准备可读事件，并添加到epoll中。
                ev.data.fd=srcsock; ev.events=EPOLLIN;
                epoll_ctl(epollfd,EPOLL_CTL_ADD,srcsock,&ev);
                ev.data.fd=dstsock; ev.events=EPOLLIN;
                epoll_ctl(epollfd,EPOLL_CTL_ADD,dstsock,&ev);

                // 更新clientsocks数组中两端soccket的值和活动时间。
                clientsocks[srcsock]=dstsock; clientsocks[dstsock]=srcsock;
                clientatime[srcsock]=time(0); clientatime[dstsock]=time(0);

                // 更新最大客户端socket。
                clienmaxsock = srcsock>clienmaxsock ? srcsock:clienmaxsock;
                clienmaxsock = dstsock>clienmaxsock ? dstsock:clienmaxsock;

                logfile.write("accept port %d client(%d,%d) ok。\n",vroute[j].srcport,srcsock,dstsock);

                continue;
            }
            ////////////////////////////////////////////////////////

            ////////////////////////////////////////////////////////
            // 如果是客户端连接的socke有事件，分三种情况：1）客户端有报文发过来；2）客户端连接已断开；3）有数据要发给客户端。

            // 如果从通道一端的socket读取到了数据，把数据存放在对端socket的缓冲区中。
            if (evs[i].events & EPOLLIN)     // 判断是否为读事件。要用&判断，有读事件是1，有写事件是4，如果读和写都有，是5。
            {
                char buffer[5000];     // 存放从接收缓冲区中读取的数据。
                int    buflen=0;          // 从接收缓冲区中读取的数据的大小。

                // 从通道的一端读取数据。
                if ( (buflen=recv(evs[i].data.fd,buffer,sizeof(buffer),0)) <= 0 )
                {
                    // 如果连接已断开，需要关闭通道两端的socket。
                    logfile.write("client(%d,%d) disconnected。\n",evs[i].data.fd,clientsocks[evs[i].data.fd]);
                    close(evs[i].data.fd);                                         // 关闭客户端的连接。
                    close(clientsocks[evs[i].data.fd]);                     // 关闭客户端对端的连接。
                    clientsocks[clientsocks[evs[i].data.fd]]=0;       // 把数组中对端的socket置空。
                    clientsocks[evs[i].data.fd]=0;                           // 把数组中本端的socket置空。
                    while(clienmaxsock>0 && clientsocks[clienmaxsock] == 0)                    //更新最大socket。
                        --clienmaxsock;

                    continue;
                }
      
                logfile.write("from %d,%d bytes\n",evs[i].data.fd,buflen);

                // 把读取到的数据追加到对端socket的buffer中。
                clientbuffer[clientsocks[evs[i].data.fd]].append(buffer,buflen);

                // 修改对端socket的事件，增加写事件。
                ev.data.fd=clientsocks[evs[i].data.fd];
                ev.events=EPOLLIN|EPOLLOUT;
                epoll_ctl(epollfd,EPOLL_CTL_MOD,ev.data.fd,&ev);

                // 更新通道两端socket的活动时间。
                clientatime[evs[i].data.fd]=time(0); 
                clientatime[clientsocks[evs[i].data.fd]]=time(0);  
            }

            // 判断客户端的socket是否有写事件（发送缓冲区没有满）。
            if (evs[i].events & EPOLLOUT)
            {
                // 把socket缓冲区中的数据发送出去。
                int writen=send(evs[i].data.fd,clientbuffer[evs[i].data.fd].data(),clientbuffer[evs[i].data.fd].length(),0);

                logfile.write("to %d,%d bytes\n",evs[i].data.fd,writen);

                // 删除socket缓冲区中已成功发送的数据。
                clientbuffer[evs[i].data.fd].erase(0,writen);

                // 如果socket缓冲区中没有数据了，不再关心socket的写件事。
                if (clientbuffer[evs[i].data.fd].length()==0)
                {
                    ev.data.fd=evs[i].data.fd;
                    ev.events=EPOLLIN;
                    epoll_ctl(epollfd,EPOLL_CTL_MOD,ev.data.fd,&ev);
                }
            }
        }
    }

    return 0;
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d。\n\n",sig);
    // 关闭监听内网程序的socket。
    close(cmdlistensock);
    // 关闭内网程序与服务端的命令通道。
    close(cmdconnsock);

    // 关闭全部监听的socket。
    for (auto &route:vroute)
        if (route.listensock>0) close(route.listensock);

    // 关闭全部客户端的socket。
    for (auto clientsock:clientsocks)
        if (clientsock>0) close(clientsock);

    close(epollfd);   // 关闭epoll。
    close(tfd);       // 关闭定时器。

    exit(0);
}

bool loadroute(const char *inifile)
{
    cifile ifile;

    if (ifile.open(inifile)==false)
    {
        logfile.write("打开代理路由参数文件(%s)失败。\n",inifile); return false;
    }

    string strbuffer;
    ccmdstr cmdstr;

   while (true)
    {
        if (ifile.readline(strbuffer)==false) break;

        // 删除注释，#后面的部分。
        auto pos=strbuffer.find("#");
        if (pos!=string::npos) strbuffer.resize(pos);

        replacestr(strbuffer,"  "," ",true);    // 把两个空格替换成一个空格.
        deletelrchr(strbuffer,' ');                 // 删除两边的空格。

        // 拆分参数。
        cmdstr.splittocmd(strbuffer," ");
        if (cmdstr.size()!=3) continue;

        memset(&stroute,0,sizeof(struct st_route));
        cmdstr.getvalue(0,stroute.srcport);          // 源端口。
        cmdstr.getvalue(1,stroute.dstip);             // 目标地址。
        cmdstr.getvalue(2,stroute.dstport);         // 目标端口。

        vroute.push_back(stroute);
    }

    return true;
}

int initserver(const int port)
{
    int sock = socket(AF_INET,SOCK_STREAM,0);
    if (sock < 0)
    {
        logfile.write("socket(%d) failed.\n",port);
        return -1;
    }

    int opt = 1; unsigned int len = sizeof(opt);
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,len);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sock,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0 )
    {
        logfile.write("bind(%d) failed.\n",port); close(sock);
        return -1;
    }

    if (listen(sock,5) != 0 )
    {
        logfile.write("listen(%d) failed.\n",port); close(sock);
        return -1;
    }

    return sock;
}
