.PHONY: clean build test 

CXX = clang++
CC = $(CXX)
CXXFLAGS = -Wall -std=c++17 -I.
LDLIBS = -luv -lfmt -ldl -pthread -lgtest

# Lambda build error in examples SRCS: = test/main.cpp $(wildcard test/test-*.cpp) $(wildcard examples/*.cpp) 
SRCS = test/main.cpp $(wildcard test/test-*.cpp)
OBJS = $(SRCS:.cpp=.o)

test: build
	test/main

build: $(OBJS)

clean:
	rm -f $(OBJS)
