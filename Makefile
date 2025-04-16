CXX = g++
CXXFLAGS_DEBUG = -std=c++17 -Wall -g -O0
CXXFLAGS_RELEASE = -std=c++17 -Wall -O2
LDFLAGS = -lm
INCLUDES = -I./tb -I./include

SRC = main.cpp
HEADERS = interval_optimizer.hpp function_fitter.hpp interval_group_compressor.hpp exprtk.hpp hw_mapping.hpp
OBJ = $(SRC:.cpp=.o)
EXEC = optimize_intervals

# Test files
TEST_SRC = tb/gelu_test.cpp
TEST_OBJ = $(TEST_SRC:.cpp=.o)
TEST_EXEC = gelu_test

all: $(EXEC) $(TEST_EXEC)

# Build the main executable
$(EXEC): $(OBJ)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) $(OBJ) -o $(EXEC) $(LDFLAGS)

# Build the test executable
$(TEST_EXEC): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) $(TEST_OBJ) -o $(TEST_EXEC) $(LDFLAGS)

debug: CXXFLAGS := $(CXXFLAGS_DEBUG)
debug: clean $(EXEC)
	@echo "Debug version built with -g and -O0."

# Compile object files for sources
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) -c $< -o $@

# Compile object files for test sources
tb/%.o: tb/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(EXEC) $(TEST_EXEC)

run: $(EXEC)
	./$(EXEC)

run_test: $(TEST_EXEC)
	./$(TEST_EXEC)

.PHONY: all clean run run_test debug