//守护程序：检查共享内存中进程的心跳，如果超时，则终止进程。
#include "_public.h"
using namespace idc;

int main(int argc,char *argv[])
{
    if(argc != 2)
    {
        printf("Using: ./checkproc logfilename\n");

        printf("Example: /project/tools/bin/procctl 10 /project/tools/bin/checkproc /tmp/log/checkproc.log\n\n");

        printf("本程序用于检查后台服务程序是否超时，若超时则终止它。\n");
        printf("注意：\n");
        printf(" 1）本程序由procctl启动，运行周期建议为10秒。\n");
        printf(" 2）为避免被普通用户误杀，本程序建议用root用户启动。\n");
        printf(" 3）如果要停止本程序，只能用killall -9 终止。\n\n\n");

        return -1;
    }
    closeioandsignal(true);

    clogfile logfile;
    if(logfile.open(argv[1]) == false)
    {
        printf("logfile.open(%s) failed\n",argv[1]);
        return -1;
    }

    int shmid = 0;
    //创建/获取共享内存，键值为SHMKEYP，大小为MAXNUMP个st_procinfo的大小。
    if ( (shmid = shmget((key_t)SHMKEYP, MAXNUMP*sizeof(struct st_procinfo), 0666|IPC_CREAT)) == -1)
    { 
        logfile.write("创建/获取共享内存(%x)失败。\n",SHMKEYP); 
        return -1; 
    }

    //将共享内存连接到当前进程的地址空间。
    st_procinfo *shm = reinterpret_cast<st_procinfo*>(shmat(shmid,0,0));

    //遍历共享内存中全部的记录，如果进程已超时，终止它。
    for(int i=0;i<MAXNUMP;i++)
    {
        //空记录
        if(shm[i].pid == 0) continue;

        //一定要把shm[i]备份出来，因为可能在中途被memset
        st_procinfo tmp = shm[i];

        //发送信号0检查进程是否已经退出
        if(kill(tmp.pid,0) == -1)//已退出
        {
            logfile.write("进程pid=%d(%s)已经不存在。\n",tmp.pid,tmp.pname);
            memset(&shm[i],0,sizeof(st_procinfo));
            continue;
        }

        //未超时
        if(time(0) - tmp.atime < tmp.timeout) continue;   

        //超时，发送信号15终止进程
        logfile.write("进程pid=%d(%s)已超时。\n",tmp.pid,tmp.pname);
        kill(tmp.pid,15);

        int iret = 0;
        //进程可能不处理信号15，每秒检查一下是否成功退出，最多5秒
        for(int j=0;j<5;j++)
        {
            sleep(1);
            iret = kill(tmp.pid,0);
            //成功退出
            if(iret == -1) break;
        }

        //5秒后还没退出
        if(iret != -1)
        {
            kill(tmp.pid,9);
            logfile.write("进程pid=%d(%s)已经强制终止。\n",tmp.pid,tmp.pname);
            memset(&shm[i],0,sizeof(st_procinfo));
        }
        else
            logfile.write("进程pid=%d(%s)已经正常终止。\n",tmp.pid,tmp.pname);
    }

    //把共享内存从当前进程中分离。
    shmdt(shm);

    return 0;
}
