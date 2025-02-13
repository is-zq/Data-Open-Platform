#此脚本用于启动数据共享平台全部的服务程序

#启动守护模块，建议在/etc/rc.local中配置，以超级用户的身份启动
/project/tools/bin/procctl 10 /project/tools/bin/checkproc /tmp/log/checkproc.log

#生成气象站点观测数据，每分钟运行一次
/project/tools/bin/procctl 60 /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log csv,xml,json

#定期(300秒)清理旧的(0.02天之前的)气象观测数据
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/idc/surfdata "*" 0.02

#定期压缩旧的服务程序备份日志
/project/tools/bin/procctl 300 /project/tools/bin/gzipfiles /log/idc "*.log.20*" 0.02

#从/tmp/idc/surfdata目录下载原始的气象观测数据文件，存放到/idcdata/surfdata目录
/project/tools/bin/procctl 30 /project/tools/bin/ftpgetfiles /log/idc/ftpgetfiles_surfdata.log "<host>127.0.0.1:21</host><mode>1</mode><username>mysql</username><password>zqMYSQL20030313</password><localpath>/idcdata/surfdata</localpath><remotepath>/tmp/idc/surfdata</remotepath><matchname>SURF_ZH*.XML,SURF_ZH*.CSV</matchname><listfilename>/idcdata/ftplist/ftpgetfiles_surfdata.list</listfilename><ptype>1</ptype><okfilename>/idcdata/ftplist/ftpgetfiles_surfdata.xml</okfilename><checkmtime>true</checkmtime><timeout>80</timeout><pname>ftpgetfiles_surfdata</pname>"

#定期清理/idcdata/surfdata目录中0.04天之前的文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /idcdata/surfdata "*" 0.04

#把/tmp/idc/surfdata目录的原始气象观测数据文件上传到/tmp/ftpputest目录。
# 注意，先创建好服务端的目录：mkdir /tmp/ftpputest 
/project/tools/bin/procctl 30 /project/tools/bin/ftpputfiles /log/idc/ftpputfiles_surfdata.log "<host>127.0.0.1:21</host><mode>1</mode><username>mysql</username><password>zqMYSQL20030313</password><localpath>/tmp/idc/surfdata</localpath><remotepath>/tmp/ftpputest</remotepath><matchname>SURF_ZH*.JSON</matchname><ptype>1</ptype><okfilename>/idcdata/ftplist/ftpputfiles_surfdata.xml</okfilename><timeout>80</timeout><pname>ftpputfiles_surfdata</pname>"

#定期清理/tmp/ftpputest目录中0.04天之前的文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/ftpputest "*" 0.04

# 文件传输的服务端程序。
/project/tools/bin/procctl 10 /project/tools/bin/fileserver 5005 /log/idc/fileserver.log

# 把目录/tmp/ftpputest中的文件上传到/tmp/tcpputest目录中。
/project/tools/bin/procctl 20 /project/tools/bin/tcpputfiles /log/idc/tcpputfiles_surfdata.log "<ip>127.0.0.1</ip><port>5005</port><ptype>1</ptype><clientpath>/tmp/ftpputest</clientpath><andchild>true</andchild><matchname>*.XML,*.CSV,*.JSON</matchname><srvpath>/tmp/tcpputest</srvpath><timetvl>10</timetvl><timeout>50</timeout><pname>tcpputfiles_surfdata</pname>"

# 把目录/tmp/tcpputest中的文件下载到/tmp/tcpgetest目录中。
/project/tools/bin/procctl 20 /project/tools/bin/tcpgetfiles /log/idc/tcpgetfiles_surfdata.log "<ip>127.0.0.1</ip><port>5005</port><ptype>1</ptype><srvpath>/tmp/tcpputest</srvpath><andchild>true</andchild><matchname>*.XML,*.CSV,*.JSON</matchname><clientpath>/tmp/tcpgetest</clientpath><timetvl>10</timetvl><timeout>50</timeout><pname>tcpgetfiles_surfdata</pname>"

# 清理/tmp/tcpgetest目录中的历史数据文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/tcpgetest "*" 0.02

# 把站点参数数据入库到T_ZHOBTCODE表中，如果站点不存在则插入，站点已存在则更新。
/project/tools/bin/procctl 120 /project/idc/bin/obtcodetodb /project/idc/ini/stcode.ini "idc/idcpwd@snorcl11g_124" "Simplified Chinese_China.AL32UTF8" /log/idc/obtcodetodb.log

# 把/idcdata/surfdata目录中的气象观测数据文件入库到T_ZHOBTMIND表中。
/project/tools/bin/procctl 10 /project/idc/bin/obtmindtodb /idcdata/surfdata "idc/idcpwd@snorcl11g_124" "Simplified Chinese_China.AL32UTF8" /log/idc/obtmindtodb.log

# 执行/project/idc/sql/deletetable.sql脚本，删除T_ZHOBTMIND表两小时之前的数据，如果启用了数据清理程序deletetable，就不必启用这行脚本了。
/project/tools/bin/procctl 120 /oracle/home/bin/sqlplus idc/idcpwd@snorcl11g_128 @/project/idc/sql/deletetable.sql
