# 开发框架头文件路径
PUBINCL = -I/project/public

# 开发框架源文件，这里直接包含cpp而不用链接库是为了方便调试
PUBCPP = /project/public/_public.cpp

# 调试参数
CFLAGS = -g
#CFLAGS = -O2

all:procctl checkproc

procctl:procctl.cpp
	g++ $(CFLAGS) -o procctl procctl.cpp
	cp -f procctl ../bin/

checkproc:checkproc.cpp
	g++ $(CFLAGS) -o checkproc checkproc.cpp $(PUBINCL) $(PUBCPP)
	cp -f checkproc ../bin/

clean:
	rm -rf procctl checkproc