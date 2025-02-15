#include "_tools.h"

// 获取表全部的列和主键列信息的类。
ctcols::ctcols()
{
    initdata();
}

void ctcols::initdata()
{
    m_vallcols.clear();
    m_vpkcols.clear();
    m_allcols.clear();
    m_pkcols.clear();
}

bool ctcols::allcols(connection &conn,char *tablename)
{
    m_vallcols.clear();
    m_allcols.clear();

    struct st_columns stcolumns;

    sqlstatement stmt;
    stmt.connect(&conn);
    // 从USER_TAB_COLUMNS字典中获取表全部的字段，把结果集转换成小写，数据字典中的表名是大写。
    stmt.prepare("\
            select lower(column_name),lower(data_type),data_length from USER_TAB_COLUMNS\
            where table_name=upper(:1) order by column_id",tablename);
    stmt.bindin(1,tablename,30);
    stmt.bindout(1,stcolumns.colname,30);
    stmt.bindout(2,stcolumns.datatype,30);
    stmt.bindout(3,stcolumns.collen);

    if (stmt.execute()!=0) return false;

    while (true)
    {
        memset(&stcolumns,0,sizeof(struct st_columns));
  
        if (stmt.next()!=0) break;

        // 列的数据类型，分为char、date和number三大类。

        // 统一将字符串类型和rowid处理为char类型
        if (strcmp(stcolumns.datatype, "char") == 0 || 
            strcmp(stcolumns.datatype, "nchar") == 0 || 
            strcmp(stcolumns.datatype, "varchar2") == 0 || 
            strcmp(stcolumns.datatype, "nvarchar2") == 0 || 
            strcmp(stcolumns.datatype, "rowid") == 0)
        {
            strcpy(stcolumns.datatype, "char");
            if (strcmp(stcolumns.datatype, "rowid") == 0)
                stcolumns.collen = 18;
        }

        // 日期时间类型
        else if (strcmp(stcolumns.datatype, "date") == 0)
        {
            stcolumns.collen = 14;
        }

        // 数字类型
        else if (strcmp(stcolumns.datatype, "number") == 0 || 
                strcmp(stcolumns.datatype, "integer") == 0 || 
                strcmp(stcolumns.datatype, "float") == 0)
        {
            strcpy(stcolumns.datatype, "number");
            stcolumns.collen = 22;
        }

        // 其他类型跳过
        else
        {
            continue;
        }

        m_allcols = m_allcols + stcolumns.colname + ",";

        m_vallcols.push_back(stcolumns);
    }

    // 删除最后一个多余的逗号。
    if (stmt.rpc()>0) deleterchr(m_allcols,',');            // obtid,ddatetime,....,keyid

    return true;
}

// 获取指定表的主键字段信息。
bool ctcols::pkcols(connection &conn,char *tablename)
{
    m_vpkcols.clear();
    m_pkcols.clear();

    struct st_columns stcolumns;

    sqlstatement stmt;
    stmt.connect(&conn);
    // 从USER_CONS_COLUMNS和USER_CONSTRAINTS字典中获取表的主键字段，把结果集转换成小写，数据字典中的表名是大写。
    stmt.prepare("select lower(column_name),position from USER_CONS_COLUMNS\
         where table_name=upper(:1)\
           and constraint_name=(select constraint_name from USER_CONSTRAINTS\
                               where table_name=upper(:2) and constraint_type='P'\
                                 and generated='USER NAME')\
         order by position");
    stmt.bindin(1,tablename,30);
    stmt.bindin(2,tablename,30);
    stmt.bindout(1,stcolumns.colname,30);
    stmt.bindout(2,stcolumns.pkseq);

    if (stmt.execute() != 0) return false;

    while (true)
    {
        memset(&stcolumns,0,sizeof(struct st_columns));

        if (stmt.next() != 0) break;

        m_pkcols = m_pkcols + stcolumns.colname + ",";

        m_vpkcols.push_back(stcolumns);
    }

    if (stmt.rpc()>0) deleterchr(m_pkcols,',');    // 删除最后一个多余的逗号。

    // 如果列是主键的字段，存放主键字段的顺序，从1开始，不是主键取值0。
    for (auto &pkcol : m_vpkcols)
    {
        for (auto &col : m_vallcols)
        {
            if (strcmp(pkcol.colname,col.colname)==0)
            {
                col.pkseq = pkcol.pkseq;
                break;
            }
        }
    }
    
    return true;
}