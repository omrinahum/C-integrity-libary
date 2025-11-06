/*
 * memory profiler - main interception layer
 * 
 * implements the LD_PRELOAD magic that intercepts malloc/free.
 * 
 * how it works:
 * 1. we define malloc() and free() functions here
 * 2. when program runs with LD_PRELOAD=libprofiler.so, the dynamic linker
 *    loads our functions before libc's
 * 3. our functions intercept the call, track it, then call the real malloc/free
 * 
 * the bootstrap problem:
 * our tracking code needs to allocate memory for metadata. if we track
 * that allocation, we get infinite recursion:
 *   malloc() -> track() -> malloc() -> track() -> ...
 * 
 * solution:
 * use the 'in_profiler' flag. when inside profiler code, we don't track.
 * this prevents recursion when our own code (like hash_table_add) calls malloc.
 * 
 * we must avoid any libc functions that might call malloc.
 * write() is a direct syscall with zero dependency on libc buffering.
 * fprintf() can call malloc internally, which would break our initialization.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>     
#include <execinfo.h>  
#include "../include/profiler_internal.h"

// maximum stack frames to capture
#define MAX_STACK_FRAMES 16

// function pointers to the real libc malloc/free 
static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void*) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;

// export these for hash_table.c to use
void* (*real_malloc_ptr)(size_t) = NULL;
void (*real_free_ptr)(void*) = NULL;
int show_stack_traces = 1;  // exported configuration

// bootstrap protection - prevents tracking our own allocations
static int in_profiler = 0;

// initialization flags  
static int profiler_initialized = 0;
static int profiler_shutting_down = 0;  // skip validation during cleanup

/*
 * initialize the profiler
 * 
 * called once on first malloc/free call.
 * uses dlsym() to get pointers to the real libc functions.
 */
static void profiler_init(void) {
    if (profiler_initialized) return;
    
    profiler_initialized = 1;
    
    // read configuration from environment variables
    const char *env_stack_traces = getenv("PROFILER_STACK_TRACES");
    if (env_stack_traces && strcmp(env_stack_traces, "0") == 0) {
        show_stack_traces = 0;  // disabled
    }
    
    // get real function pointers using dlsym
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_free = dlsym(RTLD_NEXT, "free");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    
    // verify we found the real functions
    if (!real_malloc || !real_free) {
        profiler_log("[PROFILER ERROR] Failed to find real malloc/free\n");
        _exit(1);  
    }
    
    // export for hash_table.c to use
    real_malloc_ptr = real_malloc;
    real_free_ptr = real_free;
    
    // initialize tracking system
    hash_table_init();
}

/*
 * cleanup function - called at program exit
 * 
 * uses __attribute__((destructor)) to run automatically when the 
 * shared library is unloaded.
 */
__attribute__((destructor))
static void profiler_cleanup(void) {
    profiler_shutting_down = 1;  // disable corruption detection during cleanup
    hash_table_report_leaks();
    hash_table_cleanup();
}

/*
 * intercepted malloc()
 * 
 * this gets called instead of libc's malloc.
 * we track the allocation then call the real malloc.
 */
void* malloc(size_t size) {
    // initialize on first call
    if (!profiler_initialized) {
        profiler_init();
    }
    
    // call the real malloc
    void *ptr = real_malloc(size);
    
    // track it only if we're not in the profiler code (prevents recursion) 
    // for me: eg malloc -> track -> malloc -> track -> ...
    if (!in_profiler && ptr) {
        in_profiler = 1;
        
        // capture stack trace - backtrace stores return addresses in the array
        // eg: main -> helper -> helper2, both main and helper are in the array
        void *trace[MAX_STACK_FRAMES];
        int depth = backtrace(trace, MAX_STACK_FRAMES);
        
        // check if this looks like libc infrastructure allocation
        int is_suspicious = is_likely_libc_allocation(trace, depth);
        
        // track the allocation with stack trace and suspicion flag
        hash_table_add(ptr, size, trace, depth, is_suspicious);
        in_profiler = 0;
    }
    
    return ptr;
}

/*
 * intercepted free()
 * 
 * validates pointer before freeing to detect corruption bugs of double free or an invalid free
 * 
 * if corruption is detected, reports error immediately and skips the free
 * to prevent crashes or heap corruption.
 */
void free(void *ptr) {
    // initialize if needed (shouldn't happen, but be safe)
    if (!profiler_initialized) {
        profiler_init();
    }
    
    // don't free NULL 
    if (!ptr) return;
    
    // skip validation during profiler shutdown (cleanup frees internal metadata)
    if (profiler_shutting_down) {
        real_free(ptr);
        return;
    }
    
    // validate and remove from tracking
    if (!in_profiler) {
        in_profiler = 1;
        
        // check if this pointer exists in our tracking table
        int found = hash_table_find(ptr);
        
        if (!found) {
            // pointer not in table - either double-free or invalid-free
            // report the error immediately
            report_corruption_error(ptr, "Double-Free or Invalid-Free");
            in_profiler = 0;
            
            // don't call real_free() - would crash or corrupt heap!
            return;
        }
        
        // valid free - remove from tracking
        hash_table_remove(ptr);
        in_profiler = 0;
    }
    
    // call real free
    real_free(ptr);
}

