# Define compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -pthread

# Get a list of all .cpp files in the directory
SRC = $(wildcard *.cpp)

# Generate output binaries by removing .cpp extension
BIN = $(SRC:.cpp=)

# Default target to build all binaries
all: $(BIN)

# Rule to compile each .cpp to its respective binary
%: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Clean up generated files
clean:
	rm -f $(BIN)

