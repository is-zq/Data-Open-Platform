#include "_public.h"
#include "_ooci.h"
using namespace idc;

#define THREAD_CNT 20

// 客户端的结构体。
struct st_client
{
    int      clientatime=0;     // 客户端最后一次活动的时间。
    string recvbuffer;          // 客户端的接收缓冲区。
    string sendbuffer;         // 客户端的发送缓冲区。
};

clogfile logfile;               // 本程序运行的日志。
vector<thread> wthreads_pool;     //工作线程池

// 接收/发送队列的结构体。
struct st_recvmesg
{
    int      sock=0;               // 客户端的socket。
    string message;            // 接收/发送的报文。

    st_recvmesg(int in_sock,string &in_message):sock(in_sock),message(in_message){}
};

// 进程退出函数。
void EXIT(int sig);
// 初始化服务端的监听端口。
int initserver(const int port);
// 从GET请求中获取参数的值：strget-GET请求报文的内容；name-参数名；value-参数值。
bool getvalue(const string &strget,const string &name,string &value);

// webserver类
class WebServer
{
private:
    queue<shared_ptr<st_recvmesg>> m_rq;                            // 接收队列
    mutex m_mutex_rq;                                               // 接收队列的互斥锁。
    condition_variable m_cond_rq;                                   // 接收队列的条件变量。

    queue<shared_ptr<st_recvmesg>> m_sq;                            // 发送队列
    mutex m_mutex_sq;                                               // 发送队列的互斥锁。
    int m_sendpipe[2] = {0};                                        // 工作线程通知发送线程的无名管道。

    unordered_map<int,struct st_client> clientmap;                  // 存放客户端对象的哈希表，即状态机。

    atomic_bool m_exit;                                             // 如果m_exit==true，工作线程和发送线程将退出。

public:
    int m_recvpipe[2] = {0};                                        // 主线程通知接收线程退出的管道。

    WebServer() 
    { 
        pipe(m_sendpipe);     // 创建工作线程通知发送线程的无名管道。
        pipe(m_recvpipe);      // 创建主线程通知接收线程退出的管道。
        m_exit==false;
    }

    // 接收线程主函数，listenport-监听端口。
    void recvfunc(int listenport)                  
    {
         // 初始化服务端用于监听的socket。
        int listensock=initserver(listenport);
        if (listensock<0)
        {
            logfile.write("接收线程：initserver(%d) failed.\n",listenport);
            return;
        }

        int epollfd=epoll_create1(0);

        // 为监听的socket准备读事件。
        struct epoll_event ev;               // 声明事件的数据结构。
        ev.events=EPOLLIN;                 // 读事件。
        ev.data.fd=listensock;              // 指定事件的自定义数据，会随着epoll_wait()返回的事件一并返回。
        epoll_ctl(epollfd,EPOLL_CTL_ADD,listensock,&ev);  // 把监听的socket的事件加入epollfd中。

        // 把接收主线程通知的管道加入epoll。
        ev.data.fd = m_recvpipe[0];
        ev.events = EPOLLIN;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,ev.data.fd,&ev); 

        struct epoll_event evs[10];      // 存放epoll返回的事件。

