# C Systems Integrity Library

A low-level C library for Linux that automatically detects memory leaks, double-free errors, file descriptor leaks, and concurrency deadlocks in C/C++ applications **without recompilation**.


**Working Features:**
- ✅ malloc/free/calloc/realloc interception via LD_PRELOAD
- ✅ Memory leak detection with O(1) hash table (uthash)
- ✅ Stack trace capture showing allocation sites
- ✅ Symbol resolution (filename:line format via post-processing)
- ✅ Thread-safe operation with pthread mutexes
- ✅ False positive filtering (libc infrastructure detection)
- ✅ Double-free detection
- ✅ Invalid-free detection (stack vars, random addresses, etc.)
- ✅ Clean, minimal output format
- ✅ Comprehensive test suite (5 tests)

## Quick Start

```bash
# Build the profiler
make

# Run all tests with symbol resolution (recommended)
make test-resolved

# Run a single program with profiling
./tools/run_profiler.sh ./your_program

# Run with full stack traces (including system libraries)
PROFILER_FULL_STACK=1 ./tools/run_profiler.sh ./your_program
```

## Configuration

Control profiler behavior with environment variables:

- `PROFILER_FULL_STACK` - Control stack trace verbosity (default: clean mode)
  - `0` or unset: **Clean mode** - Show only user code frames (recommended)
  - `1`: **Full stack mode** - Show all frames including system libraries

**Examples:**

**Clean Mode (Default):**
```bash
./tools/run_profiler.sh ./your_program
```
Output shows only your code:
```
[LEAK] 0x7c43af0: 1024 bytes
  at: your_program.c; line: 42
  at: your_program.c; line: 15
```

**Full Stack Mode:**
```bash
PROFILER_FULL_STACK=1 ./tools/run_profiler.sh ./your_program
```
Output shows everything with labels:
```
[LEAK] 0x7c43af0: 1024 bytes
  [SYS] <libprofiler.so+0x...>
  [USR] at: your_program.c; line: 42
  [SYS] <libc.so.6+0x...>
  [USR] at: your_program.c; line: 15
```

## How It Works

The profiler uses **LD_PRELOAD** to intercept memory allocation functions before your program calls them:

```
Your Program calls malloc()
         ↓
Our profiler intercepts it
         ↓
We track the allocation
         ↓
We call the REAL malloc()
         ↓
Return pointer to your program
```

At program exit, we report any allocations that were never freed = **memory leaks**.

## Example Output

**Memory Leak Detection:**
```
========== MEMORY LEAKS ==========
Found 2 leak(s), 1536 bytes total

[LEAK] 0x7c43af0: 1024 bytes
  at: your_program.c; line: 42

[LEAK] 0x7c43b50: 512 bytes
  at: your_program.c; line: 87

Summary:
  Real leaks: 2 allocation(s), 1536 bytes
  Libc infrastructure: 1 allocation(s), 1024 bytes (ignored)
==================================
```

**Corruption Detection:**
```
[CORRUPTION] Double-Free or Invalid-Free at 0x7c43af0
  at: your_program.c; line: 95
```

## Project Structure

```
c-systems-integrity-lib/
├── src/
│   ├── malloc_intercept.c   # LD_PRELOAD interception + corruption detection
│   ├── hash_table.c         # O(1) allocation tracking with uthash
│   └── profiler.c           # Library initialization
├── include/
│   ├── profiler_internal.h  # Internal data structures
│   └── uthash.h             # Third-party hash table library
├── tools/
│   ├── resolve_symbols.py   # Symbol resolution tool
│   └── run_profiler.sh      # Wrapper script for easy profiling
├── tests/
│   ├── test_simple_leak.c   # Basic leak detection
│   ├── test_no_leak.c       # Zero-leak verification
│   ├── test_complex_leak.c  # Real-world simulation (7 leaks)
│   ├── test_double_free.c   # Double-free detection (4 scenarios)
│   └── test_invalid_free.c  # Invalid-free detection (5 scenarios)
├── Makefile                 # Build system
└── CMemoryLeaker.md         # Full project specification
```

## Design Principles

This codebase follows these principles:

1. **Simple before complex** - Start with basics, add features incrementally
2. **One responsibility per file** - Each module has a clear purpose
3. **Clean code** - Readable, maintainable, well-commented
4. **Scalable architecture** - Easy to add new features
5. **Documentation** - Explain the "why", not just the "what"

## Requirements

- Linux (Ubuntu/Debian) or WSL on Windows
- GCC or Clang
- GNU Make

