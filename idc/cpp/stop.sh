#此脚本用于停止数据共享平台全部的服务程序

#停止调度程序
killall -9 procctl

#停止其他的服务程序
#尝试让其他服务程序正常终止
killall crtsurfdata deletefiles gzipfiles ftpgetfiles ftpputfiles
killall fileserver tcpputfiles tcpgetfiles obtcodetodb obtmindtodb
killall dminingoracle

#让其他服务程序有足够时间退出
sleep 5

#不管服务程序有没有退出，都强制杀死
killall -9 crtsurfdata deletefiles gzipfiles ftpgetfiles ftpputfiles
killall -9 fileserver tcpputfiles tcpgetfiles obtcodetodb obtmindtodb
killall -9 dminingoracle