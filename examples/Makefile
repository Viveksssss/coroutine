# 构建示例, 依赖 install.sh 装好的库:
#   头文件 /usr/include/coroutine/   库 /usr/lib/coroutine/libcoroutine.so
#
#   make        构建
#   make run    构建并运行
#   make clean
VIK_LIB ?= /usr/lib/coroutine

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -pthread -Wall
LDLIBS   := -L$(VIK_LIB) -lcoroutine -Wl,-rpath,$(VIK_LIB)

quickstart: quickstart.cpp
	$(CXX) $(CXXFLAGS) $< $(LDLIBS) -o $@

run: quickstart
	./quickstart

clean:
	rm -f quickstart

.PHONY: run clean
