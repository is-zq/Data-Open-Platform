#include "_public.h"
using namespace idc;

#define MAXSOCK  1024          // 最大连接数。

// 代理路由参数的结构体。
struct st_route
{
    int    srcport;           // 源端口。
    char dstip[31];        // 目标主机的地址。
    int    dstport;          // 目标主机的端口。
    int    listensock;      // 源端口监听的socket。
}stroute;

clogfile logfile;
cpactive pactive;       // 进程心跳。
vector<struct st_route> vroute;       // 代理路由的容器。
int clientsocks[MAXSOCK];       // 存放每个socket连接对端的socket的值。
int clientatime[MAXSOCK];       // 存放每个socket连接最后一次收发报文的时间。
string clientbuffer[MAXSOCK]; // 存放每个socket发送内容的buffer。
unordered_map<int,int> islistensock;    //socket到vroute下标的映射。
int epollfd=0;      // epoll的句柄。
int clienmaxsock=-1;     // 客户端最大的socket。

// 进程退出函数。
void EXIT(int sig);
// 向目标地址和端口发起socket连接。
int conntodst(const char *ip,const int port);
// 初始化服务端的监听端口。
int initserver(const int port);
// 加载代理路由参数
bool loadroute(const char *inifile);

int main(int argc,char *argv[])
{
    if (argc != 3)
    {
        printf("\n");
        printf("Using :./inetd logfile inifile\n\n");
        printf("Sample:./inetd /tmp/inetd.log /etc/inetd.conf\n\n");
        printf("        /project/tools/bin/procctl 5 /project/tools/bin/inetd /tmp/inetd.log /etc/inetd.conf\n\n");
        printf("本程序的功能是正向代理，如果用到了1024以下的端口，则必须由root用户启动。\n");
        printf("logfile 本程序运行的日是志文件。\n");
        printf("inifile 路由参数配置文件。\n");

        return -1;
    }

    // 关闭全部的信号和输入输出。
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程。
    // 但请不要用 "kill -9 +进程号" 强行终止。
    closeioandsignal(true);  signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    // 打开日志文件。
    if (logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败（%s）。\n",argv[1]); return -1;
    }

    pactive.addpinfo(30,"inetd");       // 设置进程的心跳超时间为30秒。

    // 把代理路由参数配置文件加载到vroute容器。
    if (loadroute(argv[2])==false) return -1;

    logfile.write("加载代理路由参数成功(%d)。\n",vroute.size());

    // 初始化服务端用于监听的socket。
    for (int i=0;i<vroute.size();i++)
    {
        if ( (vroute[i].listensock=initserver(vroute[i].srcport)) < 0 )
        {
            // 如果某一个socket初始化失败，忽略它。
            logfile.write("initserver(%d) failed.\n",vroute[i].srcport);
            continue;
        }

        // 把监听socket设置成非阻塞。
        fcntl(vroute[i].listensock,F_SETFL,fcntl(vroute[i].listensock,F_GETFD,0)|O_NONBLOCK);
        // 记录socket到下标的映射
        islistensock.emplace(vroute[i].listensock,i);
    }

    // 创建epoll句柄。
    epollfd = epoll_create1(0);

    struct epoll_event ev;

    // 向epoll添加监听socket
    for (auto route:vroute)
    {
        if (route.listensock < 0) continue;

        ev.events=EPOLLIN;
        ev.data.fd=route.listensock;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,route.listensock,&ev);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //把定时器加入epoll。
    int tfd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);   // 创建timerfd。
    struct itimerspec timeout;                                // 定时时间的数据结构。
    memset(&timeout,0,sizeof(struct itimerspec));
    timeout.it_value.tv_sec = 10;                            // 定时时间为10秒。
    timeout.it_value.tv_nsec = 0;
    timerfd_settime(tfd,0,&timeout,0);                  // 开始计时。alarm(10)
    ev.data.fd=tfd;                                                  // 为定时器准备事件。
    ev.events=EPOLLIN;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,tfd,&ev);     // 把定时器fd加入epoll。
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct epoll_event evs[10];      // 存放epoll返回的事件。

    while (true)     // 事件循环。
    {
        // 等待监视的socket有事件发生。
        int infds=epoll_wait(epollfd,evs,10,-1);

        // 返回失败。
        if (infds < 0)
        {
            logfile.write("epoll() failed。\n");
            EXIT(-1);
        }

        // 遍历epoll返回的已发生事件的数组evs。
        for (int i=0;i<infds;i++)
        {
            logfile.write("已发生事件的fd=%d(%d)\n",evs[i].data.fd,evs[i].events);

            ////////////////////////////////////////////////////////
            // 如果定时器的时间已到，1）更新进程的心跳；2）清理空闲的客户端socket，路由器也会这么做。
            if (evs[i].data.fd==tfd)
            {
               timerfd_settime(tfd,0,&timeout,0);       // 重新开始计时。

               pactive.uptatime();        // 1）更新进程心跳。

               // 2）清理空闲的客户端socket。
               for (int j=0;j<=clienmaxsock;j++)
               {
                   // 如果客户端socket空闲的时间超过80秒就关掉它。
                   if ( (clientsocks[j]>0) && ((time(0)-clientatime[j])>80) )
                   {
                       logfile.write("client(%d,%d) timeout。\n",clientsocks[j],clientsocks[clientsocks[j]]);
                       close(clientsocks[clientsocks[j]]);
                       close(clientsocks[j]);
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
            // 如果发生事件的是listensock，表示有新的客户端连上来。
            if (islistensock.find(evs[i].data.fd) != islistensock.end())
            {
                int j = islistensock[evs[i].data.fd];
                // 处理连接
                struct sockaddr_in client;
                socklen_t len = sizeof(client);
                int srcsock = accept(vroute[j].listensock,(struct sockaddr*)&client,&len);
                if (srcsock<0) continue;   //失败，直接继续
                if (srcsock>=MAXSOCK) 
                {
                    logfile.write("连接数已超过最大值%d。\n",MAXSOCK);
                    close(srcsock);
                    continue;
                }

                // 向目标地址和端口发起连接，如果连接失败，将关闭通道。
                int dstsock = conntodst(vroute[j].dstip,vroute[j].dstport);
                if (dstsock < 0)
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

                logfile.write("accept on port %d,client(%d,%d) ok。\n",vroute[j].srcport,srcsock,dstsock);

                // 为新连接的两个socket准备读事件，并添加到epoll中。
                ev.data.fd=srcsock; ev.events=EPOLLIN;
                epoll_ctl(epollfd,EPOLL_CTL_ADD,srcsock,&ev);
                ev.data.fd=dstsock; ev.events=EPOLLIN;
                epoll_ctl(epollfd,EPOLL_CTL_ADD,dstsock,&ev);

                // 更新clientsocks数组中两端soccket的值和活动时间。
                clientsocks[srcsock]=dstsock;  clientatime[srcsock]=time(0); 
                clientsocks[dstsock]=srcsock;  clientatime[dstsock]=time(0);

                // 更新最大客户端socket。
                clienmaxsock = srcsock>clienmaxsock ? srcsock:clienmaxsock;
                clienmaxsock = dstsock>clienmaxsock ? dstsock:clienmaxsock;

                continue;
            }
            ////////////////////////////////////////////////////////

            ////////////////////////////////////////////////////////
            // 如果是客户端连接的socke有事件，分三种情况：1）客户端有报文发过来；2）客户端连接已断开；3）有数据要发给客户端。

            if (evs[i].events & EPOLLIN)     // 判断是否为读事件。要用&判断，有读事件是1，有写事件是4，如果读和写都有，是5。
            {
                char buffer[5000];
                int  buflen=0;

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
      
                // 成功的读取到了数据，把接收到的报文内容原封不动的发给通道的对端。
                // 如果直接在这发，可能由于内核缓冲区满发送失败而直接跳过了。
                // send(clientsocks[evs[i].data.fd],buffer,buflen,0);

                logfile.write("from %d,%d bytes\n",evs[i].data.fd,buflen);

                // 把读取到的数据追加到对端socket的buffer中。
                clientbuffer[clientsocks[evs[i].data.fd]].append(buffer,buflen);

                // 修改对端socket的事件，增加写事件。
                ev.data.fd = clientsocks[evs[i].data.fd];
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

    // 关闭全部监听的socket。
    for (auto &route:vroute)
        if (route.listensock>0) close(route.listensock);

    // 关闭全部客户端的socket。
    for (auto clientsock:clientsocks)
        if (clientsock>0) close(clientsock);

    // 关闭epoll。
    close(epollfd);

    exit(0);
}

bool loadroute(const char *inifile)
{
    cifile ifile;

    if (ifile.open(inifile)==false)
    {
        logfile.write("打开代理路由参数文件(%s)失败。\n",inifile);
        return false;
    }

    string strbuffer;
    ccmdstr cmdstr;

    while (true)
    {
        if (ifile.readline(strbuffer)==false) break;

        // 删除注释，#后面的部分。
        auto pos=strbuffer.find("#");
        if (pos!=string::npos) strbuffer.resize(pos);

        replacestr(strbuffer,"  "," ",true);    // 把两个空格替换成一个空格。
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
        logfile.write("bind(%d) failed.\n",port);
        close(sock);
        return -1;
    }

    if (listen(sock,5) != 0 )
    {
        logfile.write("listen(%d) failed.\n",port);
        close(sock);
        return -1;
    }

    return sock;
}

int conntodst(const char *ip,const int port)
{
    // 创建客户端的socket。
    int sockfd;
    if ( (sockfd = socket(AF_INET,SOCK_STREAM,0))==-1) return -1; 

    // 通过域名获得ip地址(也可直接传ip)
    struct hostent* h;
    if ( (h = gethostbyname(ip)) == 0 )
    {
        close(sockfd);
        return -1;
    }
  
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    memcpy(&servaddr.sin_addr,h->h_addr,h->h_length);

    // 把socket设置为非阻塞。
    fcntl(sockfd,F_SETFL,fcntl(sockfd,F_GETFD,0)|O_NONBLOCK);

    if (connect(sockfd, (struct sockaddr *)&servaddr,sizeof(servaddr))<0 && errno!=EINPROGRESS)   //非阻塞connect成功会返回失败，要用errno判断
    {
        logfile.write("connect(%s,%d) failed.\n",ip,port);
        return -1;
    }

    return sockfd;
}