        while (true)     // 事件循环。
        {
            int infds=epoll_wait(epollfd,evs,10,-1);

            // 返回失败。
            if (infds < 0) { logfile.write("接收线程：epoll() failed。\n"); return; }

            // 遍历epoll返回的已发生事件的数组evs。
            for (int i=0;i<infds;i++)
            {
                logfile.write("接收线程：已发生事件的fd=%d(%d)\n",evs[i].data.fd,evs[i].events);

                ////////////////////////////////////////////////////////
                // 如果发生事件的是listensock，表示有新的客户端连上来。
                if (evs[i].data.fd==listensock)
                {
                    struct sockaddr_in client;
                    socklen_t len = sizeof(client);
                    int clientsock = accept(listensock,(struct sockaddr*)&client,&len);

                    fcntl(clientsock,F_SETFL,fcntl(clientsock,F_GETFD,0)|O_NONBLOCK);     // 把socket设置为非阻塞。

                    logfile.write("接收线程：accept client(socket=%d) ok.\n",clientsock);

                    // 为新的客户端连接准备读事件，并添加到epoll中。
                    ev.data.fd=clientsock;
                    ev.events=EPOLLIN;
                    epoll_ctl(epollfd,EPOLL_CTL_ADD,clientsock,&ev);

                    continue;
                }

                // 如果是管道有读事件。
                if (evs[i].data.fd==m_recvpipe[0])
                {
                    logfile.write("接收线程：即将退出。\n");

                    m_exit=true;      // 把退出的原子变量置为true。

                    m_cond_rq.notify_all();     // 通知全部的工作线程退出。

                    write(m_sendpipe[1],(char*)"o",1);   // 通知发送线程退出。

                    return;
                }

                // 如果是客户端连接的socke有事件，分两种情况：1）客户端有报文发过来；2）客户端连接已断开。
                if (evs[i].events & EPOLLIN)     // 判断是否为读事件。 
                {
                    char buffer[5000];     // 存放从接收缓冲区中读取的数据。
                    int    buflen=0;          // 从接收缓冲区中读取的数据的大小。

                    // 读取客户端的请求报文。
                    if ( (buflen=recv(evs[i].data.fd,buffer,sizeof(buffer),0)) <= 0 )
                    {
                        // 如果连接已断开。
                        logfile.write("接收线程：client(%d) disconnected。\n",evs[i].data.fd);

                        close(evs[i].data.fd);                      // 关闭客户端的连接。

                        clientmap.erase(evs[i].data.fd);     // 从状态机中删除客户端。

                        continue;
                    }
                    logfile.write("接收线程：recv %d,%d bytes\n",evs[i].data.fd,buflen);
                    // 把读取到的数据追加到socket的recvbuffer中。
                    clientmap[evs[i].data.fd].recvbuffer.append(buffer,buflen);
                    // 如果recvbuffer中的内容以"\r\n\r\n"结束，表示已经是一个完整的http请求报文了。
                    if ( clientmap[evs[i].data.fd].recvbuffer.compare( clientmap[evs[i].data.fd].recvbuffer.length()-4,4,"\r\n\r\n")==0)
                    {
                        logfile.write("接收线程：接收到了一个完整的请求报文。\n");
                        inrq((int)evs[i].data.fd, clientmap[evs[i].data.fd].recvbuffer);    // 把完整的请求报文入队，交给工作线程。
                        clientmap[evs[i].data.fd].recvbuffer.clear();  // 清空socket的recvbuffer。
                    }
                    else
                    {
                        if (clientmap[evs[i].data.fd].recvbuffer.size() > 10000)    // 请求太长，可能是恶意攻击
                        {
                            close(evs[i].data.fd);                      // 关闭客户端的连接。
                            clientmap.erase(evs[i].data.fd);     // 从状态机中删除客户端。
                            // 若多次超长可以考虑增加把客户端的ip加入黑名单。
                        }
                    }

                    clientmap[evs[i].data.fd].clientatime=time(0);   // 更新客户端的活动时间
                }
            }
        }
    }

    // 把客户端的socket和请求报文放入接收队列，sock-客户端的socket，message-客户端的请求报文。
    void inrq(int sock,string &message)              
    {
        shared_ptr<st_recvmesg> ptr = make_shared<st_recvmesg>(sock,message);   // 创建接收报文对象。

        lock_guard<mutex> lock(m_mutex_rq);

        m_rq.push(ptr);                   // 把接收报文对象放到接收队列中。

        m_cond_rq.notify_one();     // 通知工作线程处理接收队列中的报文。
    }
    
    // 工作线程主函数，处理接收队列中的请求报文，id-线程编号（仅用于调试和日志，没什么其它的含义）。
    void workfunc(int id)       
    {
        connection conn;

        if (conn.connecttodb("idc/idcpwd@snorcl11g_124","Simplified Chinese_China.AL32UTF8")!=0)
        {
            logfile.write("connect database(idc/idcpwd@snorcl11g_124) failed.\n%s\n",conn.message());
            return;
        }

        while (true)
        {
            shared_ptr<st_recvmesg> ptr;

            {
                unique_lock<mutex> lock(m_mutex_rq);

                while (m_rq.empty())                  // 如果队列空，进入循环，否则直接处理数据。必须用循环，不能用if
                {
                    m_cond_rq.wait(lock);            // 等待生产者的唤醒信号。

                    if (m_exit==true) 
                    {
                        logfile.write("工作线程（%d）：即将退出。\n",id);
                        return;
                    }
                }

                ptr=m_rq.front(); m_rq.pop();    // 出队一个元素。
            }

            // 处理出队的元素，即客户端的请求报文。
            logfile.write("工作线程（%d）请求：sock=%d,mesg=%s\n",id,ptr->sock,ptr->message.c_str());

            // 解析请求报文、判断权限、执行查询数据的SQL语句、生成响应报文。
            string sendbuf;
            bizmain(conn,ptr->sock,ptr->message,sendbuf);
            string message=sformat(
                "HTTP/1.1 200 OK\r\n"
                "Server: webserver\r\n"
                "Content-Type: text/html;charset=utf-8\r\n")+sformat("Content-Length:%d\r\n\r\n",sendbuf.size())+sendbuf;

            logfile.write("工作线程（%d）回应：sock=%d,mesg=%s\n",id,ptr->sock,message.c_str());

            // 把客户端的socket和响应报文放入发送队列。
            insq(ptr->sock,message);    
        }
    }

    // 处理客户端的请求报文，生成响应报文。
    // conn-数据库连接；recvbuf-http请求报文；sendbuf-http响应报文的数据部分，不包括状态行和头部信息。
    void bizmain(connection &conn,int cliensock,const string & recvbuf,string & sendbuf)
    {
        string username,passwd,intername;

        getvalue(recvbuf,"username",username);    // 解析用户名。
        getvalue(recvbuf,"passwd",passwd);            // 解析密码。
        getvalue(recvbuf,"intername",intername);   // 解析接口名。

        // 1）验证用户名和密码是否正确。
        sqlstatement stmt(&conn);
        stmt.prepare("select ip from T_USERINFO where username=:1 and passwd=:2 and rsts=1");
        string ip;
        stmt.bindin(1,username);
        stmt.bindin(2,passwd);
        stmt.bindout(1,ip,50);
        stmt.execute();
        if (stmt.next()!=0)
        {
            sendbuf="<retcode>-1</retcode><message>用户名或密码不正确。</message>\n";
            return;
        }

        char clienip[INET_ADDRSTRLEN];
        // 2）判断客户连上来的地址是否在绑定ip地址的列表中。(可选)
        if (ip.empty()==false)
        {
            struct sockaddr_in clienaddr;
            socklen_t addr_len = sizeof(clienaddr);

            // 获取对端地址
            if (getpeername(cliensock, (struct sockaddr*)&clienaddr, &addr_len) == -1)
            {
                sendbuf="<retcode>-1</retcode><message>内部错误。</message>\n";
                return;
             }

            // 将 IP 地址从二进制转换为字符串
            inet_ntop(AF_INET, &(clienaddr.sin_addr), clienip, INET_ADDRSTRLEN);

            if(ip.find(clienip) == string::npos)
            {
                sendbuf="<retcode>-1</retcode><message>不在允许ip列表内。</message>\n";
                return;
            }
        }
        
       // 3）判断用户是否有访问接口的权限。
        stmt.prepare("select count(*) from T_USERANDINTER "
                              "where username=:1 and intername=:2 and intername in (select intername from T_INTERCFG where rsts=1)");
        stmt.bindin(1,username);
        stmt.bindin(2,intername);
        int icount=0;
        stmt.bindout(1,icount);
        stmt.execute();
        stmt.next();
        if (icount==0)
        {
            sendbuf="<retcode>-1</retcode><message>用户无权限，或接口不存在。</message>\n";
            return;
        }

        // 4）根据接口名，获取接口的配置参数。
        // 从接口参数配置表T_INTERCFG中加载接口参数。
        string selectsql,colstr,bindin; 
        stmt.prepare("select selectsql,colstr,bindin from T_INTERCFG where intername=:1");
        stmt.bindin(1,intername);           // 接口名。
        stmt.bindout(1,selectsql,1000);  // 接口SQL。
        stmt.bindout(2,colstr,300);         // 输出列名。
        stmt.bindout(3,bindin,300);       // 接口参数。
        stmt.execute();                           // 这里基本上不用判断返回值，出错的可能几乎没有。
        if (stmt.next()!=0)
        {
            sendbuf="<retcode>-1</retcode><message>内部错误。</message>\n";
            return;
        }

        // http://192.168.182.124:8080?username=ty&passwd=typwd&intername=getzhobtmind3&
        // obtid=59287&begintime=20211024094318&endtime=20211024113920
        // SQL语句：   select obtid,to_char(ddatetime,'yyyymmddhh24miss'),t,p,u,wd,wf,r,vis from T_ZHOBTMIND 
        //                     where obtid=:1 and ddatetime>=to_date(:2,'yyyymmddhh24miss') and ddatetime<=to_date(:3,'yyyymmddhh24miss')
        // colstr字段： obtid,ddatetime,t,p,u,wd,wf,r,vis
        // bindin字段：obtid,begintime,endtime

        // 5）准备查询数据的SQL语句。
        stmt.prepare(selectsql);

        //////////////////////////////////////////////////
        // 根据接口配置中的参数列表（bindin字段），从请求报文中解析出参数的值，绑定到查询数据的SQL语句中。
        // 拆分输入参数bindin。
        ccmdstr cmdstr;
        cmdstr.splittocmd(bindin,",");

        // 声明用于存放输入参数的数组。
        vector<string> invalue;
        invalue.resize(cmdstr.size());

        // 从http的GET请求报文中解析出输入参数，绑定到sql中。
        for (int i=0;i<cmdstr.size();i++)
        {
            getvalue(recvbuf,cmdstr[i].c_str(),invalue[i]);
            stmt.bindin(i+1,invalue[i]);
        }
        //////////////////////////////////////////////////

        //////////////////////////////////////////////////
        // 绑定查询数据的SQL语句的输出变量。
        // 拆分colstr，可以得到结果集的字段数。
        cmdstr.splittocmd(colstr,",");

        // 用于存放结果集的数组。
        vector<string> colvalue;
        colvalue.resize(cmdstr.size());

        // 把结果集绑定到colvalue数组。
        for (int i=0;i<cmdstr.size();i++)
            stmt.bindout(i+1,colvalue[i]);
        //////////////////////////////////////////////////

        if (stmt.execute() != 0)
        {
            logfile.write("stmt.execute() failed.\n%s\n%s\n",stmt.sql(),stmt.message()); 
            sformat(sendbuf,"<retcode>%d</retcode><message>%s</message>\n",stmt.rc(),stmt.message());
            return;
        }

        sendbuf="<retcode>0</retcode><message>ok</message>\n";
        sendbuf=sendbuf+"<data>\n";           // xml内容开始的标签<data>。

        //////////////////////////////////////////////////
        // 获取结果集，每获取一条记录，拼接xml。
        while (true)
        {
            if (stmt.next() != 0) break;            // 从结果集中取一条记录。

            // 拼接每个字段的xml。
            for (int i=0;i<cmdstr.size();i++)
                sendbuf=sendbuf+sformat("<%s>%s</%s>",cmdstr[i].c_str(),colvalue[i].c_str(),cmdstr[i].c_str());

            sendbuf=sendbuf+"<endl/>\n";    // 每行结束的标志。

        }       
        //////////////////////////////////////////////////

        sendbuf=sendbuf+"</data>\n";        // xml内容结尾的标签</data>。

        logfile.write("intername=%s,count=%d\n",intername.c_str(),stmt.rpc());

        // 写接口调用日志表T_USERLOG。
        string rpc = to_string(stmt.rpc());
        stmt.prepare("insert into T_USERLOG values (SEQ_USERLOG.nextval, :1, :2, sysdate, :3, :4)");
        stmt.bindin(1,username);
        stmt.bindin(2,intername);
        stmt.bindin(3,clienip,INET_ADDRSTRLEN);
        stmt.bindin(4,rpc);
        if (stmt.execute() != 0)
        {
            logfile.write("stmt.execute() failed.\n%s\n%s\n",stmt.sql(),stmt.message()); 
            sformat(sendbuf,"<retcode>%d</retcode><message>%s</message>\n",stmt.rc(),stmt.message());
            return;
        }
        conn.commit();
    }

    // 把客户端的socket和响应报文放入发送队列，sock-客户端的socket，message-客户端的响应报文。
    void insq(int sock,string &message)              
    {
        {
            shared_ptr<st_recvmesg> ptr=make_shared<st_recvmesg>(sock,message);

            lock_guard<mutex> lock(m_mutex_sq);   // 申请加锁。

            m_sq.push(ptr);
        }

        write(m_sendpipe[1],(char*)"o",1);   // 通知发送线程处理发送队列中的数据，属于边缘触发。
    }

    // 发送线程主函数，把发送队列中的数据发送给客户端。
    void sendfunc()           
    {
        int epollfd=epoll_create1(0);
        struct epoll_event ev;  // 声明事件的数据结构。

        // 把发送队列的管道加入epoll。
        ev.data.fd = m_sendpipe[0];
        ev.events = EPOLLIN;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,ev.data.fd,&ev); 

        struct epoll_event evs[10];      // 存放epoll返回的事件。

        while (true)     // 事件循环。
        {
            // 等待监视的socket有事件发生。
            int infds=epoll_wait(epollfd,evs,10,-1);

            // 返回失败。
            if (infds < 0) { logfile.write("发送线程：epoll() failed。\n"); return; }

            // 遍历epoll返回的已发生事件的数组evs。
            for (int i=0;i<infds;i++)
            {
                logfile.write("发送线程：已发生事件的fd=%d(%d)\n",evs[i].data.fd,evs[i].events);

                ////////////////////////////////////////////////////////
                // 如果发生事件的是管道，表示发送队列中有报文需要发送。
                if (evs[i].data.fd==m_sendpipe[0])
                {
                    if (m_exit==true) 
                    {
                        logfile.write("发送线程：即将退出。\n");
                        return;
                    }

                    char cc;
                    read(m_sendpipe[0], &cc, 1);    // 读取管道中的数据，不关心其内容。

                    shared_ptr<st_recvmesg> ptr;

                    lock_guard<mutex> lock(m_mutex_sq);   // 申请加锁。

                    while (m_sq.empty()==false)
                    {
                        ptr=m_sq.front(); m_sq.pop();   // 出队一个元素（报文）。

                        // 把出队的报文保存到socket的发送缓冲区中。
                        clientmap[ptr->sock].sendbuffer.append(ptr->message);
                        
                        // 关注客户端socket的写事件。
                        ev.data.fd=ptr->sock;
                        ev.events=EPOLLOUT;
                        epoll_ctl(epollfd,EPOLL_CTL_ADD,ev.data.fd,&ev);
                    }

                    continue;
                }
                ////////////////////////////////////////////////////////

                ////////////////////////////////////////////////////////
                 // 判断客户端的socket是否有写事件（发送缓冲区没有满）。
                if (evs[i].events & EPOLLOUT)
                {
                    // 把响应报文发送给客户端。
                    int writen=send(evs[i].data.fd,clientmap[evs[i].data.fd].sendbuffer.data(),clientmap[evs[i].data.fd].sendbuffer.length(),0);

                    logfile.write("发送线程：向%d发送了%d字节。\n",evs[i].data.fd,writen);

                    // 删除socket缓冲区中已成功发送的数据。
                    clientmap[evs[i].data.fd].sendbuffer.erase(0,writen);

                    // 如果socket缓冲区中没有数据了，不再关心socket的写件事。
                    if (clientmap[evs[i].data.fd].sendbuffer.length()==0)
                    {
                        ev.data.fd=evs[i].data.fd;
                        epoll_ctl(epollfd,EPOLL_CTL_DEL,ev.data.fd,&ev);
                    }
                }
                ////////////////////////////////////////////////////////
            }
        }
    }
} webserver;

