CXX := g++
CXXFLAGS := -std=c++20 -O0 -g -Wall -Wextra -Wpedantic
CPPFLAGS := -Iinclude \
            -Ithird_party/googletest/googletest/include \
            -Ithird_party/googletest/googletest
DEBUG ?= 0
ifeq ($(DEBUG),1)
CPPFLAGS += -DPROTO_DEBUG=1
endif
LDLIBS := -pthread

BUILD := build

GTEST_DIR := third_party/googletest/googletest
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc

LIB_SRCS_CPP := src/encoder.cpp src/proto_desc.cpp src/message_encoder.cpp
TEST_SRCS_CPP := tests/tests.cpp
SRCS := $(LIB_SRCS_CPP) $(TEST_SRCS_CPP) $(GTEST_SRC)

# Map source paths -> build/<source path>.o (preserves directories)
OBJS := $(patsubst %,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

LIB := $(BUILD)/libprotoenc.a
BIN := tests_bin

.PHONY: all test lib clean

all: $(BIN)

lib: $(LIB)

$(LIB): $(patsubst %,$(BUILD)/%.o,$(LIB_SRCS_CPP))
	ar rcs $@ $^

$(BIN): $(patsubst %,$(BUILD)/%.o,$(TEST_SRCS_CPP)) $(LIB) $(BUILD)/$(GTEST_SRC).o
	$(CXX) $^ $(LDLIBS) -o $@

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
