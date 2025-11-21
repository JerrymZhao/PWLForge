CXX = g++
CXXFLAGS_DEBUG = -std=c++17 -Wall -Wextra -g -O0
CXXFLAGS_RELEASE = -std=c++17 -Wall -O3 -march=native
LDFLAGS = -lm -lpthread

# Include paths for all modules
INCLUDES = -I./include/common \
           -I./include/stage1_interval \
           -I./include/stage2_fitting \
           -I./include/stage3_compression \
           -I./include/stage4_mapping

# Directories
BUILD_DIR = build
RESULTS_DIR = results
TEST_DIR = stage_tests

# Main executable
MAIN_SRC = main.cpp
MAIN_OBJ = $(BUILD_DIR)/main.o
MAIN_EXEC = $(BUILD_DIR)/optimize_intervals

# Test executables
TEST1_SRC = $(TEST_DIR)/test_stage1.cpp
TEST1_OBJ = $(BUILD_DIR)/test_stage1.o
TEST1_EXEC = $(BUILD_DIR)/test_stage1

TEST2_SRC = $(TEST_DIR)/test_stage2.cpp
TEST2_OBJ = $(BUILD_DIR)/test_stage2.o
TEST2_EXEC = $(BUILD_DIR)/test_stage2

TEST3_SRC = $(TEST_DIR)/test_stage3.cpp
TEST3_OBJ = $(BUILD_DIR)/test_stage3.o
TEST3_EXEC = $(BUILD_DIR)/test_stage3

# Header dependencies
ALL_HEADERS = $(wildcard include/common/*.hpp) \
              $(wildcard include/stage1_interval/*.hpp) \
              $(wildcard include/stage2_fitting/*.hpp) \
              $(wildcard include/stage3_compression/*.hpp) \
              $(wildcard include/stage4_mapping/*.hpp)

#==============================================================================
# Build targets
#==============================================================================
.PHONY: all
all: $(MAIN_EXEC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RESULTS_DIR):
	mkdir -p $(RESULTS_DIR)

# Main executable
$(MAIN_EXEC): $(MAIN_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(MAIN_OBJ) -o $(MAIN_EXEC) $(LDFLAGS)

$(MAIN_OBJ): $(MAIN_SRC) $(ALL_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) -c $(MAIN_SRC) -o $(MAIN_OBJ)

# Test executables
$(TEST1_EXEC): $(TEST1_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(TEST1_OBJ) -o $(TEST1_EXEC) $(LDFLAGS)

$(TEST1_OBJ): $(TEST1_SRC) $(ALL_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) -c $(TEST1_SRC) -o $(TEST1_OBJ)

$(TEST2_EXEC): $(TEST2_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(TEST2_OBJ) -o $(TEST2_EXEC) $(LDFLAGS)

$(TEST2_OBJ): $(TEST2_SRC) $(ALL_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) -c $(TEST2_SRC) -o $(TEST2_OBJ)

$(TEST3_EXEC): $(TEST3_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(TEST3_OBJ) -o $(TEST3_EXEC) $(LDFLAGS)

$(TEST3_OBJ): $(TEST3_SRC) $(ALL_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) -c $(TEST3_SRC) -o $(TEST3_OBJ)

.PHONY: debug
debug: CXXFLAGS_RELEASE = $(CXXFLAGS_DEBUG)
debug: clean $(MAIN_EXEC)

.PHONY: tests
tests: $(TEST1_EXEC) $(TEST2_EXEC) $(TEST3_EXEC)

#==============================================================================
# Run targets
#==============================================================================
.PHONY: run
run: $(MAIN_EXEC) | $(RESULTS_DIR)
	./$(MAIN_EXEC)

.PHONY: test1
test1: $(TEST1_EXEC) | $(RESULTS_DIR)
	./$(TEST1_EXEC)

.PHONY: test2
test2: $(TEST2_EXEC) | $(RESULTS_DIR)
	./$(TEST2_EXEC)

.PHONY: test3
test3: $(TEST3_EXEC) | $(RESULTS_DIR)
	./$(TEST3_EXEC)

.PHONY: test-all
test-all: test1 test2 test3

.PHONY: pipeline
pipeline: $(TEST1_EXEC) $(TEST2_EXEC) $(TEST3_EXEC) | $(RESULTS_DIR)
	@echo "=== Stage 1+2: Partitioning & Fitting ==="
	./$(TEST1_EXEC)
	@echo ""
	@echo "=== Stage 3: Compression ==="
	./$(TEST2_EXEC)
	@echo ""
	@echo "=== Stage 4: Hardware Mapping ==="
	./$(TEST3_EXEC)

#==============================================================================
# Example targets (command-line mode)
#==============================================================================
.PHONY: example-tanh
example-tanh: $(MAIN_EXEC) | $(RESULTS_DIR)
	./$(MAIN_EXEC) "tanh(x)" -3 3 1e-5 hw

.PHONY: example-sqrt
example-sqrt: $(MAIN_EXEC) | $(RESULTS_DIR)
	./$(MAIN_EXEC) "sqrt(x)" 0.1 10 1e-4 hw

.PHONY: example-exp
example-exp: $(MAIN_EXEC) | $(RESULTS_DIR)
	./$(MAIN_EXEC) "exp(x)" -5 5 1e-5 hw

.PHONY: example-sigmoid
example-sigmoid: $(MAIN_EXEC) | $(RESULTS_DIR)
	./$(MAIN_EXEC) "sigmoid(x)" -6 6 1e-5 hw

.PHONY: example-gelu
example-gelu: $(MAIN_EXEC) | $(RESULTS_DIR)
	./$(MAIN_EXEC) "gelu(x)" -4 4 1e-5 hw

#==============================================================================
# Clean targets
#==============================================================================
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.o

.PHONY: distclean
distclean: clean
	rm -rf $(RESULTS_DIR)

.PHONY: clean-hw
clean-hw:
	find $(RESULTS_DIR) -type d -name "*hardware*" -exec rm -rf {} + 2>/dev/null || true

#==============================================================================
# Help
#==============================================================================
.PHONY: help
help:
	@echo "Build:"
	@echo "  make           - Build main program"
	@echo "  make debug     - Build with debug symbols"
	@echo "  make tests     - Build all test executables"
	@echo ""
	@echo "Run:"
	@echo "  make run       - Interactive mode"
	@echo "  make test1     - Stage 1+2 test"
	@echo "  make test2     - Stage 3 test"
	@echo "  make test3     - Stage 4 test"
	@echo "  make pipeline  - Full pipeline"
	@echo ""
	@echo "Examples (command-line):"
	@echo "  make example-tanh     - tanh(x) with hardware"
	@echo "  make example-sqrt     - sqrt(x) with hardware"
	@echo "  make example-sigmoid  - sigmoid(x) with hardware"
	@echo "  make example-gelu     - gelu(x) with hardware"
	@echo ""
	@echo "Direct usage:"
	@echo "  ./build/optimize_intervals \"func(x)\" start end error [hw]"
	@echo "  Example: ./build/optimize_intervals \"tanh(x)\" -3 3 1e-5 hw"
	@echo ""
	@echo "Clean:"
	@echo "  make clean     - Remove build files"
	@echo "  make clean-hw  - Remove hardware files only"
	@echo "  make distclean - Remove everything"