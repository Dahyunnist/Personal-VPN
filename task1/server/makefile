# Compiler settings
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra
INCLUDES := -I/usr/include/boost
LIBRARIES := -lssl -lcrypto -lpthread
LDFLAGS := -L/usr/lib/x86_64-linux-gnu

# Targets
TARGET := server
SRC := server.cpp
OBJ := $(SRC:.cpp=.o)

# Build rules
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LIBRARIES)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean
clean:
	rm -f $(OBJ) $(TARGET)

# Phony targets
.PHONY: all clean