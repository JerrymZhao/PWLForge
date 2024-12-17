# Makefile

CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2
LDFLAGS = -lm
INCLUDES = -I./tb -I./include

SRC = main.cpp
HEADERS = interval_optimizer.hpp function_fitter.hpp interval_group_compressor.hpp exprtk.hpp
OBJ = $(SRC:.cpp=.o)
EXEC = optimize_intervals

# Test files
TEST_SRC = tb/gelu_test.cpp
TEST_OBJ = $(TEST_SRC:.cpp=.o)
TEST_EXEC = gelu_test

all: $(EXEC) $(TEST_EXEC)

# Build the main executable
$(EXEC): $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OBJ) -o $(EXEC) $(LDFLAGS)

# Build the test executable
$(TEST_EXEC): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_OBJ) -o $(TEST_EXEC) $(LDFLAGS)

# Compile object files for sources
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile object files for test sources
tb/%.o: tb/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile object files for exprtk
# exprtk.o: exprtk.hpp
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c exprtk.hpp -o exprtk.o

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(EXEC) $(TEST_EXEC)

run: $(EXEC)
	./$(EXEC)

run_test: $(TEST_EXEC)
	./$(TEST_EXEC)

.PHONY: all clean run run_test
