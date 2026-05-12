.PHONY: clean build test examples build-gcc build-clang build-all test-gcc test-clang test-all package

CXX ?= g++
CXX_ID ?= $(notdir $(CXX))
BUILD_DIR ?= build/$(CXX_ID)
DIST_DIR ?= dist

CXXFLAGS ?= -Wall -std=c++20 -Iinclude -I..
LDLIBS ?= -luv -pthread -lgtest
EXAMPLE_LDLIBS ?= -luv -pthread

TEST_SRCS = tests/main.cpp $(wildcard tests/test-*.cpp)
TEST_OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(TEST_SRCS))
TEST_BIN = $(BUILD_DIR)/tests/main
EXAMPLE_BIN = $(BUILD_DIR)/examples/tcp-echo-server

build: $(TEST_BIN) examples

$(BUILD_DIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(TEST_OBJS) $(LDLIBS) -o $@

examples: $(EXAMPLE_BIN)

$(EXAMPLE_BIN): examples/tcp-echo-server.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(EXAMPLE_LDLIBS) -o $@

test: build
	$(TEST_BIN)

build-gcc:
	$(MAKE) build CXX=g++ CXX_ID=gcc

build-clang:
	$(MAKE) build CXX=clang++ CXX_ID=clang

build-all: build-gcc build-clang

test-gcc:
	$(MAKE) test CXX=g++ CXX_ID=gcc

test-clang:
	$(MAKE) test CXX=clang++ CXX_ID=clang

test-all: test-gcc test-clang

package:
	@if [ -z "$(VERSION)" ]; then \
		echo "VERSION is required, example: make package VERSION=2.0.0"; \
		exit 1; \
	fi
	@if ! printf '%s\n' "$(VERSION)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.-]+)?$$'; then \
		echo "Invalid VERSION: $(VERSION)"; \
		echo "Expected semver-like value, example: 2.0.0 or 2.0.0-rc.1"; \
		exit 1; \
	fi
	rm -rf $(DIST_DIR)/uvpp-$(VERSION) $(DIST_DIR)/uvpp-$(VERSION).tar.gz $(DIST_DIR)/checksums.txt
	mkdir -p $(DIST_DIR)/uvpp-$(VERSION)
	cp -R README.md include docs examples $(DIST_DIR)/uvpp-$(VERSION)/
	@if [ -f LICENSE ]; then cp LICENSE $(DIST_DIR)/uvpp-$(VERSION)/; fi
	printf '%s\n' "$(VERSION)" > $(DIST_DIR)/uvpp-$(VERSION)/VERSION
	tar -czf $(DIST_DIR)/uvpp-$(VERSION).tar.gz -C $(DIST_DIR) uvpp-$(VERSION)
	cd $(DIST_DIR) && sha256sum uvpp-$(VERSION).tar.gz > checksums.txt

clean:
	rm -rf build dist
