#!/bin/bash
#
# run_profiler.sh - Wrapper script for running profiler with symbol resolution
#
# Usage: ./tools/run_profiler.sh <test_binary>
#
# This script:
# 1. Runs the test binary with profiler enabled
# 2. Captures JSON output to temporary file
# 3. Runs symbol resolution on the JSON
# 4. Cleans up temporary file
#

set -e  # Exit on error

if [ $# -lt 1 ]; then
    echo "Usage: $0 <test_binary>"
    echo "Example: $0 ./tests/test_simple_leak"
    exit 1
fi

TEST_BINARY="$1"
TEMP_JSON="/tmp/profiler_output_$$.json"

# Check if binary exists
if [ ! -f "$TEST_BINARY" ]; then
    echo "Error: Binary not found: $TEST_BINARY"
    exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================="
echo "Running Profiler: $TEST_BINARY"
echo "========================================="
echo ""

# Export profiler environment variables
# PROFILER_STACK_TRACES: always enabled for symbol resolution to work
export PROFILER_STACK_TRACES=1

# PROFILER_FULL_STACK: preserve user's setting if set, otherwise default to 0
if [ -z "$PROFILER_FULL_STACK" ]; then
    export PROFILER_FULL_STACK=0
fi

# Run the profiler and capture JSON output
LD_PRELOAD="$PROJECT_DIR/libprofiler.so" "$TEST_BINARY" 2>"$TEMP_JSON"

echo ""

# Run symbol resolution
python3 "$SCRIPT_DIR/resolve_symbols.py" "$TEMP_JSON" "$TEST_BINARY"

# Clean up
rm -f "$TEMP_JSON"
