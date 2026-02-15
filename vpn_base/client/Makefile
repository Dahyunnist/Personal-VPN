# 编译器设置
CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra

# 目标文件
TARGET := client.exe

# 源文件
SRC := client.cpp

# 链接库 (Windows)
LDFLAGS := -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt

# Boost 线程库的多线程版本命名可能因系统而异
# 如果出现链接错误，尝试以下替代名称：
# -lboost_thread-mgwXX-mt  # MinGW 特定版本
# -lboost_thread

# 默认目标
all: $(TARGET)

# 主编译规则
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# 清理生成文件
clean:
	rm -f $(TARGET)

# 伪目标声明
.PHONY: all clean