# 开发框架头文件路径
PUBINCL = -I/project/public

# 开发框架源文件，这里直接包含cpp而不用链接库是为了方便调试
PUBCPP = /project/public/_public.cpp

################################
# oracle头文件路径
ORAINCL = -I$(ORACLE_HOME)/rdbms/public -I/project/public/db/oracle

# oracle库文件路径
ORALIB =  -L$(ORACLE_HOME)/lib -L.

# oracle的oci库
ORALIBS = -lclntsh

# 开发框架oracle的cpp文件名，这里直接包含进来，没有采用链接库，是为了方便调试。
ORACPP = /project/public/db/oracle/_ooci.cpp
################################

# 调试参数
CFLAGS = -g

all:crtsurfdata obtcodetodb

crtsurfdata:crtsurfdata.cpp
	g++ $(CFLAGS) -o crtsurfdata crtsurfdata.cpp $(PUBINCL) $(PUBCPP) -Im -Ic
	cp -f crtsurfdata ../bin/.

obtcodetodb:obtcodetodb.cpp
	g++ $(CFLAGS) -o obtcodetodb obtcodetodb.cpp $(PUBINCL) $(PUBCPP) $(ORAINCL) $(ORALIB) $(ORALIBS) $(ORACPP) 
	cp obtcodetodb ../bin/.

clean:
	rm -f crtsurfdata obtcodetodb