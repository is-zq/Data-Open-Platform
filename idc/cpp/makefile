# 开发框架头文件路径
PUBINCL = -I/project/public

# 开发框架源文件，这里直接包含cpp而不用链接库是为了方便调试
PUBCPP = /project/public/_public.cpp

# 调试参数
CFLAGS = -g

all:crtsurfdata

crtsurfdata:crtsurfdata.cpp
	g++ $(CFLAGS) -o crtsurfdata crtsurfdata.cpp $(PUBINCL) $(PUBCPP) -Im -Ic
	cp -f crtsurfdata ../bin/.

clean:
	rm -f crtsurfdata