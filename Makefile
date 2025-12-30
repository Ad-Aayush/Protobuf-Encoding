CXX := g++
CXXFLAGS := -std=c++20 -O0 -g -Wall -Wextra -Wpedantic
CPPFLAGS := -I. \
            -Ithird_party/googletest/googletest/include \
            -Ithird_party/googletest/googletest
LDFLAGS :=
LDLIBS := -pthread

GTEST_DIR := third_party/googletest/googletest
GTEST_SRCS := $(GTEST_DIR)/src/gtest-all.cc

SRCS := encoder.cpp tests.cpp
BIN := tests_bin

.PHONY: all test clean

all: $(BIN)

# This rule intentionally recompiles everything when you run `make`:
# (No object files, just one compile+link command.)
$(BIN): $(SRCS) $(GTEST_SRCS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
