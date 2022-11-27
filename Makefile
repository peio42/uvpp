.PHONY: clean build test 

CXX = clang++
CC = $(CXX)
CXXFLAGS = -Wall -std=c++20 -I.
LDLIBS = -luv -lfmt -ldl -pthread -lgtest

SRCS = test/main.cpp $(wildcard test/test-*.cpp)
OBJS = $(SRCS:.cpp=.o)

test: build
	./test/main

test/main: $(OBJS)

build: test/main

clean:
	rm -f $(OBJS)
