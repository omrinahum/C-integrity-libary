# Memory Profiler Makefile
# 
# Builds:
# 1. libprofiler.so - The shared library for LD_PRELOAD
# 2. Test programs - To verify the profiler works
#
# Usage:
#   make          - Build everything
#   make test     - Run tests
#   make clean    - Remove build artifacts

CC = gcc
CFLAGS = -Wall -Wextra -g -fPIC -I./include
LDFLAGS = -shared -ldl

# Output files
PROFILER_LIB = libprofiler.so
TEST_LEAK = tests/test_simple_leak
TEST_NO_LEAK = tests/test_no_leak
TEST_COMPLEX = tests/test_complex_leak
TEST_DOUBLE_FREE = tests/test_double_free
TEST_INVALID_FREE = tests/test_invalid_free

# Source files
PROFILER_SOURCES = src/malloc_intercept.c src/hash_table.c src/profiler.c
PROFILER_OBJECTS = $(PROFILER_SOURCES:.c=.o)

# Default target - build everything
all: $(PROFILER_LIB) $(TEST_LEAK) $(TEST_NO_LEAK) $(TEST_COMPLEX) $(TEST_DOUBLE_FREE) $(TEST_INVALID_FREE)
	@echo ""
	@echo "Build complete!"
	@echo "==============="
	@echo "Profiler library: $(PROFILER_LIB)"
	@echo "Test programs: $(TEST_LEAK), $(TEST_NO_LEAK), $(TEST_COMPLEX)"
	@echo "               $(TEST_DOUBLE_FREE), $(TEST_INVALID_FREE)"
	@echo ""
	@echo "To run tests:"
	@echo "  make test"

# Build the profiler shared library
$(PROFILER_LIB): $(PROFILER_OBJECTS)
	@echo "Linking profiler library..."
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Created $(PROFILER_LIB)"

# Compile profiler source files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Build test programs
# Note: We compile with -g for debug symbols
#       We compile with -rdynamic to export symbols for better stack traces
#       We compile with -no-pie to disable PIE (makes addr2line work with runtime addresses)
$(TEST_LEAK): tests/test_simple_leak.c
	@echo "Building test program: $@"
	$(CC) -g -rdynamic -no-pie $< -o $@

$(TEST_NO_LEAK): tests/test_no_leak.c
	@echo "Building test program: $@"
	$(CC) -g -rdynamic -no-pie $< -o $@

$(TEST_COMPLEX): tests/test_complex_leak.c
	@echo "Building test program: $@"
	$(CC) -g -rdynamic -no-pie $< -o $@

$(TEST_DOUBLE_FREE): tests/test_double_free.c
	@echo "Building test program: $@"
	$(CC) -g -rdynamic -no-pie $< -o $@

$(TEST_INVALID_FREE): tests/test_invalid_free.c
	@echo "Building test program: $@"
	$(CC) -g -rdynamic -no-pie $< -o $@

# Run tests with the profiler (using wrapper script with parser)
test: all
	@echo ""
	@echo "=========================================="
	@echo "TEST 1: Simple Memory Leak Detection"
	@echo "=========================================="
	@./tools/run_profiler.sh ./$(TEST_LEAK)
	@echo ""
	@echo ""
	@echo "=========================================="
	@echo "TEST 2: No Leaks (Should Pass Clean)"
	@echo "=========================================="
	@./tools/run_profiler.sh ./$(TEST_NO_LEAK)
	@echo ""
	@echo ""
	@echo "=========================================="
	@echo "TEST 3: Complex Leak Detection"
	@echo "=========================================="
	@./tools/run_profiler.sh ./$(TEST_COMPLEX)
	@echo ""
	@echo ""
	@echo "=========================================="
	@echo "TEST 4: Double-Free Detection"
	@echo "=========================================="
	@./tools/run_profiler.sh ./$(TEST_DOUBLE_FREE)
	@echo ""
	@echo ""
	@echo "=========================================="
	@echo "TEST 5: Invalid-Free Detection"
	@echo "=========================================="
	@./tools/run_profiler.sh ./$(TEST_INVALID_FREE)
	@echo ""

# Run tests with raw JSON output (no parser)
test-raw: all
	@echo "=========================================="
	@echo "Running tests with RAW JSON output"
	@echo "=========================================="
	@echo ""
	@echo "TEST 1: Simple Leak (Raw JSON)"
	@echo "---"
	LD_PRELOAD=./$(PROFILER_LIB) ./$(TEST_LEAK)
	@echo ""
	@echo ""
	@echo "TEST 2: No Leaks (Raw JSON)"
	@echo "---"
	LD_PRELOAD=./$(PROFILER_LIB) ./$(TEST_NO_LEAK)
	@echo ""
	@echo ""
	@echo "TEST 3: Complex Leak (Raw JSON)"
	@echo "---"
	LD_PRELOAD=./$(PROFILER_LIB) ./$(TEST_COMPLEX)
	@echo ""
	@echo ""
	@echo "TEST 4: Double-Free (Raw JSON)"
	@echo "---"
	LD_PRELOAD=./$(PROFILER_LIB) ./$(TEST_DOUBLE_FREE)
	@echo ""
	@echo ""
	@echo "TEST 5: Invalid-Free (Raw JSON)"
	@echo "---"
	LD_PRELOAD=./$(PROFILER_LIB) ./$(TEST_INVALID_FREE)
	@echo ""

# Run tests with FULL stack traces (including system libraries)
test-full-stack: all
	@echo "=========================================="
	@echo "Running tests with FULL STACK mode"
	@echo "=========================================="
	@echo ""
	export PROFILER_FULL_STACK=1 && ./tools/run_profiler.sh ./$(TEST_LEAK)

# Clean build artifacts
clean:
	@echo "Cleaning build files..."
	rm -f $(PROFILER_OBJECTS)
	rm -f $(PROFILER_LIB)
	rm -f $(TEST_LEAK) $(TEST_NO_LEAK) $(TEST_COMPLEX) $(TEST_DOUBLE_FREE) $(TEST_INVALID_FREE)
	@echo "Clean complete"

# Phony targets (not actual files)
.PHONY: all test test-raw test-full-stack clean help

# Help target
help:
	@echo "Memory Profiler - Build Targets"
	@echo "================================"
	@echo ""
	@echo "  make              - Build profiler library and test programs"
	@echo "  make test         - Run tests with parsed output (recommended)"
	@echo "  make test-raw     - Run tests with raw JSON output"
	@echo "  make test-full    - Run tests with full stack traces (system libs)"
	@echo "  make clean        - Remove all build artifacts"
	@echo ""
