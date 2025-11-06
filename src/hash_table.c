/*
 * hash table - allocation registry
 * 
 * manages a hash table of active memory allocations.
 * uses uthash for O(1) performance.
 * thread-safe with pthread mutex.
 */

#define _GNU_SOURCE  
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <execinfo.h>  
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>       
#include "../include/profiler_internal.h"
#include "../include/uthash.h"

// pointer to hash table head
static allocation_info_t *g_allocations = NULL;

// mutex to protect hash table from concurrent access
// static initialization, safe before any threads exist
static pthread_mutex_t hash_table_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * initialize the tracker
 * 
 * currently minimal - just resets the allocation list.
 */
void hash_table_init(void) {
    g_allocations = NULL;
}

/*
 * add an allocation to our tracking table
 * 
 * called immediately after malloc() succeeds.
 * we use real_malloc_ptr to allocate metadata (avoids recursion).
 */
void hash_table_add(void *ptr, size_t size ,void **trace, int depth, int is_suspicious) {
    if (!ptr) return;
    
    // don't track if real_malloc_ptr isn't set yet (during early init)
    if (!real_malloc_ptr) return;
    
    // allocate metadata structure using real malloc (prevents infinite recursion)
    allocation_info_t *info = (allocation_info_t*)real_malloc_ptr(sizeof(allocation_info_t));
    if (!info) {
        fprintf(stderr, "[PROFILER ERROR] Failed to allocate tracking metadata\n");
        return;
    }
    
    // initialize metadata fields
    info->ptr = ptr;
    info->size = size;
    info->timestamp = time(NULL);
    info->is_suspicious = is_suspicious;
    
    // allocate and copy stack trace
    info->stack_trace = real_malloc_ptr(depth * sizeof(void*));
    if (info->stack_trace) {
        memcpy(info->stack_trace, trace, depth * sizeof(void*));
        info->stack_depth = depth;
    } else {
        info->stack_depth = 0;
    }
    
    // lock before modifying shared hash table
    pthread_mutex_lock(&hash_table_mutex);
    
    // add to hash table
    // for me : HASH_ADD_PTR(head, keyfield, item)
    HASH_ADD_PTR(g_allocations, ptr, info);
    
    // unlock after modification complete
    pthread_mutex_unlock(&hash_table_mutex);
}

/*
 * remove an allocation from tracking
 * 
 * called when free() is called.
 * 
 * thread safety: protected by hash_table_mutex
 */
void hash_table_remove(void *ptr) {
    if (!ptr) return;
    
    // find the allocation metadata
    allocation_info_t *found;
    
    // lock before accessing shared hash table
    pthread_mutex_lock(&hash_table_mutex);
    
    // find the entry in hash table
    // for me : HASH_FIND_PTR(head, key_ptr, output)
    HASH_FIND_PTR(g_allocations, &ptr, found);
    
    if (found) {
        // remove from hash table
        HASH_DEL(g_allocations, found);
    }
    
    // unlock before freeing memory 
    pthread_mutex_unlock(&hash_table_mutex);
    
    // free outside the critical section 
    if (found) {
        // free the stack trace array first, then the struct
        if (found->stack_trace) {
            real_free_ptr(found->stack_trace);
        }
        real_free_ptr(found);
    }
    
    // not found - could be double-free or invalid-free
    // for now, just ignore. we'll add detection in phase 4
}

/*
 * check if an allocation exists in the hash table
 * 
 * called by free() to validate pointer before freeing.
 * returns 1 if found, 0 if not found.
 * 
 * thread safety: protected by hash_table_mutex
 */
int hash_table_find(void *ptr) {
    if (!ptr) return 0;
    
    allocation_info_t *found;
    
    // lock before accessing shared hash table
    pthread_mutex_lock(&hash_table_mutex);
    
    // find the entry in hash table
    HASH_FIND_PTR(g_allocations, &ptr, found);
    
    // unlock immediately after lookup
    pthread_mutex_unlock(&hash_table_mutex);
    
    if (found){
         return 1;
    }
    return 0;
}

