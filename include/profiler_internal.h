#ifndef PROFILER_INTERNAL_H
#define PROFILER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "uthash.h"  

/* 
 * allocation metadata stored for each malloc() call
 * 
 * we store the information needed to detect leaks:
 * - ptr: the address returned by malloc (used as hash key)
 * - size: number of bytes allocated
 * - timestamp: when allocation occurred
 * - stack_trace: array of return addresses (from backtrace)
 * - stack_depth: number of frames captured
 * - hh: uthash handle (required by uthash library)
 */
typedef struct allocation_info {
    void *ptr;              // the allocated address (hash key)
    size_t size;            // bytes allocated
    time_t timestamp;       // when it was allocated
    void **stack_trace;     // array of return addresses
    int stack_depth;        // number of frames in stack_trace
    int is_suspicious;      // 1 if likely libc false positive, 0 if real leak
    UT_hash_handle hh;      // uthash handle 
} allocation_info_t;

/*
 * Global state for the profiler
 * 
 * This structure holds everything the profiler needs to track.
 * For now, it's just a pointer to the hash table, but we'll
 * expand this as we add features (mutex, stats, etc.)
 */
typedef struct profiler_state {
    allocation_info_t *allocations;  // Hash table of active allocations
    int initialized;                  // Flag to prevent re-initialization
} profiler_state_t;

// Function declarations for hash table (allocation tracking)
void hash_table_init(void);
void hash_table_add(void *ptr, size_t size, void **trace, int depth, int is_suspicious);
void hash_table_remove(void *ptr);
int hash_table_find(void *ptr);  
void hash_table_report_leaks(void);
void hash_table_cleanup(void);

// Real libc function pointers (set by malloc_intercept.c)
extern void* (*real_malloc_ptr)(size_t);
extern void (*real_free_ptr)(void*);

// Configuration (set by malloc_intercept.c)
extern int show_stack_traces;  // 1 = enabled, 0 = disabled

// JSON output helpers
void write_str(const char *str);
void write_hex(unsigned long val);
void write_dec(size_t val);

#endif // PROFILER_INTERNAL_H
