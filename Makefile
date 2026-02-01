# Compiler settings
CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -O3
LDFLAGS =

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INC_DIR = include

# Files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d)
OUTPUT = $(BIN_DIR)/server

# Default target
all: $(OUTPUT)

# Build executable
$(OUTPUT): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "Build complete."

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

# Run the program
run: $(OUTPUT)
	@echo "Running $(OUTPUT)..."
	@./$(OUTPUT)

run_perf: $(OUTPUT)
	@echo "Running $(OUTPUT) with perf..."
	@perf record ./$(OUTPUT) || perf report

# Debug build
debug: CXXFLAGS = -std=c++23 -Wall -Wextra -g -MMD -MP
debug: clean $(OUTPUT)

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@rm -rf $(OBJ_DIR) $(BIN_DIR)

# Install (example)
install: $(OUTPUT)
	@echo "Installing to /usr/local/bin..."
	@install -m 755 $(OUTPUT) /usr/local/bin/

# Show help
help:
	@echo "Available targets:"
	@echo "  make         - Build the project"
	@echo "  make run     - Build and run"
	@echo "  make debug   - Build with debug symbols"
	@echo "  make clean   - Remove build artifacts"
	@echo "  make install - Install to system"

# Include auto-generated dependencies
-include $(DEPS)

.PHONY: all run debug clean install help