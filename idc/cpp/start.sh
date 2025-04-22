#此脚本用于启动数据共享平台全部的服务程序

# 启动守护模块
/project/tools/bin/procctl 10 /project/tools/bin/checkproc /tmp/log/checkproc.log

## 生成测试数据 ##
# 生成气象站点观测数据，每分钟运行一次
/project/tools/bin/procctl 60 /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log csv,xml,json

# 定期(300秒)清理旧的(0.02天之前的)气象观测数据
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/idc/surfdata "*" 0.02

# 定期压缩旧的服务程序备份日志
/project/tools/bin/procctl 300 /project/tools/bin/gzipfiles /log/idc "*.log.20*" 0.02

# 从/tmp/idc/surfdata目录下载原始的气象观测数据文件，存放到/idcdata/surfdata目录
/project/tools/bin/procctl 30 /project/tools/bin/ftpgetfiles /log/idc/ftpgetfiles_surfdata.log "<host>127.0.0.1:21</host><mode>1</mode><username>mysql</username><password>zqMYSQL20030313</password><localpath>/idcdata/surfdata</localpath><remotepath>/tmp/idc/surfdata</remotepath><matchname>SURF_ZH*.XML,SURF_ZH*.CSV</matchname><listfilename>/idcdata/ftplist/ftpgetfiles_surfdata.list</listfilename><ptype>1</ptype><okfilename>/idcdata/ftplist/ftpgetfiles_surfdata.xml</okfilename><checkmtime>true</checkmtime><timeout>80</timeout><pname>ftpgetfiles_surfdata</pname>"

# 定期清理/idcdata/surfdata目录中0.04天之前的文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /idcdata/surfdata "*" 0.04

# 把/tmp/idc/surfdata目录的原始气象观测数据文件上传到/tmp/ftpputest目录。
# 注意，先创建好服务端的目录：mkdir /tmp/ftpputest 
/project/tools/bin/procctl 30 /project/tools/bin/ftpputfiles /log/idc/ftpputfiles_surfdata.log "<host>127.0.0.1:21</host><mode>1</mode><username>mysql</username><password>zqMYSQL20030313</password><localpath>/tmp/idc/surfdata</localpath><remotepath>/tmp/ftpputest</remotepath><matchname>SURF_ZH*.JSON</matchname><ptype>1</ptype><okfilename>/idcdata/ftplist/ftpputfiles_surfdata.xml</okfilename><timeout>80</timeout><pname>ftpputfiles_surfdata</pname>"

#定期清理/tmp/ftpputest目录中0.04天之前的文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/ftpputest "*" 0.04

# 文件传输的服务端程序。
/project/tools/bin/procctl 10 /project/tools/bin/fileserver 5005 /log/idc/fileserver.log

# 把目录/tmp/ftpputest中的文件上传到/tmp/tcpputest目录中。
# 注意，先创建好服务端的目录：mkdir /tmp/tcpputest 
/project/tools/bin/procctl 20 /project/tools/bin/tcpputfiles /log/idc/tcpputfiles_surfdata.log "<ip>127.0.0.1</ip><port>5005</port><ptype>1</ptype><clientpath>/tmp/ftpputest</clientpath><andchild>true</andchild><matchname>*.XML,*.CSV,*.JSON</matchname><srvpath>/tmp/tcpputest</srvpath><timetvl>10</timetvl><timeout>50</timeout><pname>tcpputfiles_surfdata</pname>"

# 把目录/tmp/tcpputest中的文件下载到/tmp/tcpgetest目录中。
/project/tools/bin/procctl 20 /project/tools/bin/tcpgetfiles /log/idc/tcpgetfiles_surfdata.log "<ip>127.0.0.1</ip><port>5005</port><ptype>1</ptype><srvpath>/tmp/tcpputest</srvpath><andchild>true</andchild><matchname>*.XML,*.CSV,*.JSON</matchname><clientpath>/tmp/tcpgetest</clientpath><timetvl>10</timetvl><timeout>50</timeout><pname>tcpgetfiles_surfdata</pname>"

# 清理/tmp/tcpgetest目录中的历史数据文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/tcpgetest "*" 0.02

# 把站点参数数据入库到T_ZHOBTCODE表中，如果站点不存在则插入，站点已存在则更新。
/project/tools/bin/procctl 120 /project/idc/bin/obtcodetodb /project/idc/ini/stcode.ini "idc/idcpwd@snorcl11g_124" "Simplified Chinese_China.AL32UTF8" /log/idc/obtcodetodb.log

# 把/idcdata/surfdata目录中的气象观测数据文件入库到T_ZHOBTMIND表中。
/project/tools/bin/procctl 10 /project/idc/bin/obtmindtodb /idcdata/surfdata "idc/idcpwd@snorcl11g_124" "Simplified Chinese_China.AL32UTF8" /log/idc/obtmindtodb.log
##

