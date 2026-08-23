# PWLForge build entry point

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
CXX := clang++
APPLE_SDK := $(shell xcrun --show-sdk-path 2>/dev/null)
PLATFORM_CXXFLAGS := -isysroot $(APPLE_SDK) \
	-isystem $(APPLE_SDK)/usr/include/c++/v1 \
	-Wno-error=missing-template-arg-list-after-template-kw
else
CXX ?= g++
PLATFORM_CXXFLAGS :=
endif

CPPFLAGS := \
	-Isrc/pwlforge/common \
	-Isrc/pwlforge/partitioning \
	-Isrc/pwlforge/fitting \
	-Isrc/pwlforge/compression \
	-Isrc/pwlforge/hardware_export \
	-Ithird_party/exprtk

CXXFLAGS := -std=c++17 -Wall -Wextra -O3 $(PLATFORM_CXXFLAGS)
LDFLAGS := -lm -lpthread

BUILD_DIR := build
TARGET := $(BUILD_DIR)/pwlforge
SOURCE := src/pwlforge/pwlforge.cpp
HEADERS := $(shell find src third_party -type f -name '*.hpp')

.PHONY: all clean test test-matrix help

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCE) $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCE) -o $@ $(LDFLAGS)

test: $(TARGET)
	./tests/smoke_test.sh

test-matrix: $(TARGET)
	./tests/matrix_test.sh

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "make       Build PWLForge"
	@echo "make test        Build and run the tanh smoke test"
	@echo "make test-matrix Build and run the four-function regression matrix"
	@echo "make clean Remove the local build directory"