int main(int argc,char *argv[])
{
    if (argc != 3)
    {
        printf("\n");
        printf("Using :./webserver logfile port\n\n");
        printf("Sample:./webserver /log/idc/webserver.log 5088\n\n");
        printf("        /project/tools/bin/procctl 5 /project/tools/bin/webserver /log/idc/webserver.log 5088\n\n");
        printf("基于HTTP协议的数据访问接口模块。\n");
        printf("logfile 本程序运行的日是志文件。\n");
        printf("port    服务端口，例如：80、8080。\n");

        return -1;
    }

    // 关闭全部的信号和输入输出。
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程。
    // 但请不要用 "kill -9 +进程号" 强行终止。
    closeioandsignal();  signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    // 打开日志文件。
    if (logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败（%s）。\n",argv[1]); return -1;
    }

    thread rt(&WebServer::recvfunc, &webserver,atoi(argv[2]));          // 创建接收线程。

    // 创建工作线程
    for(int i=0;i<THREAD_CNT;i++)
    {
        wthreads_pool.emplace_back(&WebServer::workfunc, &webserver,1);
    }

    thread st(&WebServer::sendfunc, &webserver);                        // 创建发送线程。

    logfile.write("已启动全部的线程。\n");

    while (true)
    {
        sleep(30);
    }

    return 0;
}

