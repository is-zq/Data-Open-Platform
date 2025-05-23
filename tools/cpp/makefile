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
#CFLAGS = -g
CFLAGS = -O2

all:procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles fileserver tcpgetfiles tcpputfiles\
	dminingoracle xmltodb deletetable migratetable syncref syncinc inetd rinetd rinetdin webserver

procctl:procctl.cpp
	g++ $(CFLAGS) -o procctl procctl.cpp
	mv -f procctl ../bin/

checkproc:checkproc.cpp
	g++ $(CFLAGS) -o checkproc checkproc.cpp $(PUBINCL) $(PUBCPP)
	mv -f checkproc ../bin/

deletefiles:deletefiles.cpp
	g++ $(CFLAGS) -o deletefiles deletefiles.cpp $(PUBINCL) $(PUBCPP)
	mv -f deletefiles ../bin/

gzipfiles:gzipfiles.cpp
	g++ $(CFLAGS) -o gzipfiles gzipfiles.cpp $(PUBINCL) $(PUBCPP)
	mv -f gzipfiles ../bin/

ftpgetfiles:ftpgetfiles.cpp
	g++ $(CFLAGS) -o ftpgetfiles ftpgetfiles.cpp $(PUBINCL) $(PUBCPP) /project/public/libftp.a /project/public/_ftp.cpp
	mv -f ftpgetfiles ../bin/

ftpputfiles:ftpputfiles.cpp
	g++ $(CFLAGS) -o ftpputfiles ftpputfiles.cpp $(PUBINCL) $(PUBCPP) /project/public/libftp.a /project/public/_ftp.cpp
	mv -f ftpputfiles ../bin/

fileserver:fileserver.cpp
	g++ $(CFLAGS) -o fileserver fileserver.cpp $(PUBINCL) $(PUBCPP)
	mv -f fileserver ../bin/

tcpgetfiles:tcpgetfiles.cpp
	g++ $(CFLAGS) -o tcpgetfiles tcpgetfiles.cpp $(PUBINCL) $(PUBCPP)
	mv -f tcpgetfiles ../bin/

tcpputfiles:tcpputfiles.cpp
	g++ $(CFLAGS) -o tcpputfiles tcpputfiles.cpp $(PUBINCL) $(PUBCPP)
	mv -f tcpputfiles ../bin/

dminingoracle:dminingoracle.cpp
	g++ $(CFLAGS) -o dminingoracle dminingoracle.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP)
	mv -f dminingoracle ../bin/.

xmltodb:xmltodb.cpp _tools.cpp
	g++ $(CFLAGS) -o xmltodb xmltodb.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) _tools.cpp
	mv -f xmltodb ../bin/.

deletetable:deletetable.cpp
	g++ $(CFLAGS) -o deletetable deletetable.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP)
	mv -f deletetable ../bin/.

migratetable:migratetable.cpp _tools.cpp
	g++ $(CFLAGS) -o migratetable migratetable.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) _tools.cpp
	mv -f migratetable ../bin/.

syncref:syncref.cpp _tools.cpp
	g++ $(CFLAGS) -o syncref syncref.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) _tools.cpp
	mv -f syncref ../bin/.

syncinc:syncinc.cpp _tools.cpp
	g++ $(CFLAGS) -o syncinc syncinc.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) _tools.cpp
	mv -f syncinc ../bin/.

inetd:inetd.cpp
	g++ $(CFLAGS) -o inetd inetd.cpp $(PUBINCL) $(PUBCPP) 
	mv -f inetd ../bin/.

rinetd:rinetd.cpp
	g++ $(CFLAGS) -o rinetd rinetd.cpp $(PUBINCL) $(PUBCPP) 
	mv -f rinetd ../bin/.

rinetdin:rinetdin.cpp
	g++ $(CFLAGS) -o rinetdin rinetdin.cpp $(PUBINCL) $(PUBCPP) 
	mv -f rinetdin ../bin/.

webserver:webserver.cpp
	g++ $(CFLAGS) -o webserver webserver.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL)  $(ORALIB) $(ORALIBS) $(ORACPP) -lpthread
	mv -f webserver ../bin/.

clean:
	rm -rf procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles fileserver tcpgetfiles tcpputfiles
	rm -rf dminingoracle xmltodb deletetable migratetable syncref syncinc inetd rinetd rinetdin webserver