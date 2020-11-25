CXX = clang++
CC = $(CXX)
CXXFLAGS = -Wall -std=c++17 -I..
LDLIBS = -luv -lfmt -ldl -pthread -lgtest

SRCS = main.cpp $(wildcard test-*.cpp)
OBJS = $(SRCS:.cpp=.o)

.PHONY: clean

main: $(OBJS)

clean:
	rm -f $(OBJS)