/*
 * output a single leak in JSON format
 * uses only write() syscalls which are async-safe and do not use malloc internally (like printf)
 * 
 * Format: {"type":"leak","addr":"0x...","size":1024,"frames":[
 *           {"addr":"0x123","bin":"libprofiler.so"},
 *           {"addr":"0x456","bin":"test_program"}
 *         ]}
 */
static void output_leak_json(allocation_info_t *info) {
    write_str("{\"type\":\"leak\",\"addr\":\"");
    write_hex((unsigned long)info->ptr);
    write_str("\",\"size\":");
    write_dec(info->size);
    write_str(",\"frames\":[");
    
    // output stack trace frames with binary names
    if (show_stack_traces && info->stack_trace && info->stack_depth > 0) {
        // limit to top 7 frames
        int frames_to_show = (info->stack_depth < 7) ? info->stack_depth : 7;
        // output each frame
        for (int i = 0; i < frames_to_show; i++) {
            if (i > 0) write_str(",");
            
            // Get binary name using dladdr()
            Dl_info dl_info;
            // default is unkown
            const char *binary_name = "unknown";
            if (dladdr(info->stack_trace[i], &dl_info) && dl_info.dli_fname) {
                // Extract just the filename from the full path
                const char *slash = strrchr(dl_info.dli_fname, '/');
                binary_name = slash ? (slash + 1) : dl_info.dli_fname;
            }
            
            // Output: {"addr":"0x123","bin":"libprofiler.so"}
            write_str("{\"addr\":\"");
            write_hex((unsigned long)info->stack_trace[i]);
            write_str("\",\"bin\":\"");
            write_str(binary_name);
            write_str("\"}");
        }
    }
    
    write_str("]}\n");
}

/*
 * report all leaked allocations in JSON Lines format
 * 
 * called at program exit.
 * outputs structured JSON data (one object per line):
 * - header: leak count and total bytes
 * - leak: individual leak with address, size, and raw stack frames
 * - summary: final statistics
 * 
 * separates confirmed leaks vs suspicious leaks (likely libc).
 */
void hash_table_report_leaks(void) {
    allocation_info_t *current, *tmp;
    int confirmed_count = 0;
    int suspicious_count = 0;
    size_t confirmed_bytes = 0;
    size_t suspicious_bytes = 0;
    
    // first pass: count leaks
    HASH_ITER(hh, g_allocations, current, tmp) {
        if (!current->is_suspicious) {
            confirmed_count++;
            confirmed_bytes += current->size;
        } else {
            suspicious_count++;
            suspicious_bytes += current->size;
        }
    }
    
    // output header and leaks (only if there are leaks)
    if (confirmed_count > 0) {
        write_str("{\"type\":\"header\",\"leaks_count\":");
        write_dec(confirmed_count);
        write_str(",\"total_bytes\":");
        write_dec(confirmed_bytes);
        write_str("}\n");
        
        // output each leak
        HASH_ITER(hh, g_allocations, current, tmp) {
            if (!current->is_suspicious) {
                output_leak_json(current);
            }
        }
    }
    
    // output summary
    write_str("{\"type\":\"summary\",\"real_leaks\":");
    write_dec(confirmed_count);
    write_str(",\"real_bytes\":");
    write_dec(confirmed_bytes);
    write_str(",\"libc_leaks\":");
    write_dec(suspicious_count);
    write_str(",\"libc_bytes\":");
    write_dec(suspicious_bytes);
    write_str("}\n");
}

/*
 * cleanup tracker state
 * 
 * free all tracking metadata. called at exit.
 * uses HASH_ITER to safely delete all entries.
 * 
 * thread safety: at exit, program is single-threaded, so no need to lock
 */
void hash_table_cleanup(void) {
    allocation_info_t *current, *tmp;
    
    // iterate through the remain data in the hash and delete them
    // at program exit, we're single-threaded, so no lock needed
    HASH_ITER(hh, g_allocations, current, tmp) {
        HASH_DEL(g_allocations, current);  // remove from hash table
        
        // free stack trace array first, then the struct
        if (current->stack_trace) {
            real_free_ptr(current->stack_trace);
        }
        real_free_ptr(current);
    }
    
    g_allocations = NULL;
}