void EXIT(int sig)
{
    logfile.write("程序退出，sig=%d。\n\n",sig);

    write(webserver.m_recvpipe[1],(char*)"o",1);   // 通知接收线程退出。

    sleep(1);    // 让线程们有足够的时间退出。

    exit(0);
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
        logfile.write("listen(%d) failed.\n",port); close(sock); return -1;
    }

    fcntl(sock,F_SETFL,fcntl(sock,F_GETFD,0)|O_NONBLOCK);  // 把socket设置为非阻塞。

    return sock;
}

bool getvalue(const string &strget,const string &name,string &value)
{
    // http://192.168.182.124:8080/api?username=wucz&passwd=wuczpwd
    // GET /api?username=wucz&passwd=wuczpwd HTTP/1.1
    // Host: 192.168.182.124:8080
    // Connection: keep-alive
    // Upgrade-Insecure-Requests: 1
    // .......

    int startp=strget.find(name);                                          // 在请求行中查找参数名的位置。

    if (startp==string::npos) return false; 

    int endp=strget.find("&",startp);                                    // 从参数名的位置开始，查找&符号。
    if (endp==string::npos) endp=strget.find(" ",startp);     // 如果是最后一个参数，没有找到&符号，那就查找空格。

    if (endp==string::npos) return false;

    // 从请求行中截取参数的值。
    value=strget.substr(startp+(name.length()+1),endp-startp-(name.length()+1));

    return true;
}
