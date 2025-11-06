#!/usr/bin/env python3
"""
resolve_symbols.py
Converts raw stack trace addresses to "at: filename; line: number" format.

Usage:
    ./resolve_symbols.py <profiler_output.txt> <binary_path>
    
    Or pipe directly from stderr:
    LD_PRELOAD=./libprofiler.so ./test_program 2>&1 | ./resolve_symbols.py - ./test_program

Environment Variables:
    PROFILER_FULL_STACK=1  - Show full system stack (including libc, libpthread, etc.)
    PROFILER_FULL_STACK=0  - Show only user code frames (default, clean output)
"""

import sys
import json
import subprocess
import os
from pathlib import Path

# Check if full stack mode is enabled
FULL_STACK_MODE = os.environ.get('PROFILER_FULL_STACK', '0') == '1'

# System libraries to filter out in default mode
SYSTEM_LIBRARIES = {
    'libc.so',
    'libc-',           # libc-2.31.so, etc.
    'libpthread.so',
    'libpthread-',
    'ld-linux',        # dynamic linker
    'libdl.so',
    'libm.so',
    'libprofiler.so',  # our own profiler
}


def is_system_library(binary_path):
    """
    Check if a binary is a system library that should be filtered.
    
    Returns True if it's a system library (libc, libpthread, etc.)
    """
    binary_name = Path(binary_path).name
    for sys_lib in SYSTEM_LIBRARIES:
        if sys_lib in binary_name:
            return True
    return False


def resolve_address_with_addr2line(binary_path, address):
    """
    Use addr2line to resolve address to filename:line.
    
    Args:
        binary_path: Path to the binary
        address: Hex address string (e.g., "0x4011ea")
    
    Returns: "filename:line" or None if resolution fails
    """
    try:
        # Run addr2line
        result = subprocess.run(
            ['addr2line', '-e', binary_path, '-f', '-C', '-s', '-a', address],
            capture_output=True,
            text=True,
            timeout=2
        )
        
        if result.returncode == 0:
            lines = result.stdout.strip().split('\n')
            # addr2line output format:
            # 0x4011ea
            # main
            # test_simple_leak.c:18
            if len(lines) >= 3:
                location = lines[2]  # filename:line
                # Check if location is valid (not "??:0" or "??:?")
                if location and not location.startswith('??'):
                    return location
        
    except (subprocess.TimeoutExpired, FileNotFoundError, Exception):
        pass
    
    return None


def get_binary_for_address(address, target_binary):
    """
    Determine which binary contains this address.
    
    Strategy: Try to resolve with the target binary. If it fails, assume it's a system library.
    
    Returns: (binary_path, is_system_library, is_target_binary)
    """
    # First, try to resolve with the target binary
    resolved = resolve_address_with_addr2line(target_binary, address)
    
    if resolved and not resolved.startswith('??'):
        # Successfully resolved - it's in the target binary
        return (target_binary, False, True)
    else:
        # Failed to resolve - assume it's a system library
        # We don't know which specific library, but mark it as system
        return ("unknown_library.so", True, False)


def format_resolved_location(filename, line_num, is_system):
    """
    Format a resolved location.
    
    Returns: formatted string
    """
    if FULL_STACK_MODE:
        label = "[SYS]" if is_system else "[USR]"
        return f"  {label} at: {filename}; line: {line_num}"
    else:
        return f"  at: {filename}; line: {line_num}"


def process_event_with_frames(event_obj, target_binary):
    """
    Process any event that has frames (leak, corruption, etc.) and print formatted output.
    
    This is a unified handler - all events with stack traces use the same logic.
    Only the header differs based on event type and available fields.
    
    Args:
        event_obj: Parsed JSON object with "type", "addr", "frames", and optional other fields
        target_binary: Path to the main binary being profiled
    
    Supported formats:
        {"type":"leak","addr":"0x...","size":123,"frames":[...]}
        {"type":"Double-Free or Invalid-Free","addr":"0x...","frames":[...]}
        {"type":"<any-error>","addr":"0x...","frames":[...]}
    """
    event_type = event_obj.get('type', 'Unknown')
    addr = event_obj.get('addr', '?')
    frames = event_obj.get('frames', [])
    
    # Print event-specific header
    if event_type == 'leak':
        size = event_obj.get('size', 0)
        print(f"[LEAK] {addr}: {size} bytes")
    else:
        # All other types are errors/corruption
        print(f"[CORRUPTION] {event_type} at {addr}")
    
    # Get the target binary base name for comparison
    target_name = Path(target_binary).name
    
    # Process each frame (unified logic for all event types)
    for frame in frames:
        if isinstance(frame, dict):
            frame_addr = frame.get('addr', '?')
            binary_name = frame.get('bin', 'unknown')
        else:
            # Backward compatibility: if frame is just a string address
            frame_addr = frame
            binary_name = "unknown"
        
        # Determine if this is user code or system library
        is_user_code = (binary_name == target_name)
        is_system = is_system_library(binary_name)
        
        # In default mode, skip system library frames
        if not FULL_STACK_MODE and is_system:
            continue
        
        # Try to resolve the address with the target binary
        if is_user_code:
            resolved = resolve_address_with_addr2line(target_binary, frame_addr)
            if resolved and ':' in resolved and not resolved.startswith('??'):
                # Successfully resolved
                parts = resolved.rsplit(':', 1)
                if len(parts) == 2:
                    filename = parts[0]
                    line_num = parts[1]
                    if FULL_STACK_MODE:
                        print(f"  [USR] at: {filename}; line: {line_num}")
                    else:
                        print(f"  at: {filename}; line: {line_num}")
                continue
        
        # Unresolved or system library frame
        if FULL_STACK_MODE:
            if is_system:
                print(f"  [SYS] <{binary_name}+{frame_addr}>")
            elif is_user_code:
                # Unresolved frame in user binary = C runtime startup
                print(f"  [CRT] <{binary_name}+{frame_addr}>")
            else:
                print(f"  [???] <{binary_name}+{frame_addr}>")
    
    # Print empty line after stack
    print()