## 数据抽取 ##
# 每隔1小时把T_ZHOBTCODE表中全部的数据抽取出来。
/project/tools/bin/procctl 3600 /project/tools/bin/dminingoracle /log/idc/dminingoracle_ZHOBTCODE.log "<connstr>idc/idcpwd@snorcl11g_124</connstr><charset>Simplified Chinese_China.AL32UTF8</charset><selectsql>select obtid,cityname,provname,lat,lon,height from T_ZHOBTCODE</selectsql><fieldstr>obtid,cityname,provname,lat,lon,height</fieldstr><fieldlen>10,30,30,10,10,10</fieldlen><bfilename>ZHOBTCODE</bfilename><efilename>toidc</efilename><outpath>/idcdata/dmindata</outpath><timeout>30</timeout><pname>dminingoracle_ZHOBTCODE</pname>"

# 每30秒从T_ZHOBTMIND表中增量抽取数据。
/project/tools/bin/procctl 30 /project/tools/bin/dminingoracle /log/idc/dminingoracle_ZHOBTMIND.log "<connstr>idc/idcpwd@snorcl11g_124</connstr><charset>Simplified Chinese_China.AL32UTF8</charset><selectsql>select obtid,to_char(ddatetime,'yyyymmddhh24miss'),t,p,u,wd,wf,r,vis,keyid from T_ZHOBTMIND where keyid>:1 and obtid like '5%%'</selectsql><fieldstr>obtid,ddatetime,t,p,u,wd,wf,r,vis,keyid</fieldstr><fieldlen>5,19,8,8,8,8,8,8,8,15</fieldlen><bfilename>ZHOBTMIND</bfilename><efilename>togxpt</efilename><outpath>/idcdata/dmindata</outpath><starttime></starttime><incfield>keyid</incfield><incfilename>/idcdata/dmining/dminingoracle_ZHOBTMIND_togxpt.keyid</incfilename><timeout>30</timeout><pname>dminingoracle_ZHOBTMIND_togxpt</pname><maxcount>1000</maxcount><connstr1>scott/zq2003@snorcl11g_124</connstr1>"

# 清理/idcdata/dmindata目录中文件，防止把空间撑满。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /idcdata/dmindata "*" 0.02
##

## 文件上传 ##
# 把/idcdata/dmindata目录中的xml文件发送到/idcdata/xmltodb/vip，交给入库程序。
/project/tools/bin/procctl 20 /project/tools/bin/tcpputfiles /log/idc/tcpputfiles_togxpt.log "<ip>127.0.0.1</ip><port>5005</port><ptype>1</ptype><clientpath>/idcdata/dmindata</clientpath><srvpath>/idcdata/xmltodb/vip</srvpath><andchild>true</andchild><matchname>*.XML</matchname><timetvl>10</timetvl><timeout>50</timeout><pname>tcpputfiles_togxpt</pname>"
##

## 数据入库 ##
# 把/idcdata/xmltodb/vip目录中的xml文件入库到T_ZHOBTCODE1和T_ZHOBTMIND1。
/project/tools/bin/procctl 10 /project/tools/bin/xmltodb /log/idc/xmltodb_vip.log "<connstr>idc/idcpwd@snorcl11g_124</connstr><charset>Simplified Chinese_China.AL32UTF8</charset><inifilename>/project/idc/ini/xmltodb.xml</inifilename><xmlpath>/idcdata/xmltodb/vip</xmlpath><xmlpathbak>/idcdata/xmltodb/vipbak</xmlpathbak><xmlpatherr>/idcdata/xmltodb/viperr</xmlpatherr><timetvl>5</timetvl><timeout>50</timeout><pname>xmltodb_vip</pname>"

# 清理/idcdata/xmltodb/vipbak和/idcdata/xmltodb/viperr目录中文件。
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /idcdata/xmltodb/vipbak "*" 0.02
/project/tools/bin/procctl 300 /project/tools/bin/deletefiles /idcdata/xmltodb/viperr  "*" 0.02

# 清理T_ZHOBTMIND表中1天之前的数据。
/project/tools/bin/procctl 3600 /project/tools/bin/deletetable /log/idc/deletetable_ZHOBTMIND.log "<connstr>idc/idcpwd@snorcl11g_124</connstr><tname>T_ZHOBTMIND</tname><keycol>rowid</keycol><where>where ddatetime<sysdate-1</where><maxcount>100</maxcount><timeout>120</timeout><pname>deletetable_ZHOBTMIND</pname>"

# 把T_ZHOBTMIND1表中0.5天之前的数据迁移到T_ZHOBTMIND1_HIS表
/project/tools/bin/procctl 3600 /project/tools/bin/migratetable /log/idc/migratetable_ZHOBTMIND1.log "<connstr>idc/idcpwd@snorcl11g_124</connstr><tname>T_ZHOBTMIND1</tname><totname>T_ZHOBTMIND1_HIS</totname><keycol>rowid</keycol><where>where ddatetime<sysdate-0.5</where><maxcount>100</maxcount></starttime><timeout>120</timeout><pname>migratetable_ZHOBTMIND1</pname>"

