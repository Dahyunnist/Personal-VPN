# 全静态编译：强制链接所有库（包括 GLIBC、libstdc++、Boost、OpenSSL）
CXX = g++
# 编译参数：指定 C++17，添加静态编译和安全选项
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude -fPIC -static

# 链接参数：全静态链接所有依赖库（顺序：依赖者在后）
LDFLAGS = -lboost_system -lboost_thread  # Boost 静态库
LDFLAGS += -lssl -lcrypto                # OpenSSL 静态库
LDFLAGS += -lpthread -ldl -lz -lm        # 系统库（静态链接，需 libpthread.a 等）

# 源文件和目标文件（单文件或多文件均可）
SRCS = $(wildcard src/*.cpp)  # 若所有代码在 src/ 目录下
# 若只有单个 server.cpp，可简化为：SRCS = server.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = vpn_server

.PHONY: all clean run

all: $(TARGET)
	@echo "✅ 全静态编译完成：$(TARGET)"

# 直接编译并链接（类似单文件编译命令）
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# 编译 .cpp 为 .o（如果多文件）
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "🧹 清理完成"

run: $(TARGET)
	./$(TARGET) 10043  # 直接运行（带端口参数）