# Legacy aliases for backward compatibility (can be removed later)
def process_leak_json(leak_obj, target_binary):
    """Legacy function - redirects to unified handler."""
    return process_event_with_frames(leak_obj, target_binary)


def process_corruption_json(corruption_obj, target_binary):
    """Legacy function - redirects to unified handler."""
    return process_event_with_frames(corruption_obj, target_binary)


def process_profiler_output(input_stream, target_binary):
    """
    Process the profiler output line by line.
    Handles both JSON events and plain text.
    """
    # Track corruption events
    corruption_count = 0
    corruption_header_printed = False
    
    # Print mode indicator at the start
    if FULL_STACK_MODE:
        print("=" * 60)
        print("PROFILER MODE: FULL SYSTEM STACK DUMP")
        print("(All frames including system libraries will be shown)")
        print("=" * 60)
        print()
    
    for line in input_stream:
        line = line.strip()
        
        # Skip empty lines
        if not line:
            continue
        
        # Try to parse as JSON
        try:
            obj = json.loads(line)
            obj_type = obj.get('type', '')
            
            # Check if this is a corruption event (has frames but is not a leak)
            if 'frames' in obj and obj_type != 'leak':
                # Print header on first corruption
                if not corruption_header_printed:
                    print()
                    print("========== DOUBLE/INVALID FREE ERRORS ==========")
                    print()
                    corruption_header_printed = True
                
                # Process and count corruption
                process_event_with_frames(obj, target_binary)
                corruption_count += 1
            
            # Check if this event has frames (leak events)
            elif 'frames' in obj:
                # Unified handler for all events with stack traces
                process_event_with_frames(obj, target_binary)
            
            elif obj_type == 'header':
                # Header: {"type":"header","leaks_count":2,"total_bytes":1536}
                count = obj.get('leaks_count', 0)
                total = obj.get('total_bytes', 0)
                print()
                print("========== MEMORY LEAKS ==========")
                print(f"Found {count} leak(s), {total} bytes total")
                print()
            
            elif obj_type == 'summary':
                # Summary: {"type":"summary","real_leaks":2,"real_bytes":1536,"libc_leaks":1,"libc_bytes":1024}
                real_leaks = obj.get('real_leaks', 0)
                real_bytes = obj.get('real_bytes', 0)
                libc_leaks = obj.get('libc_leaks', 0)
                libc_bytes = obj.get('libc_bytes', 0)
                
                print("Summary:")
                print(f"  Real leaks: {real_leaks} allocation(s), {real_bytes} bytes")
                if libc_leaks > 0:
                    print(f"  Libc infrastructure: {libc_leaks} allocation(s), {libc_bytes} bytes (ignored)")
                print(f"  Free errors: {corruption_count}")
                print("==================================")
                print()
            
            else:
                # Any other type is treated as a corruption event
                # Format: {"type":"Double-Free or Invalid-Free","addr":"0x...","frames":[...]}
                process_corruption_json(obj, target_binary)
        
        except json.JSONDecodeError:
            # Not JSON - print as-is (handles non-JSON stderr output)
            print(line)


def main():
    """Main entry point."""
    if len(sys.argv) < 3:
        print("Usage: resolve_symbols.py <profiler_output.txt> <binary_path>", file=sys.stderr)
        print("   or: resolve_symbols.py - <binary_path>  (read from stdin)", file=sys.stderr)
        sys.exit(1)
    
    output_file = sys.argv[1]
    binary_path = sys.argv[2]
    
    # Validate binary exists
    if not Path(binary_path).exists():
        print(f"Error: Binary not found: {binary_path}", file=sys.stderr)
        sys.exit(1)
    
    # Check if addr2line is available
    try:
        subprocess.run(['addr2line', '--version'], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("Error: 'addr2line' not found. Please install binutils.", file=sys.stderr)
        sys.exit(1)
    
    # Process input
    if output_file == '-':
        process_profiler_output(sys.stdin, binary_path)
    else:
        with open(output_file, 'r') as f:
            process_profiler_output(f, binary_path)


if __name__ == '__main__':
    main()
