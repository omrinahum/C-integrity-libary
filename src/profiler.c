/*
 * Profiler Initialization & Coordination
 * 
 * This file handles library-wide initialization and cleanup.
 * Also provides async-safe output utilities used throughout the profiler.
 */

#define _GNU_SOURCE // to expose advanced linux internal fucntions
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "../include/profiler_internal.h"

/*
 * Async-Safe Output Utilities :used by all profiler modules to output JSON without using malloc/printf 
 */

/*
 * write a string to stderr
 * async-safe, no malloc
 */
void write_str(const char *str) {
    write(STDERR_FILENO, str, strlen(str));
}

/*
 * write an unsigned integer as hex string
 * async-safe, no malloc
 */
void write_hex(unsigned long val) {
    char buf[32];
    int i = 0;
    
    if (val == 0) {
        write(STDERR_FILENO, "0x0", 3);
        return;
    }
    
    // convert to hex (reverse order)
    while (val > 0 && i < 30) {
        int digit = val % 16;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        val /= 16;
    }
    
    // write "0x" prefix
    write(STDERR_FILENO, "0x", 2);
    
    // write digits in correct order
    while (i > 0) {
        write(STDERR_FILENO, &buf[--i], 1);
    }
}

/*
 * write an unsigned integer as decimal string
 * async-safe, no malloc
 */
void write_dec(size_t val) {
    char buf[32];
    int i = 0;
    
    if (val == 0) {
        write(STDERR_FILENO, "0", 1);
        return;
    }
    
    // convert to decimal (reverse order)
    while (val > 0 && i < 30) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    
    // write digits in correct order
    while (i > 0) {
        write(STDERR_FILENO, &buf[--i], 1);
    }
}

/*
 * Library Lifecycle Management
 */

// Library constructor - runs when .so is loaded
__attribute__((constructor))
static void profiler_lib_init(void) {
    // Currently initialization happens on first malloc call
    // This could be extended to read config, setup signal handlers, etc.
}

// Library destructor - runs when .so is unloaded  
__attribute__((destructor))
static void profiler_lib_cleanup(void) {
    // Final cleanup happens in malloc_intercept.c
}
