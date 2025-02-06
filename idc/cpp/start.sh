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