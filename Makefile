CXX := g++
CXXFLAGS := -std=c++20 -O0 -g -Wall -Wextra -Wpedantic
CPPFLAGS := -I. \
            -Ithird_party/googletest/googletest/include \
            -Ithird_party/googletest/googletest
LDLIBS := -pthread

BUILD := build

GTEST_DIR := third_party/googletest/googletest
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc

SRCS_CPP := encoder.cpp proto_desc.cpp message_encoder.cpp tests.cpp
SRCS := $(SRCS_CPP) $(GTEST_SRC)

# Map source paths -> build/<source path>.o (preserves directories)
OBJS := $(patsubst %,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

BIN := tests_bin

.PHONY: all test clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(OBJS) $(LDLIBS) -o $@

# Compile any .cpp into build/<path>.o
$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Compile any .cc into build/<path>.o
$(BUILD)/%.cc.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

test: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD) $(BIN)
