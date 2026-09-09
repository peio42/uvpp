.PHONY: clean build test examples build-gcc build-clang build-all test-gcc test-clang test-all \
	test-asan-ubsan measure-cleanup package

CXX ?= g++
CXX_ID ?= $(notdir $(CXX))
BUILD_DIR ?= build/$(CXX_ID)
DIST_DIR ?= dist

CXXFLAGS ?= -Wall -std=c++20 -Iinclude -I..
DEPFLAGS ?= -MMD -MP
LDLIBS ?= -luv -pthread -lgtest
EXAMPLE_LDLIBS ?= -luv -pthread

TEST_SRCS = tests/main.cpp $(wildcard tests/test-*.cpp)
TEST_OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(TEST_SRCS))
ALLOCATION_TEST_OBJS = $(BUILD_DIR)/tests/main.o $(BUILD_DIR)/tests/udp-allocation.o
TEST_DEPS = $(sort $(TEST_OBJS:.o=.d) $(ALLOCATION_TEST_OBJS:.o=.d))
TEST_BIN = $(BUILD_DIR)/tests/main
ALLOCATION_TEST_BIN = $(BUILD_DIR)/tests/udp-allocation
EXAMPLE_SRCS = $(wildcard examples/*.cpp)
EXAMPLE_BINS = $(patsubst examples/%.cpp,$(BUILD_DIR)/examples/%,$(EXAMPLE_SRCS))
EXAMPLE_DEPS = $(addsuffix .d,$(EXAMPLE_BINS))
METRICS_SRC = benchmarks/resource-scope-cleanup.cpp
METRICS_BIN = $(BUILD_DIR)/benchmarks/resource-scope-cleanup
METRICS_DEPS = $(METRICS_BIN).d
METRICS_LDLIBS ?= -luv -pthread
SANITIZER_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -g

build: $(TEST_BIN) $(ALLOCATION_TEST_BIN) examples

$(BUILD_DIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c $< -o $@

$(sort $(TEST_OBJS) $(ALLOCATION_TEST_OBJS)): %.o: %.d

$(TEST_BIN): $(TEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(TEST_OBJS) $(LDLIBS) -o $@

$(ALLOCATION_TEST_BIN): $(ALLOCATION_TEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(ALLOCATION_TEST_OBJS) $(LDLIBS) -o $@

examples: $(EXAMPLE_BINS)

$(BUILD_DIR)/examples/%: examples/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -MF $@.d $< $(EXAMPLE_LDLIBS) -o $@

$(EXAMPLE_BINS): %: %.d

$(METRICS_BIN): $(METRICS_SRC)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -MF $@.d $< $(METRICS_LDLIBS) -o $@

$(TEST_DEPS) $(EXAMPLE_DEPS) $(METRICS_DEPS): ;

-include $(TEST_DEPS) $(EXAMPLE_DEPS) $(METRICS_DEPS)

test: build
	$(TEST_BIN)
	$(ALLOCATION_TEST_BIN)

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

# Runs the complete suite with Clang AddressSanitizer and UndefinedBehaviorSanitizer.
# Leak checking is deliberate: high-level close state must outlive native callbacks
# but no longer. The separate build directory keeps normal artifacts untouched.
test-asan-ubsan:
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(MAKE) test CXX=clang++ CXX_ID=clang-asan-ubsan \
		CXXFLAGS="$(CXXFLAGS) $(SANITIZER_FLAGS)" \
		LDLIBS="$(LDLIBS) $(SANITIZER_FLAGS)"

# Reports C++ allocations and finish() latency for repeated multi-connection TCP
# cleanup. It is a measurement command, not a pass/fail performance budget.
measure-cleanup: $(METRICS_BIN)
	$(METRICS_BIN)

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
	printf '%s\n' "$(VERSION)" > VERSION
	rm -rf $(DIST_DIR)/uvpp-$(VERSION) $(DIST_DIR)/uvpp-$(VERSION).tar.gz $(DIST_DIR)/checksums.txt
	mkdir -p $(DIST_DIR)/uvpp-$(VERSION)
	cp -R README.md CMakeLists.txt cmake VERSION include docs examples benchmarks $(DIST_DIR)/uvpp-$(VERSION)/
	@if [ -f LICENSE ]; then cp LICENSE $(DIST_DIR)/uvpp-$(VERSION)/; fi
	tar -czf $(DIST_DIR)/uvpp-$(VERSION).tar.gz -C $(DIST_DIR) uvpp-$(VERSION)
	cd $(DIST_DIR) && sha256sum uvpp-$(VERSION).tar.gz > checksums.txt

clean:
	rm -rf build dist
