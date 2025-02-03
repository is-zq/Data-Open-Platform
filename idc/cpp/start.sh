#此脚本用于启动数据共享平台全部的服务程序

#启动守护模块，建议在/etc/rc.local中配置，以超级用户的身份启动
/project/tools/bin/procctl 10 /project/tools/bin/checkproc /tmp/log/checkproc.log

#生成气象站点观测数据，每分钟运行一次
/project/tools/bin/procctl 60 /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log csv,xml,json