# Kaleidoscope Makefile
# 使用 llvm-config 获取编译和链接选项

CXX = clang++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -I./include

# 获取 LLVM 编译选项
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null || echo "-I/usr/include/llvm-17")
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs --libs core support native irreader 2>/dev/null || echo "-lLLVM-17")

# 源文件
SRCS = src/main.cpp src/lexer.cpp src/parser.cpp src/codegen.cpp
OBJS = $(SRCS:.cpp=.o)

# 目标
TARGET = kaleidoscope

# 默认目标
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $^ -o $@ $(LLVM_LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

# 测试目标
test: $(TARGET)
	@echo "Testing basic expressions..."
	@echo "1 + 2 * 3;" | ./$(TARGET)
	@echo ""
	@echo "Testing function definition..."
	@echo "def foo(x y) x + y;" | ./$(TARGET)

.PHONY: all clean test
