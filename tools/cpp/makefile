# 开发框架头文件路径
PUBINCL = -I/project/public

# 开发框架源文件，这里直接包含cpp而不用链接库是为了方便调试
PUBCPP = /project/public/_public.cpp

##################################################
# oracle头文件路径
ORAINCL = -I$(ORACLE_HOME)/rdbms/public -I/project/public/db/oracle

# oracle库文件路径
ORALIB =  -L$(ORACLE_HOME)/lib -L.

# oracle的oci库
ORALIBS = -lclntsh

# 开发框架oracle的cpp文件名，这里直接包含进来，没有采用链接库，是为了方便调试。
ORACPP = /project/public/db/oracle/_ooci.cpp
##################################################

# 调试参数
CFLAGS = -g
#CFLAGS = -O2

all:procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles fileserver tcpgetfiles tcpputfiles\
	dminingoracle xmltodb deletetable migratetable

procctl:procctl.cpp
	g++ $(CFLAGS) -o procctl procctl.cpp
	cp -f procctl ../bin/

checkproc:checkproc.cpp
	g++ $(CFLAGS) -o checkproc checkproc.cpp $(PUBINCL) $(PUBCPP)
	cp -f checkproc ../bin/

deletefiles:deletefiles.cpp
	g++ $(CFLAGS) -o deletefiles deletefiles.cpp $(PUBINCL) $(PUBCPP)
	cp -f deletefiles ../bin/

gzipfiles:gzipfiles.cpp
	g++ $(CFLAGS) -o gzipfiles gzipfiles.cpp $(PUBINCL) $(PUBCPP)
	cp -f gzipfiles ../bin/

ftpgetfiles:ftpgetfiles.cpp
	g++ $(CFLAGS) -o ftpgetfiles ftpgetfiles.cpp $(PUBINCL) $(PUBCPP) /project/public/libftp.a /project/public/_ftp.cpp
	cp -f ftpgetfiles ../bin/

ftpputfiles:ftpputfiles.cpp
	g++ $(CFLAGS) -o ftpputfiles ftpputfiles.cpp $(PUBINCL) $(PUBCPP) /project/public/libftp.a /project/public/_ftp.cpp
	cp -f ftpputfiles ../bin/

fileserver:fileserver.cpp
	g++ $(CFLAGS) -o fileserver fileserver.cpp $(PUBINCL) $(PUBCPP)
	cp -f fileserver ../bin/

tcpgetfiles:tcpgetfiles.cpp
	g++ $(CFLAGS) -o tcpgetfiles tcpgetfiles.cpp $(PUBINCL) $(PUBCPP)
	cp -f tcpgetfiles ../bin/

tcpputfiles:tcpputfiles.cpp
	g++ $(CFLAGS) -o tcpputfiles tcpputfiles.cpp $(PUBINCL) $(PUBCPP)
	cp -f tcpputfiles ../bin/

dminingoracle:dminingoracle.cpp
	g++ $(CFLAGS) -o dminingoracle dminingoracle.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP)
	cp -f dminingoracle ../bin/.

xmltodb:xmltodb.cpp _tools.cpp
	g++ $(CFLAGS) -o xmltodb xmltodb.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) _tools.cpp
	cp -f xmltodb ../bin/.

deletetable:deletetable.cpp
	g++ $(CFLAGS) -o deletetable deletetable.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP)
	cp -f deletetable ../bin/.

migratetable:migratetable.cpp _tools.cpp
	g++ $(CFLAGS) -o migratetable migratetable.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) _tools.cpp
	cp -f migratetable ../bin/.

clean:
	rm -rf procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles fileserver tcpgetfiles tcpputfiles
	rm -rf dminingoracle xmltodb deletetable migratetable