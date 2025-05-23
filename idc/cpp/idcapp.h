#ifndef IDCAPP_H
#define IDCAPP_H

#include "_public.h"
#include "_ooci.h"
using namespace idc;

// 全国气象观测数据操作类。
class CZHOBTMIND
{
private:
    struct st_zhobtmind
    {
        char obtid[6];            // 站点代码。
        char ddatetime[21];  // 数据时间，精确到分钟。
        char t[11];                 // 温度，单位：0.1摄氏度。
        char p[11];                // 气压，单位：0.1百帕。
        char u[11];                // 相对湿度，0-100之间的值。
        char wd[11];             // 风向，0-360之间的值。
        char wf[11];              // 风速：单位0.1m/s。
        char r[11];                // 降雨量：0.1mm。
        char vis[11];             // 能见度：0.1米。
    };

    connection  &m_conn;     // 数据库连接。
    clogfile    &m_logfile;       // 日志文件。

    sqlstatement m_stmt;       // 插入表操作的sql。

    string m_buffer;                // 从文件中读到的一行。

    st_zhobtmind m_zhobtmind;   // 全国气象观测数据结构体变量。
public:
    CZHOBTMIND(connection &conn,clogfile &logfile):m_conn(conn),m_logfile(logfile) {}

    ~CZHOBTMIND() {}

    //  把从文件读到的一行数据拆分到m_zhobtmind结构体中。
	bool splitbuffer(const string &strbuffer,const bool bisxml);

    // 把m_zhobtmind结构体中的数据插入到T_ZHOBTMIND表中。
    bool inserttable();  
};

#endif