/*
 * intercepted calloc()
 * 
 * calloc allocates and zeros memory. track it like malloc.
 */
void* calloc(size_t nmemb, size_t size) {
    if (!profiler_initialized) {
        profiler_init();
    }
    
    // call real calloc and track it
    void *ptr = real_calloc(nmemb, size);
    
    if (!in_profiler && ptr) {
        in_profiler = 1;
        
        // capture stack trace
        void *trace[MAX_STACK_FRAMES];
        int depth = backtrace(trace, MAX_STACK_FRAMES);
        
        // check if this looks like libc infrastructure allocation
        int is_suspicious = is_likely_libc_allocation(trace, depth);
        
        hash_table_add(ptr, nmemb * size, trace, depth, is_suspicious);
        in_profiler = 0;
    }
    
    return ptr;
}

/*
 * intercepted realloc()
 * 
 * realloc can act like malloc (if ptr is NULL), free (if size is 0),
 * or move the allocation to a new address.
 * we remove old tracking and add new tracking.
 */
void* realloc(void *ptr, size_t size) {
    if (!profiler_initialized) {
        profiler_init();
    }
    
    // if ptr is NULL, this is just malloc
    if (!ptr) {
        return malloc(size);
    }
    
    // if size is 0, this is just free
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    // call real realloc
    void *new_ptr = real_realloc(ptr, size);
    
    // update tracking: remove old, add new
    if (!in_profiler) {
        in_profiler = 1;
        hash_table_remove(ptr);
        if (new_ptr) {
            // capture stack trace
            void *trace[MAX_STACK_FRAMES];
            int depth = backtrace(trace, MAX_STACK_FRAMES);
            
            // check if this looks like libc infrastructure allocation
            int is_suspicious = is_likely_libc_allocation(trace, depth);
            
            hash_table_add(new_ptr, size, trace, depth, is_suspicious);
        }
        in_profiler = 0;
    }
    
    return new_ptr;
}

// safe output function - uses direct syscall, never calls malloc
static void profiler_log(const char *msg) {
    write(STDERR_FILENO, msg, strlen(msg));
}

/*
 * check if allocation likely came from libc infrastructure
 *
 * examines stack trace to detect allocations from libc.so which are likely to use malloc internally-
 * we cant control it, i dont want the user to see it.
 * 
 * key insight: we only check the immidiate caller (frame 1).
 * we only care if libc DIRECTLY called malloc, not user code.
 * 
 * uses dladdr() which:
 * - does NOT call malloc 
 * - maps function address to its shared library- so we can detect if its lib.c
 * 
 * returns: 1 if suspicious (likely false positive), 0 if real leak
 */
static int is_likely_libc_allocation(void **stack_trace, int depth) {
    if (!stack_trace || depth < 2) {
        return 0;  // can't determine, assume real
    }
    
    // only check frame 1 (immediate caller of malloc)
    // frame 0 is malloc itself, frame 1 is who called malloc
    // dl_info is a struct which holds the inforamtion about the shared libary
    Dl_info info;
    
    // dladdr() fills the info structure based on the address in trace[1]
    if (dladdr(stack_trace[1], &info) != 0) {
        // check if immediate caller is from libc.so
        if (info.dli_fname && strstr(info.dli_fname, "libc.so")) {
            return 1;  // direct libc call, suspicious
        }
    }
    
    return 0;  // not from libc, likely user code
}

/*
 * report memory corruption error
 * 
 * called when we detect double-free or invalid-free.
 * reports immediately, doesn't wait for program exit.
 * 
 * outputs JSON format: {"type":"Double-Free or Invalid-Free","addr":"0x...","frames":[...]}
 * stack trace can be disabled with PROFILER_STACK_TRACES=0
 */
static void report_corruption_error(void *ptr, const char *error_type) {
    // Output corruption event in JSON format 
    write_str("{\"type\":\"");
    write_str(error_type);
    write_str("\",\"addr\":\"");
    write_hex((unsigned long)ptr);
    write_str("\",\"frames\":[");
    
    // Capture and output stack trace if enabled (top 7 frames only)
    if (show_stack_traces) {
        void *stack_trace[MAX_STACK_FRAMES];
        int depth = backtrace(stack_trace, MAX_STACK_FRAMES);
        int frames_to_show = (depth < 7) ? depth : 7;
        
        for (int i = 0; i < frames_to_show; i++) {
            if (i > 0) write_str(",");
            
            // Get binary name using dladdr()
            Dl_info dl_info;
            const char *binary_name = "unknown";
            if (dladdr(stack_trace[i], &dl_info) && dl_info.dli_fname) {
                // Extract just the filename from the full path
                const char *slash = strrchr(dl_info.dli_fname, '/');
                binary_name = slash ? (slash + 1) : dl_info.dli_fname;
            }
            
            // Output: {"addr":"0x123","bin":"libprofiler.so"}
            write_str("{\"addr\":\"");
            write_hex((unsigned long)stack_trace[i]);
            write_str("\",\"bin\":\"");
            write_str(binary_name);
            write_str("\"}");
        }
    }
    
    write_str("]}\n");
}
