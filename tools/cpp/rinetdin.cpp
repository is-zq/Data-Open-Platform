#include "_public.h"
using namespace idc;

#define MAXSOCK  1024

clogfile logfile;
cpactive pactive;     // 进程心跳。
int  cmdconnsock;  // 内网程序与外网程序的命令通道的socket。
int clientsocks[MAXSOCK];       // 存放每个socket连接对端的socket的值。
int clientatime[MAXSOCK];       // 存放每个socket连接最后一次收发报文的时间。
string clientbuffer[MAXSOCK]; // 存放每个socket发送内容的buffer。
int epollfd=0;     // epoll的句柄。
int tfd=0;            // 定时器的句柄。
int clienmaxsock=-1;     // 客户端最大的socket。

// 进程退出函数。
void EXIT(int sig);
// 向目标ip和端口发起socket连接，bio取值：false-非阻塞io，true-阻塞io，默认false.
int conntodst(const char *ip,const int port,bool bio=false);

int main(int argc,char *argv[])
{
    if (argc != 4)
    {
        printf("\n");
        printf("Using :./rinetdin logfile ip port\n\n");
        printf("Sample:./rinetdin /tmp/rinetdin.log 192.168.182.124 5001\n\n");
        printf("        /project/tools/bin/procctl 5 /project/tools/bin/rinetdin /tmp/rinetdin.log 192.168.182.124 5001\n\n");
        printf("logfile 本程序运行的日志文件名。\n");
        printf("ip      外网代理程序的ip地址。\n");
        printf("port    外网代理程序的通讯端口。\n\n\n");
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

    pactive.addpinfo(30,"inetd");       // 设置进程的心跳超时间为30秒。

    // 建立与外网程序的命令通道，采用阻塞的socket。
    if ((cmdconnsock=conntodst(argv[2],atoi(argv[3]),true))<0)
    {
        logfile.write("tcpclient.connect(%s,%s) 失败。\n",argv[2],argv[3]);
        return -1;
    }

    logfile.write("与外部代理服务器的命令通道已建立(cmdconnsock=%d)。\n",cmdconnsock);

    // 命令通道建立之后，再设置为非阻塞的。
    fcntl(cmdconnsock,F_SETFL,fcntl(cmdconnsock,F_GETFD,0)|O_NONBLOCK);

    epollfd=epoll_create(1);
    struct epoll_event ev;
    // 为命令通道的socket准备可读事件。
    ev.events=EPOLLIN;
    ev.data.fd=cmdconnsock;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,cmdconnsock,&ev);

    // 创建定时器。
    tfd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);  // 创建timerfd。
    struct itimerspec timeout;
    memset(&timeout,0,sizeof(struct itimerspec));
    timeout.it_value.tv_sec = 20;           // 超时时间为20秒。
    timeout.it_value.tv_nsec = 0;
    timerfd_settime(tfd,0,&timeout,0);  // 开始计时。
  
    // 为定时器准备事件。
    ev.events=EPOLLIN;               
    ev.data.fd=tfd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,tfd,&ev);       // 把定时器的读事件加入epollfd中。

    pactive.addpinfo(30,"rinetdin");   // 设置进程的心跳超时间为30秒。

    struct epoll_event evs[10];      // 存放epoll返回的事件。

    while (true)
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
            ////////////////////////////////////////////////////////
            // 如果定时器的时间已到，1）设置进程的心跳；2）清理空闲的客户端socket。
            if (evs[i].data.fd==tfd)
            {
                timerfd_settime(tfd,0,&timeout,NULL);  // 重新开始计时。

                pactive.uptatime();        // 1）更新进程心跳。

                // 2）清理空闲的客户端socket。
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
            // 如果发生事件的是命令通道。
            if (evs[i].data.fd==cmdconnsock)
            {
                // 读取命令通道socket报文内容。
                char buffer[256];
                memset(buffer,0,sizeof(buffer));
                if (recv(cmdconnsock,buffer,sizeof(buffer),0)<=0)
                {
                    logfile.write("与外网的命令通道已断开。\n");
                    EXIT(-1);
                }

                // 如果收到的是心跳报文。
                if (strcmp(buffer,"<activetest>")==0) continue;

                // 如果收到的是新建连接的命令。
                // 向外网服务端发起连接请求。
                int srcsock=conntodst(argv[2],atoi(argv[3]));
                if (srcsock<0) continue;
                if (srcsock>=MAXSOCK)
                {
                    logfile.write("连接数已超过最大值%d。\n",MAXSOCK);
                    close(srcsock);
                    continue;
                }

                // 从命令报文内容中获取目标服务器的地址和端口。
                char dstip[11];
                int  dstport;
                getxmlbuffer(buffer,"dstip",dstip,30);
                getxmlbuffer(buffer,"dstport",dstport);

                // 向目标服务器的地址和端口发起socket连接。
                int dstsock=conntodst(dstip,dstport);
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

                // 把内网和外网的socket对接在一起。
                logfile.write("新建内外网通道(%d,%d) ok。\n",srcsock,dstsock);

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
    // 关闭内网程序与外网程序的命令通道。
    close(cmdconnsock);
    // 关闭全部客户端的socket。
    for (auto clientsock:clientsocks)
        if (clientsock>0) close(clientsock);
    close(epollfd);   // 关闭epoll。
    close(tfd);       // 关闭定时器。
    exit(0);
}

int conntodst(const char *ip,const int port,bool bio)
{
    // 创建客户端的socket。
    int sockfd;
    if ( (sockfd = socket(AF_INET,SOCK_STREAM,0))==-1) return -1; 

    // 向服务器发起连接请求。
    struct hostent* h;
    if ( (h = gethostbyname(ip)) == 0 )
    {
        close(sockfd);
        return -1;
    }
  
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port); // 指定服务端的通讯端口。
    memcpy(&servaddr.sin_addr,h->h_addr,h->h_length);

    // 把socket设置为非阻塞。
    if (bio==false) fcntl(sockfd,F_SETFL,fcntl(sockfd,F_GETFD,0)|O_NONBLOCK);

    if (connect(sockfd, (struct sockaddr *)&servaddr,sizeof(servaddr))<0)
    {
        if (errno!=EINPROGRESS)
        {
            logfile.write("connect(%s,%d) failed.\n",ip,port);
            return -1;
        }
    }

    return sockfd;
}