# 清理T_ZHOBTMIND1_HIS表中1天之前的数据。
/project/tools/bin/procctl 3600 /project/tools/bin/deletetable /log/idc/deletetable_ZHOBTMIND1_HIS.log "<connstr>idc/idcpwd@snorcl11g_124</connstr><tname>T_ZHOBTMIND1_HIS</tname><keycol>rowid</keycol><where>where ddatetime<sysdate-1</where><maxcount>100</maxcount><timeout>120</timeout><pname>deletetable_ZHOBTMIND1_HIS</pname>"
##

## 数据同步 ##
# 不分批同步，把T_ZHOBTCODE1@db124同步到T_ZHOBTCODE2@db128
/project/tools/bin/procctl 10 /project/tools/bin/syncref /log/idc/syncref_ZHOBTCODE2.log "<localconnstr>idc/idcpwd@snorcl11g_128</localconnstr><charset>Simplified Chinese_China.AL32UTF8</charset><linktname>T_ZHOBTCODE1@db124</linktname><localtname>T_ZHOBTCODE2</localtname><remotecols>obtid,cityname,provname,lat,lon,height,upttime,keyid</remotecols><localcols>stid,cityname,provname,lat,lon,height,upttime,recid</localcols><rwhere>where obtid like '57%%'</rwhere><lwhere>where stid like '57%%'</lwhere><synctype>1</synctype><timeout>50</timeout><pname>syncref_ZHOBTCODE2</pname>"

# 分批同步，把T_ZHOBTCODE1@db124同步到T_ZHOBTCODE3@db128
/project/tools/bin/procctl 10 /project/tools/bin/syncref /log/idc/syncref_ZHOBTCODE3.log "<localconnstr>idc/idcpwd@snorcl11g_128</localconnstr><charset>Simplified Chinese_China.AL32UTF8</charset><linktname>T_ZHOBTCODE1@db124</linktname><localtname>T_ZHOBTCODE3</localtname><remotecols>obtid,cityname,provname,lat,lon,height,upttime,keyid</remotecols><localcols>stid,cityname,provname,lat,lon,height,upttime,recid</localcols><rwhere>where obtid like '57%%'</rwhere><synctype>2</synctype><remoteconnstr>idc/idcpwd@snorcl11g_124</remoteconnstr><remotetname>T_ZHOBTCODE1</remotetname><remotekeycol>obtid</remotekeycol><localkeycol>stid</localkeycol><keylen>5</keylen><maxcount>10</maxcount><timeout>50</timeout><pname>syncref_ZHOBTCODE3</pname>"

# 把T_ZHOBTMIND1@db124表中全部的记录增量同步到T_ZHOBTMIND2中。
/project/tools/bin/procctl 10 /project/tools/bin/syncinc /log/idc/syncinc_ZHOBTMIND2.log "<localconnstr>idc/idcpwd@snorcl11g_128</localconnstr><remoteconnstr>idc/idcpwd@snorcl11g_124</remoteconnstr><charset>Simplified Chinese_China.AL32UTF8</charset><remotetname>T_ZHOBTMIND1</remotetname><lnktname>T_ZHOBTMIND1@db124</lnktname><localtname>T_ZHOBTMIND2</localtname><remotecols>obtid,ddatetime,t,p,u,wd,wf,r,vis,upttime,keyid</remotecols><localcols>stid,ddatetime,t,p,u,wd,wf,r,vis,upttime,recid</localcols><remotekeycol>keyid</remotekeycol><localkeycol>recid</localkeycol><maxcount>300</maxcount><timetvl>2</timetvl><timeout>50</timeout><pname>syncinc_ZHOBTMIND2</pname>"

# 把T_ZHOBTMIND1@db124表中满足"and obtid like '57%'"的记录增量同步到T_ZHOBTMIND3中。
/project/tools/bin/procctl 10 /project/tools/bin/syncinc /log/idc/syncinc_ZHOBTMIND3.log "<localconnstr>idc/idcpwd@snorcl11g_128</localconnstr><remoteconnstr>idc/idcpwd@snorcl11g_124</remoteconnstr><charset>Simplified Chinese_China.AL32UTF8</charset><remotetname>T_ZHOBTMIND1</remotetname><lnktname>T_ZHOBTMIND1@db124</lnktname><localtname>T_ZHOBTMIND3</localtname><remotecols>obtid,ddatetime,t,p,u,wd,wf,r,vis,upttime,keyid</remotecols><localcols>stid,ddatetime,t,p,u,wd,wf,r,vis,upttime,recid</localcols><where>and obtid like '54%%'</where><remotekeycol>keyid</remotekeycol><localkeycol>recid</localkeycol><maxcount>300</maxcount><timetvl>2</timetvl><timeout>30</timeout><pname>syncinc_ZHOBTMIND3</pname>"
##

## HTTP访问接口
# 启动WebServer
/project/tools/bin/procctl 5 /project/tools/bin/webserver /log/idc/webserver.log "<port>8080</port><connstr>idc/idcpwd@snorcl11g_124</connstr><charset>Simplified Chinese_China.AL32UTF8</charset><timeout>50</timeout><pname>webserver</pname>"
##
