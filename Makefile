.PHONY: clean build test examples

CXX = g++
CXXFLAGS = -Wall -std=c++20 -Iinclude -I..
LDLIBS = -luv -pthread -lgtest
EXAMPLE_LDLIBS = -luv -pthread

SRCS = tests/main.cpp $(wildcard tests/test-*.cpp)
OBJS = $(SRCS:.cpp=.o)

build: tests/main examples

tests/main: $(OBJS)
	$(CXX) $(OBJS) $(LDLIBS) -o $@

examples: examples/tcp-echo-server

examples/tcp-echo-server: examples/tcp-echo-server.cpp
	$(CXX) $(CXXFLAGS) $< $(EXAMPLE_LDLIBS) -o $@

test: build
	./tests/main

clean:
	rm -f $(OBJS) tests/main examples/tcp-echo-server
