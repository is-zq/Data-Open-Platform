# 开发框架头文件路径
PUBINCL = -I/project/public

# 开发框架源文件，这里直接包含cpp而不用链接库是为了方便调试
PUBCPP = /project/public/_public.cpp

# 调试参数
CFLAGS = -g
#CFLAGS = -O2

all:procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles fileserver tcpputfiles

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

# tcpgetfiles:tcpgetfiles.cpp
# 	g++ $(CFLAGS) -o tcpgetfiles tcpgetfiles.cpp $(PUBINCL) $(PUBCPP)
# 	cp -f tcpgetfiles ../bin/

tcpputfiles:tcpputfiles.cpp
	g++ $(CFLAGS) -o tcpputfiles tcpputfiles.cpp $(PUBINCL) $(PUBCPP)
	cp -f tcpputfiles ../bin/

clean:
	rm -rf procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles fileserver tcpputfiles