/* Test: Complex Allocation - Expected: 3 leaks */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

char* create_buffer(size_t size) {
    char *buf = malloc(size);
    if (buf) memset(buf, 'A', size);
    return buf;
}

int* create_array(int count) {
    int *arr = malloc(count * sizeof(int));
    if (arr) {
        for (int i = 0; i < count; i++) arr[i] = i;
    }
    return arr;
}

void process_data(void) {
    malloc(512);  // Leak
}

int main(void) {
    create_buffer(1024);  // Leak
    create_array(100);    // Leak
    process_data();       // Leak
    
    char *temp = create_buffer(256);
    free(temp);  // OK
    
    printf("Test: Complex Allocation\n");
    printf("Expected: 3 leaks (1024 + 400 + 512 bytes)\n");
    return 0;
}
